/* Low-level interface for the Intel(R) Graphics Technology target,
   for the remote server of GDB.
   Copyright (C) 2019-2021 Free Software Foundation, Inc.

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
#include "arch/intelgt.h"
#include "dll.h"
#include "hostio.h"
#include "igfxdbg.h"
#include "regcache.h"
#include "tdesc.h"
#include "nonstop-low.h"

#include "../features/intelgt-grf.c"
#include "../features/intelgt-arf9.c"
#include "../features/intelgt-arf11.c"
#include "../features/intelgt-arf12.c"

int using_threads = 1;

constexpr unsigned long TIMEOUT_INFINITE = (unsigned long) -1;
constexpr unsigned long TIMEOUT_NOHANG = 1;

/* The device event that we shall process next.  */

static GTEvent *next_event = nullptr;

/* The flag that denotes whether we have issued an interrupt request
   for which we have not checked the stop events, yet.  The purpose of
   this flag is to prevent sending the request multiple times.  */
static bool interrupt_in_progress = false;

/* Convenience macros.  */

#define dprintf(...)						\
  do								\
    {								\
      if (debug_threads)					\
	{							\
	  fprintf (stderr, "%s: ", __FUNCTION__);		\
	  fprintf (stderr, __VA_ARGS__);			\
	  fprintf (stderr, "\n");				\
	  fflush (stderr);					\
	}							\
    }								\
  while (0)

/* Convert an igfxdbg library return value to string.  */

static const char *
igfxdbg_result_to_string (APIResult result)
{
  switch (result)
    {
    case eGfxDbgResultSuccess:
      return _("Success");
    case eGfxDbgResultFailure:
      return _("Failure");
    case eGfxDbgResultInvalidHandle:
      return _("Invalid handle");
    case eGfxDbgResultInvalidRequest:
      return _("Invalid request");
    case eGfxDbgResultInvalidRange:
      return _("Invalid range");
    case eGfxDbgResultInvalidIndex:
      return _("Invalid index");
    case eGfxDbgResultIncorrectRegisterSize:
      return _("Incorrect register size");
    case eGfxDbgResultThreadNotStopped:
      return _("Thread not stopped");
    case eGfxDbgResultUnsupportedRegister:
      return _("Unsupported register");
    case eGfxDbgResultTimedOut:
      return _("Timed out");
    case eGfxDbgResultWrongVersion:
      return _("Wrong version");
    case eGfxDbgResultOptionNotSupported:
      return _("Option not supported");
    case eGfxDbgResultWrongValueForOption:
      return _("Wrong value for option");
    case eGfxDbgResultIncorrectRegistrySettings:
      return _("Incorrect registry settings");
    }

  return _("Unknown error");
}

/* Convert an internal register group to igfxdbg register type.  */

static RegisterType
igfxdbg_reg_type (intelgt::reg_group group)
{
  using namespace intelgt;

  switch (group)
    {
    case reg_group::Address:
      return eArfAddressRegister;
    case reg_group::Accumulator:
      return eArfAccumulatorRegister;
    case reg_group::Flag:
      return eArfFlagRegister;
    case reg_group::ChannelEnable:
      return eArfChannelEnableRegister;
    case reg_group::StackPointer:
      return eArfStackPointerRegister;
    case reg_group::State:
      return eArfStateRegister;
    case reg_group::Control:
      return eArfControlRegister;
    case reg_group::NotificationCount:
      return eArfNotificationCountRegister;
    case reg_group::InstructionPointer:
      return eArfInstructionPointerRegister;
    case reg_group::ThreadDependency:
      return eArfThreadDependencyRegister;
    case reg_group::Timestamp:
      return eArfTimestampRegister;
    case reg_group::FlowControl:
      return eArfFlowControlRegister;
    case reg_group::Grf:
      return eGrfRegister;
    case reg_group::ExecMaskPseudo:
      return eExecMaskPseudoRegister;
    case reg_group::Mme:
      return eArfMmeRegister;
    }

  return eInvalidRegisterType;
}

/* GT-specific process info to save as process_info's
   private target data.  */

struct process_info_private : public nonstop_process_info
{
  /* GT device handle.  */
  GTDeviceHandle device_handle;

  /* Architectural info.  */
  intelgt::arch_info *intelgt_info;
};

/* GT-specific thread info to save as thread_info's
   private target data.  */

struct intelgt_thread : public nonstop_thread_info
{
  /* GT Thread handle.  */
  GTThreadHandle handle;
};

/* Given a THREAD, return the intelgt_thread data stored
   as its target data.  */

static intelgt_thread *
get_intelgt_thread (thread_info *thread)
{
  return static_cast<intelgt_thread *> (get_thread_nti (thread));
}

/* Find the architectural info for the current process.  */

static intelgt::arch_info *
get_intelgt_info ()
{
  process_info *proc = current_process ();
  return proc->priv->intelgt_info;
}

