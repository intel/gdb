/* Internal interfaces for the nonstop target code for gdbserver.

   Copyright (C) 2002-2021 Free Software Foundation, Inc.

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

  /* If this flag is set, the next stop request (e.g.  SIGSTOP) will be
     ignored (the process will be immediately resumed).  This means
     that either we sent the SIGSTOP to it ourselves and got some
     other pending event (so the SIGSTOP is still pending), or that we
     stopped the inferior implicitly (e.g. via PTRACE_ATTACH) and have
     not waited for it yet.  */
  bool stop_expected;

  /* If this flag is set, the thread is known to be stopped right now (stop
     event already received in a wait()).  */
  bool stopped;

  /* The reason the thread last stopped, if we need to track it
     (breakpoint, watchpoint, etc.)  */
  enum target_stop_reason stop_reason;
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

  /* Send a stop request to NTI.  */
  void send_sigstop (nonstop_thread_info *nti);

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
						 thread_resume &resume);

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

  /* Return true if this target supports resuming all threads
     in one go.  */
  virtual bool supports_resume_all ();

  /* Resume all threads in a single request made to the debug interface.
     Targets that have this feature should override this method and
     return true in supports_resume_all.  PID is -1 if all threads
     of all processes are to be resumed.  */
  virtual void resume_all_threads (int pid);

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
     they should be re-issued if necessary.

     If WANT_ALL_RESUMED is true, we received a wildcard request to
     continue all threads.  In this case, instead of resuming each
     thread one by one, all threads are resumed in shot, if the target
     supports that operation.  */
  virtual void resume_one_thread (thread_info *thread,
				  bool leave_all_stopped,
				  bool want_all_resumed);

  /* Handle a resume_stop request for an already-stopped thread.  Any
     target-specific handling that's not done in resume_one_thread can
     be done in this method.  */
  virtual void resume_stop_one_stopped_thread (nonstop_thread_info *nti);

  /* Return true if NTI, which is about to be resumed, has a pending
     status.  False, otherwise.  */
  virtual bool has_pending_status (nonstop_thread_info *nti);

  /* Enqueue the signal SIG for NTI, which is about to be resumed.
     By default, this is a no-op.  */
  virtual void enqueue_signal_pre_resume (nonstop_thread_info *nti,
					  int signal);

  /* This function is called once per thread.  We check the thread's
     last resume request, which will tell us whether to resume, step, or
     leave the thread stopped.  Any signal the client requested to be
     delivered has already been enqueued at this point.

     If any thread that GDB wants running is stopped at an internal
     breakpoint that needs stepping over, we start a step-over operation
     on that particular thread, and leave all others stopped.  */
  virtual void proceed_one_nti (nonstop_thread_info *nti,
				nonstop_thread_info *except);

  /* Handle a resume_stop request for an NTI.  */
  virtual void proceed_one_nti_for_resume_stop (nonstop_thread_info *nti);

  /* NTI is about to be resumed.  Return true if it should be stepped,
     false otherwise.  */
  virtual bool resume_one_nti_should_step (nonstop_thread_info *nti);

  /* Resume execution of NTI.  If STEP, single-step it.  If SIGNAL is
     nonzero, give it that signal.  No error is thrown if NTI
     disappears while we try to resume it.  */
  virtual void resume_one_nti (nonstop_thread_info *nti, bool step,
			       int signal, void *siginfo) = 0;

  /* Return true if THREAD is doing hardware single step.  */
  bool maybe_hw_step (thread_info *thread);

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

  /* The target-specific way of sending a stop request to NTI.  */
  virtual void low_send_sigstop (nonstop_thread_info *nti) = 0;

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
