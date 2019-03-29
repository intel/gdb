/* Target-dependent code for X86-based targets.

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

#ifndef X86_TDEP_H
#define X86_TDEP_H

#define X86_NUM_CET_REGS 2

/* Fill CET user mode registers in REGCACHE with the appropriate
   values from buffer BUF.  In case indirect branch tracking is enabled only,
   only cet_msr is filled.  */

extern void x86_supply_cet (regcache *regcache,
			    const uint64_t buf[X86_NUM_CET_REGS]);

/* Fill CET user mode registers in buffer BUF with the values from REGCACHE.
   In case indirect branch tracking is enabled only, pl3_ssp in BUF is set
   to 0.  */

extern void x86_collect_cet (const regcache *regcache,
			     uint64_t buf[X86_NUM_CET_REGS]);

/* Checks whether PC lies in an indirect branch thunk using registers
   REGISTER_NAMES[LO] (inclusive) to REGISTER_NAMES[HI] (exclusive).  */

extern bool x86_in_indirect_branch_thunk (CORE_ADDR pc,
					  const char * const *register_names,
					  int lo, int hi);

#endif /* x86-tdep.h */
