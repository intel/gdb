/* Copyright (C) 2002-2026 Free Software Foundation, Inc.

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

#include "dll.h"

#include <algorithm>

/* An "unspecified" CORE_ADDR, for match_dll.  */
#define UNSPECIFIED_CORE_ADDR (~(CORE_ADDR) 0)

/* Record a newly loaded DLL at BASE_ADDR for the current process.  */

void
loaded_dll (const char *name, CORE_ADDR base_addr)
{
  loaded_dll (current_process (), name, base_addr);
}

/* Record a newly loaded DLL at BASE_ADDR for PROC.  */

void
loaded_dll (process_info *proc, const char *name, CORE_ADDR base_addr)
{
  gdb_assert (proc != nullptr);
  proc->all_dlls.emplace_back (name != nullptr ? name : "", base_addr);
  proc->dlls_changed = true;
}

/* Record a newly loaded in-memory DLL at BASE_ADDR for PROC.  */

void
loaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
	    CORE_ADDR base_addr)
{
  gdb_assert (proc != nullptr);

  /* We do not support overlapping in-memory libraries.  */
  std::list<dll_info> &dlls = proc->all_dlls;
  std::list<dll_info>::iterator it
    = std::find_if (dlls.begin (), dlls.end (),
		    [begin, end] (const dll_info &dll)
	{
	  /* DLL precedes the new library; note that end is exclusive.  */
	  if (dll.end <= begin)
	    return false;
	  /* DLL succeeds the new library; note that end is exclusive.  */
	  if (end <= dll.begin)
	    return false;
	  /* DLL overlaps with the new library.  */
	  return true;
	});

  if (it != dlls.end ())
    error (_("In-memory library [%s;%s) overlaps with [%s;%s)."),
	   paddress (begin), paddress (end), paddress (it->begin),
	   paddress (it->end));

  proc->all_dlls.emplace_back (begin, end, base_addr);
  proc->dlls_changed = true;
}

/* Record that the DLL with NAME and BASE_ADDR has been unloaded
   from the current process.  */

void
unloaded_dll (const char *name, CORE_ADDR base_addr)
{
  unloaded_dll (current_process (), name, base_addr);
}

static void
unload_dll_if (process_info *proc,
	       std::function<bool (const dll_info &)> pred)
{
  auto iter = std::find_if (proc->all_dlls.begin (), proc->all_dlls.end (),
			    pred);

  if (iter == proc->all_dlls.end ())
    /* For some inferiors we might get unloaded_dll events without having
       a corresponding loaded_dll.  In that case, the dll cannot be found
       in ALL_DLL, and there is nothing further for us to do.

       This has been observed when running 32bit executables on Windows64
       (i.e. through WOW64, the interface between the 32bits and 64bits
       worlds).  In that case, the inferior always does some strange
       unloading of unnamed dll.  */
    return;
  else
    {
      /* DLL has been found so remove the entry and free associated
	 resources.  */
      proc->all_dlls.erase (iter);
      proc->dlls_changed = true;
    }
}

/* Record that the DLL with NAME and BASE_ADDR has been unloaded
   from PROC.  */

void
unloaded_dll (process_info *proc, const char *name, CORE_ADDR base_addr)
{
  unload_dll_if (proc, [&] (const dll_info &dll)
    {
      if (dll.location != dll_info::on_disk)
	return false;

      if (base_addr != UNSPECIFIED_CORE_ADDR
	  && base_addr == dll.base_addr)
	return true;

      if (name != NULL && dll.name == name)
	return true;

      return false;
    });
}

/* Record that the in-memory DLL from BEGIN to END loaded at BASE_ADDR has been
   unloaded.  */

void
unloaded_dll (process_info *proc, CORE_ADDR begin, CORE_ADDR end,
	      CORE_ADDR base_addr)
{
  unload_dll_if (proc, [&] (const dll_info &dll)
    {
      if (dll.location != dll_info::in_memory)
	return false;

      if (base_addr != UNSPECIFIED_CORE_ADDR && base_addr == dll.base_addr)
	return true;

      /* We do not require the end address to be specified - we don't
	 support partially unloaded libraries, anyway.  */
      if ((begin == dll.begin)
	  && (end == UNSPECIFIED_CORE_ADDR || end == dll.end))
	return true;

      return false;
    });
}

/* See dll.h.  */

void
notify_dlls (process_info *proc)
{
  if (proc == nullptr)
    return;

  for (dll_info &dll : proc->all_dlls)
    dll.hidden = false;
}

/* Acknowledge DLL in PROC.  */

static void
ack_dll (process_info *proc, const dll_info &dll)
{
  switch (dll.location)
    {
    case dll_info::on_disk:
      target_ack_library (proc, dll.name.c_str ());
      return;

    case dll_info::in_memory:
      target_ack_in_memory_library (proc, dll.begin, dll.end);
      return;
    }

  gdb_assert_not_reached ("unknown dll location: %x", dll.location);
}

/* See dll.h.  */

void
ack_dlls (process_info *proc)
{
  if (proc == nullptr)
    return;

  for (dll_info &dll : proc->all_dlls)
    {
      if (!dll.hidden)
	ack_dll (proc, dll);
    }
}
