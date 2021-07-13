/* Low level interface to for the remote server for GDB
   that implements architecture-independent non-stop behavior.

   Copyright (C) 1995-2020 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "server.h"
#include "nonstop-low.h"

#ifndef USE_WIN32API
#include <signal.h>
#include <fcntl.h>
#endif

#include "gdbsupport/gdb-sigmask.h"

nonstop_thread_info *
get_thread_nti (thread_info *thread)
{
  return static_cast<nonstop_thread_info *> (thread_target_data (thread));
}

/* Async interaction stuff.

   The read/write ends of the pipe registered as waitable file in the
   event loop.  */
static int event_pipe[2] = { -1, -1 };

/* True if we are currently in async mode.  */

bool
target_is_async_p ()
{
  return (event_pipe[0] != -1);
}

/* Get rid of any pending event in the pipe.  */

void
async_file_flush ()
{
#ifndef USE_WIN32API
  int ret;
  char buf;

  do
    ret = read (event_pipe[0], &buf, 1);
  while (ret >= 0 || (ret == -1 && errno == EINTR));
#else
  gdb_assert_not_reached ("async_file_flush should not be called on Windows.");
#endif
}

/* Put something in the pipe, so the event loop wakes up.  */

void
async_file_mark ()
{
#ifndef USE_WIN32API
  int ret;

  async_file_flush ();

  do
    ret = write (event_pipe[1], "+", 1);
  while (ret == 0 || (ret == -1 && errno == EINTR));

  /* Ignore EAGAIN.  If the pipe is full, the event loop will already
     be awakened anyway.  */
#else
  gdb_assert_not_reached ("async_file_mark should not be called on Windows.");
#endif
}

bool
nonstop_process_target::supports_non_stop ()
{
#ifndef USE_WIN32API
  return true;
#else
  return false;
#endif
}

bool
nonstop_process_target::async (bool enable)
{
  bool previous = target_is_async_p ();

  if (debug_threads)
    debug_printf ("nonstop_async (%d), previous=%d\n",
		  enable, previous);

  if (previous != enable)
    {
#ifndef USE_WIN32API
      sigset_t mask;
      sigemptyset (&mask);
      sigaddset (&mask, SIGCHLD);

      gdb_sigmask (SIG_BLOCK, &mask, NULL);

      if (enable)
	{
	  if (pipe (event_pipe) == -1)
	    {
	      event_pipe[0] = -1;
	      event_pipe[1] = -1;
	      gdb_sigmask (SIG_UNBLOCK, &mask, NULL);

	      warning ("creating event pipe failed.");
	      return previous;
	    }

	  fcntl (event_pipe[0], F_SETFL, O_NONBLOCK);
	  fcntl (event_pipe[1], F_SETFL, O_NONBLOCK);

	  /* Register the event loop handler.  */
	  add_file_handler (event_pipe[0],
			    handle_target_event, NULL);

	  /* Always trigger a wait.  */
	  async_file_mark ();
	}
      else
	{
	  delete_file_handler (event_pipe[0]);

	  close (event_pipe[0]);
	  close (event_pipe[1]);
	  event_pipe[0] = -1;
	  event_pipe[1] = -1;
	}

      gdb_sigmask (SIG_UNBLOCK, &mask, NULL);
#else
      gdb_assert_not_reached ("async should not be called on Windows.");
#endif
    }

  return previous;
}

int
nonstop_process_target::start_non_stop (bool nonstop)
{
#ifndef USE_WIN32API
  /* Register or unregister from event-loop accordingly.  */
  async (nonstop);

  if (target_is_async_p () != (nonstop != false))
    return -1;

  return 0;
#else
  gdb_assert_not_reached ("start_non_stop should not be called on Windows.");
#endif
}

ptid_t
nonstop_process_target::wait (ptid_t ptid,
			      target_waitstatus *ourstatus,
			      int target_options)
{
  ptid_t event_ptid;

  /* Flush the async file first.  */
  if (target_is_async_p ())
    async_file_flush ();

  do
    {
      event_ptid = low_wait (ptid, ourstatus, target_options);
    }
  while ((target_options & TARGET_WNOHANG) == 0
	 && event_ptid == null_ptid
	 && ourstatus->kind == TARGET_WAITKIND_IGNORE);

  /* If at least one stop was reported, there may be more.  A single
     SIGCHLD can signal more than one child stop.  */
  if (target_is_async_p ()
      && (target_options & TARGET_WNOHANG) != 0
      && event_ptid != null_ptid)
    async_file_mark ();

  return event_ptid;
}

