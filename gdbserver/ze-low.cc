/* Target interface for level-zero based targets for gdbserver.
   See https://github.com/oneapi-src/level-zero.git.

   Copyright (C) 2020-2024 Free Software Foundation, Inc.

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
#include "ze-low.h"
#include "dll.h"

#include <level_zero/zet_api.h>
#include <exception>
#include <sstream>
#include <iomanip>
#include <cstring> /* For snprintf.  */
#include <thread>
#include <utility>
#include <algorithm>
#include <set>

#ifndef USE_WIN32API
#  include <signal.h>
#  include <fcntl.h>
#endif


/* Convenience macros.  */

#define dprintf(fmt, ...) \
  debug_prefixed_printf_cond (debug_threads, "ze-low", fmt, ##__VA_ARGS__)

#ifndef USE_WIN32API
/* Async interaction stuff.

   The read/write ends of the pipe registered as waitable file in the
   event loop.  */
static int ze_event_pipe[2] = { -1, -1 };
#endif

/* Return whether we're in async mode.  */

static bool
ze_is_async ()
{
#ifndef USE_WIN32API
  return (ze_event_pipe[0] != -1);
#else
  return false;
#endif
}

/* Get rid of any pending event in the pipe.  */

static void
ze_async_flush ()
{
  if (!ze_is_async ())
    return;

#ifndef USE_WIN32API
  int ret;
  char buf;

  errno = 0;
  do
    ret = read (ze_event_pipe[0], &buf, 1);
  while (ret >= 0 || (ret == -1 && errno == EINTR));
#else
  error (_("%s: tbd"), __FUNCTION__);
#endif
}

/* Put something in the pipe, so the event loop wakes up.  */

static void
ze_async_mark ()
{
  if (!ze_is_async ())
    return;

#ifndef USE_WIN32API
  int ret;

  ze_async_flush ();

  errno = 0;
  do
    ret = write (ze_event_pipe[1], "+", 1);
  while (ret == 0 || (ret == -1 && errno == EINTR));

  /* Ignore EAGAIN.  If the pipe is full, the event loop will already
     be awakened anyway.  */
#else
  error (_("%s: tbd"), __FUNCTION__);
#endif
}

/* Return a human-readable device thread id component string.  */

static std::string
ze_thread_id_component_str (uint32_t component)
{
  if (component == UINT32_MAX)
    return std::string ("all");

  return std::to_string (component);
}

/* See ze-low.h.  */

std::string
ze_thread_id_str (const ze_device_thread_t &thread)
{
  std::stringstream sstream;
  sstream << ze_thread_id_component_str (thread.slice)
	  << "."
	  << ze_thread_id_component_str (thread.subslice)
	  << "."
	  << ze_thread_id_component_str (thread.eu)
	  << "."
	  << ze_thread_id_component_str (thread.thread);

  return sstream.str ();
}

/* Return a human-readable UUID string.  */

static std::string
uuid_str (const uint8_t uuid[], size_t size)
{
  std::stringstream sstream;
  while (size--)
    sstream << std::setw (2) << uuid[size];

  return sstream.str ();
}

/* Return a human-readable device UUID string.  */

static std::string
driver_uuid_str (const ze_driver_uuid_t &uuid)
{
  return uuid_str (uuid.id, sizeof (uuid.id));
}

/* Return a human-readable process state string.  */

static const char *
ze_process_state_str (ze_process_state state)
{
  switch (state)
    {
    case ze_process_visible:
      return "visible";

    case ze_process_hidden:
      return "hidden";
    }

  return "unknown";
}

/* Return the pid for DEVICE.  */

static int
ze_device_pid (const ze_device_info &device)
{
  if (device.process != nullptr)
    return device.process->pid;

  return 0;
}

/* Return the device for PROCESS.  */

static ze_device_info *
ze_process_device (process_info *process)
{
  if (process == nullptr)
    return nullptr;

  process_info_private *zeproc = process->priv;
  if (zeproc == nullptr)
    return nullptr;

  return zeproc->device;
}

/* Return the device for THREAD.  */

static ze_device_info *
ze_thread_device (const thread_info *thread)
{
  if (thread == nullptr)
    return nullptr;

  process_info *process = get_thread_process (thread);
  return ze_process_device (process);
}

/* Returns whether ID is in SET.  */

static bool
ze_device_thread_in (ze_device_thread_t id, ze_device_thread_t set)
{
  if ((set.slice != UINT32_MAX) && (set.slice != id.slice))
    return false;

  if ((set.subslice != UINT32_MAX) && (set.subslice != id.subslice))
    return false;

  if ((set.eu != UINT32_MAX) && (set.eu != id.eu))
    return false;

  if ((set.thread != UINT32_MAX) && (set.thread != id.thread))
    return false;

  return true;
}

/* Call FUNC for each thread on DEVICE matching ID.  */

template <typename Func>
static void
for_each_thread (const ze_device_info &device, ze_device_thread_t id,
		 Func func)
{
  int pid = ze_device_pid (device);
  for_each_thread (pid, [id, func] (thread_info *tp)
    {
      ze_device_thread_t tid = ze_thread_id (tp);
      if (ze_device_thread_in (tid, id))
	func (tp);
    });
}

/* Store a tdesc and regset info in thread/thread privates.  */

static void
ze_store_tdesc (thread_info *tp, const ze_tdesc *cached_tdesc)
{
  tp->tdesc = cached_tdesc->tdesc.get ();
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);
  zetp->regset_info_p = cached_tdesc->regset_info.get ();
}

/* Add a process for DEVICE.  */

static process_info *
ze_add_process (ze_device_info *device, ze_process_state state)
{
  gdb_assert (device != nullptr);

  process_info *process = add_process (device->ordinal, 1);
  process->priv = new process_info_private (device, state);
  device->process = process;

  /* Enumerate threads on the device we attached to.

     We debug the entire device so we can enumerate all threads at once.  They
     will be idle some of the time and we won't be able to interact with them.
     When work gets submitted to the device, the thread dispatcher will
     distribute the work onto device threads.

     The alternative of only representing threads that are currently executing
     work would be too intrusive as we'd need to stop each thread on every
     dispatch.  */

  /* Initial target descriptor for all threads.  */
  const ze_tdesc *cached_tdesc = device->tdesc_cache.any ();

  long tid = 0;
  uint32_t slice, sslice, eu, thread;
  const ze_device_properties_t &properties = device->properties;
  for (slice = 0; slice < properties.numSlices; ++slice)
    for (sslice = 0; sslice < properties.numSubslicesPerSlice; ++sslice)
      for (eu = 0; eu < properties.numEUsPerSubslice; ++eu)
	for (thread = 0; thread < properties.numThreadsPerEU; ++thread)
	  {
	    /* We use the device ordinal as process id.  */
	    ptid_t ptid = ptid_t ((int) device->ordinal, ++tid, 0l);

	    /* We can only support that many threads.  */
	    if (tid < 0)
	      error (_("Too many threads on device %lu: %s."),
		     device->ordinal, properties.name);

	    /* Storing the 128b device thread id in the private data.  We might
	       want to extend ptid_t and put it there so GDB can show it to the
	       user.  */
	    ze_thread_info *zetp = new ze_thread_info {};
	    zetp->id.slice = slice;
	    zetp->id.subslice = sslice;
	    zetp->id.eu = eu;
	    zetp->id.thread = thread;

	    /* Assume threads are running until we hear otherwise.  */
	    zetp->exec_state = ze_thread_state_running;

	    thread_info *tp = add_thread (ptid, zetp);

	    /* Start each thread with the device's tdesc.  This will change
	       possibly later but it's important that each thread reports
	       that it has a tdesc which will be indicated during thread list
	       query.  */
	    ze_store_tdesc (tp, cached_tdesc);
	  }

  device->nthreads = tid;
  device->nresumed = tid;

  dprintf ("process %d (%s) with %ld threads created for device %lu: %s.",
	   (int) device->ordinal, ze_process_state_str (state), tid,
	   device->ordinal, properties.name);

  return process;
}

/* Remove a level-zero PROCESS.  */

static void
ze_remove_process (process_info *process)
{
  gdb_assert (process != nullptr);

  process->for_each_thread ([] (thread_info *thread)
    {
      delete (ze_thread_info *) thread->target_data;
      thread->target_data = nullptr;

      remove_thread (thread);
    });

  process_info_private *zeinfo = process->priv;
  gdb_assert (zeinfo != nullptr);

  /* We may or may not have a device.

     When we got detached, we will remove the device first, and remove the
     process when we select an event from one of its threads.

     When we get a process exit event, the device will remain after the process
     has been removed.  */
  ze_device_info *device = zeinfo->device;
  if (device != nullptr)
    {
      gdb_assert (device->process == process);
      device->process = nullptr;
    }

  process->priv = nullptr;
  delete zeinfo;

  remove_process (process);
}

/* Show PROCESS.  */

static void
ze_show_process (process_info *process)
{
  gdb_assert (process != nullptr);
  process_info_private *priv = process->priv;

  gdb_assert (priv != nullptr);
  switch (priv->state)
    {
    case ze_process_visible:
      return;

    case ze_process_hidden:
      /* FIXME: report state change

	 Set priv->status and report the new visibility to GDB.  */
      priv->state = ze_process_visible;
      return;
    }

  internal_error (_("unknown process state"));
}

/* Hide PROCESS.  */

static void
ze_hide_process (process_info *process)
{
  gdb_assert (process != nullptr);
  process_info_private *priv = process->priv;

  gdb_assert (priv != nullptr);
  switch (priv->state)
    {
    case ze_process_hidden:
      return;

    case ze_process_visible:
      /* FIXME: report state change

	 Set priv->status and report the new visibility to GDB.  */
      priv->state = ze_process_hidden;
      return;
    }

  internal_error (_("unknown process state"));
}

/* Attach to DEVICE and create a hidden process for it.

   Modifies DEVICE as a side-effect.
   Returns the created process or nullptr if DEVICE does not support debug.  */

static process_info *
ze_attach (ze_device_info *device)
{
  gdb_assert (device != nullptr);

  if (device->session != nullptr)
    error (_("Already attached to %s."), device->properties.name);

  device->debug_attach_state = zetDebugAttach (device->handle, &device->config,
					       &device->session);
  switch (device->debug_attach_state)
    {
    case ZE_RESULT_SUCCESS:
      if (device->session == nullptr)
	error (_("Bad handle returned by zetDebugAttach on %s."),
	       device->properties.name);

      return ze_add_process (device, ze_process_hidden);

    case ZE_RESULT_NOT_READY:
      /* We're too early.  The level-zero user-mode driver has not been
	 initialized, yet.  */
      error (_("Attempting to attach too early to %s."),
	     device->properties.name);

    case ZE_RESULT_ERROR_UNSUPPORTED_FEATURE:
      /* Not all sub-devices support attaching to them.  */
      dprintf ("Attach not supported on %s", device->properties.name);
      return nullptr;

    case ZE_RESULT_ERROR_NOT_AVAILABLE:
      /* Someone else is already attached.  This could be us if we already
	 attached to some other sub-device in this device tree.  */
      error (_("Someone is already attached to %s."),
	     device->properties.name);

    default:
      error (_("Failed to attach to %s (%x)."), device->properties.name,
	     device->debug_attach_state);
    }
}

/* Detach from DEVICE.  */

