/* Perform an inferior function call, for GDB, the GNU debugger.

   Copyright (C) 2003-2022 Free Software Foundation, Inc.

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

#ifndef INFCALL_H
#define INFCALL_H

#include "dummy-frame.h"
#include "gdbsupport/array-view.h"

struct value;
struct type;

/* All the meta data necessary to extract the call's return value.  */

struct call_return_meta_info
{
  /* The caller frame's architecture.  */
  struct gdbarch *gdbarch;

  /* The called function.  */
  value *function;

  /* The return value's type.  */
  type *value_type;

  /* Are we returning a value using a structure return or a normal
     value return?  */
  int struct_return_p;

  /* If using a structure return, this is the structure's address.  */
  CORE_ADDR struct_addr;
};


/* Determine a function's address and its return type from its value.
   If the function is a GNU ifunc, then return the address of the
   target function, and set *FUNCTION_TYPE to the target function's
   type, and *RETVAL_TYPE to the target function's return type.
   Calls error() if the function is not valid for calling.  */

extern CORE_ADDR find_function_addr (struct value *function, 
				     struct type **retval_type,
				     struct type **function_type = NULL);

/* Perform a function call in the inferior.

   ARGS is a vector of values of arguments.  FUNCTION is a value, the
   function to be called.  Returns a value representing what the
   function returned.  May fail to return, if a breakpoint or signal
   is hit during the execution of the function.

   DEFAULT_RETURN_TYPE is used as function return type if the return
   type is unknown.  This is used when calling functions with no debug
   info.

   ARGS is modified to contain coerced values.  */

extern struct value *call_function_by_hand (struct value *function,
					    type *default_return_type,
					    gdb::array_view<value *> args);

/* Similar to call_function_by_hand and additional call
   register_dummy_frame_dtor with DUMMY_DTOR and DUMMY_DTOR_DATA for the
   created inferior call dummy frame.  */

extern struct value *
  call_function_by_hand_dummy (struct value *function,
			       type *default_return_type,
			       gdb::array_view<value *> args,
			       dummy_frame_dtor_ftype *dummy_dtor,
			       void *dummy_dtor_data);

/* Throw an error indicating that the user tried to call a function
   that has unknown return type.  FUNC_NAME is the name of the
   function to be included in the error message; may be NULL, in which
   case the error message doesn't include a function name.  */

extern void error_call_unknown_return_type (const char *func_name);

/* Perform the standard coercions that are specified
   for arguments to be passed to C, Ada or Fortran functions.

   If PARAM_TYPE is non-NULL, it is the expected parameter type.
   IS_PROTOTYPED is non-zero if the function declaration is prototyped.  */

extern value *default_value_arg_coerce (gdbarch *gdbarch, value *arg,
					type *param_type, int is_prototyped);

/* Reserve space on the stack for a value of the given type.
   Return the address of the allocated space.
   Make certain that the value is correctly aligned.
   The SP argument is modified.  */

extern CORE_ADDR default_reserve_stack_space (gdbarch *gdbarch,
					      const type *values_type,
					      CORE_ADDR &sp);


/* Extract the called function's return value.  */

extern value *
default_get_inferior_call_return_value (gdbarch *gdbarch,
					call_return_meta_info *ri);

#endif
