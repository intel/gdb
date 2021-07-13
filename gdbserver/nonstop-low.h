/* Internal interfaces for the nonstop target code for gdbserver.

   Copyright (C) 2002-2020 Free Software Foundation, Inc.

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

#ifndef GDBSERVER_NONSTOP_LOW_H
#define GDBSERVER_NONSTOP_LOW_H

/* The target-specific private data for a process_info.  Nonstop
   targets should derive their private process info from this
   struct.  */

struct nonstop_process_info
{
  /* Backlink to the parent object.  */
  process_info *process;
};

/* The target-specific private data for a thread_info.  Nonstop
   targets should derive their private thread info from this
   struct.  */

struct nonstop_thread_info
{
  /* Backlink to the parent object.  */
  thread_info *thread;

  /* A link used when resuming.  It is initialized from the resume request,
     and then processed and cleared when the thread is resumed.  */
  thread_resume *resume;

  /* Range to single step within.  This is a copy of the step range
     passed along the last resume request.  See 'struct
     thread_resume'.  */
  CORE_ADDR step_range_start;	/* Inclusive */
  CORE_ADDR step_range_end;	/* Exclusive */
};

/* The target that defines abstract nonstop behavior without relying
   on any platform specifics (e.g.  ptrace).  */

class nonstop_process_target : public process_stratum_target
{
public:

  bool supports_non_stop () override;

  bool async (bool enable) override;

  int start_non_stop (bool enable) override;

  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       int options) override final;

  void resume (thread_resume *resume_info, size_t n) override;

protected:

  /* This function is called once per thread via for_each_thread.
     We look up which resume request applies to THREAD and mark it with a
     pointer to the appropriate resume request.

     This algorithm is O(threads * resume elements), but resume elements
     is small (and will remain small at least until GDB supports thread
     suspension).  */
  virtual void set_resume_request (thread_info *thread,
				   thread_resume *resume, size_t n);

  /* Return true if RESUME is a request that applies to THREAD.
     Return false, otherwise.  */
  virtual bool resume_request_applies_to_thread (thread_info *thread,
						 thread_resume &resume) = 0;

  /* This method is called after a resume request has been set for
     THREAD.  It is the target's chance to do any post-setups, such as
     dequeuing a deferred signal.  */
  virtual void post_set_resume_request (thread_info *thread);

  /* Return true if THREAD still has an interesting status pending.
     If not (e.g., it had stopped for a breakpoint that is gone), return
     false.  */
  virtual bool thread_still_has_status_pending (thread_info *thread) = 0;

  /* Return true if THREAD that GDB wants running is stopped at an
     internal breakpoint that we need to step over.  It assumes that
     any required STOP_PC adjustment has already been propagated to
     the inferior's regcache.  */
  virtual bool thread_needs_step_over (thread_info *thread) = 0;

  /* Return true if breakpoints are supported.  */
  virtual bool supports_breakpoints () = 0;

  /* This function is called once per thread.  We check the thread's
     resume request, which will tell us whether to resume, step, or
     leave the thread stopped; and what signal, if any, it should be
     sent.

     For threads which we aren't explicitly told otherwise, we preserve
     the stepping flag; this is used for stepping over gdbserver-placed
     breakpoints.

     If the thread should be left with a pending event, we queue any needed
     signals, since we won't actually resume.  We already have a pending
     event to report, so we don't need to preserve any step requests;
     they should be re-issued if necessary.  */
  virtual void resume_one_thread (thread_info *thread,
				  bool leave_all_stopped) = 0;

  /* Start a step-over operation on THREAD.  When THREAD stopped at a
     breakpoint, to make progress, we need to remove the breakpoint out
     of the way.  If we let other threads run while we do that, they may
     pass by the breakpoint location and miss hitting it.  To avoid
     that, a step-over momentarily stops all threads while THREAD is
     single-stepped by either hardware or software while the breakpoint
     is temporarily uninserted from the inferior.  When the single-step
     finishes, we reinsert the breakpoint, and let all threads that are
     supposed to be running, run again.  */
  virtual void start_step_over (thread_info *thread) = 0;

  /* Wait for process, return status.  */
  virtual ptid_t low_wait (ptid_t ptid, target_waitstatus *ourstatus,
			   int target_options) = 0;
};

/* Given THREAD, return its nonstop_thread_info.  */
nonstop_thread_info *get_thread_nti (thread_info *thread);

/* Async communication functions.  */

/* Query if the target is in async mode.  */
bool target_is_async_p ();

/* Get rid of any pending event in the pipe.  */
void async_file_flush ();

/* Put something in the pipe, so the event loop wakes up.  */
void async_file_mark ();

#endif /* GDBSERVER_NONSTOP_LOW_H */