static void
ze_detach (ze_device_info *device)
{
  gdb_assert (device != nullptr);

  zet_debug_session_handle_t session = device->session;
  if (session == nullptr)
    error (_("Already detached from %s."), device->properties.name);

  dprintf ("device %lu=%s", device->ordinal, device->properties.name);

  ze_result_t status  = zetDebugDetach (session);
  switch (status)
    {
    case ZE_RESULT_ERROR_DEVICE_LOST:
    case ZE_RESULT_SUCCESS:
      device->session = nullptr;
      break;

    default:
      error (_("Failed to detach from %s (%x)."), device->properties.name,
	     status);
    }
}

/* Return a human-readable detach reason string.  */

static std::string
ze_detach_reason_str (zet_debug_detach_reason_t reason)
{
  switch (reason)
    {
    case ZET_DEBUG_DETACH_REASON_INVALID:
      return std::string (_("invalid"));

    case ZET_DEBUG_DETACH_REASON_HOST_EXIT:
      return std::string (_("the host process exited"));
    }

  return std::string (_("unknown"));
}

/* Return a human-readable module debug information format string.  */

static std::string
ze_debug_info_format_str (zet_module_debug_info_format_t format)
{
  switch (format)
    {
    case ZET_MODULE_DEBUG_INFO_FORMAT_ELF_DWARF:
      return std::string (_("DWARF"));
    }

  return std::string (_("unknown"));
}

/* Return a human-readable event string.  */

static std::string
ze_event_str (const zet_debug_event_t &event)
{
  std::stringstream sstream;

  switch (event.type)
    {
    case ZET_DEBUG_EVENT_TYPE_INVALID:
      sstream << "invalid";
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_DETACHED:
      sstream << "detached, reason="
	      << ze_detach_reason_str (event.info.detached.reason);
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_PROCESS_ENTRY:
      sstream << "process entry";
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_PROCESS_EXIT:
      sstream << "process exit";
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_MODULE_LOAD:
      sstream << "module load, format="
	      << ze_debug_info_format_str (event.info.module.format)
	      << ", module=[" << std::hex << event.info.module.moduleBegin
	      << "; " << std::hex << event.info.module.moduleEnd
	      << "), addr=" << std::hex << event.info.module.load
	      << ", need-ack: "
	      << ((event.flags & ZET_DEBUG_EVENT_FLAG_NEED_ACK) != 0);
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_MODULE_UNLOAD:
      sstream << "module unload, format="
	      << ze_debug_info_format_str (event.info.module.format)
	      << ", module=[" << std::hex << event.info.module.moduleBegin
	      << "; " << std::hex << event.info.module.moduleEnd
	      << "), addr=" << std::hex << event.info.module.load;
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_THREAD_STOPPED:
      sstream << "thread stopped, thread="
	      << ze_thread_id_str (event.info.thread.thread);
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_THREAD_UNAVAILABLE:
      sstream << "thread unavailable, thread="
	      << ze_thread_id_str (event.info.thread.thread);
      return sstream.str ();

    case ZET_DEBUG_EVENT_TYPE_PAGE_FAULT:
      sstream << "page fault"
	      << ", address=0x" << std::hex << event.info.page_fault.address
	      << ", mask=0x" << std::hex << event.info.page_fault.mask
	      << ", reason=0x" << std::hex << event.info.page_fault.reason;
      return sstream.str ();
    }

  sstream << "unknown, code=" << event.type;
  return sstream.str ();
}

/* Acknowledge an event, if necessary.  */

static void
ze_ack_event (const ze_device_info &device, const zet_debug_event_t &event)
{
  /* There is nothing to do for events that do not need acknowledging.  */
  if ((event.flags & ZET_DEBUG_EVENT_FLAG_NEED_ACK) == 0)
    return;

  ze_result_t status = zetDebugAcknowledgeEvent (device.session, &event);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    default:
      error (_("error acknowledging event: %s: %x."),
	     ze_event_str (event).c_str (), status);
    }
}

/* Clear TP's resume state.  */

static void
ze_clear_resume_state (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  zetp->resume_state = ze_thread_resume_none;
}

/* Set TP's resume state from RKIND.  */

static void
ze_set_resume_state (thread_info *tp, resume_kind rkind)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  switch (rkind)
    {
    case resume_continue:
      zetp->resume_state = ze_thread_resume_run;
      return;

    case resume_step:
      zetp->resume_state = ze_thread_resume_step;
      return;

    case resume_stop:
      zetp->resume_state = ze_thread_resume_stop;
      return;
    }

  internal_error (_("bad resume kind: %d."), rkind);
}

/* Return TP's resume state.  */

static enum ze_thread_resume_state_t
ze_resume_state (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return ze_thread_resume_none;

  return zetp->resume_state;
}

/* Return TP's execution state.  */

static enum ze_thread_exec_state_t
ze_exec_state (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return ze_thread_state_unknown;

  return zetp->exec_state;
}

/* Return whether TP has a stop execution state.  */

static bool
ze_thread_stopped (const thread_info *tp)
{
  ze_thread_exec_state_t state = ze_exec_state (tp);

  return ((state == ze_thread_state_stopped)
	  || (state == ze_thread_state_held)
	  || (state == ze_thread_state_paused));
}

/* Return whether TP has a pending event.  */

static bool
ze_has_waitstatus (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return false;

  return (zetp->waitstatus.kind () != TARGET_WAITKIND_IGNORE);
}

/* Return whether TP has a pending priority event.  */

static bool
ze_has_priority_waitstatus (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return false;

  switch (zetp->waitstatus.kind ())
    {
    case TARGET_WAITKIND_IGNORE:
    case TARGET_WAITKIND_UNAVAILABLE:
      return false;

    case TARGET_WAITKIND_STOPPED:
      /* If this thread was stopped via an interrupt, it is an interesting
	 case if GDB wanted it stopped with a stop resume request.  */
      if ((zetp->stop_reason == TARGET_STOPPED_BY_NO_REASON)
	  && (zetp->waitstatus.sig () == GDB_SIGNAL_TRAP))
	return (zetp->resume_state == ze_thread_resume_stop);

      /* If this thread stopped spuriously, it is not interesting.  */
      if ((zetp->stop_reason == TARGET_STOPPED_BY_NO_REASON)
	  && (zetp->waitstatus.sig () == GDB_SIGNAL_0))
	return false;

      return true;

    default:
      return true;
    }
}

/* Return TP's waitstatus and clear it in TP.  */

static target_waitstatus
ze_move_waitstatus (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return {};

  target_waitstatus status = zetp->waitstatus;
  zetp->waitstatus.set_ignore ();

  return status;
}

/* Indicate that we have been detached from the device corresponding to
   PROCESS.  */

static void
ze_device_detached (process_info *process, zet_debug_detach_reason_t reason)
{
  gdb_assert (process != nullptr);

  /* We model getting detached from a device as the corresponding device process
     exiting with the detach reason as exit status.

     In the first step, we mark all threads of that process exited.  We already
     use the process exit wait status as all threads will exit together.

     In the second step, when one of those threads gets selected for reporting
     its event, we will remove the process as part of the reporting flow.  */

  process->for_each_thread ([reason] (thread_info *tp)
    {
      ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      zetp->waitstatus.set_exited ((int) reason);
    });
}

/* Find the regset containing REGNO on DEVICE or throw if not found.  */

static ze_regset_info
ze_find_regset (const thread_info *tp, long regno)
{
  const ze_thread_info *zetp = ze_thread (tp);
  const ze_regset_info_t &regsets = *zetp->regset_info_p;

  for (const ze_regset_info &regset : regsets)
    {
      if (regno < regset.begin)
	continue;

      if (regset.end <= regno)
	continue;

      return regset;
    }

  const ze_device_info *device = ze_thread_device (tp);
  error (_("No register %ld on %s."), regno, device->properties.name);
}

/* Fetch all registers for THREAD on DEVICE into REGCACHE.  */

static void
ze_fetch_all_registers (const thread_info *tp,
			regcache *regcache)
{
  const ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  const ze_thread_info *zetp = ze_thread (tp);
  const ze_regset_info_t &regsets = *zetp->regset_info_p;
  ze_device_thread_t thread = ze_thread_id (tp);

  for (const ze_regset_info &regset : regsets)
    {
      gdb_assert (regset.begin <= regset.end);
      long lnregs = regset.end - regset.begin;

      gdb_assert (lnregs < UINT32_MAX);
      uint32_t nregs = (uint32_t) lnregs;

      std::vector<uint8_t> buffer (regset.size * nregs);
      ze_result_t status
	= zetDebugReadRegisters (device->session, thread, regset.type, 0,
				 nregs, buffer.data ());
      switch (status)
	{
	case ZE_RESULT_SUCCESS:
	case ZE_RESULT_ERROR_NOT_AVAILABLE:
	  {
	    size_t offset = 0;
	    long reg = regset.begin;

	    for (; reg < regset.end; reg += 1, offset += regset.size)
	      {
		if (status == ZE_RESULT_SUCCESS)
		  supply_register (regcache, reg, &buffer[offset]);
		else
		  supply_register (regcache, reg, nullptr);
	      }
	  }
	  break;

	default:
	  warning (_("Error %x reading regset %" PRIu32 " for %s on %s."),
		   status, regset.type, ze_thread_id_str (thread).c_str (),
		   device->properties.name);

	  break;
	}
    }
}

/* Fetch register REGNO for THREAD on DEVICE into REGCACHE.  */

static void
ze_fetch_register (const thread_info *tp,
		   regcache *regcache, long regno)
{
  ze_regset_info regset = ze_find_regset (tp, regno);
  ze_device_thread_t thread = ze_thread_id (tp);

  gdb_assert (regset.begin <= regno);
  long lrsno = regno - regset.begin;

  gdb_assert (lrsno <= UINT32_MAX);
  uint32_t rsno = (uint32_t) lrsno;

  const ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  std::vector<uint8_t> buffer (regset.size);
  ze_result_t status
    = zetDebugReadRegisters (device->session, thread, regset.type, rsno, 1,
			     buffer.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      supply_register (regcache, regno, buffer.data ());
      break;

    case ZE_RESULT_ERROR_NOT_AVAILABLE:
      supply_register (regcache, regno, nullptr);
      break;

    default:
      warning (_("Error %x reading register %ld (regset %" PRIu32
		 ") for %s on %s."), status, regno, regset.type,
	       ze_thread_id_str (thread).c_str (), device->properties.name);
      break;
    }
}

/* Store all registers for THREAD on DEVICE from REGCACHE.  */

static void
ze_store_all_registers (const thread_info *tp,
			regcache *regcache)
{
  const ze_thread_info *zetp = ze_thread (tp);
  const ze_regset_info_t &regsets = *zetp->regset_info_p;
  ze_device_thread_t thread = ze_thread_id (tp);

  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  for (const ze_regset_info &regset : regsets)
    {
      if (!regset.is_writeable)
	continue;

      gdb_assert (regset.begin <= regset.end);
      long lnregs = regset.end - regset.begin;

      gdb_assert (lnregs < UINT32_MAX);
      uint32_t nregs = (uint32_t) lnregs;

      std::vector<uint8_t> buffer (regset.size * nregs);
      size_t offset = 0;
      long reg = regset.begin;
      for (; reg < regset.end; reg += 1, offset += regset.size)
	collect_register (regcache, reg, &buffer[offset]);

      ze_result_t status
	= zetDebugWriteRegisters (device->session, thread, regset.type, 0,
				  nregs, buffer.data ());
      switch (status)
	{
	case ZE_RESULT_SUCCESS:
	  break;

	default:
	  error (_("Error %x writing regset %" PRIu32 " for %s on %s."),
		 status, regset.type, ze_thread_id_str (thread).c_str (),
		 device->properties.name);
	}
    }
}

/* Store register REGNO for THREAD on DEVICE from REGCACHE.  */