void
nonstop_process_target::resume (thread_resume *resume_info, size_t n)
{
  thread_info *need_step_over = nullptr;

  if (debug_threads)
    {
      debug_enter ();
      debug_printf ("resume:\n");
    }

  for_each_thread ([=] (thread_info *thread)
    {
      set_resume_request (thread, resume_info, n);
    });

  /* If there is a thread which would otherwise be resumed, which has
     a pending status, then don't resume any threads - we can just
     report the pending status.  Make sure to queue any signals that
     would otherwise be sent.  In non-stop mode, we'll apply this
     logic to each thread individually.  We consume all pending events
     before considering to start a step-over (in all-stop).  */
  bool any_pending = false;
  if (!non_stop)
    any_pending = find_thread ([this] (thread_info *thread)
		    {
		      nonstop_thread_info *nti = get_thread_nti (thread);

		      /* Threads that will not be resumed are not
			 interesting, because we might not wait for
			 them next time through 'wait'.  */
		      if (nti->resume == nullptr)
			return false;

		      return thread_still_has_status_pending (thread);
		    }) != nullptr;

  /* If there is a thread which would otherwise be resumed, which is
     stopped at a breakpoint that needs stepping over, then don't
     resume any threads - have it step over the breakpoint with all
     other threads stopped, then resume all threads again.  Make sure
     to queue any signals that would otherwise be delivered or
     queued.  */
  if (!any_pending && supports_breakpoints ())
    need_step_over = find_thread ([this] (thread_info *thread)
		       {
			 return thread_needs_step_over (thread);
		       });

  bool leave_all_stopped = (need_step_over != NULL || any_pending);

  if (debug_threads)
    {
      if (need_step_over != NULL)
	debug_printf ("Not resuming all, need step over\n");
      else if (any_pending)
	debug_printf ("Not resuming, all-stop and found "
		      "a thread with pending status\n");
      else
	debug_printf ("Resuming, no pending status or step over needed\n");
    }

  /* Even if we're leaving threads stopped, resume them because e.g.
     we may have to queue all signals we'd otherwise deliver.  */
  for_each_thread ([&] (thread_info *thread)
    {
      resume_one_thread (thread, leave_all_stopped);
    });

  if (need_step_over != nullptr)
    start_step_over (need_step_over);

  if (debug_threads)
    {
      debug_printf ("resume done\n");
      debug_exit ();
    }

  /* We may have events that were pending that can/should be sent to
     the client now.  Trigger a 'wait' call.  */
  if (target_is_async_p ())
    async_file_mark ();
}

void
nonstop_process_target::set_resume_request (thread_info *thread,
					    thread_resume *resume,
					    size_t n)
{
  nonstop_thread_info *nti = get_thread_nti (thread);

  for (int ndx = 0; ndx < n; ndx++)
    {
      ptid_t ptid = resume[ndx].thread;
      if (ptid == minus_one_ptid
	  || ptid == thread->id
	  /* Handle both 'pPID' and 'pPID.-1' as meaning 'all threads
	     of PID'.  */
	  || (ptid.pid () == pid_of (thread)
	      && (ptid.is_pid ()
		  || ptid.lwp () == -1)))
	{
	  if (!resume_request_applies_to_thread (thread, resume[ndx]))
	    continue;

	  nti->resume = &resume[ndx];
	  thread->last_resume_kind = nti->resume->kind;

	  nti->step_range_start = nti->resume->step_range_start;
	  nti->step_range_end = nti->resume->step_range_end;

	  post_set_resume_request (thread);

	  return;
	}
    }

  /* No resume action for this thread.  */
  nti->resume = NULL;
}

void
nonstop_process_target::post_set_resume_request (thread_info *thread)
{
  /* Do nothing by default.  */
}
