/* Low-level interface for the Intel(R) Graphics Technology target,
   for the remote server of GDB.
   Copyright (C) 2019-2020 Free Software Foundation, Inc.

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

#include "../features/intelgt-grf.c"
#include "../features/intelgt-arf9.c"
#include "../features/intelgt-arf11.c"

int using_threads = 1;

constexpr unsigned long TIMEOUT_INFINITE = (unsigned long) -1;

static intelgt::arch_info *intelgt_info;

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
    }

  return eInvalidRegisterType;
}

/* GT-specific process info to save as process_info's
   private target data.  */

struct process_info_private
{
  /* GT device handle.  */
  GTDeviceHandle device_handle;
};

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

  /* FIXME: Make thread_id 'long' and pid 'int' in igfxdbg.h.  */
  ptid_t ptid = ptid_t {(int) event->pid, (long) info.thread_id, 0l};
  thread_info *gdb_thread = find_thread_ptid (ptid);

  if (gdb_thread == nullptr)
    {
      dprintf ("An unknown GT thread detected; adding to the list");
      gdb_thread = add_thread (ptid, event->thread);
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
      /* At this point, we would have to 'free' gdb_thread->target_data
	 had we dynamically allocated it.  Because it is an opaque pointer
	 to us, we simply set the pointer to null.  */
      gdb_thread->target_data = nullptr;
      remove_thread (gdb_thread);
    });
}

/* Target op definitions for an Intel GT target.  */

class intelgt_process_target : public process_stratum_target
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

  void resume (thread_resume *resume_info, size_t n) override;

  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       target_wait_flags options) override;

  void fetch_registers (regcache *regcache, int regno) override;

  void store_registers (regcache *regcache, int regno) override;

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr,
		   int len) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len) override;

  void request_interrupt () override;

  bool supports_z_point_type (char z_type) override;

  int insert_point (enum raw_bkpt_type type, CORE_ADDR addr,
		    int size, raw_breakpoint *bp) override;

  int remove_point (enum raw_bkpt_type type, CORE_ADDR addr,
		    int size, raw_breakpoint *bp) override;

  bool supports_hardware_single_step () override;

  CORE_ADDR read_pc (regcache *regcache) override;

  void write_pc (regcache *regcache, CORE_ADDR pc) override;

  bool supports_thread_stopped () override;

  bool thread_stopped (thread_info *thread) override;

  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size) override;

private:

  target_desc *create_target_description (intelgt::version gt_version);

  void read_gt_register (regcache *regcache, GTThreadHandle thread,
			 int index);

  void write_gt_register (regcache *regcache, GTThreadHandle thread,
			  int index);
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
    }

  intelgt_info = intelgt::arch_info::get_or_create (gt_version);

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

  target_desc *tdesc = nullptr;
  switch (info.gen_major)
    {
    case 9:
      tdesc = create_target_description (intelgt::version::Gen9);
      break;

    case 11:
      tdesc = create_target_description (intelgt::version::Gen11);
      break;

    default:
      error (_("The GT %d.%d architecture is not supported"),
	     info.gen_major, info.gen_minor);
    }

  init_target_desc (tdesc, expedite_regs);

  process_info *proc = add_process (pid, 1 /* attached */);
  process_info_private *proc_priv = XCNEW (struct process_info_private);
  proc_priv->device_handle = device;
  proc->priv = proc_priv;
  proc->tdesc = tdesc;

  fprintf (stderr, "intelgt: attached to device with id 0x%x (Gen%d)\n",
	   info.device_id, info.gen_major);

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

  GTThreadHandle handle = (GTThreadHandle) gdb_thread->target_data;

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

/* The 'resume' target op.  */

void
intelgt_process_target::resume (thread_resume *resume_info, size_t n)
{
  ptid_t ptid = resume_info->thread;
  enum resume_kind kind = resume_info->kind;

  dprintf ("thread: %s, resume: %d", target_pid_to_str (ptid), kind);

  if (ptid == minus_one_ptid)
    {
      regcache_invalidate ();

      if (kind == resume_step)
	error (_("single-stepping all threads is not supported"));
      else
	{
	  GTDeviceHandle device = current_process ()->priv->device_handle;
	  APIResult result = igfxdbg_ContinueExecutionAll (device);
	  if (result != eGfxDbgResultSuccess)
	    error (_("failed to continue all the threads; result: %s"),
		   igfxdbg_result_to_string (result));
	}
    }
  else
    {
      thread_info *gdb_thread = find_thread_ptid (ptid);
      regcache_invalidate_thread (gdb_thread);
      GTThreadHandle handle = (GTThreadHandle) gdb_thread->target_data;

      if (kind == resume_step)
	{
	  if (resume_info->step_range_end != 0)
	    error (_("range-stepping not supported"));

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
    }
}

/* Handle a 'kernel loaded' event.  */

static void
handle_kernel_loaded (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventKernelLoaded);
  gdb_assert (event->kernel != nullptr);

  loaded_dll (event->details.kernel_load_event.pathname,
	      event->details.kernel_load_event.load_address);
}