static void
ze_store_register (const thread_info *tp,
		   regcache *regcache, long regno)
{
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);
  ze_regset_info regset = ze_find_regset (tp, regno);
  ze_device_thread_t thread = ze_thread_id (tp);

  if (!regset.is_writeable)
    error (_("Writing read-only register %ld (regset %" PRIu32
	     ") for %s on %s."), regno, regset.type,
	   ze_thread_id_str (thread).c_str (), device->properties.name);

  gdb_assert (regset.begin <= regno);
  long lrsno = regno - regset.begin;

  gdb_assert (lrsno <= UINT32_MAX);
  uint32_t rsno = (uint32_t) lrsno;

  std::vector<uint8_t> buffer (regset.size);
  collect_register (regcache, regno, buffer.data ());

  ze_result_t status
    = zetDebugWriteRegisters (device->session, thread, regset.type, rsno, 1,
			      buffer.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    default:
      /* Writing the register (in particular, CR0) would fail for
	 threads that are silently resumed by dbgUMD.  This can be
	 occasionally seen for large workloads when resuming or
	 running the "info threads" command following an interrupt.
	 Erroring out in this case can distort the command's flow and
	 lead to a hang-like behavior.  We do not use a 'warning'
	 because if there are too many threads, the communication pipe
	 between GDB and gdbserver can become flooded.  Hence we
	 prefer a dprintf here.  */

      dprintf (_("Error %x writing register %ld (regset %" PRIu32
		 ") for %s on %s."), status,  regno, regset.type,
	       ze_thread_id_str (thread).c_str (),
	       device->properties.name);
    }
}

/* Discard TP's regcache.  */

static void
ze_discard_regcache (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  gdb_assert (regcache != nullptr);

  regcache->discard ();
}

/* Prepare for resuming TP.  Return true if TP should be actually
   resumed.  */

static bool
ze_prepare_for_resuming (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  /* We should not call this function if there is a priority
     waitstatus.  */
  gdb_assert (!ze_has_priority_waitstatus (tp));

  /* When we get detached, we will remove the device but we will also mark
     each thread exited.  We shouldn't try to resume them.  */
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  ze_thread_exec_state_t state = zetp->exec_state;
  switch (state)
    {
    case ze_thread_state_paused:
      zetp->exec_state = ze_thread_state_stopped;
      [[fallthrough]];

    case ze_thread_state_stopped:
      device->nresumed++;
      if (device->nresumed > device->nthreads)
	{
	  device->nresumed = device->nthreads;
	  dprintf ("capping device %lu's nresumed at %ld (all)",
		   device->ordinal, device->nthreads);
	}
      return true;

    case ze_thread_state_held:
      gdb_assert_not_reached ("threads with 'held' state should "
			      "have been turned into 'stopped'");

    case ze_thread_state_unavailable:
      device->nresumed++;
      if (device->nresumed > device->nthreads)
	{
	  device->nresumed = device->nthreads;
	  dprintf ("capping device %lu's nresumed at %ld (all)",
		   device->ordinal, device->nthreads);
	}

      zetp->exec_state = ze_thread_state_running;

      /* Ignore resuming unavailable threads.  */
      return false;

    case ze_thread_state_running:
      /* Ignore resuming already running threads.  */
      return false;

    case ze_thread_state_unknown:
      warning (_("thread %s has unknown execution "
		 "state"), tp->id.to_string ().c_str ());
      return false;
    }

  internal_error (_("bad execution state: %d."), state);
}

/* Prepare for stopping TP.  Return true if TP should be
   actually stopped by sending an interrupt to the target.  */

static bool
ze_prepare_for_stopping (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  /* When we get detached, we will remove the device but we will also mark
     each thread exited.  We shouldn't try to stop them.  */
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  ze_thread_exec_state_t state = zetp->exec_state;
  switch (state)
    {
    case ze_thread_state_stopped:
      /* We silently ignore already stopped threads.  */
      return false;

    case ze_thread_state_held:
      gdb_assert_not_reached ("threads with 'held' state should "
			      "have been turned into 'stopped'");

    case ze_thread_state_paused:
      /* A paused thread is already stopped.  */
      zetp->exec_state = ze_thread_state_stopped;
      return false;

    case ze_thread_state_unavailable:
    case ze_thread_state_running:
      return true;

    case ze_thread_state_unknown:
      warning (_("thread %s has unknown execution state"),
	       tp->id.to_string ().c_str ());
      return false;
    }

  internal_error (_("bad execution state: %d."), state);
}

/* Resume THREAD on DEVICE.  */

static void
ze_resume (ze_device_info &device, ze_device_thread_t thread)
{
  dprintf ("device %lu=%s, thread=%s", device.ordinal,
	   device.properties.name, ze_thread_id_str (thread).c_str ());

  ze_result_t status = zetDebugResume (device.session, thread);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    case ZE_RESULT_ERROR_NOT_AVAILABLE:
      /* Ignore this if we're not modeling DEVICE as a process anymore.  */
      if (device.process == nullptr)
	break;

      /* The thread is already running or unavailable.

	 Assuming our thread state tracking is correct, the thread isn't
	 running, so we assume it became unavailable.  That is strange,
	 too, as we had it stopped.  */
      warning (_("thread %s unexpectedly unavailable on %s."),
	       ze_thread_id_str (thread).c_str (), device.properties.name);

      /* Update our thread state to reflect the target.  */
      for_each_thread (device, thread, [&] (thread_info *tp)
	{
	  ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  zetp->exec_state = ze_thread_state_unavailable;
	  zetp->waitstatus.set_unavailable ();
	});
      break;

    default:
      error (_("Failed to resume %s on %s: %x."),
	     ze_thread_id_str (thread).c_str (), device.properties.name,
	     status);
    }
}

/* Interrupt THREAD on DEVICE.  */

static void
ze_interrupt (ze_device_info &device, ze_device_thread_t thread)
{
  dprintf ("device %lu=%s, thread=%s, nresumed=%ld%s",
	   device.ordinal, device.properties.name,
	   ze_thread_id_str (thread).c_str (), device.nresumed,
	   ((device.nresumed == device.nthreads) ? " (all)" : ""));

  ze_result_t status = zetDebugInterrupt (device.session, thread);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      if (ze_is_thread_id_all (thread))
	device.ninterrupts++;

      break;

    case ZE_RESULT_NOT_READY:
      /* We already requested THREAD to be stopped.  We do not track
	 requests so let's ignore this.  */
      break;

    case ZE_RESULT_ERROR_NOT_AVAILABLE:
      /* The thread is already stopped or unavailable.

	 Assuming that our state tracking works, update non-stopped
	 threads to reflect that.  */
      for_each_thread (device, thread, [&] (thread_info *tp)
	{
	  if (ze_thread_stopped (tp))
	    return;

	  ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  zetp->exec_state = ze_thread_state_unavailable;
	  zetp->waitstatus.set_unavailable ();
	});
	break;

    default:
      error (_("Failed to interrupt %s on %s: %x."),
	     ze_thread_id_str (thread).c_str (), device.properties.name,
	     status);
    }
}

bool
ze_target::is_range_stepping (thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  if (ze_thread_stopped (tp)
      && (zetp->resume_state == ze_thread_resume_step)
      && (zetp->stop_reason == TARGET_STOPPED_BY_SINGLE_STEP))
    {
      regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
      CORE_ADDR pc = read_pc (regcache);

      return ((pc >= zetp->step_range_start)
	      && (pc < zetp->step_range_end));
    }

  return false;
}

int
ze_target::attach_to_device (uint32_t pid, ze_device_handle_t device)
{
  ze_device_properties_t properties;

  memset (&properties, 0, sizeof (properties));
  properties.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
  properties.pNext = nullptr;

  ze_result_t status = zeDeviceGetProperties (device, &properties);
  if (status != ZE_RESULT_SUCCESS)
    {
      warning (_("Failed to obtain device properties (%x)."),
	       status);
      return 0;
    }

  /* We're a bit paranoid.  */
  properties.name[ZE_MAX_DEVICE_NAME-1] = 0;

  int nattached = 0;
  uint32_t nsub_devices = 0;
  status = zeDeviceGetSubDevices (device, &nsub_devices, nullptr);
  if (status != ZE_RESULT_SUCCESS)
    warning (_("Failed to get number of sub-devices in %s (%x)."),
	     properties.name, status);
  else if (nsub_devices > 0)
    {
      std::vector<ze_device_handle_t> sub_devices (nsub_devices);
      status = zeDeviceGetSubDevices (device, &nsub_devices,
				      sub_devices.data ());
      if (status != ZE_RESULT_SUCCESS)
	warning (_("Failed to enumerate sub-devices in %s (%x)."),
		 properties.name, status);
      else
	for (ze_device_handle_t sub_device : sub_devices)
	  nattached += attach_to_device (pid, sub_device);
    }

  /* If we attached to a sub-device, we're done.  We won't be able to attach to
     a parent device, anymore.  */
  if (nattached > 0)
    return nattached;

  /* Allow affecting the normal attach behaviour via environment variables by
     disallowing attaching to devices or sub-devices.  */
  if (properties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE)
    {
      const char * const disallow_sub_dev
	= std::getenv ("ZE_GDB_DO_NOT_ATTACH_TO_SUB_DEVICE");
      if (disallow_sub_dev != nullptr && *disallow_sub_dev != 0)
	return nattached;
    }
  else
    {
      const char * const disallow_dev
	= std::getenv ("ZE_GDB_DO_NOT_ATTACH_TO_DEVICE");
      if (disallow_dev != nullptr && *disallow_dev != 0)
	return nattached;
    }

  uint32_t nregsets = 0;
  status = zetDebugGetRegisterSetProperties (device, &nregsets, nullptr);
  if (status != ZE_RESULT_SUCCESS)
    {
      warning (_("Failed to obtain number of register sets in %s (%x)."),
	       properties.name, status);
      return nattached;
    }

  ze_regset_properties_v_t regsets (nregsets);
  status = zetDebugGetRegisterSetProperties (device, &nregsets,
					     regsets.data ());
  if (status != ZE_RESULT_SUCCESS)
    {
      warning (_("Failed to obtain register sets in %s (%x)."),
	       properties.name, status);
      return nattached;
    }

  /* Check with the actual target implementation whether it supports this kind
     of device.  */
  if (!is_device_supported (properties, regsets))
    {
      dprintf ("skipping unsupported device %s.", properties.name);
      return nattached;
    }

  std::unique_ptr<ze_device_info> dinfo { new ze_device_info };
  dinfo->config.pid = pid;
  dinfo->handle = device;
  dinfo->properties = properties;

  ze_pci_ext_properties_t pci_properties {};
  status = zeDevicePciGetPropertiesExt (device, &pci_properties);
  if (status != ZE_RESULT_SUCCESS)
    {
      warning (_("Failed to obtain PCI properties in %s (%x)."),
	       properties.name, status);
      pci_properties.address.domain = 0;
      pci_properties.address.bus = 0;
      pci_properties.address.device = 0;
      pci_properties.address.function = 0;
    }

  /* Generate device location for display.  */
  dinfo->pci_slot = string_printf ("%04" PRIx32 ":%02" PRIx32 ":%02" PRIx32
				   ".%" PRId32,
				   pci_properties.address.domain,
				   pci_properties.address.bus,
				   pci_properties.address.device,
				   pci_properties.address.function);

  (void)create_tdesc (dinfo.get (), regsets);

  unsigned long ordinal = this->ordinal + 1;
  if (ordinal == 0)
    internal_error (_("device ordinal overflow."));

  dinfo->ordinal = ordinal;

  try
    {
      process_info *process = ze_attach (dinfo.get ());
      if (process == nullptr)
	return nattached;
    }
  catch (const gdb_exception_error &except)
    {
      warning ("%s", except.what ());
    }

  /* Add the device even if we were not able to attach to allow attempting to
     attach to it explicitly later on.  */
  devices.push_back (dinfo.release ());
  this->ordinal = ordinal;

  nattached += 1;
  return nattached;
}

