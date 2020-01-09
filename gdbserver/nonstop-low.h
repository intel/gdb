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
};

/* The target that defines abstract nonstop behavior without relying
   on any platform specifics (e.g.  ptrace).  */

class nonstop_process_target : public process_stratum_target
{
public:

  bool supports_non_stop () override;

  bool async (bool enable) override;

  int start_non_stop (bool enable) override;
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
