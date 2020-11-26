/* Target interface for level-zero based targets for gdbserver.
   See https://github.com/oneapi-src/level-zero.git.

   Copyright (C) 2020-2022 Free Software Foundation, Inc.

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

#ifndef USE_WIN32API
#  include <signal.h>
#  include <fcntl.h>
#endif


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
    return pid_of (device.process);

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
ze_thread_device (thread_info *thread)
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

/* Add a process for DEVICE.  */

static process_info *
ze_add_process (ze_device_info *device, ze_process_state state)
{
  gdb_assert (device != nullptr);

  process_info *process = add_process (device->ordinal, 1);
  process->priv = new process_info_private (device, state);
  process->tdesc = device->tdesc.get ();
  device->process = process;

  /* Enumerate threads on the device we attached to.

     We debug the entire device so we can enumerate all threads at once.  They
     will be idle some of the time and we won't be able to interact with them.
     When work gets submitted to the device, the thread dispatcher will
     distribute the work onto device threads.

     The alternative of only representing threads that are currently executing
     work would be too intrusive as we'd need to stop each thread on every
     dispatch.  */
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

	    add_thread (ptid, zetp);
	  }

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

  for_each_thread (pid_of (process), [] (thread_info *thread)
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

  internal_error (__FILE__, __LINE__, _("unknown process state"));
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

  internal_error (__FILE__, __LINE__, _("unknown process state"));
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

  ze_result_t status = zetDebugAttach (device->handle, &device->config,
				       &device->session);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      if (device->session == nullptr)
	error (_("Bad handle returned by zetDebugAttach on %s."),
	       device->properties.name);

      return ze_add_process (device, ze_process_hidden);

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
	     status);
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
	      << "), addr=" << std::hex << event.info.module.load;
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

  internal_error (__FILE__, __LINE__, _("bad resume kind: %d."), rkind);
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

/* Return whether TP has a pending event.  */

static bool
ze_has_waitstatus (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return false;

  return (zetp->waitstatus.kind != TARGET_WAITKIND_IGNORE);
}

/* Return whether TP has a pending priority event.  */

static bool
ze_has_priority_waitstatus (const thread_info *tp)
{
  const ze_thread_info *zetp = ze_thread (tp);
  if (zetp == nullptr)
    return false;

  switch (zetp->waitstatus.kind)
    {
    case TARGET_WAITKIND_IGNORE:
    case TARGET_WAITKIND_UNAVAILABLE:
      return false;

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
    return { TARGET_WAITKIND_IGNORE };

  target_waitstatus status = zetp->waitstatus;
  zetp->waitstatus = { TARGET_WAITKIND_IGNORE };

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

  for_each_thread (pid_of (process), [reason] (thread_info *tp)
    {
      ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      zetp->waitstatus = { TARGET_WAITKIND_EXITED };
      zetp->waitstatus.value.integer = (int) reason;
    });
}

/* Find the regset containing REGNO on DEVICE or throw if not found.  */

static ze_regset_info
ze_find_regset (const ze_device_info &device, long regno)
{
  for (const ze_regset_info &regset : device.regsets)
    {
      if (regno < regset.begin)
	continue;

      if (regset.end <= regno)
	continue;

      return regset;
    }

  error (_("No register %ld on %s."), regno, device.properties.name);
}

/* Fetch all registers for THREAD on DEVICE into REGCACHE.  */

static void
ze_fetch_all_registers (const ze_device_info &device,
			const ze_device_thread_t thread,
			regcache *regcache)
{
  for (const ze_regset_info &regset : device.regsets)
    {
      gdb_assert (regset.begin <= regset.end);
      long lnregs = regset.end - regset.begin;

      gdb_assert (lnregs < UINT32_MAX);
      uint32_t nregs = (uint32_t) lnregs;

      std::vector<uint8_t> buffer (regset.size * nregs);
      ze_result_t status
	= zetDebugReadRegisters (device.session, thread, regset.type, 0,
				 nregs, buffer.data ());
      switch (status)
	{
	case ZE_RESULT_SUCCESS:
	  {
	    size_t offset = 0;
	    long reg = regset.begin;
	    for (; reg < regset.end; reg += 1, offset += regset.size)
	      supply_register (regcache, reg, &buffer[offset]);
	  }
	  break;

	case ZE_RESULT_ERROR_NOT_AVAILABLE:
	  break;

	default:
	  warning (_("Error %x reading regset %" PRIu32 " for %s on %s."),
		   status, regset.type, ze_thread_id_str (thread).c_str (),
		   device.properties.name);

	  break;
	}
    }
}

/* Fetch register REGNO for THREAD on DEVICE into REGCACHE.  */

static void
ze_fetch_register (const ze_device_info &device,
		   const ze_device_thread_t thread,
		   regcache *regcache, long regno)
{
  ze_regset_info regset = ze_find_regset (device, regno);

  gdb_assert (regset.begin <= regno);
  long lrsno = regno - regset.begin;

  gdb_assert (lrsno <= UINT32_MAX);
  uint32_t rsno = (uint32_t) lrsno;

  std::vector<uint8_t> buffer (regset.size);
  ze_result_t status
    = zetDebugReadRegisters (device.session, thread, regset.type, rsno, 1,
			     buffer.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      supply_register (regcache, regno, buffer.data ());
      break;

    case ZE_RESULT_ERROR_NOT_AVAILABLE:
      break;

    default:
      warning (_("Error %x reading register %ld (regset %" PRIu32
		 ") for %s on %s."), status, regno, regset.type,
	       ze_thread_id_str (thread).c_str (), device.properties.name);
      break;
    }
}

/* Store all registers for THREAD on DEVICE from REGCACHE.  */

static void
ze_store_all_registers (const ze_device_info &device,
			const ze_device_thread_t thread,
			regcache *regcache)
{
  for (const ze_regset_info &regset : device.regsets)
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
	= zetDebugWriteRegisters (device.session, thread, regset.type, 0,
				  nregs, buffer.data ());
      switch (status)
	{
	case ZE_RESULT_SUCCESS:
	  break;

	default:
	  error (_("Error %x writing regset %" PRIu32 " for %s on %s."),
		 status, regset.type, ze_thread_id_str (thread).c_str (),
		 device.properties.name);
	}
    }
}