/* Given a GTEvent, return the corresponding process_info.  */

static process_info *
find_process_from_gt_event (GTEvent *event)
{
  return find_process ([event] (process_info *p)
	   {
	     return event->device == p->priv->device_handle;
	   });
}

/* Given a GTEvent, return the corresponding thread_info.  */

static thread_info *
find_thread_from_gt_event (GTEvent *event)
{
  ThreadDetails info;
  info.size_of_this = sizeof (info);

  APIResult result = igfxdbg_GetThreadDetails (event->thread, &info);
  if (result != eGfxDbgResultSuccess)
    error (_("could not get thread details; result: %s"),
	   igfxdbg_result_to_string (result));

  process_info *proc = find_process_from_gt_event (event);
  gdb_assert (proc != nullptr);
  /* FIXME: Make thread_id 'long' in igfxdbg.h.  */
  ptid_t ptid = ptid_t {proc->pid, (long) info.thread_id, 0l};
  thread_info *gdb_thread = find_thread_ptid (ptid);

  if (gdb_thread == nullptr)
    {
      dprintf ("An unknown GT thread detected; adding to the list");
      intelgt_thread *new_thread = new intelgt_thread {};
      new_thread->handle = event->thread;
      gdb_thread = add_thread (ptid, new_thread);
      new_thread->thread = gdb_thread;
    }

  return gdb_thread;
}

/* Remove all threads that have the given process PID.  */

static void
clear_all_threads (int pid)
{
  dprintf ("Clearing all threads of %d", pid);

  for_each_thread (pid, [] (thread_info *gdb_thread)
    {
      dprintf ("Deleting %s", target_pid_to_str (gdb_thread->id));
      intelgt_thread *gt_thread = get_intelgt_thread (gdb_thread);
      delete gt_thread;
      gdb_thread->target_data = nullptr;
      remove_thread (gdb_thread);
    });
}

/* Target op definitions for an Intel GT target.  */

class intelgt_process_target : public nonstop_process_target
{
public:

  int create_inferior
      (const char *program,
       const std::vector<char *> &program_args) override;

  int attach (unsigned long pid) override;

  int kill (process_info *proc) override;

  int detach (process_info *proc) override;

  void mourn (process_info *proc) override;

  void join (int pid) override;

  bool thread_alive (ptid_t pid) override;

  void fetch_registers (regcache *regcache, int regno) override;

  void store_registers (regcache *regcache, int regno) override;

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr,
		   int len) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len) override;

  void request_interrupt () override;

  bool supports_z_point_type (char z_type) override;

  bool supports_hardware_single_step () override;

  CORE_ADDR read_pc (regcache *regcache) override;

  void write_pc (regcache *regcache, CORE_ADDR pc) override;

  bool supports_thread_stopped () override;

  bool thread_stopped (thread_info *thread) override;

  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size) override;

  bool supports_stopped_by_sw_breakpoint () override;

  bool stopped_by_sw_breakpoint () override;

protected: /* Target ops from nonstop_process_target.  */

  ptid_t low_wait (ptid_t ptid, target_waitstatus *ourstatus,
		   int target_options) override;

  bool supports_breakpoints () override;

  void resume_one_nti (nonstop_thread_info *nti, bool step, int signal,
		       void *siginfo) override;

  void low_send_sigstop (nonstop_thread_info *nti) override;

  bool supports_resume_all () override;

  void resume_all_threads (int pid) override;

  bool thread_still_has_status_pending (thread_info *thread) override;

  bool thread_needs_step_over (thread_info *thread) override;

  void start_step_over (thread_info *thread) override;

private:

  target_desc *create_target_description (intelgt::version gt_version);

  void read_gt_register (regcache *regcache, GTThreadHandle thread,
			 int index);

  void write_gt_register (regcache *regcache, GTThreadHandle thread,
			  int index);

  CORE_ADDR get_pc (nonstop_thread_info *nti);

  bool breakpoint_at (CORE_ADDR where);

  void wait_for_sigstop ();

  void handle_kernel_loaded (GTEvent *event);

  void handle_kernel_unloaded (GTEvent *event);

  void handle_thread_started (GTEvent *event);

  ptid_t handle_thread_stopped (GTEvent *event, target_waitstatus *status);

  void handle_thread_exited (GTEvent *event);

  ptid_t handle_device_exited (GTEvent *event, target_waitstatus *status);

  ptid_t handle_step_completed (GTEvent *event, target_waitstatus *status);

  ptid_t process_single_event (GTEvent *event, target_waitstatus *status,
			       int options);

  void process_thread_stopped_event (thread_info *gdb_thread, GTEvent *event,
				     target_waitstatus *status,
				     bool mark_pending);
};

/* The 'create_inferior' target op.
   gdbserver cannot create a GT inferior.  */

int
intelgt_process_target::create_inferior (const char *program,
					 const std::vector<char *> &args)
{
  error (_("Inferior creation not supported; "
	   "consider using the --attach or --multi option."));

  return -1; /* Failure */
}

