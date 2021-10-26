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
#include "gdbsupport/common-debug.h"
#include <unordered_map>

int using_threads = 1;

constexpr unsigned long TIMEOUT_INFINITE = (unsigned long) -1;
constexpr unsigned long TIMEOUT_NOHANG = 1;

extern unsigned long int intelgt_hostpid;

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
	  debug_printf ("%s: ", __FUNCTION__);			\
	  debug_printf (__VA_ARGS__);				\
	  debug_printf ("\n");					\
	}							\
    }								\
  while (0)


/* gdbserver-gt register group information.  */

enum class reg_group : unsigned short
  {
    Address = 0,
    Accumulator,
    Flag,
    ChannelEnable,
    StackPointer,
    State,
    Control,
    NotificationCount,
    ProgramCounter,
    ThreadDependency,
    Timestamp,
    FlowControl,
    Grf,
    ExecMaskPseudo,
    Mme,
    SBA,
    Debug,
    Count
  };

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

/* Parse group string (e.g. from a feature xml) as reg_group.
   Returns reg_group::Count if match was not found.  */

static reg_group
string_to_group (const std::string &name)
{
  static const char *names[(int) reg_group::Count]
      = { "address",
	  "accumulator",
	  "flag",
	  "channel_enable",
	  "stack_pointer",
	  "state",
	  "control",
	  "notification_count",
	  "program_counter",
	  "thread_dependency",
	  "timestamp",
	  "flow_control",
	  "grf",
	  "exec_mask_pseudo",
	  "mme",
	  "sba",
	  "vdr" };

  int idx = 0;
  for (const char *s : names)
    {
      if (name == s)
	return (reg_group) idx;
      ++idx;
    }

  return reg_group::Count;
}

/* Convert an internal register group to igfxdbg register type.  */

static RegisterType
igfxdbg_reg_type (reg_group group)
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
    case reg_group::ProgramCounter:
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
    case reg_group::Debug:
      return eExecMaskPseudoRegister;
    case reg_group::Mme:
      return eArfMmeRegister;
    case reg_group::SBA:
      return eDebugPseudoRegister;
    }

  return eInvalidRegisterType;
}

/* GT-specific process info to save as process_info's
   private target data.  */

struct process_info_private : public nonstop_process_info
{
  /* GT device handle.  */
  GTDeviceHandle device_handle;

  /* GT device info.  */
  GTDeviceInfo device_info;

  /* DCD device index.  */
  unsigned int dcd_device_index;

  /* Map of global regnum to the in-group regnums */
  std::unordered_map<int, int> regnum_groups;
};

static std::unordered_map<GTDeviceHandle,
			  process_info_private *> process_infos;

/* Calculate regnum relative to a register position
   within own group and store it the returned map.  */

static std::unordered_map<int, int>
calculate_reg_groups (target_desc *tdesc)
{
  std::unordered_map<std::string, long> groups;
  std::unordered_map<int, int> result;

  for (tdesc_feature_up &feature : tdesc->features)
    {
      for (tdesc_reg_up &reg : feature->registers)
	{
	  result[reg->target_regnum] = groups[reg->group];
	  groups[reg->group]++;
	}
    }

  return result;
}

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

static process_info *add_new_gt_process (process_info_private *proc_priv);

/* Given a GTEvent, return the corresponding process_info.  */

static process_info *
find_process_from_gt_event (GTEvent *event)
{
  process_info *proc
    = find_process ([event] (process_info *p)
	{
	  return event->device == p->priv->device_handle;
	});

  if (proc == nullptr)
    {
      if (event->type == eGfxDbgEventDeviceExited)
	return nullptr;

      /* This is the first time we see an event from this device.  */
      process_info_private *proc_priv = process_infos[event->device];
      if (proc_priv == nullptr)
	error (_("received an event from an unitialized device"));

      proc = add_new_gt_process (proc_priv);
    }

  return proc;
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
      if (event->type != eGfxDbgEventThreadStarted)
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
		   int len, unsigned int addr_space = 0) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len, unsigned int addr_space = 0) override;

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

  bool supports_multi_process () override;

  bool supports_pid_to_exec_file () override;

  const char *pid_to_exec_file (int pid) override;

  void initialize_device (unsigned int dcd_device_index);

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

  ptid_t handle_interrupt_timedout (GTEvent *event, target_waitstatus *status);

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