int
ze_target::attach_to_devices (uint32_t pid)
{
  uint32_t ndrivers = 0;
  ze_result_t status = zeDriverGet (&ndrivers, nullptr);
  if (status != ZE_RESULT_SUCCESS)
    error (_("Failed to get number of device drivers (%x)."), status);

  std::vector<ze_driver_handle_t> drivers (ndrivers);
  status = zeDriverGet (&ndrivers, drivers.data ());
  if (status != ZE_RESULT_SUCCESS)
    error (_("Failed to enumerate device drivers (%x)."), status);

  int nattached = 0;
  for (ze_driver_handle_t driver : drivers)
    {
      ze_driver_properties_t properties;

      memset (&properties, 0, sizeof (properties));
      properties.stype = ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES;
      properties.pNext = nullptr;

      status = zeDriverGetProperties (driver, &properties);
      if (status != ZE_RESULT_SUCCESS)
	{
	  warning (_("Failed to obtain driver properties (%x)."),
		   status);
	  continue;
	}

      ze_api_version_t version;
      status = zeDriverGetApiVersion (driver, &version);
      if (status != ZE_RESULT_SUCCESS)
	{
	  warning (_("Failed to obtain API version in %s (%x)."),
		   driver_uuid_str (properties.uuid).c_str (),
		   status);
	  continue;
	}

      switch (ZE_MAJOR_VERSION (version))
	{
	case 1:
	  /* We should be OK with all minor versions.  */
	  break;

	default:
	  warning (_("Unsupported API version in %s (%x)."),
		   driver_uuid_str (properties.uuid).c_str (),
		   ZE_MAJOR_VERSION (version));
	  continue;
	}

      uint32_t ndevices = 0;
      status = zeDeviceGet (driver, &ndevices, nullptr);
      if (status != ZE_RESULT_SUCCESS)
	{
	  warning (_("Failed to get number of devices in %s (%x)."),
		   driver_uuid_str (properties.uuid).c_str (),
		   status);
	  continue;
	}

      std::vector<ze_device_handle_t> devices (ndevices);
      status = zeDeviceGet (driver, &ndevices, devices.data ());
      if (status != ZE_RESULT_SUCCESS)
	{
	  warning (_("Failed to enumerate devices in %s (%x)."),
		   driver_uuid_str (properties.uuid).c_str (),
		   status);
	  continue;
	}

      dprintf ("scanning driver %s (%" PRIu32 " devices)",
	       driver_uuid_str (properties.uuid).c_str (), ndevices);

      for (ze_device_handle_t device : devices)
	nattached += attach_to_device (pid, device);
    }

  return nattached;
}

uint64_t
ze_target::fetch_events (ze_device_info &device)
{
  /* There are no events if we're not attached.  */
  if (device.session == nullptr)
    return 0;

  uint64_t nevents = 0;
  for (;;)
    {
      zet_debug_event_t event = {};
      ze_result_t status = zetDebugReadEvent (device.session, 0ull, &event);
      switch (status)
	{
	case ZE_RESULT_SUCCESS:
	  nevents += 1;
	  break;

	case ZE_RESULT_NOT_READY:
	  return nevents;

	default:
	  error (_("error fetching events from %s: %x."),
		 device.properties.name, status);
	}

      dprintf ("received event from device %lu: %s", device.ordinal,
	       ze_event_str (event).c_str ());

      switch (event.type)
	{
	case ZET_DEBUG_EVENT_TYPE_INVALID:
	  break;

	case ZET_DEBUG_EVENT_TYPE_DETACHED:
	  {
	    process_info *process = device.process;
	    if (process != nullptr)
	      ze_device_detached (process, event.info.detached.reason);

	    /* We're detached, now.  */
	    device.session = nullptr;
	  }
	  return nevents;

	case ZET_DEBUG_EVENT_TYPE_PROCESS_ENTRY:
	  ze_ack_event (device, event);
	  ze_show_process (device.process);
	  continue;

	case ZET_DEBUG_EVENT_TYPE_PROCESS_EXIT:
	  ze_ack_event (device, event);
	  ze_hide_process (device.process);
	  continue;

	case ZET_DEBUG_EVENT_TYPE_MODULE_LOAD:
	  {
	    /* We would not remain attached without a process.  */
	    process_info *process = device.process;
	    gdb_assert (process != nullptr);

	    bool need_ack
	      = ((event.flags & ZET_DEBUG_EVENT_FLAG_NEED_ACK) != 0);

	    /* HACK: empty module load event

	       If an application does not provide an ELF file
	       (e.g. shared library), level-zero provides a module load event
	       with begin and end initialized to zero.  Those 'empty' events
	       result in an error when forwarded to GDB.

	       We ignore 'empty' module load events on GDB-server
	       side and acknowledge the event to level-zero.

	       This hack will become obsolete with zebin.  */
	    if (event.info.module.moduleBegin < event.info.module.moduleEnd)
	      loaded_dll (process, event.info.module.moduleBegin,
			  event.info.module.moduleEnd,
			  event.info.module.load, need_ack);
	    else
	      {
		dprintf ("empty module load event received: %s",
			 ze_event_str (event).c_str ());
		ze_ack_event (device, event);
		continue;
	      }

	    /* If level-zero is not requesting the event to be
	       acknowledged, we're done.

	       This happens when attaching to an already running process,
	       for example.  We will receive module load events for
	       modules that have already been loaded.

	       No need to inform GDB, either, as we expect GDB to query
	       shared libraries after attach.  */
	    if (!need_ack)
	      continue;

	    device.ack_pending.emplace_back (event);

	    /* Loading a new module is a process event.  We do not want to
	       overwrite other process events, however, as module loads
	       can also be communicated as part of other events.  */
	    process_info_private *zeproc = process->priv;
	    gdb_assert (zeproc != nullptr);

	    if (zeproc->waitstatus.kind () != TARGET_WAITKIND_IGNORE)
	      continue;

	    /* We use UNAVAILABLE rather than LOADED as the latter implies
	       that the target has stopped.  */
	    zeproc->waitstatus.set_unavailable ();
	  }
	  continue;

	case ZET_DEBUG_EVENT_TYPE_MODULE_UNLOAD:
	  {
	    /* We would not remain attached without a process.  */
	    process_info *process = device.process;
	    gdb_assert (process != nullptr);

	    /* HACK: empty module unload event

	       We ignore the module load event for 'empty' events and
	       need to ignore the corresponding module unload event.  */
	    if (event.info.module.moduleBegin < event.info.module.moduleEnd)
	      unloaded_dll (process, event.info.module.moduleBegin,
			    event.info.module.moduleEnd,
			    event.info.module.load);
	    else
	      dprintf ("empty module unload event received: %s",
		       ze_event_str (event).c_str ());

	    /* We don't need an ack, here, but maybe level-zero does.  */
	    ze_ack_event (device, event);

	    /* We do not notify GDB immediately about the module unload.
	       This is harmless until we reclaim the memory for something
	       else.  In our case, this can only be another module and we
	       will notify GDB in that case.  */
	  }
	  continue;

	case ZET_DEBUG_EVENT_TYPE_THREAD_STOPPED:
	  {
	    ze_device_thread_t tid = event.info.thread.thread;
	    ze_ack_event (device, event);

	    /* We would not remain attached without a process.  */
	    process_info *process = device.process;
	    gdb_assert (process != nullptr);

	    uint32_t nstopped = 0;
	    for_each_thread (device, tid, [&] (thread_info *tp)
	      {
		/* Ignore threads we know to be stopped.

		   We already analyzed the stop reason and probably
		   destroyed it in the process.  */
		if (ze_thread_stopped (tp))
		  return;

		/* Prevent underflowing.  */
		if (device.nresumed > 0)
		  device.nresumed--;

		ze_thread_info *zetp = ze_thread (tp);
		gdb_assert (zetp != nullptr);

		/* Discard any registers we may have fetched while TP was
		   unavailable.  */
		ze_discard_regcache (tp);
		try
		  {
		    /* Find the thread's tdesc and create one for its
		       regset if needed.  */
		    update_thread_tdesc (tp);

		    gdb_signal signal = GDB_SIGNAL_0;

		    /* If this is an unavailable thread with a 'stop'
		       resume state, from GDB's point of view the
		       thread was interrupted.  In all-stop mode, we
		       keep the event held to not confuse GDB.

		       Do the state update before get_stop_reason
		       below, so that in case we access memory, we
		       will do that using the right thread
		       context.  */
		    if (!non_stop
			&& (zetp->exec_state == ze_thread_state_unavailable)
			&& (zetp->resume_state == ze_thread_resume_stop))
		      zetp->exec_state = ze_thread_state_held;
		    else
		      zetp->exec_state = ze_thread_state_stopped;

		    target_stop_reason reason = get_stop_reason (tp, signal);

		    zetp->stop_reason = reason;
		    zetp->waitstatus.set_stopped (signal);
		    nstopped += 1;
		  }
		/* FIXME: exceptions

		   We'd really like to catch some 'thread_unavailable'
		   exception rather than assuming that any exception is
		   due to thread availability.  */
		catch (...)
		  {
		    zetp->exec_state = ze_thread_state_unavailable;
		    zetp->waitstatus.set_unavailable ();
		  }
	      });

	    dprintf ("device %lu's nresumed=%ld%s",
		     device.ordinal, device.nresumed,
		     ((device.nresumed == device.nthreads) ? " (all)" : ""));

	    /* This is the response to an interrupt if TID is "all".  */
	    if (ze_is_thread_id_all (tid))
	      {
		if (device.ninterrupts > 0)
		  device.ninterrupts--;
		else
		  warning (_("ignoring spurious stop-all event on "
			     "device %lu"), device.ordinal);
	      }

	    /* A thread event turns a process visible.  */
	    if (nstopped > 0)
	      ze_show_process (process);
	  }
	  continue;

	case ZET_DEBUG_EVENT_TYPE_THREAD_UNAVAILABLE:
	  {
	    ze_device_thread_t tid = event.info.thread.thread;
	    ze_ack_event (device, event);

	    /* We would not remain attached without a process.  */
	    process_info *process = device.process;
	    gdb_assert (process != nullptr);

	    for_each_thread (device, tid, [&] (thread_info *tp)
	      {
		/* Ignore threads we know to be stopped.

		   They would not be considered in the response event for
		   an interrupt request.  */
		if (ze_thread_stopped (tp))
		  return;

		/* Prevent underflowing.  */
		if (device.nresumed > 0)
		  device.nresumed--;

		ze_thread_info *zetp = ze_thread (tp);
		gdb_assert (zetp != nullptr);

		zetp->exec_state = ze_thread_state_unavailable;
		zetp->waitstatus.set_unavailable ();
	      });

	    dprintf ("device %lu's nresumed=%ld%s",
		     device.ordinal, device.nresumed,
		     ((device.nresumed == device.nthreads) ? " (all)" : ""));

	    /* This is the response to an interrupt if TID is "all".  */
	    if (ze_is_thread_id_all (tid))
	      {
		if (device.ninterrupts > 0)
		  device.ninterrupts--;
		else
		  warning (_("ignoring spurious unavailable-all event on "
			     "device %lu"), device.ordinal);
	      }
	  }
	  continue;

	case ZET_DEBUG_EVENT_TYPE_PAGE_FAULT:
	  {
	    /* This is a process event.  */
	    process_info *process = device.process;
	    if (process == nullptr)
	      {
		dprintf ("internal process info is not available");
		continue;
	      }

	    process_info_private *zeproc = process->priv;
	    gdb_assert (zeproc != nullptr);

	    zeproc->waitstatus.set_signalled (GDB_SIGNAL_SEGV);
	  }
	  continue;
	}

      /* We only get here if we have not processed EVENT.  */
      warning (_("ignoring event '%s' on %s."),
	       ze_event_str (event).c_str (),
	       device.properties.name);

      /* Acknowledge the ignored event so we don't get stuck.  */
      ze_ack_event (device, event);
    }
}