/* Create a GT target description.  */

target_desc *
intelgt_process_target::create_target_description (
    intelgt::version gt_version)
{
  target_desc_up tdesc = allocate_target_description ();

  set_tdesc_architecture (tdesc.get (), "intelgt");
  set_tdesc_osabi (tdesc.get (), "GNU/Linux");

  long regnum = create_feature_intelgt_grf (tdesc.get (), 0);

  switch (gt_version)
    {
    case intelgt::version::Gen9:
      regnum = create_feature_intelgt_arf9 (tdesc.get (), regnum);
      break;
    case intelgt::version::Gen11:
      regnum = create_feature_intelgt_arf11 (tdesc.get (), regnum);
      break;
    case intelgt::version::Gen12:
      regnum = create_feature_intelgt_arf12 (tdesc.get (), regnum);
      break;
    }

  return tdesc.release ();
}

/* The 'attach' target op for the given process id.
   Returns -1 if attaching is unsupported, 0 on success, and calls
   error() otherwise.  */

int
intelgt_process_target::attach (unsigned long pid)
{
  dprintf ("Attaching to pid %lu", pid);

  GTDeviceHandle device;
  GTDeviceInfo info;

  APIResult result = igfxdbg_Init ((ProcessID) pid, &device, &info, -1);
  if (result != eGfxDbgResultSuccess)
    error (_("failed to initialize intelgt device for debug"));

  static const char *expedite_regs[] = {"ip", "sp", "emask", nullptr};

  intelgt::version gt_version;
  switch (info.gen_major)
    {
    case 9:
      gt_version = intelgt::version::Gen9;
      break;

    case 11:
      gt_version = intelgt::version::Gen11;
      break;

    case 12:
      gt_version = intelgt::version::Gen12;
      break;

    default:
      error (_("The GT %d.%d architecture is not supported"),
	     info.gen_major, info.gen_minor);
    }

  target_desc *tdesc = create_target_description (gt_version);
  init_target_desc (tdesc, expedite_regs);

  process_info *proc = add_process (pid, 1 /* attached */);
  process_info_private *proc_priv = XCNEW (struct process_info_private);
  proc_priv->device_handle = device;
  proc_priv->intelgt_info
    = intelgt::arch_info::get_or_create (gt_version);
  proc->priv = proc_priv;
  proc->tdesc = tdesc;

  fprintf (stderr, "intelgt: attached to device with id 0x%x (Gen%d)\n",
	   info.device_id, info.gen_major);

  /* FIXME: At this point, we have not added any threads, yet.
     This creates a problem in nonstop mode.
     We may want to hang here until the first thread creation event
     is received.  */
  if (target_is_async_p ())
    async_file_mark ();

  return 0; /* success */
}

/* The 'detach' target op.
   Return -1 on failure, and 0 on success.  */

int
intelgt_process_target::detach (process_info *proc)
{
  dprintf ("pid: %d", proc->pid);

  mourn (proc);

  return 0;
}

/* The 'kill' target op.
   Return -1 on failure, and 0 on success.  */

int
intelgt_process_target::kill (process_info *proc)
{
  dprintf ("pid: %d", proc->pid);
  /* For now kill is the same as detach.  */
  return detach (proc);
}

/* The 'mourn' target op.  */

void
intelgt_process_target::mourn (process_info *proc)
{
  dprintf ("Process pid; %d", proc->pid);

  APIResult result = igfxdbg_ShutDown (proc->priv->device_handle);
  if (result != eGfxDbgResultSuccess)
    {
      dprintf (_("could not shutdown the device; result: %s"),
	       igfxdbg_result_to_string (result));
    }

  clear_all_threads (proc->pid);
  free (proc->priv);
  proc->priv = nullptr;
  remove_process (proc);
}

/* The 'join' target op.
   Wait for inferior PID to exit.  */

void
intelgt_process_target::join (int pid)
{
  dprintf ("pid: %d", pid);
  /* Shutdown in 'detach' is sufficient.  Do nothing.  */
}

/* The 'thread_alive' target op.  */

bool
intelgt_process_target::thread_alive (ptid_t ptid)
{
  dprintf ("ptid: %s", target_pid_to_str (ptid));

  thread_info *gdb_thread = find_thread_ptid (ptid);
  if (gdb_thread == nullptr)
    return false;

  GTThreadHandle handle = get_intelgt_thread (gdb_thread)->handle;

  ThreadDetails info;
  info.size_of_this = sizeof (info);

  APIResult result = igfxdbg_GetThreadDetails (handle, &info);
  if (result != eGfxDbgResultSuccess)
    {
      dprintf (_("could not get thread details; result: %s"),
	       igfxdbg_result_to_string (result));
      return false;
    }

  return (info.is_alive) ? true : false;
}

/* Handle a 'kernel loaded' event.  */

