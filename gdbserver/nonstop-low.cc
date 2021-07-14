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

bool
nonstop_process_target::resume_request_applies_to_thread (thread_info *thread,
							  thread_resume &resume)
{
  const char *pid_str = target_pid_to_str (ptid_of (thread));

  if (resume.kind == resume_stop
      && thread->last_resume_kind == resume_stop)
    {
      if (debug_threads)
	debug_printf ("already %s %s at GDB's request\n",
		      (thread->last_status.kind == TARGET_WAITKIND_STOPPED)
		      ? "stopped"
		      : "stopping",
		      pid_str);

      return false;
    }

  /* Ignore (wildcard) resume requests for already-resumed
     threads.  */
  if (resume.kind != resume_stop
      && thread->last_resume_kind != resume_stop)
    {
      if (debug_threads)
	debug_printf ("already %s %s at GDB's request\n",
		      (thread->last_resume_kind == resume_step)
		      ? "stepping"
		      : "continuing",
		      pid_str);
      return false;
    }

  /* If the thread has a pending event that has already been
     reported to GDBserver core, but GDB has not pulled the
     event out of the vStopped queue yet, likewise, ignore the
     (wildcard) resume request.  */
  if (in_queued_stop_replies (thread->id))
    {
      if (debug_threads)
	debug_printf ("not resuming %s: has queued stop reply\n",
		      pid_str);
      return false;
    }

  return true;
}

void
nonstop_process_target::send_sigstop (nonstop_thread_info *nti)
{
  const char *pid_str = target_pid_to_str (ptid_of (nti->thread));

  /* If we already have a pending stop signal, don't send another.  */
  if (nti->stop_expected)
    {
      if (debug_threads)
	debug_printf ("Have pending sigstop for %s\n", pid_str);

      return;
    }

  if (debug_threads)
    debug_printf ("Sending sigstop to %s\n", pid_str);

  nti->stop_expected = true;
  low_send_sigstop (nti);
}

void
nonstop_process_target::resume_one_thread (thread_info *thread,
					   bool leave_all_stopped)
{
  nonstop_thread_info *nti = get_thread_nti (thread);
  const char *pid_str = target_pid_to_str (ptid_of (thread));

  if (nti->resume == nullptr)
    return;

  if (nti->resume->kind == resume_stop)
    {
      if (debug_threads)
	debug_printf ("resume_stop request for %s\n", pid_str);

      if (!nti->stopped)
	{
	  if (debug_threads)
	    debug_printf ("stopping %s\n", pid_str);

	  /* Stop the thread, and wait for the event asynchronously,
	     through the event loop.  */
	  send_sigstop (nti);
	}
      else
	{
	  if (debug_threads)
	    debug_printf ("already stopped %s\n", pid_str);

	  /* The LWP may have been stopped in an internal event that
	     was not meant to be notified back to GDB (e.g., gdbserver
	     breakpoint), so we should be reporting a stop event in
	     this case too.  */

	  /* If the thread already has a pending SIGSTOP, this is a
	     no-op.  Otherwise, something later will presumably resume
	     the thread and this will cause it to cancel any pending
	     operation, due to last_resume_kind == resume_stop.  If
	     the thread already has a pending status to report, we
	     will still report it the next time we wait - see
	     status_pending_p_callback.  */

	  /* Give the low target a chance to process the request.  */
	  resume_stop_one_stopped_thread (nti);
	}

      /* For stop requests, we're done.  */
      nti->resume = nullptr;
      thread->last_status.kind = TARGET_WAITKIND_IGNORE;
      return;
    }

  /* If this thread which is about to be resumed has a pending status,
     then don't resume it - we can just report the pending status.
     Likewise if it is suspended, because e.g., another thread is
     stepping past a breakpoint.  Make sure to queue any signals that
     would otherwise be sent.  In all-stop mode, we do this decision
     based on if *any* thread has a pending status.  If there's a
     thread that needs the step-over-breakpoint dance, then don't
     resume any other thread but that particular one.  */
  bool leave_pending = has_pending_status (nti) || leave_all_stopped;

  /* If we have a new signal, enqueue the signal.  */
  if (nti->resume->sig != 0)
    enqueue_signal_pre_resume (nti, nti->resume->sig);

  if (!leave_pending)
    {
      if (debug_threads)
	debug_printf ("resuming %s\n", pid_str);

      proceed_one_nti (nti, nullptr);
    }
  else
    {
      if (debug_threads)
	debug_printf ("leaving %s stopped\n", pid_str);
    }

  thread->last_status.kind = TARGET_WAITKIND_IGNORE;
  nti->resume = nullptr;
}

void
nonstop_process_target::resume_stop_one_stopped_thread
  (nonstop_thread_info *nti)
{
  /* Do nothing by default.  */
}

bool
nonstop_process_target::has_pending_status (nonstop_thread_info *nti)
{
  return nti->thread->status_pending_p;
}

void
nonstop_process_target::enqueue_signal_pre_resume (nonstop_thread_info *nti,
						   int signal)
{
  /* Do nothing by default.  */
}
