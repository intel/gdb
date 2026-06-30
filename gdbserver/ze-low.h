/* Target interface for Level-Zero based targets for gdbserver.
   See https://github.com/oneapi-src/level-zero.git.

   Copyright (C) 2020-2026 Free Software Foundation, Inc.

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

#ifndef GDBSERVER_ZE_LOW_H
#define GDBSERVER_ZE_LOW_H

#include "target.h"
#include "tdesc.h"

#include <level_zero/zet_api.h>
#include <string>
#include <vector>
#include <list>


/* Information about register sets reported in target descriptions.

   The main use of this is to find the information relevant for fetching
   and storing registers via Level-Zero based on register numbers.  */
struct ze_regset_info
{
  /* The device-specific Level-Zero register set type.  */
  uint32_t type;

  /* The register size in bytes for reading/writing.  */
  uint32_t size;

  /* The begin (inclusive) and end (exclusive) register numbers for this
     regset.

     This is used to map register numbers to regset types.  */
  int begin, end;

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

  /* Number of interrupts sent to this target.  */
  unsigned long ninterrupts = 0;
};

/* A thread's resume state.

   This is very similar to enum resume_kind except that we need an
   additional none case to model the thread not being mentioned in any
   resume request.  */

enum ze_thread_resume_state_t
{
  /* Gdbserver did not ask anything of this thread.  */
  ZE_THREAD_RESUME_NONE,

  /* The thread shall stop.  */
  ZE_THREAD_RESUME_STOP,

  /* The thread shall run.  */
  ZE_THREAD_RESUME_RUN,

  /* The thread shall step.  */
  ZE_THREAD_RESUME_STEP
};

/* A thread's execution state.  */

enum ze_thread_exec_state_t
{
  /* We do not know the thread state.  This is likely an error condition.  */
  ZE_THREAD_STATE_UNKNOWN,

  /* The thread is stopped and is expected to remain stopped until we
     resume it.  */
  ZE_THREAD_STATE_STOPPED,

  /* The thread is stopped by pause_all ().  In unpause_all (), we need to
     resume just the paused threads.

     In particular, we need to distinguish threads that reported their
     event to higher layers in gdbserver and hence have their waitstatus
     clear (set to ignore) from threads that were paused and had their
     waitstatus cleared by pause_all ().  */
  ZE_THREAD_STATE_PAUSED,

  /* The thread is running.

     We do not get exit events, so we will only learn about the thread
     becoming idle/unavailable if we try to stop it.  */
  ZE_THREAD_STATE_RUNNING,

  /* The thread exited.  */
  ZE_THREAD_STATE_EXITED,
};

/* Thread private data for Level-Zero targets.  */

struct ze_thread_info
{
  /* The thread identifier.  */
  ze_device_thread_t id;

  /* The thread's resume state.

     What does gdbserver want this thread to do.  */
  enum ze_thread_resume_state_t resume_state = ZE_THREAD_RESUME_NONE;

  /* The start/end addresses for range-stepping.  */
  CORE_ADDR step_range_start = 0;
  CORE_ADDR step_range_end = 0;

  /* The thread's execution state.

     What is this thread actually doing.  */
  enum ze_thread_exec_state_t exec_state = ZE_THREAD_STATE_UNKNOWN;

  /* The thread's stop reason.

     This is only valid if EXEC_STATE == ZE_THREAD_STATE_STOPPED.  */
  target_stop_reason stop_reason = TARGET_STOPPED_BY_NO_REASON;

  /* The waitstatus for this thread's last event.

     TARGET_WAITKIND_IGNORE means that there is no last event.  */
  target_waitstatus waitstatus {};
};

/* Return the ZE thread info for TP.  */

static inline ze_thread_info *
ze_thread (thread_info *tp)
{
  if (tp == nullptr)
    return nullptr;

  return (ze_thread_info *) tp->target_data ();
}

/* Return the ZE thread info for const TP.  */

static inline const ze_thread_info *
ze_thread (const thread_info *tp)
{
  if (tp == nullptr)
    return nullptr;

  return (const ze_thread_info *) tp->target_data ();
}

/* Return the Level-Zero thread id for all threads.  */

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

/* Return the Level-Zero thread id for THREAD.  */

static inline ze_device_thread_t
ze_thread_id (const thread_info *thread)
{
  const ze_thread_info *zetp = ze_thread (thread);
  if (zetp == nullptr)
    error (_("No thread."));

  return zetp->id;
}

/* Return a human-readable device thread id string.  */

std::string ze_thread_id_str (const ze_device_thread_t &thread);

/* Return the device for THREAD.  */

ze_device_info *ze_thread_device (const thread_info *thread);

/* Return the thread for TID on DEVICE if it exists; nullptr otherwise.  */

