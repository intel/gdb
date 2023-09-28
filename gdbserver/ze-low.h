/* Target interface for level-zero based targets for gdbserver.
   See https://github.com/oneapi-src/level-zero.git.

   Copyright (C) 2020-2023 Free Software Foundation, Inc.

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

#ifndef GDBSERVER_LEVEL_ZERO_LOW_H
#define GDBSERVER_LEVEL_ZERO_LOW_H

#include "target.h"
#include "tdesc.h"

#include <level_zero/zet_api.h>
#include <string>
#include <vector>
#include <list>

/* Ze-low target's packet buffer size.  */
#define ZE_TARGET_PBUFSIZ 33808

/* Information about register sets reported in target descriptions.

   The main use of this is to find the information relevant for fetching
   and storing registers via level-zero base on register numbers.  */
struct ze_regset_info
{
  /* The device-specific level-zero register set type.  */
  uint32_t type;

  /* The register size in bytes for reading/writing.  */
  uint32_t size;

  /* The begin (inclusive) and end (exclusive) register numbers for this
     regset.

     This is used to map register numbers to regset types.  */
  long begin, end;

  /* Whether the regset is writable.  We assume all are readable.  */
  bool is_writeable;
};

/* A vector of regset infos.  */
typedef std::vector<ze_regset_info> ze_regset_info_t;

/* A vector of expedite register names.

   The names are expected to be string literals.  The vector must be
   terminated with a single nullptr entry.  */
typedef std::vector<const char *> expedite_t;

/* A list of debug events.  */

typedef std::list<zet_debug_event_t> events_t;

/* Information about devices we're attached to.

   This is pretty similar to process_info.  The difference is that we only
   want to tell GDB about devices that the host application actually uses.
   To know that, however, we need to attach to all available devices.  */

struct ze_device_info
{
  /* The debug session configuration.  */
  zet_debug_config_t config = {};

  /* The device handle.  This must not be nullptr.  */
  ze_device_handle_t handle = nullptr;

  /* The device's properties.  */
  ze_device_properties_t properties = {};

  /* The debug session handle.

     This is nullptr if we are not currently attached.  */
  zet_debug_session_handle_t session = nullptr;

  /* The state for debug attach attempt.

     This is complementary information for debug session handle.  The
     debug session handle is null, when debug attach attempt fails.
     In this case, debug attach state contains more information on
     the last error.  */
  ze_result_t debug_attach_state;

  /* The target description for this device.  */
  target_desc_up tdesc;

  /* The register sets reported in the device's target description.  */
  ze_regset_info_t regsets;

  /* The expedite registers used for this device's target description.  */
  expedite_t expedite;

  /* The device enumeration ordinal number.  */
  unsigned long ordinal = 0;

  /* The process for this device.

     We model devices we're attached to as inferior process.  In GDB, we
     hide inferiors representing devices that are not currently used and
     only show inferiors for devices that are in use.

     If we are not attached to this device, PROCESS will be nullptr.  */
  process_info *process = nullptr;

  /* A list of to-be-acknowledged events.  */
  events_t ack_pending;

  /* Total number of threads on this device.  */
  unsigned long nthreads = 0;

  /* Number of resumed threads.  The value is useful for deciding if
     we can omit sending an actual interrupt request when we want all
     threads to be stopped in all-stop mode.

     The value can underflow because of unavailable threads becoming
     available and generating stop events.  Therefore we pay care to
     prevent underflowing.  */
  unsigned long nresumed = 0;

  /* Number of interrupts sent to this target.  */
  unsigned long ninterrupts = 0;

  /* Device location as a string, built from PCI properties.  */
  std::string pci_slot;
};

/* A thread's resume state.

   This is very similar to enum resume_kind except that we need an
   additional none case to model the thread not being mentioned in any
   resume request.  */

enum ze_thread_resume_state_t
{
  /* Gdbserver did not ask anything of this thread.  */
  ze_thread_resume_none,

  /* The thread shall stop.  */
  ze_thread_resume_stop,

  /* The thread shall run.  */
  ze_thread_resume_run,

  /* The thread shall step.  */
  ze_thread_resume_step
};

/* A thread's execution state.  */

enum ze_thread_exec_state_t
{
  /* We do not know the thread state.  This is likely an error condition.  */
  ze_thread_state_unknown,

  /* The thread is stopped and is expected to remain stopped until we
     resume it.  */
  ze_thread_state_stopped,

  /* The thread is stopped but we are holding its stop event until we
     resume it.  */
  ze_thread_state_held,