void
intelgt_process_target::handle_kernel_loaded (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventKernelLoaded);
  gdb_assert (event->kernel != nullptr);

  process_info *proc = find_process_from_gt_event (event);
  loaded_dll (proc, event->details.kernel_load_event.pathname,
	      event->details.kernel_load_event.load_address);
}

/* Handle a 'kernel unloaded' event.  */

void
intelgt_process_target::handle_kernel_unloaded (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventKernelUnloaded);
  gdb_assert (event->kernel != nullptr);

  process_info *proc = find_process_from_gt_event (event);
  unloaded_dll (proc, event->details.kernel_load_event.pathname,
		event->details.kernel_load_event.load_address);
}

/* Handle a 'thread started' event.  */

void
intelgt_process_target::handle_thread_started (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventThreadStarted);

  if (event->thread == nullptr)
    error (_("Got a nullptr thread handle"));

  ThreadDetails info;
  info.size_of_this = sizeof (info);

  APIResult result = igfxdbg_GetThreadDetails (event->thread, &info);
  if (result != eGfxDbgResultSuccess)
    error (_("could not get details about new thread; result: %s"),
	   igfxdbg_result_to_string (result));

  process_info *proc = find_process_from_gt_event (event);
  gdb_assert (proc != nullptr);
  /* FIXME: Make thread_id 'long' in igfxdbg.h.  */
  ptid_t ptid = ptid_t {proc->pid, (long) info.thread_id, 0l};
  intelgt_thread *new_thread = new intelgt_thread {};
  new_thread->handle = event->thread;
  new_thread->thread = add_thread (ptid, new_thread);

  dprintf ("Added %s", target_pid_to_str (ptid));
}

void
intelgt_process_target::process_thread_stopped_event (thread_info *gdb_thread,
						      GTEvent *event,
						      target_waitstatus *status,
						      bool mark_pending)
{
  nonstop_thread_info *nti = get_thread_nti (gdb_thread);
  nti->stopped = true;
  nti->stop_expected = false;
  nti->stop_reason = TARGET_STOPPED_BY_NO_REASON;
  gdb_thread->last_resume_kind = resume_stop;

  status->kind = TARGET_WAITKIND_STOPPED;
  if (event->details.stopped_from_interrupt)
    {
      status->value.sig = GDB_SIGNAL_0;
      interrupt_in_progress = false;
    }
  else
    {
      status->value.sig = GDB_SIGNAL_TRAP;
      if (breakpoint_at (get_pc (nti)))
	nti->stop_reason = TARGET_STOPPED_BY_SW_BREAKPOINT;
    }

  /* Mark this event as pending.  If this is going to be reported,
     we will clear the flag in 'wait'.  */
  if (mark_pending)
    {
      gdb_thread->last_status = *status;
      gdb_thread->status_pending_p = 1;

      dprintf ("Marked stop event of %s", target_pid_to_str (gdb_thread->id));
    }
  else
    dprintf ("Processed stop event of %s", target_pid_to_str (gdb_thread->id));
}

/* Handle a 'thread stopped' event.  */

ptid_t
intelgt_process_target::handle_thread_stopped (GTEvent *event,
					       target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventThreadStopped);
  gdb_assert (event->thread != nullptr);

  ptid_t ptid = null_ptid;

  thread_info *gdb_thread = find_thread_from_gt_event (event);
  dprintf ("gdb_thread: %s", target_pid_to_str (gdb_thread->id));

  /* FIXME: This is a workaround.  If this is the result of an interrupt,
     mark all the running threads as stopped.  */
  if (event->details.stopped_from_interrupt)
    {
      dprintf ("stop event is from an interrupt");

      bool mark_pending = true;
      process_info *proc = find_process_from_gt_event (event);
      for_each_thread (proc->pid, [&] (thread_info *thread)
	{
	  if (mark_pending)
	    ptid = thread->id;

	  if (!thread->status_pending_p)
	    {
	      process_thread_stopped_event (thread, event, status,
					    mark_pending);
	      /* If in all-stop mode, mark only one thread with a pending
		 stop event.  The others are stopped internally and not
		 reported to GDB.  */
	      if (!non_stop)
		mark_pending = false;
	    }
	});
    }
  else
    {
      nonstop_thread_info *nti = get_thread_nti (gdb_thread);
      if (nti->stopped)
	{
	  dprintf ("Thread %s is already stopped, not reporting",
		   target_pid_to_str (gdb_thread->id));
	  return null_ptid;
	}
      process_thread_stopped_event (gdb_thread, event, status, true);
      ptid = gdb_thread->id;
    }

  return ptid;
}

/* Handle a 'thread exited' event.  */

void
intelgt_process_target::handle_thread_exited (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventThreadExited);
  gdb_assert (event->thread != nullptr);

  thread_info *gdb_thread = find_thread_from_gt_event (event);

  ptid_t ptid = gdb_thread->id;
  dprintf ("Removing thread %s", target_pid_to_str (ptid));

  intelgt_thread *gt_thread = get_intelgt_thread (gdb_thread);
  delete gt_thread;
  gdb_thread->target_data = nullptr;
  remove_thread (gdb_thread);
}

