/* Control-flow Enforcement Technology support for GDB, the GNU debugger.

   Copyright (C) 2018-2022 Free Software Foundation, Inc.

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

#ifndef X86_CET_H
#define X86_CET_H

#include "memrange.h"

/* Check if the shadow stack is enabled and read CET specific registers.  */
extern bool shstk_is_enabled (CORE_ADDR *ssp, uint64_t *cet_msr);

/* Get the CET specific registers.  Returns true on success.  */
extern bool cet_get_registers (const ptid_t tid, CORE_ADDR *ssp,
			       uint64_t *cet_msr);

/* Set the CET specific registers.  Returns true on success.  */
extern bool cet_set_registers (const ptid_t tid, const CORE_ADDR *ssp,
			       const uint64_t *cet_msr);

/* Retrieve the mapped memory regions[ADDR_LOW, ADDR_HIGH) for a given
   address ADDR in memory space of process TID by reading the process
   information from its pseudo-file systema.  Return true if addr is in that
   range.  */
extern bool cet_get_shstk_mem_range (const CORE_ADDR addr, mem_range *range);

#endif /* X86_CET_H */