/* Handle a 'kernel unloaded' event.  */

static void
handle_kernel_unloaded (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventKernelUnloaded);
  gdb_assert (event->kernel != nullptr);

  unloaded_dll (event->details.kernel_load_event.pathname,
		event->details.kernel_load_event.load_address);
}

/* Handle a 'thread started' event.  */

static void
handle_thread_started (int parent_pid, GTEvent *event)
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

  /* FIXME: Make thread_id 'long' in igfxdbg.h.  */
  ptid_t ptid = ptid_t {parent_pid, (long) info.thread_id, 0l};
  add_thread (ptid, event->thread);

  dprintf ("Added %s", target_pid_to_str (ptid));
}

/* Handle a 'thread stopped' event.  */

static ptid_t
handle_thread_stopped (GTEvent *event, target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventThreadStopped);
  gdb_assert (event->thread != nullptr);

  thread_info *gdb_thread = find_thread_from_gt_event (event);

  status->kind = TARGET_WAITKIND_STOPPED;
  if (event->details.stopped_from_interrupt)
    /* FIXME: This is a HACK against all-stop multi-target.
       Normally we should pass GDB_SIGNAL_INT.  */
    status->value.sig = GDB_SIGNAL_0;
  else
    status->value.sig = GDB_SIGNAL_TRAP;

  /* Mark this event as pending.  If this is going to be reported,
     we will clear the flag in 'wait'.  */
  gdb_thread->last_status = *status;
  gdb_thread->status_pending_p = 1;

  ptid_t ptid = gdb_thread->id;
  dprintf ("Marked stop event of %s", target_pid_to_str (ptid));

  return ptid;
}

/* Handle a 'thread exited' event.  */

static void
handle_thread_exited (GTEvent *event)
{
  gdb_assert (event->type == eGfxDbgEventThreadExited);
  gdb_assert (event->thread != nullptr);

  thread_info *gdb_thread = find_thread_from_gt_event (event);

  ptid_t ptid = gdb_thread->id;
  dprintf ("Removing thread %s", target_pid_to_str (ptid));

  /* At this point, we would have to 'free' gdb_thread->target_data,
     had we dynamically allocated it.  Because it is an opaque pointer
     to us, we simply set the pointer to null.  */
  gdb_thread->target_data = nullptr;
  remove_thread (gdb_thread);
}

/* Handle a 'device exited' event.  */

static ptid_t
handle_device_exited (GTEvent *event, target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventDeviceExited);

  status->kind = TARGET_WAITKIND_EXITED;
  status->value.integer = /* exit code */ 0;

  /* FIXME: Make pid 'int' in igfxdbg.h.  */
  int pid = (int) event->pid;
  clear_all_threads (pid);

  return ptid_t {pid};
}

/* Handle a 'step completed' event.  */

static ptid_t
handle_step_completed (GTEvent *event, target_waitstatus *status)
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

  return ptid;
}

/* Process a single event.  */