/* Handle a 'device exited' event.  */

ptid_t
intelgt_process_target::handle_device_exited (GTEvent *event,
					      target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventDeviceExited);

  status->kind = TARGET_WAITKIND_EXITED;
  status->value.integer = /* exit code */ 0;

  process_info *proc = find_process_from_gt_event (event);
  gdb_assert (proc != nullptr);

  return ptid_t {proc->pid};
}

/* Handle a 'step completed' event.  */

ptid_t
intelgt_process_target::handle_step_completed (GTEvent *event,
					       target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventStepCompleted);
  gdb_assert (event->thread != nullptr);

  thread_info *gdb_thread = find_thread_from_gt_event (event);
  ptid_t ptid = gdb_thread->id;

  status->kind = TARGET_WAITKIND_STOPPED;
  status->value.sig = GDB_SIGNAL_TRAP;

  /* Mark this event as pending.  If this is going to be reported,
     we will clear the flag in 'wait'.  */
  gdb_thread->last_status = *status;
  gdb_thread->status_pending_p = 1;
  nonstop_thread_info *nti = get_thread_nti (gdb_thread);
  nti->stopped = true;
  nti->stop_reason = TARGET_STOPPED_BY_SINGLE_STEP;

  return ptid;
}

/* Process a single event.  */

ptid_t
intelgt_process_target::process_single_event (GTEvent *event,
					      target_waitstatus *status,
					      int options)
{
  switch (event->type)
    {
    case eGfxDbgEventDeviceExited:
      dprintf ("Processing a device exited event");
      return handle_device_exited (event, status);

    case eGfxDbgEventThreadStopped:
      dprintf ("Processing a thread stopped event");
      return handle_thread_stopped (event, status);

    case eGfxDbgEventThreadStarted:
      dprintf ("Processing a thread started event");
      handle_thread_started (event);
      return null_ptid;

    case eGfxDbgEventKernelLoaded:
      dprintf ("Processing a kernel loaded event");
      handle_kernel_loaded (event);
      return null_ptid;

    case eGfxDbgEventThreadExited:
      dprintf ("Processing a thread exited event");
      handle_thread_exited (event);
      return null_ptid;

    case eGfxDbgEventKernelUnloaded:
      dprintf ("Processing a kernel unloaded event");
      handle_kernel_unloaded (event);
      return null_ptid;

    case eGfxDbgEventStepCompleted:
      dprintf ("Processing a step completed event");
      return handle_step_completed (event, status);

    case eGfxDbgEventInvalid:
    case eGfxDbgEventReserved:
    case eGfxDbgEventThreadsStopped:
      break;
    }

  error (_("Unsupported GT event type: %d"), event->type);
}

/* The 'wait' target op.  */

ptid_t
intelgt_process_target::low_wait (ptid_t ptid, target_waitstatus *status,
				  int options)
{
  if (!non_stop)
    dprintf ("ptid: %s, options: 0x%x", target_pid_to_str (ptid), options);

  if (!(ptid.is_pid () || ptid == minus_one_ptid))
    error (_("Waiting on an individual thread is not supported"));

  GTDeviceHandle device_handle;
  if (ptid == minus_one_ptid)
    {
      if (current_thread == nullptr)
	{
	  status->kind = TARGET_WAITKIND_IGNORE;
	  return null_ptid;
	}
      device_handle = nullptr; /* Match any device.  */
    }
  else
    {
      process_info *proc = find_process_pid (ptid.pid ());
      if (proc == nullptr)
	error (_("%s: cannot find process_info for pid %s"), __FUNCTION__,
	       target_pid_to_str (ptid));
      device_handle = proc->priv->device_handle;
    }

  static GTEvent gt_event;
  gt_event.size_of_this = sizeof (gt_event);

  ptid_t id = null_ptid;

  while (id == null_ptid)
    {
      unsigned long timeout = TIMEOUT_INFINITE;
      if ((options & TARGET_WNOHANG) != 0)
	timeout = TIMEOUT_NOHANG;

      if (next_event == nullptr)
	{
	  APIResult result = igfxdbg_WaitForEvent (device_handle, &gt_event,
						   timeout);
	  if (result == eGfxDbgResultTimedOut
	      && timeout != TIMEOUT_INFINITE)
	    {
	      if (target_is_async_p ())
		async_file_mark ();
	      status->kind = TARGET_WAITKIND_IGNORE;
	      return null_ptid;
	    }

	  if (result != eGfxDbgResultSuccess)
	    {
	      dprintf (_("failed to wait on the device; result: %s"),
		       igfxdbg_result_to_string (result));
	      return minus_one_ptid;
	    }

	  next_event = &gt_event;
	}

      /* All-stop:
	 Process all the events, report the first stop event.  The
	 other stop events are not reported now, but stay as pending
	 in their eventing thread.

	 Non-stop:
	 Report the first stop event; do not process the remaining ones
	 now.  Just keep them under the GT_EVENT pointer.  They will be
	 reported when the 'wait' op is called.  */
      while (next_event != nullptr)
	{
	  struct target_waitstatus event_status;
	  ptid_t eventing_ptid
	    = process_single_event (next_event, &event_status, options);

	  next_event = next_event->next;
	  if (id == null_ptid && eventing_ptid != null_ptid)
	    {
	      /* This is the event we will report.  */
	      id = eventing_ptid;
	      *status = event_status;
	      thread_info *thread = find_thread_ptid (id);
	      if (thread != nullptr)
		thread->status_pending_p = 0;
	      if (non_stop)
		break;
	    }
	}

      if (next_event == nullptr)
	{
	  APIResult result = igfxdbg_ReleaseEvent (device_handle, &gt_event);
	  if (result != eGfxDbgResultSuccess)
	    {
	      dprintf (_("failed to release the event; result: %s"),
		       igfxdbg_result_to_string (result));
	      return minus_one_ptid;
	    }
	}
    }

  return id;
}

