/* Copyright (C) 2019-2021 Free Software Foundation, Inc.

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

#include "gdbsupport/common-defs.h"
#include "intelgt.h"
#include <stdlib.h>

namespace intelgt {

/* Get the bit at POS in INST.  */

bool
get_inst_bit (const gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  return (byte & mask) != 0;
}

/* Set the bit at POS in INST.  */

bool
set_inst_bit (gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  const bool old = (byte & mask) != 0;
  inst[idx] |= mask;

  return old;
}

/* Clear the bit at POS in INST.  */

bool
clear_inst_bit (gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  const bool old = (byte & mask) != 0;
  inst[idx] &= ~mask;

  return old;
}

} /* namespace intelgt */
