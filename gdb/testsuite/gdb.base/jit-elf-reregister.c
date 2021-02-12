/* This test program is part of GDB, the GNU debugger.

   Copyright 2019-2021 Free Software Foundation, Inc.

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

#include "jit-protocol.h"
#include "jit-elf-util.h"

/* Must be defined by .exp file when compiling to know
   what address to map the ELF binary to.  */
#ifndef LOAD_ADDRESS
#error "Must define LOAD_ADDRESS"
#endif

int
main (int argc, char *argv[])
{
  /* Used as backing storage for GDB to populate argv.  */
  char *fake_argv[2];
  
  size_t obj_size;
  void *load_addr = (void *) (size_t) LOAD_ADDRESS;
  void *addr = load_elf (argv[1], &obj_size, load_addr);
  int (*jit_function) ()
      = (int (*) ()) load_symbol (addr, "jit_function_0001");

  struct jit_code_entry *const entry
      = (struct jit_code_entry *) calloc (1, sizeof (*entry));

  entry->symfile_addr = (const char *) addr;
  entry->symfile_size = obj_size;
  entry->prev_entry = __jit_debug_descriptor.relevant_entry;
  __jit_debug_descriptor.relevant_entry = entry;
  __jit_debug_descriptor.first_entry = entry;
  __jit_debug_descriptor.action_flag = JIT_REGISTER;
  __jit_debug_register_code ();

  jit_function (); /* first-call */

  __jit_debug_descriptor.action_flag = JIT_UNREGISTER;
  __jit_debug_register_code ();

  addr = load_elf (argv[1], &obj_size, addr);
  jit_function = (int (*) ()) load_symbol (addr, "jit_function_0001");

  entry->symfile_addr = (const char *) addr;
  entry->symfile_size = obj_size;
  __jit_debug_descriptor.relevant_entry = entry;
  __jit_debug_descriptor.first_entry = entry;
  __jit_debug_descriptor.action_flag = JIT_REGISTER;
  __jit_debug_register_code ();

  jit_function ();
}
