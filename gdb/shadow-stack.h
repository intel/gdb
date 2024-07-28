/* Definitions to manage a shadow stack pointer for GDB, the GNU debugger.

   Copyright (C) 2024 Free Software Foundation, Inc.

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

#ifndef SHADOW_STACK_H
#define SHADOW_STACK_H

/* If shadow stack is enabled, push the address NEW_ADDR on the shadow
   stack and update the shadow stack pointer accordingly.  */

void shadow_stack_push (gdbarch *gdbarch, const CORE_ADDR new_addr);

/* Unwind the previous shadow stack pointer of THIS_FRAME's shadow stack
   pointer.  REGNUM is the register number of the shadow stack pointer.
   Return a value that is unavailable in case we cannot unwind the
   previous shadow stack pointer.  Otherwise, return a value containing
   the previous shadow stack pointer.  */

value * dwarf2_prev_ssp (const frame_info_ptr &this_frame,
			 void **this_cache, int regnum);

#endif /* #ifndef SHADOW_STACK_H */