/* Store register REGNO for THREAD on DEVICE from REGCACHE.  */

static void
ze_store_register (const ze_device_info &device,
		   const ze_device_thread_t thread,
		   regcache *regcache, long regno)
{
  ze_regset_info regset = ze_find_regset (device, regno);

  if (!regset.is_writeable)
    error (_("Writing read-only register %ld (regset %" PRIu32
	     ") for %s on %s."), regno, regset.type,
	   ze_thread_id_str (thread).c_str (), device.properties.name);

  gdb_assert (regset.begin <= regno);
  long lrsno = regno - regset.begin;

  gdb_assert (lrsno <= UINT32_MAX);
  uint32_t rsno = (uint32_t) lrsno;

  std::vector<uint8_t> buffer (regset.size);
  supply_register (regcache, regno, buffer.data ());

  ze_result_t status
    = zetDebugWriteRegisters (device.session, thread, regset.type, rsno, 1,
			      buffer.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    default:
      error (_("Error %x writing register %ld (regset %" PRIu32
	       ") for %s on %s."), status,  regno, regset.type,
	     ze_thread_id_str (thread).c_str (),
	     device.properties.name);
    }
}

/* Discard TP's regcache.  */

static void
ze_discard_regcache (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ 0);
  gdb_assert (regcache != nullptr);

  regcache->registers_valid = 0;
}

/* Resume THREAD on DEVICE.  */

static void
ze_resume (const ze_device_info &device, ze_device_thread_t thread)
{
  ze_result_t status = zetDebugResume (device.session, thread);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      /* Update our thread state to reflect the target.  */
      for_each_thread (device, thread, [&] (thread_info *tp)
	{
	  ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  if (zetp->exec_state != ze_thread_state_stopped)
	    return;

	  zetp->exec_state = ze_thread_state_running;
	});
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

	  if (zetp->exec_state != ze_thread_state_stopped)
	    return;

	  zetp->exec_state = ze_thread_state_unavailable;
	  zetp->waitstatus = { TARGET_WAITKIND_UNAVAILABLE };
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
ze_interrupt (const ze_device_info &device, ze_device_thread_t thread)
{
  ze_result_t status = zetDebugInterrupt (device.session, thread);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
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
	  ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  if (zetp->exec_state == ze_thread_state_stopped)
	    return;

	  zetp->exec_state = ze_thread_state_unavailable;
	  zetp->waitstatus = { TARGET_WAITKIND_UNAVAILABLE };
	});
	break;

    default:
      error (_("Failed to interrupt %s on %s: %x."),
	     ze_thread_id_str (thread).c_str (), device.properties.name,
	     status);
    }
}