void
ze_target::fetch_events_all_devices_no_resumed ()
{
  uint64_t nresumed = 0;
  do {
    nresumed = 0;
    for (ze_device_info *device : devices)
      {
	gdb_assert (device != nullptr);

	/* Ignore devices we're not modelling as processes.  */
	if (device->process == nullptr)
	  continue;

	/* Event processing maintains the number of resumed threads.  */
	fetch_events (*device);
	nresumed += device->nresumed;
      }
  }
  while (nresumed != 0);
}

void
ze_target::init ()
{
  ze_result_t status = zeInit (0);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    default:
      error (_("Failed to initialize level-zero: %x"), status);
    }
}

bool
ze_target::async (bool enable)
{
  bool previous = ze_is_async ();
  if (previous != enable)
    {
#ifndef USE_WIN32API
      if (enable)
	{
	  try
	    {
	      errno = 0;
	      int status = pipe (ze_event_pipe);
	      if (status == -1)
		error (_("Failed to create event pipe: %s."),
		       safe_strerror (errno));

	      status = fcntl (ze_event_pipe[0], F_SETFL, O_NONBLOCK);
	      if (status == -1)
		error (_("Failed to set pipe[0] to non-blocking: %s."),
		       safe_strerror (errno));

	      status = fcntl (ze_event_pipe[1], F_SETFL, O_NONBLOCK);
	      if (status == -1)
		error (_("Failed to set pipe[1] to non-blocking: %s."),
		       safe_strerror (errno));

	      /* Register the event loop handler.  */
	      add_file_handler (ze_event_pipe[0],
				handle_target_event, NULL,
				"ze-low");

	      /* Always trigger a wait.  */
	      ze_async_mark ();
	    }
	  catch (std::exception &ex)
	    {
	      warning ("%s", ex.what ());

	      if (ze_event_pipe[0] != -1)
		{
		  close (ze_event_pipe[0]);
		  ze_event_pipe[0] = -1;
		}

	      if (ze_event_pipe[1] != -1)
		{
		  close (ze_event_pipe[1]);
		  ze_event_pipe[1] = -1;
		}
	    }
	}
      else
	{
	  delete_file_handler (ze_event_pipe[0]);

	  close (ze_event_pipe[0]);
	  close (ze_event_pipe[1]);
	  ze_event_pipe[0] = -1;
	  ze_event_pipe[1] = -1;
	}
#else
      error (_("%s: tbd"), __FUNCTION__);
#endif
    }

  return previous;
}

int
ze_target::create_inferior (const char *program,
			    const std::vector<char *> &argv)
{
  /* Level-zero does not support creating inferiors.  */
  return -1;
}

int
ze_target::attach (int pid)
{
  if (!devices.empty ())
    critical_error (0x02, _("Already attached."));

  uint32_t hostpid = (uint32_t) pid;
  if ((int) hostpid != pid)
    critical_error (0x02, _("Host process id is not supported."));

  int ndevices = attach_to_devices (hostpid);
  if (ndevices == 0)
    critical_error (0x02, _("No supported devices found."));

  /* Let's check if we were able to attach to at least one device.  */
  int nattached = 0;
  std::stringstream sstream;
  sstream << "Failed to attach to any device.";
  for (ze_device_info *device : devices)
    {
      gdb_assert (device != nullptr);
      switch (device->debug_attach_state)
	{
	case ZE_RESULT_SUCCESS:
	  if (device->session == nullptr)
	    {
	      sstream << "\nDevice " << device->ordinal << " ["
		      << device->properties.name << "] : "
		      << "failed to initialize debug session";
	      continue;
	    }

	  /* GDB (and higher layers of gdbserver) expects threads stopped on
	     attach in all-stop mode.  In non-stop mode, GDB explicitly
	     sends a stop request.  */
	  if (!non_stop)
	    {
	      int device_pid = ze_device_pid (*device);
	      for_each_thread (device_pid, [this] (thread_info *tp)
		{
		  ze_set_resume_state (tp, resume_stop);
		  bool should_stop = ze_prepare_for_stopping (tp);
		  gdb_assert (should_stop);
		});

	      ze_device_thread_t all = ze_thread_id_all ();
	      ze_interrupt (*device, all);
	    }

	  nattached += 1;
	  break;
	case ZE_RESULT_NOT_READY:
	  sstream << "\nDevice " << device->ordinal << " ["
		  << device->properties.name << "] : "
		  << "attempting to attach too early";
	  break;
	case ZE_RESULT_ERROR_UNSUPPORTED_FEATURE:
	  sstream << "\nDevice " << device->ordinal << " ["
		  << device->properties.name << "] : "
		  << "attaching is not supported";
	  break;
	case ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS:
	  sstream << "\nDevice " << device->ordinal << " ["
		  << device->properties.name << "] : "
		  << "attaching is not permitted";
	  break;
	case ZE_RESULT_ERROR_NOT_AVAILABLE:
	  sstream << "\nDevice " << device->ordinal << " ["
		  << device->properties.name << "] : "
		  << "a debugger is already attached";
	  break;
	default:
	  sstream << "\nDevice " << device->ordinal << " ["
		  << device->properties.name << "] : "
		  << "failed to attach with error code '"
		  << std::hex << device->debug_attach_state
		  << std::resetiosflags (std::ios::basefield)
		  << "'";
	  break;
	}
    }

  if (nattached == 0)
    critical_error (0x02, sstream.str ().c_str ());

  /* In all-stop mode above, we interrupted the devices.  Now we make sure
     they come to a stop state.  So, we fetch events until no device has any
     resumed threads left.  There might be low priority events (e.g.
     'module load', 'process entry') we should fetch before fetching higher
     priority events in the subsequent call of 'wait ()'.  If not done here,
     we fetch the lower priority events in 'wait ()', report an UNAVAILABLE
     status to GDB and then fetch the higher priority events in 'pause_all'.
     In a live attach scenario, we don't receive a 'continue' resume request
     and would miss the thread stopped event.  */
  if (!non_stop)
    fetch_events_all_devices_no_resumed ();

  /* Return the ID of the last device we attached to.  */
  int device_pid = ze_device_pid (*(devices.back ()));
  return device_pid;
}

int
ze_target::detach (process_info *proc)
{
  gdb_assert (proc != nullptr);

  process_info_private *priv = proc->priv;
  gdb_assert (priv != nullptr);

  ze_device_info *device = priv->device;
  if (device != nullptr)
    {
      /* Resume all the threads on the device.  GDB must have already
	 removed all the breakpoints.  */
      try
	{
	  /* Clear all the pending events first.  */
	  for_each_thread (pid_of (proc), [] (thread_info *tp)
	    {
	      (void) ze_move_waitstatus (tp);
	    });

	  resume (*device);
	}
      catch (const gdb_exception_error &except)
	{
	  /* Swallow the error.  We are detaching anyway.  */
	  dprintf ("%s", except.what ());
	}

      ze_detach (device);
    }

  mourn (proc);
  return 0;
}

int
ze_target::kill (process_info *proc)
{
  /* Level-zero does not support killing inferiors.  */
  return -1;
}

void
ze_target::mourn (process_info *proc)
{
  ze_remove_process (proc);
}

void
ze_target::join (int pid)
{
  /* Nothing to do for level-zero targets.  */
}

void
ze_target::resume (ze_device_info &device)
{
  gdb_assert (device.process != nullptr);

  bool has_thread_to_resume = false;
  for_each_thread (ze_device_pid (device), [&] (thread_info *tp)
    {
      ze_set_resume_state (tp, resume_continue);
      if (ze_prepare_for_resuming (tp))
	{
	  prepare_thread_resume (tp);
	  regcache_invalidate_thread (tp);
	  has_thread_to_resume = true;
	}
    });

  /* There is nothing to resume if nothing is stopped.  */
  if (!has_thread_to_resume)
    return;

  ze_device_thread_t all = ze_thread_id_all ();
  ze_resume (device, all);
}

void
ze_target::resume_single_thread (thread_info *thread)
{
  ze_device_info *device = ze_thread_device (thread);
  gdb_assert (device != nullptr);
  ze_thread_info *zetp = ze_thread (thread);
  gdb_assert (zetp != nullptr);

  bool should_resume = ze_prepare_for_resuming (thread);
  gdb_assert (should_resume);
  prepare_thread_resume (thread);
  regcache_invalidate_thread (thread);
  ze_resume (*device, zetp->id);
}

size_t
ze_target::mark_eventing_threads (ptid_t resume_ptid, resume_kind rkind)
{
  /* Note that even if we stopped all, unavailable threads may still
     report new events as we were not able to stop them.

     We ignore those threads and the unavailable event they report.  */

  size_t num_eventing = 0;
  for_each_thread (resume_ptid, [=, &num_eventing] (thread_info *tp)
    {
      if (!ze_has_priority_waitstatus (tp))
	{
	  (void) ze_move_waitstatus (tp);
	  return;
	}

      ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      /* If the thread's stop event was being held, it is now the time
	 to convert the state to 'stopped' to unleash the event.  */
      if (zetp->exec_state == ze_thread_state_held)
	zetp->exec_state = ze_thread_state_stopped;

      /* TP may have stopped at a breakpoint that is already deleted
	 by GDB.  Consider TP as an eventing thread only if the BP is
	 still there.  Because we are inside the 'resume' request, if
	 the BP is valid, GDB must have already re-inserted it.

	 FIXME: Keep track of the stop_pc and compare it with the
	 current (i.e. to-be-resumed) pc.  */
      if ((zetp->exec_state == ze_thread_state_stopped)
	  && (zetp->stop_reason == TARGET_STOPPED_BY_SW_BREAKPOINT)
	  && !is_at_breakpoint (tp))
	{
	  /* The BP is gone.  Clear the waitstatus, too.  */
	  target_waitstatus waitstatus = ze_move_waitstatus (tp);
	  if (waitstatus.kind () != TARGET_WAITKIND_STOPPED)
	    warning (_("thread %s has waitstatus %s, expected 'STOPPED'."),
		     tp->id.to_string ().c_str (),
		     waitstatus.to_string ().c_str ());
	  return;
	}

      /* TP may have stopped during range-stepping, but we reported
	 another thread to GDB.  This means the range-stepping state
	 of TP is canceled.

	 The condition here is similar to, but not the same as
	 is_range_stepping.  We do not check here if the thread's stop
	 pc is within the stepping range.  We rather only check if there
	 was a range to step, because the thread may have stopped just
	 when it came out of the range.  We should cancel the event in
	 that case, too.  */
      if (ze_thread_stopped (tp)
	  && (zetp->stop_reason == TARGET_STOPPED_BY_SINGLE_STEP)
	  && (zetp->step_range_end > zetp->step_range_start))
	{
	  target_waitstatus waitstatus = ze_move_waitstatus (tp);
	  dprintf ("Thread %s (%s) was range-stepping, "
		   "canceling the pending event",
		   tp->id.to_string ().c_str (),
		   ze_thread_id_str (zetp->id).c_str ());
	  return;
	}

      /* Recover the resume state so that the thread can be picked up
	 by 'wait'.  */
      ze_set_resume_state (tp, rkind);
      num_eventing++;
    });

  dprintf ("there are %zu eventing threads for ptid %s", num_eventing,
	   resume_ptid.to_string ().c_str ());

  return num_eventing;
}