  /* The thread is running.  We do not know whether it is still available
     to us and we're able to stop it or whether it would eventually hit a
     breakpoint.

     When a thread completes executing a kernel it becomes idle and may
     pick up other workloads, either in this context or in another
     process' context.

     In the former case, it would still be considered RUNNING from our
     point of view, even though it started over again with a new set of
     arguments.  In the latter case, it would be UNAVAILABLE.  */
  ze_thread_state_running,

  /* The thread is currently not available to us.  It may be idle or it
     may be executing work on behalf of a different process.

     We cannot distinguish those cases.  We're not able to interact with
     that thread.  It may become available again at any time, though.

     From GDB's view, a thread may switch between RUNNING and UNAVAILABLE.
     We will only know the difference when we try to stop it.  It's not
     entirely clear whether we need to distinguish the two, at all.  */
  ze_thread_state_unavailable
};

/* Thread private data for level-zero targets.  */

struct ze_thread_info
{
  /* The thread identifier.  */
  ze_device_thread_t id;

  /* The thread's resume state.

     What does gdbserver want this thread to do.  */
  enum ze_thread_resume_state_t resume_state = ze_thread_resume_none;

  /* The start/end addresses for range-stepping.  */
  CORE_ADDR step_range_start = 0;
  CORE_ADDR step_range_end = 0;

  /* The thread's execution state.

     What is this thread actually doing.  */
  enum ze_thread_exec_state_t exec_state = ze_thread_state_unknown;

  /* The thread's stop reason.

     This is only valid if EXEC_STATE == ZE_THREAD_STATE_STOPPED
     or EXEC_STATE == ZE_THREAD_STATE_HELD.  */
  target_stop_reason stop_reason = TARGET_STOPPED_BY_NO_REASON;

  /* The waitstatus for this thread's last event.

     TARGET_WAITKIND_IGNORE means that there is no last event.  */
  target_waitstatus waitstatus {};
};

/* Return the ZE thread info for TP.  */

static inline
ze_thread_info *
ze_thread (thread_info *tp)
{
  if (tp == nullptr)
    return nullptr;

  return (ze_thread_info *) tp->target_data;
}

/* Return the ZE thread info for const TP.  */

static inline
const ze_thread_info *
ze_thread (const thread_info *tp)
{
  if (tp == nullptr)
    return nullptr;

  return (const ze_thread_info *) tp->target_data;
}

/* Return the level-zero thread id for all threads.  */

static inline ze_device_thread_t
ze_thread_id_all ()
{
  ze_device_thread_t all;
  all.slice = UINT32_MAX;
  all.subslice = UINT32_MAX;
  all.eu = UINT32_MAX;
  all.thread = UINT32_MAX;

  return all;
}

/* Return true if TID is the all thread id.  */

static inline bool
ze_is_thread_id_all (ze_device_thread_t tid)
{
  return (tid.slice == UINT32_MAX
	  && tid.subslice == UINT32_MAX
	  && tid.eu == UINT32_MAX
	  && tid.thread == UINT32_MAX);
}

/* Return the level-zero thread id for THREAD.  */

static inline ze_device_thread_t
ze_thread_id (const thread_info *thread)
{
  const ze_thread_info *zetp = ze_thread (thread);
  if (zetp == nullptr)
    error (_("No thread."));

  return zetp->id;
}

/* Return a human-readable device thread id string.  */

extern std::string ze_thread_id_str (const ze_device_thread_t &thread);

/* The state of a process.  */

enum ze_process_state
{
  /* The process is visible to the user.  */
  ze_process_visible,

  /* The process is hidden from the user.  */
  ze_process_hidden
};

/* Process info private data for level-zero targets.  */

struct process_info_private
{
  /* The device we're modelling as process.

     In case we get forcefully detached from the device this process
     represents, DEVICE will be nullptr.  The process will remain until
     the detach event can be reported to GDB.  */
  ze_device_info *device;

  /* The state of this process.  */
  ze_process_state state;

  /* The waitstatus for this process's last event.

     While stop events are reported on threads, module loads and unloads
     as well as entry and exit are reports on the process itself.

     Neither of these events implies that any of the process' threads
     stopped or is even available.

     TARGET_WAITKIND_IGNORE means that there is nothing to report.  */
  target_waitstatus waitstatus {};

  process_info_private (ze_device_info *dev, ze_process_state st)
    : device (dev), state (st)
    {}
};

/* Target op definitions for level-zero based targets.  */

class ze_target : public process_stratum_target
{
public:
  /* Initialize the level-zero target.

     We cannot do this inside the ctor since zeInit() would generate a
     worker thread that would inherit the uninitialized async I/O
     state.

     Postpone initialization until after async I/O has been
     initialized.  */
  void init ();