int
ze_target::attach_to_device (uint32_t pid, ze_device_handle_t device)
{
  ze_device_properties_t properties
    { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES, nullptr };
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

  std::vector<zet_debug_regset_properties_t> regsets (nregsets);
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

  target_desc *tdesc = create_tdesc (properties, regsets,
				     dinfo->regsets,
				     dinfo->expedite);
  dinfo->tdesc.reset (tdesc);

  unsigned long ordinal = this->ordinal + 1;
  if (ordinal == 0)
    internal_error (__FILE__, __LINE__, _("device ordinal overflow."));

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
      ze_driver_properties_t properties
	{ ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES, nullptr };
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

      dprintf ("received event: %s", ze_event_str (event).c_str ());

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
	    loaded_dll (process, event.info.module.moduleBegin,
			event.info.module.moduleEnd,
			event.info.module.load, need_ack);

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

	    if (zeproc->waitstatus.kind != TARGET_WAITKIND_IGNORE)
	      continue;

	    /* We use UNAVAILABLE rather than LOADED as the latter implies
	       that the target has stopped.  */
	    zeproc->waitstatus = { TARGET_WAITKIND_UNAVAILABLE };
	  }
	  continue;

	case ZET_DEBUG_EVENT_TYPE_MODULE_UNLOAD:
	  {
	    /* We would not remain attached without a process.  */
	    process_info *process = device.process;
	    gdb_assert (process != nullptr);

	    unloaded_dll (process, event.info.module.moduleBegin,
			  event.info.module.moduleEnd,
			  event.info.module.load);

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

	    /* A thread event turns a process visible.  */
	    ze_show_process (process);

	    for_each_thread (device, tid, [&] (thread_info *tp)
	      {
		ze_thread_info *zetp = ze_thread (tp);
		gdb_assert (zetp != nullptr);

		/* Ignore threads we know to be stopped.

		   We already analyzed the stop reason and probably
		   destroyed it in the process.  */
		if (zetp->exec_state == ze_thread_state_stopped)
		  return;

		/* Discard any registers we may have fetched while TP was
		   unavailable.  */
		ze_discard_regcache (tp);
		try
		  {
		    gdb_signal signal = GDB_SIGNAL_0;
		    target_stop_reason reason = get_stop_reason (tp, signal);

		    zetp->exec_state = ze_thread_state_stopped;
		    zetp->stop_reason = reason;
		    zetp->waitstatus = { TARGET_WAITKIND_STOPPED, signal };
		  }
		/* FIXME: exceptions

		   We'd really like to catch some 'thread_unavailable'
		   exception rather than assuming that any exception is
		   due to thread availability.  */
		catch (...)
		  {
		    zetp->exec_state = ze_thread_state_unavailable;
		    zetp->waitstatus = { TARGET_WAITKIND_UNAVAILABLE };
		  }
	      });
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
		ze_thread_info *zetp = ze_thread (tp);
		gdb_assert (zetp != nullptr);

		/* Ignore threads we know to be stopped.

		   They would not be considered in the response event for
		   an interrupt request.  */
		if (zetp->exec_state == ze_thread_state_stopped)
		  return;

		zetp->exec_state = ze_thread_state_unavailable;
		zetp->waitstatus = { TARGET_WAITKIND_UNAVAILABLE };
	      });
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
ze_target::attach (unsigned long pid)
{
  if (!devices.empty ())
    error (_("Already attached."));

  uint32_t hostpid = (uint32_t) pid;
  if ((unsigned long) hostpid != pid)
    error (_("Host process id would be truncated."));

  int ndevices = attach_to_devices (hostpid);
  if (ndevices == 0)
    error (_("No supported devices found."));

  /* Let's check if we were able to attach to at least one device.  */
  int nattached = 0;
  for (ze_device_info *device : devices)
    {
      gdb_assert (device != nullptr);
      if (device->session == nullptr)
	continue;

      /* GDB (and higher layers of gdbserver) expects threads stopped on
	 attach in all-stop mode.  */
      if (!non_stop)
	resume (*device, resume_stop);

      nattached += 1;
    }

  if (nattached == 0)
    error (_("Failed to attach to any device."));

  return 0;
}