/* Read a register from the GT device into regcache.
   INDEX is the index of the register in regcache.  */

void
intelgt_process_target::read_gt_register (regcache *regcache,
					  GTThreadHandle thread, int index)
{
  intelgt::arch_info *intelgt_info = get_intelgt_info ();
  const intelgt::gt_register &reg = intelgt_info->get_register (index);
  gdb_assert (reg.size_in_bytes <= intelgt_info->max_reg_size ());
  unsigned int buffer_size = intelgt_info->max_reg_size ();
  std::unique_ptr<gdb_byte[]> buffer (new gdb_byte[buffer_size]);

  APIResult result
    = igfxdbg_ReadRegisters (thread, igfxdbg_reg_type (reg.group),
			     reg.local_index, buffer.get (),
			     reg.size_in_bytes);
  if (result != eGfxDbgResultSuccess)
    error (_("could not read a register; result: %s"),
	   igfxdbg_result_to_string (result));

  supply_register (regcache, index, buffer.get ());
}

/* The 'fetch_registers' target op.  */

void
intelgt_process_target::fetch_registers (regcache *regcache, int regno)
{
  dprintf ("regno: %d", regno);

  GTThreadHandle handle = get_intelgt_thread (current_thread)->handle;
  intelgt::arch_info *intelgt_info = get_intelgt_info ();

  if (regno == -1) /* All registers.  */
    for (int i = 0; i < intelgt_info->num_registers (); i++)
      read_gt_register (regcache, handle, i);
  else
    read_gt_register (regcache, handle, regno);
}

/* Write a register from the regcache into the GT device.
   INDEX is the index of the register in regcache.  */

void
intelgt_process_target::write_gt_register (regcache *regcache,
					   GTThreadHandle thread, int index)
{
  intelgt::arch_info *intelgt_info = get_intelgt_info ();
  const intelgt::gt_register &reg = intelgt_info->get_register (index);
  gdb_assert (reg.size_in_bytes <= intelgt_info->max_reg_size ());
  unsigned int buffer_size = intelgt_info->max_reg_size ();
  std::unique_ptr<gdb_byte[]> buffer (new gdb_byte[buffer_size]);

  collect_register (regcache, index, buffer.get ());
  APIResult result
    = igfxdbg_WriteRegisters (thread, igfxdbg_reg_type (reg.group),
			      reg.local_index, buffer.get (),
			      reg.size_in_bytes);
  if (result != eGfxDbgResultSuccess)
    error (_("could not write a register; result: %s"),
	   igfxdbg_result_to_string (result));
}

/* The 'store_registers' target op.  */

void
intelgt_process_target::store_registers (regcache *regcache, int regno)
{
  dprintf ("regno: %d", regno);

  GTThreadHandle handle = get_intelgt_thread (current_thread)->handle;
  if (!igfxdbg_IsThreadStopped (handle))
    return;

  intelgt::arch_info *intelgt_info = get_intelgt_info ();

  if (regno == -1) /* All registers.  */
    for (int i = 0; i < intelgt_info->num_registers (); i++)
      write_gt_register (regcache, handle, i);
  else
    write_gt_register (regcache, handle, regno);
}

/* The 'read_memory' target op.
   Returns 0 on success and errno on failure.  */

int
intelgt_process_target::read_memory (CORE_ADDR memaddr,
				     unsigned char *myaddr, int len)
{
  dprintf ("memaddr: %s, len: %d", core_addr_to_string_nz (memaddr), len);

  if (len == 0)
    {
      /* Zero length read always succeeds.  */
      return 0;
    }

  GTThreadHandle handle = get_intelgt_thread (current_thread)->handle;

  unsigned read_size = 0;
  APIResult result = igfxdbg_ReadMemory (handle, memaddr, myaddr,
					 len, &read_size);
  if (result != eGfxDbgResultSuccess)
    {
      dprintf (_("failed to read memory; result: %s"),
	       igfxdbg_result_to_string (result));
      return EIO;
    }

  /* FIXME: igfxdbg ignores READ_SIZE.  */

  return 0;
}