static ptid_t
process_single_event (int parent_pid, GTEvent *event,
		      target_waitstatus *status)
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
      handle_thread_started (parent_pid, event);
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
intelgt_process_target::wait (ptid_t ptid, target_waitstatus *status,
			      target_wait_flags options)
{
  dprintf ("ptid: %s, options: 0x%x", target_pid_to_str (ptid), (int) options);

  if (!(ptid.is_pid () || ptid == minus_one_ptid))
    error (_("Waiting on an individual thread is not supported"));

  process_info *proc;
  if (ptid == minus_one_ptid)
    proc = current_process ();
  else
    proc = find_process_pid (ptid.pid ());
  if (proc == nullptr)
    error (_("%s: cannot find process_info for pid %s"), __FUNCTION__,
	   target_pid_to_str (ptid));

  GTDeviceHandle device_handle = proc->priv->device_handle;
  GTEvent gt_event;
  gt_event.size_of_this = sizeof (gt_event);

  ptid_t id = null_ptid;

  while (id == null_ptid)
    {
      APIResult result = igfxdbg_WaitForEvent (device_handle, &gt_event,
					       TIMEOUT_INFINITE);
      if (result != eGfxDbgResultSuccess)
	{
	  dprintf (_("failed to wait on the device; result: %s"),
		   igfxdbg_result_to_string (result));
	  return minus_one_ptid;
	}
      /* Process all the events, report the first stop event.  The
	 other stop events are not reported now, but stay as pending
	 in their eventing thread.  */
      GTEvent *event = &gt_event;
      while (event != nullptr)
	{
	  struct target_waitstatus event_status;
	  ptid_t eventing_ptid
	    = process_single_event (proc->pid, event, &event_status);

	  if (id == null_ptid && eventing_ptid != null_ptid)
	    {
	      /* This is the event we will report.  */
	      id = eventing_ptid;
	      *status = event_status;
	      thread_info *thread = find_thread_ptid (id);
	      if (thread != nullptr)
		thread->status_pending_p = 0;
	    }
	  event = event->next;
	}

      result = igfxdbg_ReleaseEvent (device_handle, &gt_event);
      if (result != eGfxDbgResultSuccess)
	{
	  dprintf (_("failed to release the event; result: %s"),
		   igfxdbg_result_to_string (result));
	  return minus_one_ptid;
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

  GTThreadHandle handle = (GTThreadHandle) current_thread->target_data;

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

  GTThreadHandle handle = (GTThreadHandle) current_thread->target_data;
  if (!igfxdbg_IsThreadStopped (handle))
    return;

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

  GTThreadHandle handle = (GTThreadHandle) current_thread->target_data;

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

  GTThreadHandle handle = (GTThreadHandle) current_thread->target_data;

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

  GTDeviceHandle device_handle = current_process ()->priv->device_handle;
  APIResult result = igfxdbg_Interrupt (device_handle);
  if (result != eGfxDbgResultSuccess)
    error (_("could not interrupt; result: %s"),
	   igfxdbg_result_to_string (result));
}

/* The 'supports_z_point_type' target op.  */

bool
intelgt_process_target::supports_z_point_type (char z_type)
{
  dprintf ("z_type: %c", z_type);

  switch (z_type)
    {
    case Z_PACKET_SW_BP:
      return true;
    case Z_PACKET_HW_BP:
    case Z_PACKET_WRITE_WP:
    case Z_PACKET_READ_WP:
    case Z_PACKET_ACCESS_WP:
    default:
      return false;
    }
}

/* The 'insert_point' target op.
   Returns 0 on success, -1 on failure and 1 on unsupported.  */

int
intelgt_process_target::insert_point (enum raw_bkpt_type type,
				      CORE_ADDR addr, int kind,
				      raw_breakpoint *bp)
{
  dprintf ("type: %d, addr: %s, kind: %d", type,
	   core_addr_to_string_nz (addr), kind);

  if (type != raw_bkpt_type_sw)
    return 1;

  return insert_memory_breakpoint (bp);
}

/* The 'remove_point' target op.
   Returns 0 on success, -1 on failure and 1 on unsupported.  */

int
intelgt_process_target::remove_point (enum raw_bkpt_type type,
				      CORE_ADDR addr, int kind,
				      raw_breakpoint* bp)
{
  dprintf ("type: %d, addr: %s, kind: %d", type,
	   core_addr_to_string_nz (addr), kind);

  if (type != raw_bkpt_type_sw)
    return 1;

  return remove_memory_breakpoint (bp);
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

  GTThreadHandle handle = (GTThreadHandle) gdb_thread->target_data;
  return igfxdbg_IsThreadStopped (handle);
}

/* The 'sw_breakpoint_from_kind' target op.  */

const gdb_byte*
intelgt_process_target::sw_breakpoint_from_kind (int kind, int *size)
{
  dprintf ("kind: %d", kind);

  switch (kind)
    {
    case intelgt::BP_INSTRUCTION:
      *size = intelgt_info->breakpoint_inst_length ();
      return intelgt_info->breakpoint_inst ();
    }

  dprintf ("Unrecognized breakpoint kind: %d", kind);

  *size = 0;
  return NULL;
}

bool
intelgt_process_target::supports_hardware_single_step ()
{
  return true;
}

/* The Intel GT target ops object.  */

static intelgt_process_target the_intelgt_target;

void
initialize_low ()
{
  set_target_ops (&the_intelgt_target);
}