  bool supports_hardware_single_step () override { return true; }
  bool supports_range_stepping () override { return true; }
  bool supports_multi_process () override { return true; }
  bool supports_non_stop () override { return true; }
  int start_non_stop (bool enable) override { async (enable); return 0; }

  bool async (bool enable) override;

  int create_inferior (const char *program,
		       const std::vector<char *> &argv) override;

  int attach (unsigned long pid) override;
  int detach (process_info *proc) override;

  int kill (process_info *proc) override;
  void mourn (process_info *proc) override;
  void join (int pid) override;

  void resume (thread_resume *resume_info, size_t n) override;
  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       target_wait_flags options) override;

  void fetch_registers (regcache *regcache, int regno) override;
  void store_registers (regcache *regcache, int regno) override;

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr,
		   int len, unsigned int addr_space = 0) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len, unsigned int addr_space = 0) override;

  /* We model h/w threads - they do not exit.  */
  bool thread_alive (ptid_t ptid) override { return true; }
  bool supports_thread_stopped () override { return true; }
  bool thread_stopped (struct thread_info *tp) override;

  /* We model h/w threads - the list is fixed.  */
  bool has_fixed_thread_list () override { return true; }

  void request_interrupt () override;

  void pause_all (bool freeze) override;
  void unpause_all (bool unfreeze) override;

  bool supports_pid_to_exec_file () override { return true; }
  const char *pid_to_exec_file (int pid) override { return ""; }

  void ack_library (process_info *process, const char *name) override;
  void ack_in_memory_library (process_info *process, CORE_ADDR begin,
			      CORE_ADDR end) override;

  std::string thread_id_str (thread_info *thread) override;

  const std::string id_str (process_info *process) override;

  /* Query packet buffer size.  */
  int query_pbuf_size () override { return ZE_TARGET_PBUFSIZ; }

private:
  typedef std::list<ze_device_info *> devices_t;

  /* The devices we care about.  */
  devices_t devices;

  /* The current device ordinal number used for enumerating devices.  */
  unsigned long ordinal = 0;

  /* The freeze count for pause_all ().  */
  uint32_t frozen = 0;

  /* Attach to PID on devices in the device tree rooted at DEVICE.
     Returns the number of devices we attached to.  */
  int attach_to_device (uint32_t pid, ze_device_handle_t device);

  /* Attach to all available devices for process PID and store them in
     this object.  Returns the number of devices we attached to.  */
  int attach_to_devices (uint32_t pid);

  /* Fetch and process events from DEVICE.  Return number of events.  */
  uint64_t fetch_events (ze_device_info &device);

  /* Return the number of threads that match the RESUME_PTID and have
     new events to report.  Also recover these threads' resume state
     to RKIND.  */
  size_t mark_eventing_threads (ptid_t resume_ptid, enum resume_kind rkind);

  /* Resume all threads on DEVICE.  */
  void resume (ze_device_info &device);

  /* Return true if TP has single-stepped within its stepping range.  */
  bool is_range_stepping (thread_info *tp);

protected:
  /* Check whether a device is supported by this target.  */
  virtual bool is_device_supported
    (const ze_device_properties_t &,
     const std::vector<zet_debug_regset_properties_t> &) = 0;

  /* Create a target description for a device and populate the
     corresponding regset information.  */
  virtual target_desc *create_tdesc
    (ze_device_info *dinfo,
     const std::vector<zet_debug_regset_properties_t> &) = 0;

  /* Return whether TP is at a breakpoint.  */
  virtual bool is_at_breakpoint (thread_info *tp) = 0;

  /* TP stopped.  Find out why and return the stop reason.  Optionally
     fill in SIGNAL.  */
  virtual target_stop_reason get_stop_reason (thread_info *tp,
					      gdb_signal &signal) = 0;

  /* Prepare TP for resuming using TP's RESUME_STATE.

     This sets the ze execution state, typically to running.  */
  virtual void prepare_thread_resume (thread_info *tp) = 0;

  /* Read the memory in the context of thread TP.  */
  virtual int read_memory (thread_info *tp, CORE_ADDR memaddr,
			   unsigned char *myaddr, int len,
			   unsigned int addr_space = 0);

  /* Write the memory in the context of thread TP.  */
  virtual int write_memory (thread_info *tp, CORE_ADDR memaddr,
			    const unsigned char *myaddr, int len,
			    unsigned int addr_space = 0);
};

#endif /* GDBSERVER_LEVEL_ZERO_LOW_H */
