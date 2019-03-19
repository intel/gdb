/* Copyright (C) 2019-2020 Free Software Foundation, Inc.

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
    internal_error (_("bad bit offset: %d"), pos);

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
    internal_error (_("bad bit offset: %d"), pos);

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
    internal_error (_("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  const bool old = (byte & mask) != 0;
  inst[idx] &= ~mask;

  return old;
}

xe_version
get_xe_version (uint32_t device_id)
{
  switch (device_id)
    {
      case 0x4F80:
      case 0x4F81:
      case 0x4F82:
      case 0x4F83:
      case 0x4F84:
      case 0x4F85:
      case 0x4F86:
      case 0x4F87:
      case 0x4F88:
      case 0x5690:
      case 0x5691:
      case 0x5692:
      case 0x5693:
      case 0x5694:
      case 0x5695:
      case 0x5696:
      case 0x5697:
      case 0x5698:
      case 0x56A0:
      case 0x56A1:
      case 0x56A2:
      case 0x56A3:
      case 0x56A4:
      case 0x56A5:
      case 0x56A6:
      case 0x56A7:
      case 0x56A8:
      case 0x56A9:
      case 0x56B0:
      case 0x56B1:
      case 0x56B2:
      case 0x56B3:
      case 0x56BA:
      case 0x56BB:
      case 0x56BC:
      case 0x56BD:
      case 0x56C0:
      case 0x56C1:
      case 0x56C2:
      case 0x56CF:
      case 0x7D40:
      case 0x7D45:
      case 0x7D67:
      case 0x7D41:
      case 0x7D55:
      case 0x7DD5:
      case 0x7D51:
      case 0x7DD1:
	return XE_HPG;

      case 0x0201:
      case 0x0202:
      case 0x0203:
      case 0x0204:
      case 0x0205:
      case 0x0206:
      case 0x0207:
      case 0x0208:
      case 0x0209:
      case 0x020A:
      case 0x020B:
      case 0x020C:
      case 0x020D:
      case 0x020E:
      case 0x020F:
      case 0x0210:
	return XE_HP;

      case 0x0BD0:
      case 0x0BD4:
      case 0x0BD5:
      case 0x0BD6:
      case 0x0BD7:
      case 0x0BD8:
      case 0x0BD9:
      case 0x0BDA:
      case 0x0BDB:
      case 0x0B69:
      case 0x0B6E:
	return XE_HPC;

      case 0x6420:
      case 0x64A0:
      case 0x64B0:

      case 0xE202:
      case 0xE209:
      case 0xE20B:
      case 0xE20C:
      case 0xE20D:
      case 0xE210:
      case 0xE211:
      case 0xE212:
      case 0xE216:
      case 0xE220:
      case 0xE221:
      case 0xE222:
      case 0xE223:
	return XE2;

      case 0xB080:
      case 0xB081:
      case 0xB082:
      case 0xB083:
      case 0xB08F:
      case 0xB090:
      case 0xB0A0:
      case 0xB0B0:
	return XE3;

      default:
	return XE_INVALID;
    }
}

} /* namespace intelgt */
