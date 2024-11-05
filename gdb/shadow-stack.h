/* Definitions to manage a shadow stack pointer for GDB, the GNU debugger.

   Copyright (C) 2024 Free Software Foundation, Inc.
   Copyright (C) 2024 Intel Corporation

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

/* Data for the printing of shadow stack frame information, exposed as
   command options.  */

struct shadow_stack_print_options
{
  const char *print_frame_info = print_frame_info_auto;
};

/* Implementation of "backtrace shadow" comand.  */

void backtrace_shadow_command (const char *arg, int from_tty);

/* Create an option_def_group array grouping all the "backtrace shadow"
   options, with SSP_OPTS as contexts.  */

std::array<gdb::option::option_def_group, 1>
  make_backtrace_shadow_options_def_group
    (shadow_stack_print_options *print_options);

enum class ssp_unwind_stop_reason
{
  /* No particular reason; either we haven't tried unwinding yet, or we
     didn't fail.  */
  no_error = 0,

  /* We could not read the memory of the shadow stack element.  */
  memory_read_error
};

/* Information of a shadow stack frame belonging to a shadow stack element
   at shadow stack pointer SSP.  */

class shadow_stack_frame_info
{
public:
  /* If possible, unwind the previous shadow stack frame info.  RANGE is
     the shadow stack memory range [start_address, end_address) belonging
     to this frame's shadow stack pointer.  If we cannot unwind the
     previous frame info, set the unwind_stop_reason attribute.  If we
     reached the bottom of the shadow stack just don't return a value.  */
  std::optional<shadow_stack_frame_info> unwind_prev_shadow_stack_frame_info
    (gdbarch *gdbarch, std::pair<CORE_ADDR, CORE_ADDR> range);

  /* The shadow stack pointer.  */
  CORE_ADDR ssp;
  /* The value of the shadow stack at SSP.  */
  CORE_ADDR value;
  /* The level of the element on the shadow stack.  */
  unsigned long level;
  /* If unwinding of the previous frame info fails assign this value to a
     matching condition ssp_unwind_stop_reason
     > ssp_unwind_stop_reason::no_error.  */
  ssp_unwind_stop_reason unwind_stop_reason
    = ssp_unwind_stop_reason::no_error;
};

#endif /* #ifndef SHADOW_STACK_H */