/* The 'write_memory' target op.
   Returns 0 on success and errno on failure.  */

int
intelgt_process_target::write_memory (CORE_ADDR memaddr,
				      const unsigned char *myaddr, int len)
{
  dprintf ("memaddr: %s, len: %d", core_addr_to_string_nz (memaddr), len);

  if (len == 0)
    {
      /* Zero length write always succeeds.  */
      return 0;
    }

  GTThreadHandle handle = get_intelgt_thread (current_thread)->handle;

  unsigned written_size = 0;
  APIResult result = igfxdbg_WriteMemory (handle, memaddr, myaddr,
					  len, &written_size);
  if (result != eGfxDbgResultSuccess)
    {
      dprintf (_("failed to write memory; result: %s"),
	       igfxdbg_result_to_string (result));
      return EIO;
    }

  /* FIXME: igfxdbg ignores WRITTEN_SIZE.  */

  return 0;
}

/* The 'request_interrupt' target op.  */

void
intelgt_process_target::request_interrupt ()
{
  dprintf ("attempting interrupt");

  /* If we don't have a current_thread, we cannot interrupt.  This
     case may happen when the kernel already terminated but GDB sent
     the interrupt request before receiving our exit event.  */
  if (current_thread == nullptr)
    return;

  if (interrupt_in_progress)
    {
      dprintf ("request ignored; an interrupt is already in progress");
      return;
    }

  GTDeviceHandle device_handle = current_process ()->priv->device_handle;
  APIResult result = igfxdbg_Interrupt (device_handle);
  if (result != eGfxDbgResultSuccess)
    error (_("could not interrupt; result: %s"),
	   igfxdbg_result_to_string (result));

  interrupt_in_progress = true;
}

/* The 'supports_z_point_type' target op.  */

bool
intelgt_process_target::supports_z_point_type (char z_type)
{
  dprintf ("z_type: %c", z_type);

  /* We do not support breakpoints.

     Use gdbarch methods that use read/write memory target operations for
   setting s/w breakopints.  */
  return false;
}

/* The 'read_pc' target op.  */

CORE_ADDR
intelgt_process_target::read_pc (struct regcache *regcache)
{
  int regno = find_regno (regcache->tdesc, "ip");
  unsigned int buf;
  collect_register (regcache, regno, &buf);
  dprintf ("regno: %d, ip: %x", regno, buf);

  return (CORE_ADDR) buf;
}

/* The 'write_pc' target op.  */

void
intelgt_process_target::write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  int regno = find_regno (regcache->tdesc, "ip");
  dprintf ("regno: %d, ip: %s", regno, core_addr_to_string_nz (pc));
  supply_register (regcache, regno, &pc);
}

/* The 'thread_stopped' target op.  */

bool
intelgt_process_target::supports_thread_stopped ()
{
  return true;
}

bool
intelgt_process_target::thread_stopped (struct thread_info *gdb_thread)
{
  dprintf ("pid: %s", target_pid_to_str (gdb_thread->id));

  GTThreadHandle handle = get_intelgt_thread (gdb_thread)->handle;
  return igfxdbg_IsThreadStopped (handle);
}

/* The 'sw_breakpoint_from_kind' target op.  */

const gdb_byte*
intelgt_process_target::sw_breakpoint_from_kind (int kind, int *size)
{
  dprintf ("kind: %d", kind);

  /* We do not support breakpoint instructions.

     Use gdbarch methods that use read/write memory target operations for
     setting s/w breakopints.  */
  *size = 0;
  return NULL;
}

bool
intelgt_process_target::supports_hardware_single_step ()
{
  return true;
}

bool
intelgt_process_target::breakpoint_at (CORE_ADDR where)
{
  dprintf ("where: %s", core_addr_to_string_nz (where));

  intelgt::arch_info *intelgt_info = get_intelgt_info ();
  bool is_breakpoint = false;
  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = read_memory (where, inst, intelgt::MAX_INST_LENGTH);
  if (err == 0)
    is_breakpoint = intelgt_info->has_breakpoint (inst);
  else
    dprintf ("failed to read memory at %s", core_addr_to_string_nz (where));

  dprintf ("%sbreakpoint found.", is_breakpoint ? "" : "no ");
  return is_breakpoint;
}

CORE_ADDR
intelgt_process_target::get_pc (nonstop_thread_info *nti)
{
  dprintf ("nti: %s", target_pid_to_str (nti->thread->id));

  return read_pc (get_thread_regcache (nti->thread, 1));
}

bool
intelgt_process_target::supports_breakpoints ()
{
  return true;
}