int
ze_target::detach (process_info *proc)
{
  gdb_assert (proc != nullptr);

  process_info_private *priv = proc->priv;
  gdb_assert (priv != nullptr);

  ze_device_info *device = priv->device;
  if (device != nullptr)
    ze_detach (device);

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
ze_target::resume (const ze_device_info &device, enum resume_kind rkind)
{
  switch (rkind)
    {
    case resume_continue:
    case resume_step:
      {
	/* Check if any thread has new events to report.

	   Note that even if we stopped all, unavailable threads may still
	   report new events as we were not able to stop them.

	   We ignore those threads and the unavailable event they report.  */
	int pid = ze_device_pid (device);
	thread_info *eventing
	  = find_thread (pid, [] (thread_info *tp)
	      {
		return ze_has_priority_waitstatus (tp);
	      });

	/* If we have nothing to report, we can simply resume everything
	   with a single resume request for all-stop and non-stop.  */
	if (eventing == nullptr)
	  {
	    uint32_t nstopped = 0;
	    for_each_thread (pid, [this, rkind, &nstopped] (thread_info *tp)
	      {
		ze_set_resume_state (tp, rkind);

		enum ze_thread_exec_state_t state = ze_exec_state (tp);
		switch (state)
		  {
		  case ze_thread_state_stopped:
		    /* We have reported the previous stop.  */
		    gdb_assert (!ze_has_waitstatus (tp));

		    nstopped += 1;
		    switch (rkind)
		      {
		      case resume_continue:
		      case resume_step:
			prepare_thread_resume (tp, rkind);
			regcache_invalidate_thread (tp);
			return;

		      default:
			gdb_assert_not_reached (_("unexpected resume kind"));
		      }

		  case ze_thread_state_unavailable:
		    /* Clear any previous unavailable status.

		       The thread is still running for all we know.  */
		    (void) ze_move_waitstatus (tp);
		    return;

		  case ze_thread_state_running:
		    /* The thread doesn't have anything to report.  */
		    gdb_assert (!ze_has_waitstatus (tp));
		    return;

		  case ze_thread_state_unknown:
		    warning (_("thread %d.%ld has unknown execution state"),
			     tp->id.pid (), tp->id.lwp ());
		    return;
		  }

		internal_error (__FILE__, __LINE__,
				_("bad execution state: %d."), state);
	      });

	    /* There is nothing to resume if nothing is stopped.  */
	    if (nstopped == 0)
	      return;

	    ze_device_thread_t all = ze_thread_id_all ();
	    ze_resume (device, all);
	    return;
	  }

	/* In all-stop mode, we're done.  There already is an event to
	   report so there's no reason to resume the target.

	   We still need to set the resume state so we will actually
	   report the event.

	   In non-stop mode, we resume threads individually.  */
	for_each_thread (pid, [this, rkind] (thread_info *tp)
	  {
	    if (!non_stop)
	      ze_set_resume_state (tp, rkind);
	    else
	      resume (tp, rkind);
	  });
      }
      return;

    case resume_stop:
      {
	/* The stop case is comparably easy since the target will ignore
	   already stopped threads.  */
	int pid = ze_device_pid (device);
	for_each_thread (pid, [this, rkind] (thread_info *tp)
	  {
	    ze_set_resume_state (tp, rkind);
	  });

	ze_device_thread_t all = ze_thread_id_all ();
	ze_interrupt (device, all);
	return;
      }
    }

  internal_error (__FILE__, __LINE__, _("bad resume kind: %d."), rkind);
}

void
ze_target::resume (thread_info *tp, enum resume_kind rkind)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  /* When we get detached, we will remove the device but we will also mark
     each thread exited.  We shouldn't try to resume them.  */
  ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  ze_thread_exec_state_t state = zetp->exec_state;
  switch (state)
    {
    case ze_thread_state_stopped:
      /* We silently ignore threads that still need to report an event.  */
      if (ze_has_priority_waitstatus (tp))
	return;

      switch (rkind)
	{
	case resume_continue:
	  zetp->resume_state = ze_thread_resume_run;

	  prepare_thread_resume (tp, rkind);
	  regcache_invalidate_thread (tp);
	  ze_resume (*device, zetp->id);
	  return;

	case resume_step:
	  zetp->resume_state = ze_thread_resume_step;

	  prepare_thread_resume (tp, rkind);
	  regcache_invalidate_thread (tp);
	  ze_resume (*device, zetp->id);
	  return;

	case resume_stop:
	  zetp->resume_state = ze_thread_resume_stop;
	  /* We silently ignore already stopped threads.  */
	  return;
	}

      internal_error (__FILE__, __LINE__, _("bad resume kind: %d."), rkind);

    case ze_thread_state_running:
    case ze_thread_state_unavailable:
      switch (rkind)
	{
	case resume_continue:
	  zetp->resume_state = ze_thread_resume_run;
	  /* Silently ignore already running or unavailable threads.  */
	  return;

	case resume_step:
	  zetp->resume_state = ze_thread_resume_step;
	  /* Silently ignore already running or unavailable threads.  */
	  return;

	case resume_stop:
	  zetp->resume_state = ze_thread_resume_stop;
	  ze_interrupt (*device, zetp->id);
	  return;
	}

      internal_error (__FILE__, __LINE__, _("bad resume kind: %d."), rkind);

    case ze_thread_state_unknown:
      warning (_("thread %d.%ld has unknown execution "
		 "state"), tp->id.pid (), tp->id.lwp ());
      return;
    }

  internal_error (__FILE__, __LINE__, _("bad execution state: %d."), state);
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

  /* Let's hope that we're not getting any conflicting resume requests.

     If higher layers cannot guarantee that, we'd need to add a check, but
     this should really be the responsibility of higher layers, IMHO.  */
  for (size_t i = 0; i < n; ++i)
    {
      const thread_resume &rinfo = resume_info[i];
      if (rinfo.sig != 0)
	warning (_("ignoring signal on resume request for %d.%ld"),
		 rinfo.thread.pid (), rinfo.thread.lwp ());

      if ((rinfo.kind == resume_step)
	  && (rinfo.step_range_start != rinfo.step_range_end))
	warning (_("ignoring range step request for %d.%ld"),
		 rinfo.thread.pid (), rinfo.thread.lwp ());

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

	      resume (*device, rinfo.kind);
	    }
	}
      else
	{
	  thread_info *tp = find_thread_ptid (rptid);
	  resume (tp, rinfo.kind);
	}
    }
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

	      ptid_t devid = ptid_t (ze_device_pid (*device));
	      if (devid.matches (ptid))
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
	    if (!ptid_t (pid_of (proc)).matches (ptid))
	      return false;

	    process_info_private *zeproc = proc->priv;
	    gdb_assert (zeproc != nullptr);

	    return (zeproc->waitstatus.kind != TARGET_WAITKIND_IGNORE);
	  });

      /* If we found a process event, we're done.

	 We do not take any special care about fairness as we expect process
	 events to be rather rare.  */
      if (process != nullptr)
	{
	  process_info_private *zeproc = process->priv;
	  gdb_assert (zeproc != nullptr);

	  *status = zeproc->waitstatus;
	  zeproc->waitstatus = { TARGET_WAITKIND_IGNORE };

	  return ptid_t (pid_of (process));
	}

      /* We defer reporting THREAD_UNAVAILABLE events until there are no
	 other events to report on the target.

	 In all-stop mode, we will ignore unavailable threads when
	 resuming the target.  So, unless we explicitly try to interact
	 with them, unavailable threads should be transparent to an
	 all-stop target.

	 In non-stop mode, we give more time for unavailable threads to
	 become available and report an event.  */
      thread_info *thread
	= find_thread_in_random ([ptid, this] (thread_info *tp)
	  {
	    if (!tp->id.matches (ptid))
	      return false;

	    /* Only consider threads that were resumed.  */
	    ze_thread_resume_state_t state = ze_resume_state (tp);
	    if (state == ze_thread_resume_none)
	      return false;

	    return ze_has_priority_waitstatus (tp);
	  });

      if (thread == nullptr)
	thread = find_thread_in_random ([ptid, this] (thread_info *tp)
	  {
	    if (!tp->id.matches (ptid))
	      return false;

	    /* Only consider threads that were resumed.  */
	    ze_thread_resume_state_t state = ze_resume_state (tp);
	    if (state == ze_thread_resume_none)
	      return false;

	    return ze_has_waitstatus (tp);
	  });

      if (thread != nullptr)
	{
	  /* Stop all other threads.

	     Do this before moving THREAD's waitstatus in case it throws.  */
	  if (!non_stop)
	    pause_all (false);

	  *status = ze_move_waitstatus (thread);

	  /* FIXME: switch_to_thread

	     Why isn't the caller switching based on the returned ptid?  */
	  switch_to_thread (thread);
	  return ptid_of (thread);
	}

      std::this_thread::yield ();
    }
  while ((options & TARGET_WNOHANG) == 0);

  /* We only get here if we did not find any event to report.  */

  *status = { TARGET_WAITKIND_IGNORE };
  return null_ptid;
}