thread_info *ze_find_thread (const ze_device_info &device,
			     const ze_device_thread_t &tid);

/* Return the PTID for TID on DEVICE.  */

ptid_t ze_thread_ptid (const ze_device_info &device,
		       const ze_device_thread_t &tid);

/* Process info private data for Level-Zero targets.  */

struct process_info_private
{
  /* The device we're modelling as process.

     In case we get forcefully detached from the device this process
     represents, DEVICE will be nullptr.  The process will remain until
     the detach event can be reported to GDB.  */
  ze_device_info *device;

  process_info_private (ze_device_info *dev)
    : device (dev)
    {}
};

/* Target op definitions for Level-Zero based targets.  */

class ze_target : public process_stratum_target
{
public:
  /* Initialize the Level-Zero target.

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
  bool always_non_stop () override { return true; }
  int start_non_stop (bool enable) override;

  bool async (bool enable) override;

  int create_inferior (const char *program,
		       const std::string &args) override;

  int attach (int pid) override;
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
		   int len) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len) override;

  bool thread_alive (ptid_t ptid) override;
  bool supports_thread_stopped () override { return true; }
  bool thread_stopped (struct thread_info *tp) override;

  void request_interrupt () override;

  void pause_all (bool freeze) override;
  void unpause_all (bool unfreeze) override;

  bool supports_pid_to_exec_file () override { return true; }
  const char *pid_to_exec_file (int pid) override { return ""; }

  bool uses_library_notifications () override { return true; }
  void ack_library (process_info *process, const char *name) override;
  void ack_in_memory_library (process_info *process, CORE_ADDR begin,
			      CORE_ADDR end) override;

private:
  typedef std::list<ze_device_info *> devices_t;

  /* The devices we care about.  */
  devices_t devices;

  /* The current device ordinal number used for enumerating devices.  */
  unsigned long ordinal = 0;

  /* The freeze count for pause_all ().  */
  uint32_t frozen = 0;

  /* Whether a global interrupt was requested.

     This is used to reply NO_RESUMED if we requested an interrupt and no
     thread responded to break a hang if the last dispatch exited.  */
  bool interrupted = false;

  /* Attach to PID on devices in the device tree rooted at DEVICE.
     Returns the number of devices we attached to.  */
  int attach_to_device (uint32_t pid, ze_device_handle_t device);

  /* Attach to all available devices for process PID and store them in
     this object.  Returns the number of devices we attached to.  */
  int attach_to_devices (uint32_t pid);

  /* Fetch and process events from DEVICE.  Return number of events.  */
  uint64_t fetch_events (ze_device_info &device);

  /* Push library notifications.

     Call this every time events have been fetched.  */
  void ze_notif_libraries ();

  /* Return the number of threads that match the RESUME_PTID and have
     new events to report.  Also recover these threads' resume state
     to RKIND.  */
  size_t mark_eventing_threads (ptid_t resume_ptid, enum resume_kind rkind);

  /* Resume all threads on DEVICE.  */
  void resume (ze_device_info &device);

  /* Resume a single thread.  This is a helper method that prepares
     the thread for resuming, invalidates its regcache, and then
     resumes.  The method should be called only when we are sure the
     thread should be resumed.  */
  void resume_single_thread (thread_info *thread);

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
     const std::vector<zet_debug_regset_properties_t> &,
     const ze_pci_ext_properties_t &) = 0;

  /* Return whether TP is at a breakpoint.  */
  virtual bool is_at_breakpoint (thread_info *tp) = 0;

  /* TID stopped on DEVICE.  Find out why and return the stop reason.
     Optionally fill in SIGNAL.  */
  virtual target_stop_reason get_stop_reason (const ze_device_info &device,
					      const ze_device_thread_t &tid,
					      gdb_signal &signal) = 0;

  /* Prepare TP for resuming using TP's RESUME_STATE.

     This sets the ze execution state, typically to running.  */
  virtual void prepare_thread_resume (thread_info *tp) = 0;

  /* Read the memory in the context of thread TP.  */
  int read_memory (thread_info *tp, CORE_ADDR memaddr,
		   unsigned char *myaddr, int len);

  /* Read the memory in the context of TID on DEVICE.  */
  int read_memory (const ze_device_info &device,
		   const ze_device_thread_t &tid, CORE_ADDR memaddr,
		   unsigned char *myaddr, int len);

  /* Write the memory in the context of thread TP.  */
  int write_memory (thread_info *tp, CORE_ADDR memaddr,
		    const unsigned char *myaddr, int len);

  /* Write the memory in the context of TID on DEVICE.  */
  int write_memory (const ze_device_info &device,
		    const ze_device_thread_t &tid, CORE_ADDR memaddr,
		    const unsigned char *myaddr, int len);
};

#endif /* GDBSERVER_ZE_LOW_H */