/* Display a resume request for logging purposes.  */

static void
print_resume_info (const thread_resume &rinfo)
{
  ptid_t rptid = rinfo.thread;

  switch (rinfo.kind)
    {
    case resume_continue:
      dprintf ("received 'continue' resume request for (%s)",
	       rptid.to_string ().c_str ());
      return;

    case resume_step:
      dprintf ("received 'step' resume request for (%s)"
	       " in range [0x%" PRIx64 ", 0x%" PRIx64 ")",
	       rptid.to_string ().c_str (),
	       rinfo.step_range_start, rinfo.step_range_end);
      return;

    case resume_stop:
      dprintf ("received 'stop' resume request for (%s)",
	       rptid.to_string ().c_str ());
      return;
    }

  internal_error (_("bad resume kind: %d."), rinfo.kind);
}

/* Normalize the resume requests for easier processing later on.  */

static void
normalize_resume_infos (thread_resume *resume_info, size_t n)
{
  for (size_t i = 0; i < n; ++i)
    {
      thread_resume &rinfo = resume_info[i];
      ptid_t rptid = rinfo.thread;

      /* Log the original requests.  */
      print_resume_info (rinfo);

      /* We convert ptids of the form (p, -1, 0) to (p, 0, 0) to make
	 'ptid.matches' work.  This transformation is safe because we
	 enumerate the threads starting at 1.  */
      if ((rptid.lwp () == -1) && (rptid.pid () > 0))
	rinfo.thread = ptid_t (rptid.pid (), 0, 0);

      if (rinfo.sig != 0)
	{
	  /*  Clear out the signal.  Our target does not accept
	      signals.  */
	  warning (_("Ignoring signal on resume request for %s"),
		   rinfo.thread.to_string ().c_str ());
	  rinfo.sig = 0;
	}
    }
}

/* Resuming threads of a device all at once with a single API call
   is preferable to resuming threads individually.  Therefore, we
   want to combine individual resume requests with wildcard resumes,
   if possible.

   For instance, if we receive "vCont;s:1;s:2;c", we would like to
   make a single ze_resume call with the 'all.all.all.all' thread id
   after preparing threads 1 and 2 for stepping and the others for
   continuing.

   We preprocess the resume requests to find for which devices we
   shall combine the requests.  We attempt a merge in all-stop mode
   when the requests contain continue/step requests only.  */

static std::set<ze_device_info *>
find_wildcard_devices (thread_resume *resume_info, size_t n,
		       const std::list<ze_device_info *> &devices)
{
  std::set<ze_device_info *> wildcard_devices;

  if (non_stop)
    return wildcard_devices;

  for (size_t i = 0; i < n; ++i)
    {
      if (resume_info[i].kind == resume_stop)
	{
	  wildcard_devices.clear ();
	  break;
	}

      ptid_t rptid = resume_info[i].thread;
      if (rptid == minus_one_ptid)
	{
	  for (ze_device_info *device : devices)
	    wildcard_devices.insert (device);
	  break;
	}

      if (rptid.is_pid ())
	{
	  process_info *proc = find_process_pid (rptid.pid ());
	  ze_device_info *device = ze_process_device (proc);
	  if (device != nullptr)
	    wildcard_devices.insert (device);
	}
    }

  return wildcard_devices;
}

void
ze_target::resume (thread_resume *resume_info, size_t n)
{
  if (frozen)
    return;

  /* In all-stop mode, a new resume request overwrites any previous
     request.  We're going to set the request for affected threads below.
     Clear it for all threads, here.

     In the resume-all case, this will iterate over all threads twice to
     first clear and then set the resume request.  Not ideal, but if we
     first iterated over all threads to set the resume state, we'd also
     have to iterate over all threads again in order to actually resume
     them.

     And if we inverted the loops (i.e. iterate over threads, then over
     resume requests), we'd miss out on the opportunity to resume all
     threads at once.  */
  if (!non_stop)
    for_each_thread ([] (thread_info *tp)
      {
	ze_clear_resume_state (tp);
      });

  normalize_resume_infos (resume_info, n);

  /* Check if there is a thread with a pending event for any of the
     resume requests.  In all-stop mode, we would omit actually
     resuming the target if there is such a thread.  In non-stop mode,
     we omit resuming the thread itself.  */
  size_t num_eventing = 0;
  for (size_t i = 0; i < n; ++i)
    {
      const thread_resume &rinfo = resume_info[i];
      resume_kind rkind = rinfo.kind;
      ptid_t rptid = rinfo.thread;

      if (rkind == resume_stop)
	continue;

      num_eventing += mark_eventing_threads (rptid, rkind);
    }

  if ((num_eventing > 0) && !non_stop)
    return;

  std::set<ze_device_info *> wildcard_devices
    = find_wildcard_devices (resume_info, n, devices);

  std::set<ze_device_info *> devices_to_resume;

  /* Lambda for applying a resume info on a single thread.  */
  auto apply_resume_info = ([&] (const thread_resume &rinfo,
				 thread_info *tp)
    {
      if (ze_has_priority_waitstatus (tp))
	return;

      ze_set_resume_state (tp, rinfo.kind);
      ze_device_info *device = ze_thread_device (tp);
      ze_device_thread_t tid = ze_thread_id (tp);

      switch (rinfo.kind)
	{
	case resume_stop:
	  if (ze_prepare_for_stopping (tp))
	    ze_interrupt (*device, tid);
	  break;

	case resume_step:
	  {
	    ze_thread_info *zetp = ze_thread (tp);
	    gdb_assert (zetp != nullptr);

	    regcache *regcache
	      = get_thread_regcache (tp, /* fetch = */ false);
	    CORE_ADDR pc = read_pc (regcache);

	    /* For single-stepping, start == end.  Typically, both are 0.
	       For range-stepping, the PC must be within the range.  */
	    CORE_ADDR start = rinfo.step_range_start;
	    CORE_ADDR end = rinfo.step_range_end;
	    gdb_assert ((start == end) || ((pc >= start) && (pc < end)));

	    zetp->step_range_start = start;
	    zetp->step_range_end = end;
	  }
	  [[fallthrough]];

	case resume_continue:
	  if (ze_prepare_for_resuming (tp))
	    {
	      prepare_thread_resume (tp);
	      regcache_invalidate_thread (tp);

	      /* If the device can be resumed as a whole,
		 omit resuming the thread individually.  */
	      if (wildcard_devices.count (device) == 0)
		ze_resume (*device, tid);
	      else
		devices_to_resume.insert (device);
	    }
	  break;
	}
    });

  /* We may receive multiple requests that apply to a thread.  E.g.
     "vCont;r0xff10,0xffa0:p1.9;c" could be sent to make thread 1.9 do
     range-stepping from 0xff10 to 0xffa0, while continuing others.
     According to the Remote Protocol Section E.2 (Packets),
     "For each inferior thread, the leftmost action with a matching
     thread-id is applied."  For this reason, we keep track of which
     threads have been resumed individually so that we can skip them
     when processing wildcard requests.

     Alternatively, we could have the outer loop iterate over threads
     and the inner loop iterate over resume infos to find the first
     matching resume info for each thread.  There may, however, be a
     large number of threads and a handful of resume infos that apply
     to a few threads only.  For performance reasons, we prefer to
     iterate over resume infos in the outer loop.  */
  std::set<thread_info *> individually_resumed_threads;
  for (size_t i = 0; i < n; ++i)
    {
      const thread_resume &rinfo = resume_info[i];
      gdb_assert (rinfo.sig == 0);
      ptid_t rptid = rinfo.thread;
      int rpid = rptid.pid ();
      if ((rptid == minus_one_ptid)
	  || rptid.is_pid ()
	  || (rptid.lwp () == -1))
	{
	  for (ze_device_info *device : devices)
	    {
	      gdb_assert (device != nullptr);

	      int pid = ze_device_pid (*device);
	      if ((rpid != -1) && (rpid != pid))
		continue;

	      for_each_thread (pid, [&] (thread_info *tp)
		{
		  /* We trust that GDB will not send us wildcard resume
		     requests with overlapping pids.  Hence, we track
		     only individually-resumed threads.  */
		  if (individually_resumed_threads.count (tp) == 0)
		    apply_resume_info (rinfo, tp);
		});
	    }
	}
      else
	{
	  thread_info *tp = find_thread_ptid (rptid);
	  apply_resume_info (rinfo, tp);
	  individually_resumed_threads.insert (tp);
	}
    }

  /* Finally, resume the whole devices.  */
  ze_device_thread_t all = ze_thread_id_all ();
  for (ze_device_info *device : devices_to_resume)
    ze_resume (*device, all);
}

/* Look for a thread preferably with a priority stop
   event.  If we cannot find such an event, we look for an
   interrupt-related stop event, e.g.  a stop because of an
   external Ctrl-C or an internal pause_all request.  We pick
   a THREAD_UNAVAILABLE event for reporting as the last resort.

   We first make an iteration over the threads to figure out
   what kind of an event we can report.  Once found, we select
   a thread randomly.

   In all-stop mode, we will ignore unavailable threads when
   resuming the target.  So, unless we explicitly try to interact
   with them, unavailable threads should be transparent to an
   all-stop target.

   In non-stop mode, we give more time for unavailable threads to
   become available and report an event.  */

static thread_info *
ze_find_eventing_thread (ptid_t ptid)
{
  using thread_predicate = bool (*) (const thread_info *);
  thread_predicate is_stopped = [] (const thread_info *tp)
  {
    return (ze_thread (tp)->waitstatus.kind () == TARGET_WAITKIND_STOPPED);
  };

  thread_predicate predicate = nullptr;
  find_thread (ptid, [&] (thread_info *tp)
    {
      /* Only consider threads that were resumed.  */
      ze_thread_resume_state_t state = ze_resume_state (tp);
      if (state == ze_thread_resume_none)
	return false;

      /* If this thread's event is being held, we do not pick it for
	 reporting.  */
      ze_thread_exec_state_t exec_state = ze_exec_state (tp);
      if (exec_state == ze_thread_state_held)
	return false;

      if (ze_has_priority_waitstatus (tp))
	{
	  predicate = ze_has_priority_waitstatus;
	  return true;
	}

      if (is_stopped (tp))
	predicate = is_stopped;
      else if ((predicate == nullptr) && ze_has_waitstatus (tp))
	predicate = ze_has_waitstatus;

    return false;
    });

  thread_info *thread = nullptr;
  if (predicate != nullptr)
    thread = find_thread_in_random (ptid, [predicate] (thread_info *tp)
      {
	/* Only consider threads that were resumed.  */
	ze_thread_resume_state_t state = ze_resume_state (tp);
	if (state == ze_thread_resume_none)
	  return false;

	/* Threads with held events are not picked.  */
	ze_thread_exec_state_t exec_state = ze_exec_state (tp);
	if (exec_state == ze_thread_state_held)
	  return false;

	return predicate (tp);
      });
  return thread;
}

