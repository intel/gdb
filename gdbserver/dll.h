/* Copyright (C) 1993-2026 Free Software Foundation, Inc.

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

#ifndef GDBSERVER_DLL_H
#define GDBSERVER_DLL_H

#include <list>

struct process_info;

struct dll_info
{
  enum location_t
  {
    on_disk,
    in_memory
  };

  dll_info (const std::string &name_, CORE_ADDR base_addr_)
    : location (on_disk), name (name_), base_addr (base_addr_)
  {}

  dll_info (CORE_ADDR begin_, CORE_ADDR end_, CORE_ADDR base_addr_)
    : location (in_memory), begin (begin_), end (end_), base_addr (base_addr_)
  {}

  /* Where the library bits are stored.  */
  location_t location;

  /* The name of a file on disk containing the library.

     This is only valid if LOCATION == ON_DISK.  */
  std::string name;

  /* The address range in memory containing the library.

     This is only valid if LOCATION == IN_MEMORY.  */
  CORE_ADDR begin;
  CORE_ADDR end;

  /* The base address at which the library is loaded.  */
  CORE_ADDR base_addr;

  /* Whether to tell GDB about this library.

     We use this when library notifications are used to track which
     libraries belong to the current library event, so when GDB
     acknowledges the event, we know which libraries GDB has acknowledged.

     New libraries that were added in the meantime need to wait for the
     next library event.

     For targets that do not use library notifications, this will be
     ignored and GDB will always get the full list of libraries.  This
     means that library annotations in stop replies cannot be mixed with
     library notifications.  */
  bool hidden = true;
};

extern void loaded_dll (const char *name, CORE_ADDR base_addr);
extern void loaded_dll (process_info *proc, const char *name,
			CORE_ADDR base_addr);
extern void loaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
			CORE_ADDR base_addr);
extern void unloaded_dll (const char *name, CORE_ADDR base_addr);
extern void unloaded_dll (process_info *proc, const char *name,
			  CORE_ADDR base_addr);
extern void unloaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
			  CORE_ADDR base_addr);

/* Clear the hidden flag for all libraries in PROC.  */
extern void notify_dlls (process_info *proc);

/* Acknowledge all non-hidden libraries.  */
extern void ack_dlls (process_info *proc);

#endif /* GDBSERVER_DLL_H */