void
ze_target::fetch_registers (regcache *regcache, int regno)
{
  ze_device_thread_t tid = ze_thread_id (current_thread);
  ze_device_info *device = ze_thread_device (current_thread);
  gdb_assert (device != nullptr);

  if (regno == -1)
    ze_fetch_all_registers (*device, tid, regcache);
  else
    ze_fetch_register (*device, tid, regcache, regno);
}

void
ze_target::store_registers (regcache *regcache, int regno)
{
  ze_device_thread_t tid = ze_thread_id (current_thread);
  ze_device_info *device = ze_thread_device (current_thread);
  gdb_assert (device != nullptr);

  if (regno == -1)
    ze_store_all_registers (*device, tid, regcache);
  else
    ze_store_register (*device, tid, regcache, regno);
}

/* Determine the thread id and device context for accessing ADDR_SPACE
   from THREAD.  */
static std::pair<ze_device_thread_t, ze_device_info *>
ze_memory_access_context (thread_info *thread, unsigned int addr_space)
{
  /* With a stopped thread, we can access all address spaces, and we
     should be able to determine the device for that thread.  */
  enum ze_thread_exec_state_t state = ze_exec_state (thread);
  if (state == ze_thread_state_stopped)
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
ze_target::read_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len,
			unsigned int addr_space)
{
  zet_debug_memory_space_desc_t desc
    = { ZET_STRUCTURE_TYPE_DEBUG_MEMORY_SPACE_DESC };
  desc.type = (zet_debug_memory_space_type_t) addr_space;
  desc.address = (uint64_t) memaddr;

  std::pair<ze_device_thread_t, ze_device_info *> context
    = ze_memory_access_context (current_thread, addr_space);
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
ze_target::write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
			 int len, unsigned int addr_space)
{
  zet_debug_memory_space_desc_t desc
    = { ZET_STRUCTURE_TYPE_DEBUG_MEMORY_SPACE_DESC };
  desc.type = (zet_debug_memory_space_type_t) addr_space;
  desc.address = (uint64_t) memaddr;

  std::pair<ze_device_thread_t, ze_device_info *> context
    = ze_memory_access_context (current_thread, addr_space);
  ze_device_thread_t thread = context.first;
  ze_device_info *device = context.second;
  gdb_assert (device != nullptr);

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
  if (freeze)
    {
      if (frozen == UINT32_MAX)
	internal_error (__FILE__, __LINE__, _("freeze count overflow"));
      frozen += 1;
    }

  /* Nothing to stop if we were frozen already.  */
  if (frozen > 1)
    return;

  /* Unavailable threads may become available and hence respond to our
     interrupt request.  To ensure that we are actually waiting for an
     unavailable thread's response, set the state to unknown and have it
     changed by fetch_events.

     This allows us to distinguish an older unavailable state from the
     current state at the time of our interrupt request.  */
  for_each_thread ([] (thread_info *tp)
    {
      enum ze_thread_exec_state_t state = ze_exec_state (tp);
      if (state != ze_thread_state_unavailable)
	return;

      ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      zetp->exec_state = ze_thread_state_unknown;
    });

  /* We start by interrupting all threads on all devices.

     Threads that are already stopped will be ignored and, in case all
     threads were already stopped on a device, we'd handle the resulting
     error in ze_interrupt.  */
  ze_device_thread_t all = ze_thread_id_all ();
  for (ze_device_info *device : devices)
    {
      gdb_assert (device != nullptr);
      ze_interrupt (*device, all);
    }

  /* Fetch events until each thread is either stopped or unavailable.

     We could use a worklist to only poll threads that are not stopped,
     yet.  I'm not sure this speeds up waiting, though, and I wouldn't
     expect more than two iterations, anyway.  */
  thread_info *thread = nullptr;
  do {
    for (ze_device_info *device : devices)
      {
	gdb_assert (device != nullptr);
	fetch_events (*device);
      }

    thread = find_thread ([] (thread_info *tp)
      {
	enum ze_thread_exec_state_t state = ze_exec_state (tp);
	switch (state)
	  {
	  case ze_thread_state_stopped:
	  case ze_thread_state_unavailable:
	    return false;

	  case ze_thread_state_running:
	  case ze_thread_state_unknown:
	    return true;
	  }

	internal_error (__FILE__, __LINE__, _("bad execution state: %d."),
			state);
      });
  } while (thread != nullptr);
}

