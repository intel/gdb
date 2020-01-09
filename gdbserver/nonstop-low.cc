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
#include "gdbsupport/gdb-sigmask.h"
#include "gdbsupport/event-pipe.h"

#include <signal.h>

nonstop_thread_info *
get_thread_nti (thread_info *thread)
{
  return static_cast<nonstop_thread_info *> (thread_target_data (thread));
}

/* Async interaction stuff.

   The event pipe registered as a waitable file in the event loop.  */
static event_pipe the_event_pipe;

/* True if we are currently in async mode.  */

bool
target_is_async_p ()
{
  return the_event_pipe.is_open ();
}

/* Get rid of any pending event in the pipe.  */

void
async_file_flush ()
{
  the_event_pipe.flush ();
}

/* Put something in the pipe, so the event loop wakes up.  */

void
async_file_mark ()
{
  the_event_pipe.mark ();
}

bool
nonstop_process_target::supports_non_stop ()
{
  return true;
}

bool
nonstop_process_target::async (bool enable)
{
  bool previous = target_is_async_p ();

  threads_debug_printf ("async (%d), previous=%d",
			enable, previous);

  if (previous != enable)
    {
      sigset_t mask;
      sigemptyset (&mask);
      sigaddset (&mask, SIGCHLD);

      gdb_sigmask (SIG_BLOCK, &mask, NULL);

      if (enable)
	{
	  if (!the_event_pipe.open_pipe ())
	    {
	      gdb_sigmask (SIG_UNBLOCK, &mask, NULL);

	      warning ("creating event pipe failed.");
	      return previous;
	    }

	  /* Register the event loop handler.  */
	  add_file_handler (the_event_pipe.event_fd (),
			    handle_target_event, NULL,
			    "nonstop-low");

	  /* Always trigger a wait.  */
	  async_file_mark ();
	}
      else
	{
	  delete_file_handler (the_event_pipe.event_fd ());

	  the_event_pipe.close_pipe ();
	}

      gdb_sigmask (SIG_UNBLOCK, &mask, NULL);
    }

  return previous;
}

int
nonstop_process_target::start_non_stop (bool nonstop)
{
  /* Register or unregister from event-loop accordingly.  */
  async (nonstop);

  if (target_is_async_p () != (nonstop != false))
    return -1;

  return 0;
}
