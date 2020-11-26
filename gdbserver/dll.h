/* Copyright (C) 1993-2022 Free Software Foundation, Inc.

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

  dll_info (const std::string &name_, CORE_ADDR base_addr_, bool need_ack_)
    : location (on_disk), name (name_), base_addr (base_addr_),
      need_ack (need_ack_)
  {}

  dll_info (CORE_ADDR begin_, CORE_ADDR end_, CORE_ADDR base_addr_,
	    bool need_ack_)
    : location (in_memory), begin (begin_), end (end_), base_addr (base_addr_),
      need_ack (need_ack_)
  {}

  location_t location;
  std::string name;
  CORE_ADDR begin;
  CORE_ADDR end;
  CORE_ADDR base_addr;
  bool need_ack;
};

extern void clear_dlls (void);
/* Throws NOT_SUPPORTED_ERROR if library acknowledgement is requested
  (NEED_ACK = TRUE) and not supported.  */
extern void loaded_dll (const char *name, CORE_ADDR base_addr,
			bool need_ack = false);
extern void loaded_dll (process_info *proc, const char *name,
			CORE_ADDR base_addr, bool need_ack = false);
extern void loaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
			CORE_ADDR base_addr, bool need_ack = false);
extern void unloaded_dll (const char *name, CORE_ADDR base_addr);
extern void unloaded_dll (process_info *proc, const char *name,
			  CORE_ADDR base_addr);
extern void unloaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
			  CORE_ADDR base_addr);
extern void ack_dll (const char *name);
extern void ack_dll (process_info *proc, const char *name);
extern void ack_dll (CORE_ADDR begin, CORE_ADDR end);
extern void ack_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end);

#endif /* GDBSERVER_DLL_H */