void
ze_target::unpause_all (bool unfreeze)
{
  if (unfreeze)
    {
      if (frozen == 0)
	internal_error (__FILE__, __LINE__, _("freeze count underflow"));
      frozen -= 1;
    }

  /* Nothing to resume if we're still frozen.  */
  if (frozen > 1)
    return;

  /* In non-stop mode, we resume all threads that have not reported an
     event (other than stopped because of an interrupt request) that we
     have not reported, yet.

     In all-stop mode, we first check whether any thread on any device
     reported such an event.  Only if there is nothing to report we will
     resume everything.

     Note that in the presence of unavailable threads, removing threads
     races with threads becoming available and reporting events.  */

  /* In both cases, we start by fetching latest events.  */
  for (ze_device_info *device : devices)
    {
      gdb_assert (device != nullptr);
      fetch_events (*device);
    }

  if (non_stop)
    {
      /* Let's resume threads by device and try to group them.  */
      for (ze_device_info *device : devices)
	{
	  gdb_assert (device != nullptr);

	  /* Ignore devices we're not modelling as processes.  */
	  process_info *process = device->process;
	  if (process == nullptr)
	    continue;

	  /* Check if we have any thread with an event to report.  */
	  thread_info *thread
	    = find_thread (pid_of (process), [] (thread_info *tp)
		{
		  return ze_has_priority_waitstatus (tp);
		});

	  /* If we don't, we can group all threads together.

	     Otherwise, we send individual resume requests.  Let's not try
	     to group them any further for now.  */
	  if (thread == nullptr)
	    {
	      ze_device_thread_t all = ze_thread_id_all ();
	      ze_resume (*device, all);
	    }
	  else
	    {
	      for_each_thread (pid_of (process), [device] (thread_info *tp)
		{
		  const ze_thread_info *zetp = ze_thread (tp);
		  gdb_assert (zetp != nullptr);

		  if (ze_has_priority_waitstatus (tp))
		    return;

		  ze_device_thread_t id = ze_thread_id (tp);
		  ze_resume (*device, id);
		});
	    }
	}
    }
  else
    {
      /* Check whether we have events we have not reported, yet.

	 We ignore THREAD_UNAVAILABLE events.  The reporting thread hasn't
	 really stopped.  */
      thread_info *thread
	= find_thread ([] (thread_info *tp)
	    {
	      return ze_has_priority_waitstatus (tp);
	    });

      /* If we have at least one thread event, keep the target stopped.

	 We will report the event in the next wait ().  */
      if (thread != nullptr)
	return;

      /* There are no unreported events.  Let's resume everything.  */
      ze_device_thread_t all = ze_thread_id_all ();
      for (ze_device_info *device : devices)
	{
	  gdb_assert (device != nullptr);
	  ze_resume (*device, all);
	}
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