/* Create a GT target description.  Important requirement is
   for each individual feature/regset to list registers in the same
   order as the intended DWARF numbering order for that regset.  */

static target_desc *
create_target_description (GTDeviceInfo &info)
{
  target_desc_up tdesc = allocate_target_description ();

  set_tdesc_architecture (tdesc.get (), "intelgt");
  set_tdesc_osabi (tdesc.get (), "GNU/Linux");
  set_tdesc_device (tdesc.get (), std::to_string (info.gen_major).c_str ());

  struct tdesc_feature *feature;
  long regnum = 0;
  char reg_name[6] = {0};

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_grf);
  for (int i = 0; i <= 127; ++i)
    {
      snprintf (reg_name, 6, "r%d", i);
      tdesc_create_reg (feature, reg_name, regnum++, 1, "grf", 256, "uint256");
    }

  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.debug");
  tdesc_create_reg (feature, "emask", regnum++, 1, "vdr", 32, "uint32");
  tdesc_create_reg (feature, "iemask", regnum++, 1, "vdr", 32, "uint32");

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_sba);
  tdesc_create_reg (feature, "btbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "scrbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "genstbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "sustbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "blsustbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "blsastbase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "isabase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "iobase", regnum++, 1, "sba", 64, "uint64");
  tdesc_create_reg (feature, "dynbase", regnum++, 1, "sba", 64, "uint64");

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_addr);
  tdesc_create_reg (feature, "a0", regnum++, 1, "address", 256, "uint256");

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_acc);
  for (int i = 0; i <= 9; ++i)
    {
      snprintf (reg_name, 6, "acc%d", i);
      tdesc_create_reg (feature, reg_name, regnum++, 1, "accumulator", 256, "uint256");
    }

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_flag);
  tdesc_create_reg (feature, "f0", regnum++, 1, "flag", 32, "uint32");
  tdesc_create_reg (feature, "f1", regnum++, 1, "flag", 32, "uint32");
  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.ce");
  tdesc_create_reg (feature, "ce", regnum++, 1, "channel_enable", 32, "uint32");
  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.state");
  tdesc_create_reg (feature, "sr0", regnum++, 1, "state", 128, "uint128");
  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.control");
  tdesc_create_reg (feature, "cr0", regnum++, 1, "control", 128, "uint128");
  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.td");
  tdesc_create_reg (feature, "tdr", regnum++, 1, "thread_dependency", 128, "uint128");
  feature = tdesc_create_feature (tdesc.get (), "org.gnu.gdb.intelgt.timestamp");
  tdesc_create_reg (feature, "tm0", regnum++, 1, "timestamp", 128, "uint128");

  feature = tdesc_create_feature (tdesc.get (), intelgt::feature_mme);
  for (int i = 0; i <= 7; ++i)
    {
      snprintf (reg_name, 6, "mme%d", i);
      tdesc_create_reg (feature, reg_name, regnum++, 1, "mme", 256, "uint256");
    }

  return tdesc.release ();
}

/* Iterate all features until the register with the requested
   target regnum is found.  Shouldn't be necessary once gdbserver
   tdesc.h is more in sync with gdbsupport/tdesc.h.  */
static const tdesc_reg *
tdesc_find_register (const target_desc *tdesc, int index)
{
  for (const tdesc_feature_up &feature : tdesc->features)
    for (const tdesc_reg_up &reg : feature->registers)
      if (reg->target_regnum == index)
	return reg.get ();

  return nullptr;
}