ptid_t
ze_target::wait (ptid_t ptid, target_waitstatus *status,
		 target_wait_flags options)
{
  /* We need to wait for further events.  */
  ze_async_mark ();

  do
    {
      /* We start by fetching all events.

	 This will mark threads stopped and also process solist updates.  We may
	 get solist updates even if all device threads are running.

	 For all-stop, we anyway want to stop all threads and drain events
	 before reporting the stop to GDB.

	 For non-stop, this will allow us to group stop events for multiple
	 threads.  */
      uint64_t nevents;
      do
	{
	  nevents = 0;

	  for (ze_device_info *device : devices)
	    {
	      gdb_assert (device != nullptr);
	      /* Fetch from any device, regardless of PTID, so that we
		 drain the event queues as much as possible.  We use
		 PTID down below to filter the events anyway.  */
	      nevents += fetch_events (*device);
	    }
	}
      while (nevents > 0);

      /* Next, find a matching entity, whose event we'll report.

	 We prioritize process events since they are typically a lot rarer and
	 further have higher impact and should be handled before any thread
	 events of that process.

	 Process events are no stop events.  They leave threads running,
	 even in all-stop mode.  */
      process_info *process
	= find_process ([ptid, this] (process_info *proc)
	  {
	    if (!ptid_t (proc->pid).matches (ptid))
	      return false;

	    process_info_private *zeproc = proc->priv;
	    gdb_assert (zeproc != nullptr);

	    return (zeproc->waitstatus.kind () != TARGET_WAITKIND_IGNORE);
	  });

      /* If we found a process event, it is our primary candidate.

	 Process events with a low priority UNAVAILABLE waitstatus do not
	 stop the target in all-stop, but some of its threads might have a
	 pending waitstatus, which requires the stop.  If such THREAD is
	 found, we prioritize it, clean the process waitstatus, and fall
	 through to the thread reporting.  The process event will
	 piggyback on it.

	 We do not take any special care about fairness as we expect process
	 events to be rather rare.  */
      thread_info *thread = nullptr;
      if (process != nullptr)
	{
	  process_info_private *zeproc = process->priv;
	  gdb_assert (zeproc != nullptr);
	  ptid_t process_ptid = ptid_t (process->pid);

	  /* If we got an unavailable process event, try to find another
	     eventing thread for this process.  */
	  if (zeproc->waitstatus.kind () == TARGET_WAITKIND_UNAVAILABLE)
	    thread = ze_find_eventing_thread (process_ptid);

	  /* If not found, return the process and clean its waitstatus.  */
	  if (thread == nullptr)
	    {
	      *status = zeproc->waitstatus;
	      zeproc->waitstatus.set_ignore ();

	      return process_ptid;
	    }

	  /* THREAD should always match the PTID: we got a process event,
	     so PTID must be either minus_one or the process's ptid.  */
	  gdb_assert (thread->id.matches (ptid));

	  /* The process event will piggyback onto the THREAD event.
	     However, we still need to clean the process status.  */
	  zeproc->waitstatus.set_ignore ();
	}

      /* If we have previously found THREAD for the PROCESS, we use it.
	 Otherwise, proceed with searching for a thread event for PTID.  */
      if (thread == nullptr)
	thread = ze_find_eventing_thread (ptid);

      if (thread != nullptr)
	{
	  ze_thread_info *zetp = ze_thread (thread);
	  gdb_assert (zetp != nullptr);

	  if (is_range_stepping (thread))
	    {
	      /* We are inside the stepping range.  Resume the thread
		 and go back to fetching events.  */
	      dprintf ("thread %s is stepping in range "
		       "[0x%" PRIx64 ", 0x%" PRIx64 ")",
		       ze_thread_id_str (zetp->id).c_str (),
		       zetp->step_range_start, zetp->step_range_end);

	      zetp->waitstatus.set_ignore ();
	      gdb_assert (zetp->resume_state == ze_thread_resume_step);

	      resume_single_thread (thread);
	      continue;
	    }

	  /* Resume any thread we didn't want stopped.  */
	  if ((zetp->stop_reason == TARGET_STOPPED_BY_NO_REASON)
	      && (zetp->waitstatus.kind () == TARGET_WAITKIND_STOPPED)
	      && (zetp->waitstatus.sig () == GDB_SIGNAL_0))
	    {
	      dprintf ("silently resuming thread %s (%s)",
		       thread->id.to_string ().c_str (),
		       ze_thread_id_str (zetp->id).c_str ());

	      /* Undo any previous holding of the event.  */
	      zetp->exec_state = ze_thread_state_stopped;
	      zetp->waitstatus.set_ignore ();
	      ze_set_resume_state (thread, resume_continue);

	      resume_single_thread (thread);
	      continue;
	    }

	  /* Stop all other threads.

	     Save the waitstatus before, because pause_all clears all
	     low-priority events.  */
	  *status = zetp->waitstatus;

	  if (!non_stop)
	    pause_all (false);

	  /* Now also clear the thread's event, regardless of its
	     priority.  */
	  zetp->waitstatus.set_ignore ();
	  zetp->step_range_start = 0;
	  zetp->step_range_end = 0;

	  /* FIXME: switch_to_thread

	     Why isn't the caller switching based on the returned ptid?  */
	  switch_to_thread (thread);
	  return ptid_of (thread);
	}

      std::this_thread::yield ();
    }
  while ((options & TARGET_WNOHANG) == 0);

  /* We only get here if we did not find any event to report.  */

  status->set_ignore ();
  return null_ptid;
}

void
ze_target::fetch_registers (regcache *regcache, int regno)
{
  if (regno == -1)
    ze_fetch_all_registers (regcache->thread, regcache);
  else
    ze_fetch_register (regcache->thread, regcache, regno);
}

void
ze_target::store_registers (regcache *regcache, int regno)
{
  if (regno == -1)
    ze_store_all_registers (regcache->thread, regcache);
  else
    ze_store_register (regcache->thread, regcache, regno);
}

/* Determine the thread id and device context for accessing ADDR_SPACE
   from THREAD.  */
static std::pair<ze_device_thread_t, ze_device_info *>
ze_memory_access_context (thread_info *thread, unsigned int addr_space)
{
  /* With a stopped thread, we can access all address spaces, and we
     should be able to determine the device for that thread.  */
  if (ze_thread_stopped (thread))
    return std::pair<ze_device_thread_t, ze_device_info *>
      { ze_thread_id (thread), ze_thread_device (thread) };

  /* Without a stopped thread, we may only access the default address
     space and only in the context of thread ALL.  */
  if (addr_space != ZET_DEBUG_MEMORY_SPACE_TYPE_DEFAULT)
    error (_("need thread to access non-default address space."));

  /* Try to determine the device using THREAD but fall back to the current
     process' device, e.g. if THREAD is nullptr.  */
  ze_device_info *device = ze_thread_device (thread);
  if (device == nullptr)
    {
      process_info *process = current_process ();
      device = ze_process_device (process);

      if (device == nullptr)
	error (_("cannot determine device for memory access."));
    }

  return std::pair<ze_device_thread_t, ze_device_info *>
    { ze_thread_id_all (), device };
}

int
ze_target::read_memory (thread_info *tp, CORE_ADDR memaddr,
			unsigned char *myaddr, int len,
			unsigned int addr_space)
{
  zet_debug_memory_space_desc_t desc;

  memset (&desc, 0, sizeof (desc));
  desc.stype = ZET_STRUCTURE_TYPE_DEBUG_MEMORY_SPACE_DESC;
  desc.pNext = nullptr;
  desc.type = (zet_debug_memory_space_type_t) addr_space;
  desc.address = (uint64_t) memaddr;

  std::pair<ze_device_thread_t, ze_device_info *> context
    = ze_memory_access_context (tp, addr_space);
  ze_device_thread_t thread = context.first;
  ze_device_info *device = context.second;
  gdb_assert (device != nullptr);

  ze_result_t status = zetDebugReadMemory (device->session, thread, &desc,
					   len, myaddr);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      return 0;

    default:
      dprintf ("error reading %d bytes of memory from %s with %s: %x",
	       len, core_addr_to_string_nz (memaddr),
	       ze_thread_id_str (thread).c_str (), status);

      return EIO;
    }
}

int
ze_target::read_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len,
			unsigned int addr_space)
{
  return read_memory (current_thread, memaddr, myaddr, len, addr_space);
}

int
ze_target::write_memory (thread_info *tp, CORE_ADDR memaddr,
			 const unsigned char *myaddr, int len,
			 unsigned int addr_space)
{
  zet_debug_memory_space_desc_t desc;

  memset (&desc, 0, sizeof (desc));
  desc.stype = ZET_STRUCTURE_TYPE_DEBUG_MEMORY_SPACE_DESC;
  desc.pNext = nullptr;
  desc.type = (zet_debug_memory_space_type_t) addr_space;
  desc.address = (uint64_t) memaddr;

  std::pair<ze_device_thread_t, ze_device_info *> context
    = ze_memory_access_context (tp, addr_space);
  ze_device_thread_t thread = context.first;
  ze_device_info *device = context.second;
  gdb_assert (device != nullptr);

  dprintf ("writing %d bytes of memory to %s with %s",
	   len, core_addr_to_string_nz (memaddr),
	   ze_thread_id_str (thread).c_str ());

  ze_result_t status = zetDebugWriteMemory (device->session, thread, &desc,
					    len, myaddr);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      return 0;

    default:
      dprintf ("error writing %d bytes of memory to %s with %s: %x",
	       len, core_addr_to_string_nz (memaddr),
	       ze_thread_id_str (thread).c_str (), status);

      return EIO;
    }
}

int
ze_target::write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
			 int len, unsigned int addr_space)
{
  return write_memory (current_thread, memaddr, myaddr, len, addr_space);
}

bool
ze_target::thread_stopped (struct thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  return (zetp->exec_state == ze_thread_state_stopped);
}

void
ze_target::request_interrupt ()
{
  if (!has_current_process ())
    error (_("no current process."));

  process_info *process = current_process ();
  gdb_assert (process != nullptr);

  process_info_private *priv = process->priv;
  gdb_assert (priv != nullptr);

  /* The only reason why we would not have a device is if we got detached.

     There is nothing to interrupt in that case.  */
  ze_device_info *device = priv->device;
  if (device == nullptr)
    return;

  /* Interrupt is not a resume request.  */

  ze_device_thread_t all = ze_thread_id_all ();
  ze_interrupt (*device, all);
}

void
ze_target::pause_all (bool freeze)
{
  dprintf ("freeze: %d", freeze);

  if (freeze)
    {
      if (frozen == UINT32_MAX)
	internal_error (_("freeze count overflow"));
      frozen += 1;
    }

  /* Nothing to stop if we were frozen already.  */
  if (frozen > 1)
    return;

  /* Interrupting all threads on devices that have any resumed threads.

     Threads that are already stopped will be ignored by the interrupt.  */
  ze_device_thread_t all = ze_thread_id_all ();
  for (ze_device_info *device : devices)
    {
      gdb_assert (device != nullptr);

      /* Ignore devices we're not modelling as processes.  */
      if (device->process == nullptr)
	continue;

      if ((device->nresumed != 0) && (device->ninterrupts == 0))
	ze_interrupt (*device, all);
    }

  /* Fetch events until no device has any resumed threads left.  */
  fetch_events_all_devices_no_resumed ();

  /* Mark threads we interrupted paused so unpause_all can find then.  */
  for_each_thread ([] (thread_info *tp)
    {
      /* A thread without waitstatus has already been processed by a
	 previous pause_all or it has reported its event to higher layers
	 via wait.

	 Don't mark it paused.  It either already is, if it was stopped by
	 a previous pause_all, or higher layers assume it to be stopped so
	 we don't want it so be resumed by unpause_all.  */
      if (!ze_has_waitstatus (tp))
	return;

      /* Do not mark threads that wait would pick, even if their event was
	 only just fetched.  */
      if (ze_has_priority_waitstatus (tp))
	return;

      ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      /* Clear the non-priority waitstatus so wait doesn't pick the thread
	 to report an (unavailable) event we just fetched.  */
      zetp->waitstatus.set_ignore ();

      /* Ignore threads that aren't stopped, most likely because they are
	 unavailable.

	 Even though an unavailable thread may have responded to our
	 interrupt, we do not mark it paused because we need to treat
	 unavailable and stopped threads differently in unpause_all.  */
      if (ze_thread_stopped (tp))
	zetp->exec_state = ze_thread_state_paused;
    });
}