void
intelgt_process_target::resume_one_nti (nonstop_thread_info *nti, bool step,
					int signal, void *siginfo)
{
  dprintf ("nti: %s, step: %d, signal: %d",
	   target_pid_to_str (nti->thread->id), step, signal);

  if (!nti->stopped)
    return;

  if (nti->thread->status_pending_p)
    {
      dprintf ("not resuming; has pending status");
      return;
    }

  process_info *proc = get_thread_process (nti->thread);
  if (proc->tdesc != nullptr)
    dprintf ("  %s from pc 0x%lx", step ? "step" : "continue",
	     (long) get_pc (nti));

  regcache_invalidate_thread (nti->thread);

  GTThreadHandle handle = get_intelgt_thread (nti->thread)->handle;
  if (step)
    {
      APIResult result = igfxdbg_StepOneInstruction (handle);
      if (result != eGfxDbgResultSuccess)
	error (_("failed to step the thread; result: %s"),
	       igfxdbg_result_to_string (result));
    }
  else
    {
      APIResult result = igfxdbg_ContinueExecution (handle);
      if (result != eGfxDbgResultSuccess)
	error (_("failed to continue the thread; result: %s"),
	       igfxdbg_result_to_string (result));
    }

  nti->stopped = false;
  nti->stop_reason = TARGET_STOPPED_BY_NO_REASON;
  nti->thread->status_pending_p = 0;
}

void
intelgt_process_target::low_send_sigstop (nonstop_thread_info *nti)
{
  dprintf ("nti: %s", target_pid_to_str (nti->thread->id));
  if (nti->stopped)
    dprintf ("thread already stopped");

  request_interrupt ();

  if (target_is_async_p ())
    async_file_mark ();
}

void
intelgt_process_target::wait_for_sigstop ()
{
  dprintf ("enter");

  if (!interrupt_in_progress)
    return;

  /* Interrupt halts the whole device.  Receiving an stop event from a
     single thread is sufficient to conclude that the device stopped,
     if the thread stopped due to a interrupt request.  */
  target_waitstatus status;
  while (true)
    {
      ptid_t event_ptid = wait (minus_one_ptid, &status, 0);
      if (status.kind == TARGET_WAITKIND_STOPPED
	  && status.value.sig == GDB_SIGNAL_0)
	break; /* We got what we were expecting.  */
      else
	{
	  /* Mark this as pending, and keep listening.  */
	  thread_info *thread = find_thread_ptid (event_ptid);
	  if (thread == nullptr)
	    continue;

	  thread->last_status = status;
	  thread->status_pending_p = true;
	  dprintf ("unexpected event; thread: %s, kind: %d, signal: %d",
		   target_pid_to_str (thread->id),
		   status.kind, status.value.sig);
	}
    }

  for_each_thread ([] (thread_info *thread)
    {
      nonstop_thread_info *nti = get_thread_nti (thread);
      nti->stop_expected = false;
      nti->stopped = true;
    });
}

bool
intelgt_process_target::supports_resume_all ()
{
  return true;
}

void
intelgt_process_target::resume_all_threads (int pid)
{
  dprintf ("enter, pid: %d", pid);

  regcache_invalidate ();

  GTDeviceHandle device = current_process ()->priv->device_handle;
  APIResult result = igfxdbg_ContinueExecutionAll (device);
  if (result != eGfxDbgResultSuccess)
    error (_("failed to continue all the threads; result: %s"),
	   igfxdbg_result_to_string (result));

  for_each_thread ([] (thread_info *thread)
    {
      nonstop_thread_info *nti = get_thread_nti (thread);
      nti->stopped = false;
      nti->stop_reason = TARGET_STOPPED_BY_NO_REASON;
    });

  if (target_is_async_p ())
    async_file_mark ();
}

bool
intelgt_process_target::thread_still_has_status_pending (thread_info *thread)
{
  dprintf ("thread: %s", target_pid_to_str (thread->id));
  return thread->status_pending_p;
}

bool
intelgt_process_target::thread_needs_step_over (thread_info *thread)
{
  dprintf ("thread: %s", target_pid_to_str (thread->id));

  /* GDB should be handling step-over for us.  */
  return false;
}

void
intelgt_process_target::start_step_over (thread_info *thread)
{
  dprintf ("thread: %s", target_pid_to_str (thread->id));
  /* Do nothing.  GDB should be handling step-over via resume
     requests.  */
}

bool
intelgt_process_target::supports_stopped_by_sw_breakpoint ()
{
  return true;
}

bool
intelgt_process_target::stopped_by_sw_breakpoint ()
{
  nonstop_thread_info *nti = get_thread_nti (current_thread);

  return (nti->stop_reason == TARGET_STOPPED_BY_SW_BREAKPOINT);
}

/* The Intel GT target ops object.  */

static intelgt_process_target the_intelgt_target;

void
initialize_low ()
{
  set_target_ops (&the_intelgt_target);
}