/* Add a new process using the given PROC_PRIV.  */
static process_info *
add_new_gt_process (process_info_private *proc_priv)
{
  static const char *expedite_regs[] = {"cr0", "emask", nullptr};

  GTDeviceInfo &info = proc_priv->device_info;
  switch (info.gen_major)
    {
    case 9:
    case 11:
    case 12:
      break;

    default:
      error (_("The GT %d.%d architecture is not supported"),
	     info.gen_major, info.gen_minor);
    }

  target_desc *tdesc = create_target_description (info);
  init_target_desc (tdesc, expedite_regs);

  unsigned int device_index = proc_priv->dcd_device_index + 1;

  process_info *proc = add_process (device_index, 1 /* attached */);
  proc_priv->regnum_groups = calculate_reg_groups (tdesc);
  proc->priv = proc_priv;
  proc->tdesc = tdesc;

  fprintf (stderr, "intelgt: attached to device %d of %d;"
	   " id 0x%x (Gen%d)\n", device_index, igfxdbg_NumDevices (),
	   info.device_id, info.gen_major);

  return proc;
}

/* Initialize the device at index DCD_DEVICE_INDEX for debug.  */

void
intelgt_process_target::initialize_device (unsigned int dcd_device_index)
{
  /* For device initialization we need the host pid and the device
     index.  For the host pid, we use the --hostpid argument.  */

  GTDeviceHandle device;
  GTDeviceInfo info;

  APIResult result
    = igfxdbg_InitDevice ((ProcessID) intelgt_hostpid, dcd_device_index,
			  &device, &info, -1);
  if (result != eGfxDbgResultSuccess)
    error (_("failed to initialize intelgt device for debug"));

  process_info_private *proc_priv = new struct process_info_private;
  proc_priv->device_handle = device;
  proc_priv->device_info = info;
  proc_priv->dcd_device_index = dcd_device_index;

  process_infos[device] = proc_priv;

  dprintf ("initialized device [hostpid: %lu, dcd instance: %d, "
	   "id: 0x%x (Gen%d)]", intelgt_hostpid, dcd_device_index,
	   info.device_id, info.gen_major);
}

/* The 'attach' target op for the given process id.
   Returns -1 if attaching is unsupported, 0 on success, and calls
   error() otherwise.  */