void
ze_target::unpause_all (bool unfreeze)
{
  dprintf ("freeze: %d", unfreeze);

  if (unfreeze)
    {
      if (frozen == 0)
	internal_error (_("freeze count underflow"));
      frozen -= 1;
    }

  /* Nothing to resume if we're still frozen.  */
  if (frozen > 1)
    return;

  /* Resume threads that were marked by pause_all as well as unavailable
     threads that were not requested to stop.

     Pause_all leaves the latter marked unavailable.  We don't really
     resume them as they were not actually stopped on the target, but we
     need to update the thread state and some statistics.  */

  /* Check which devices are safe to be resumed and which need to be
     checked for individual threads to be resumed.

     In all-stop mode, finding a single thread would already block the
     unpause.  We do not expect this to be performance critical (or used
     at all), however, so let's unify all-stop and non-stop as much as
     possible.  */
  std::set<ze_device_info *> devices_to_check;
  std::set<ze_device_info *> devices_to_resume {devices.begin (),
    devices.end ()};

  for_each_thread ([&] (thread_info *tp)
    {
      ze_thread_exec_state_t state = ze_exec_state (tp);
      switch (state)
	{
	case ze_thread_state_paused:
	  return;

	case ze_thread_state_unavailable:
	  {
	    /* Distinguish unavailable threads that we tried to interrupt
	       in pause_all from those that GDB tried to interrupt with a
	       stop resume request.  */
	    ze_thread_resume_state_t resume_state = ze_resume_state (tp);
	    if (resume_state != ze_thread_resume_stop)
	      return;
	  }
	  [[fallthrough]];

	case ze_thread_state_stopped:
	case ze_thread_state_held:
	  {
	    ze_device_info *device = ze_thread_device (tp);
	    if (device == nullptr)
	      return;

	    devices_to_check.insert (device);
	    devices_to_resume.erase (device);
	  }
	  return;

	case ze_thread_state_running:
	  warning (_("thread %d.%ld running in unpause"), tp->id.pid (),
		   tp->id.lwp ());
	  return;

	case ze_thread_state_unknown:
	  warning (_("thread %d.%ld has unknown execution "
		     "state"), tp->id.pid (), tp->id.lwp ());
	  return;
	}

      internal_error (_("bad execution state: %d."), state);
    });

  /* In all-stop mode, any device that cannot be resumed aborts unpause.  */
  if (!non_stop && !devices_to_check.empty ())
    return;

  /* Resume individual threads.

     In all-stop mode, this will be empty.  */
  for (ze_device_info *device : devices_to_check)
    {
      gdb_assert (device != nullptr);

      int pid = ze_device_pid (*device);
      for_each_thread (pid, [this] (thread_info *tp)
	{
	  ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  ze_thread_exec_state_t state = zetp->exec_state;
	  switch (state)
	    {
	    case ze_thread_state_stopped:
	    case ze_thread_state_held:
	    case ze_thread_state_running:
	    case ze_thread_state_unknown:
	      /* We already diagnosed unexpected states above.  */
	      return;

	    case ze_thread_state_unavailable:
	      {
		/* Don't touch threads that GDB wants stopped.  */
		ze_thread_resume_state_t resume_state = ze_resume_state (tp);
		if (resume_state == ze_thread_resume_stop)
		  return;

		/* We don't plan to resume but we still need to prepare TP
		   for nresumed tracking and thread state management.  */
		bool should_resume = ze_prepare_for_resuming (tp);
		gdb_assert (!should_resume);
	      }
	      return;

	    case ze_thread_state_paused:
	      resume_single_thread (tp);
	      return;
	    }
	});
    }

  /* Resume entire devices at once.  */
  for (ze_device_info *device : devices_to_resume)
    {
      gdb_assert (device != nullptr);

      /* Skip devices we're not modeling as processes.  */
      if (device->process == nullptr)
	continue;

      resume (*device);
    }
}

void
ze_target::ack_library (process_info *process, const char *name)
{
  /* All libraries are in-memory.  */
  warning (_("unexpected acknowledgement requested for library %s."), name);
}

void
ze_target::ack_in_memory_library (process_info *process,
				  CORE_ADDR begin, CORE_ADDR end)
{
  gdb_assert (process != nullptr);

  process_info_private *zeproc = process->priv;
  gdb_assert (zeproc != nullptr);

  /* The only reason why we would not have a device is if we got detached.

     There is nothing to acknowledge in that case.  */
  ze_device_info *device = zeproc->device;
  if (device == nullptr)
    {
      dprintf ("[%s;%s) device not found.", core_addr_to_string_nz (begin),
	       core_addr_to_string_nz (end));
      return;
    }

  events_t &events = device->ack_pending;
  events_t::iterator it
    = std::find_if (events.begin (), events.end (),
		    [begin, end] (const zet_debug_event_t &ev)
	{
	  return ((ev.type == ZET_DEBUG_EVENT_TYPE_MODULE_LOAD)
		  && (ev.info.module.moduleBegin == begin)
		  && (ev.info.module.moduleEnd == end));
	});

  if (it == events.end ())
    {
      dprintf ("[%s;%s) not found.", core_addr_to_string_nz (begin),
	       core_addr_to_string_nz (end));
      return;
    }

  ze_ack_event (*device, *it);
  events.erase (it);

  dprintf ("[%s;%s) acknowledged.", core_addr_to_string_nz (begin),
	   core_addr_to_string_nz (end));
}

std::string
ze_target::thread_id_str (thread_info *thread)
{
  const ze_thread_info *zetp = ze_thread (thread);
  gdb_assert (zetp != nullptr);

  std::stringstream id_str;
  id_str << "ZE " << ze_thread_id_str (zetp->id);

  return id_str.str ();
}

const std::string
ze_target::id_str (process_info *process)
{
  gdb_assert (process != nullptr);

  process_info_private *zeproc = process->priv;
  gdb_assert (zeproc != nullptr);

  ze_device_info *device = zeproc->device;
  gdb_assert (device != nullptr);

  std::string id = string_printf ("device [%s]", device->pci_slot.c_str ());
  if ((device->properties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE) != 0)
    id += string_printf (".%d", device->properties.subdeviceId);

  return id;
}

/* Less than operator for zet_debug_regset_properties_t.  Used by
   map from ze_regset_properties_v_t to ze_tdesc.  It uses these fields from
   zet_debug_regset_properties_t for strict weak ordering.

   uint32_t type;			 ///< device-specific register set type
   uint32_t version;			 ///< device-specific version of this
					 ///< register set
   uint32_t count;			 ///< number of registers in the
					 ///< set
   uint32_t byteSize;			 ///< the size required for
					 ///< reading or writing a register
					 ///< in bytes
*/

static bool
operator<(const zet_debug_regset_properties_t &lhs,
	  const zet_debug_regset_properties_t &rhs)
{
  return (std::tie (lhs.type, lhs.version, lhs.count, lhs.byteSize)
	  < std::tie (rhs.type, rhs.version, rhs.count, rhs.byteSize));
}

/* Fetch a thread's register set and select a tdesc.  Lookup in the tdesc
   cache.  If not found create a new tdesc from Level-Zero properties.
   Returns pointer to tdesc cache info.  */

const ze_tdesc *
ze_target::select_thread_tdesc (thread_info *tp,
				const ze_regset_properties_v_t &regsets)
{
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  const ze_tdesc *cached_tdesc = device->tdesc_cache.find (regsets);
  target_desc *tdesc = nullptr;

  /* The thread has no tdesc, create one.  */
  if (cached_tdesc == nullptr)
    {
      tdesc = create_tdesc (device, regsets);
      cached_tdesc = device->tdesc_cache.find (regsets);
      dprintf ("created tdesc %p (id=%u).", tdesc, tdesc->id);
    }
  else
    tdesc = cached_tdesc->tdesc.get ();

  /* The thread's tdesc has changed, release regcache.  */
  if (tp->tdesc != tdesc)
    {
      free_register_cache_thread (tp);
      auto zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);
      zetp->thread_changed = true;
    }

  return cached_tdesc;
}

/* Guard against use of possibly undefined zetDebugGetThreadRegisterSet-
   Properties API.  We will always have at least one tdesc that was created
   after attaching.  */

void
ze_target::update_thread_tdesc (thread_info *tp)
{
#ifdef HAVE_ZET_PFNDEBUGGETTHREADREGISTERSETPROPERTIES_T
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);
  ze_device_thread_t tid = ze_thread_id (tp);

  uint32_t nregsets = 0;
  ze_result_t status = zetDebugGetThreadRegisterSetProperties (device->session,
							       tid,
							       &nregsets,
							       nullptr);
  if (status != ZE_RESULT_SUCCESS)
    return;

  ze_regset_properties_v_t regsets (nregsets);
  status = zetDebugGetThreadRegisterSetProperties (device->session, tid,
						   &nregsets, regsets.data ());
  if (status != ZE_RESULT_SUCCESS)
    error (_("Failed to obtain register sets (%x)."), status);

  /* Select and store a matching tdesc based on the regsets in register
     properties.  */
  ze_store_tdesc (tp, select_thread_tdesc (tp, regsets));
#endif /* HAVE_ZET_PFNDEBUGGETTHREADREGISTERSETPROPERTIES_T  */
}

/* We support sending only threads that have target description related changes
   since our HW has a fixed thread set where no threads get added or destroyed
   after setup.  We will be asked for every thread, so maybe compiling a changed
   thread set and returning this in this target op would eventually be better.
 */

bool
ze_target::thread_changed (thread_info *thread)
{
  gdb_assert (has_delta_thread_list ());

  auto zetp = ze_thread (thread);
  gdb_assert (zetp != nullptr);

  return zetp->thread_changed;
}

void
ze_target::set_thread_changed (thread_info *thread, bool state)
{
  auto zetp = ze_thread (thread);
  gdb_assert (zetp != nullptr);

  zetp->thread_changed = state;
}

/* ZE TDESC CACHE implementation.  */

/* Add a new cache entry for a given regset.  We cache tdesc, regset.  */

void
ze_tdesc_cache::add (const ze_regset_properties_v_t &regsets,
		     target_desc_up tdesc, ze_regset_info_up regset_info)
{
  /* We have already seen this regset layout.  */
  if (find (regsets) != nullptr)
    {
      gdb_assert (find (regsets)->tdesc == tdesc);
      return;
    }

  tdescs_m[regsets]
    = { std::move (tdesc), std::move (regset_info) };
}

/* Find a target description given a regset vector.  This also tells us
   that we need to create a new tdesc if none is found and returns the
   tdesc cache entry for the regsets.  */

const ze_tdesc *
ze_tdesc_cache::find (const ze_regset_properties_v_t &regsets) const
{
  ze_tdesc_map::const_iterator it = tdescs_m.find (regsets);
  return (it != tdescs_m.end ()) ? &it->second : nullptr;
}

/* Return any tdesc from the cache.  This will assert if none is found and
   that means there's a generic error.  */

const ze_tdesc *
ze_tdesc_cache::any () const
{
  gdb_assert (!tdescs_m.empty ());
  return &tdescs_m.begin ()->second;
}
