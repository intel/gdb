/* Control-flow Enforcement Technology support for GDB, the GNU debugger.

   Copyright (C) 2018 Free Software Foundation, Inc.

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

/* Check if the shadow stack is enabled.  */
extern bool shstk_is_enabled ();

/* Get the CET specific registers.  Returns true on success.  */
extern bool cet_get_registers (const ptid_t tid, CORE_ADDR *ssp,
			       uint64_t *cet_msr);

/* Set the CET specific registers.  Returns true on success.  */
extern bool cet_set_registers (const ptid_t tid, const CORE_ADDR *ssp,
			       const uint64_t *cet_msr);

#endif /* X86_CET_H */