int
intelgt_process_target::attach (unsigned long device_index)
{
  if (device_index == 0)
    {
      /* Just initialize and return.  We rely on waiting the target
	 and adding the process when the first stop event is
	 received.  */
      igfxdbg_SetDefaultShaderEnabled (false);
      for (int i = 0; i < igfxdbg_NumDevices (); ++i)
	initialize_device (i);
      return 0;
    }

  /* DCD uses 0-based indexing.  We show 1-based indexing because
     "0" in a ptid has special meaning in GDB.  */
  if (device_index > igfxdbg_NumDevices ())
    error (_("no device '%ld' found, there are %d devices"),
	   device_index, igfxdbg_NumDevices ());
  unsigned int dcd_device_index = device_index - 1;
  initialize_device (dcd_device_index);

  process_info_private *proc_priv = nullptr;
  for (auto &info : process_infos)
    if (info.second->dcd_device_index == dcd_device_index)
      {
	proc_priv = info.second;
	break;
      }

  if (proc_priv == nullptr)
    error (_("no device with index %lu is found"), device_index);

  add_new_gt_process (proc_priv);

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
  delete proc->priv;
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


  thread_info *gdb_thread = find_thread_from_gt_event (event);
  dprintf ("Added %s", target_pid_to_str (gdb_thread->id));
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
  if (proc == nullptr)
    return null_ptid;

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

/* Handle an 'interrupt timeout' event.  */

ptid_t
intelgt_process_target::handle_interrupt_timedout (GTEvent *event,
						   target_waitstatus *status)
{
  gdb_assert (event->type == eGfxDbgEventInterruptTimedOut);
  interrupt_in_progress = false;
  status->kind = TARGET_WAITKIND_NO_RESUMED;

  if (event->device == nullptr)
    return minus_one_ptid;

  process_info *proc = find_process_from_gt_event (event);
  return ptid_t (proc->pid);
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

    case eGfxDbgEventInterruptTimedOut:
      dprintf ("Processing an interrupt timeout");
      return handle_interrupt_timedout (event, status);

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
    device_handle = nullptr; /* Match any device.  */
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
  const tdesc_reg* reg = tdesc_find_register (current_process ()->tdesc, index);
  if (reg == nullptr)
    error (_("register %d was not found in tdesc"), index);
  int regsize = reg->bitsize / 8;
  std::unique_ptr<gdb_byte[]> buffer (new gdb_byte[regsize]);
  memset(buffer.get(), 0, regsize);

  if (reg->name == "isabase")
    {
      /* Need to pretend $isabase is always 0 for the legacy ELF binary
         format to work, as it expects $pc to be the same as $ip.  */
      supply_register (regcache, index, buffer.get ());
      return;
    }

  reg_group group = string_to_group (reg->group);
  if (group >= reg_group::Count)
    error (_("register %d is of unknown group %s"), index,
	   reg->group.c_str ());

  RegisterType regtype = igfxdbg_reg_type (group);
  int gindex = current_process ()->priv->regnum_groups[index];
  APIResult result
    = igfxdbg_ReadRegisters (thread, regtype,
			     /* igfxdbg includes iemask/emask in debug group, adjust: */
			     regtype == eDebugPseudoRegister ? gindex + 2 : gindex,
			     buffer.get (), regsize);

  if (result != eGfxDbgResultSuccess)
    error (_("could not read the register %d %d %d; result: %s"),
	   index, regtype, gindex, igfxdbg_result_to_string (result));

  supply_register (regcache, index, buffer.get ());
}

/* The 'fetch_registers' target op.  */

void
intelgt_process_target::fetch_registers (regcache *regcache, int regno)
{
  dprintf ("regno: %d", regno);

  GTThreadHandle handle = get_intelgt_thread (current_thread)->handle;

  if (regno == -1) /* All registers.  */
    for (int i = 0; i < regcache->tdesc->reg_defs.size (); i++)
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
  const tdesc_reg* reg = tdesc_find_register (current_process ()->tdesc, index);
  if (reg == nullptr)
    error (_("register %d was not found in tdesc"), index);
  reg_group group = string_to_group (reg->group);
  if (group >= reg_group::Count)
    error (_("register %d is of unknown group %s"), index,
	   reg->group.c_str ());
  int regsize = reg->bitsize / 8;
  std::unique_ptr<gdb_byte[]> buffer (new gdb_byte[regsize]);
  memset(buffer.get(), 0, regsize);

  collect_register (regcache, index, buffer.get ());
  RegisterType regtype = igfxdbg_reg_type (group);
  int gindex = current_process ()->priv->regnum_groups[index];
  APIResult result
    = igfxdbg_WriteRegisters (thread, igfxdbg_reg_type (group),
			      /* igfxdbg includes iemask/emask in debug group, adjust: */
			      regtype == eDebugPseudoRegister ? gindex + 2 : gindex,
			      buffer.get (), regsize);
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

  if (regno == -1) /* All registers.  */
    for (int i = 0; i < regcache->tdesc->reg_defs.size (); i++)
      write_gt_register (regcache, handle, i);
  else
    write_gt_register (regcache, handle, regno);
}

/* The 'read_memory' target op.
   Returns 0 on success and errno on failure.  */

int
intelgt_process_target::read_memory (CORE_ADDR memaddr,
				     unsigned char *myaddr, int len,
				     unsigned int addr_space)
{
  dprintf ("memaddr: %s, len: %d", core_addr_to_string_nz (memaddr), len);

  if (len == 0)
    {
      /* Zero length read always succeeds.  */
      return 0;
    }

  GTThreadHandle handle = nullptr;
  if (current_thread != nullptr)
    handle = get_intelgt_thread (current_thread)->handle;

  GTDeviceHandle device = current_process ()->priv->device_handle;

  unsigned read_size = 0;
  APIResult result = igfxdbg_ReadMemory (handle, memaddr, myaddr,
					 len, &read_size, device);
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
				      const unsigned char *myaddr, int len,
				      unsigned int addr_space)
{
  dprintf ("memaddr: %s, len: %d", core_addr_to_string_nz (memaddr), len);

  if (len == 0)
    {
      /* Zero length write always succeeds.  */
      return 0;
    }

  GTThreadHandle handle = nullptr;
  if (current_thread != nullptr)
    handle = get_intelgt_thread (current_thread)->handle;

  GTDeviceHandle device = current_process ()->priv->device_handle;

  unsigned written_size = 0;
  APIResult result = igfxdbg_WriteMemory (handle, memaddr, myaddr,
					  len, &written_size, device);
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

  if (interrupt_in_progress)
    {
      dprintf ("request ignored; an interrupt is already in progress");
      return;
    }

  for_each_process ([] (process_info *proc)
    {
      GTDeviceHandle device = proc->priv->device_handle;
      APIResult result = igfxdbg_Interrupt (device);
      if (result != eGfxDbgResultSuccess)
	error (_("could not interrupt; result: %s"),
	       igfxdbg_result_to_string (result));
      interrupt_in_progress = true;
    });

  if (!interrupt_in_progress)
    {
      /* No process exists yet that we can interrupt.  Send a generic
	 interrupt.  */
      APIResult result = igfxdbg_Interrupt (nullptr);
      if (result != eGfxDbgResultSuccess)
	error (_("could not interrupt; result: %s"),
	       igfxdbg_result_to_string (result));
      interrupt_in_progress = true;
    }
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
  int regno = find_regno (regcache->tdesc, "cr0");
  uint8_t cr0[16];
  collect_register (regcache, regno, &cr0);
  /* CR0 elements are 4 byte wide. $ip is the same as CR0.2 */
  CORE_ADDR ip = * (CORE_ADDR*) (cr0 + 8);
  dprintf ("ip: %lx", ip);
  return ip;
}

/* The 'write_pc' target op.  */

void
intelgt_process_target::write_pc (struct regcache *regcache, CORE_ADDR ip)
{
  int regno = find_regno (regcache->tdesc, "cr0");
  dprintf ("ip: %s", core_addr_to_string_nz (ip));
  uint8_t cr0[16];
  collect_register (regcache, regno, &cr0);
  * (CORE_ADDR*) (cr0 + 8) = ip;
  supply_register (regcache, regno, &cr0);
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

  bool is_breakpoint = false;
  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = read_memory (where, inst, intelgt::MAX_INST_LENGTH);
  if (err == 0)
    is_breakpoint = intelgt::has_breakpoint (inst);
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

  for_each_process ([=] (process_info *proc)
    {
      if (pid != -1 && pid != proc->pid)
	return;

      regcache_invalidate_pid (proc->pid);

      GTDeviceHandle device = proc->priv->device_handle;
      APIResult result = igfxdbg_ContinueExecutionAll (device);
      if (result != eGfxDbgResultSuccess)
	error (_("failed to continue all the threads; result: %s"),
	       igfxdbg_result_to_string (result));
    });

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

bool
intelgt_process_target::supports_multi_process ()
{
  return true;
}

bool
intelgt_process_target::supports_pid_to_exec_file ()
{
  return true;
}

const char *
intelgt_process_target::pid_to_exec_file (int pid)
{
  return "";
}

/* The Intel GT target ops object.  */

static intelgt_process_target the_intelgt_target;

void
initialize_low ()
{
  if (intelgt_hostpid == 0)
    error (_("intelgt: a HOSTPID must be specified via --hostpid."));
  dprintf ("intelgt: using %lu as the host pid\n", intelgt_hostpid);

  set_target_ops (&the_intelgt_target);
}
