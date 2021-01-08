/* AMX TILECFG register handling for  GNU/Linux x86 (i386 and x86-64).

   Copyright (C) 2021 Free Software Foundation, Inc.

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

#include "elf/common.h"
#include "gdbsupport/common-defs.h"
#include "gdbsupport/x86-xstate.h"
#include "nat/gdb_ptrace.h"
#include "nat/x86-linux-amx.h"
#include <sys/uio.h>
#include <utility>

tilecfg_reg::tilecfg_reg (int tid):tilecfg_reg ()
{
  if (tid == 0)
    return; /* Use default values.  */

  x86_extended_feature ef
    = get_x86_extended_feature (X86_XSTATE_XTILEDATA_ID);
  unsigned int xstate_size = ef.size + ef.offset;
  std::unique_ptr<uint8_t[]> xstateregs (new uint8_t[xstate_size]);
  struct iovec iov;

  iov.iov_base = xstateregs.get ();
  iov.iov_len = xstate_size;
  if (ptrace (PTRACE_GETREGSET, tid, (unsigned int) NT_X86_XSTATE,
      &iov) < 0)
    {
      perror_with_name (_("Couldn't read extended state status"));
      return; /* Use default values.  */
    }

  unsigned int tilecfg_offset
    = get_x86_extended_feature (X86_XSTATE_XTILECFG_ID).offset;

  /* Read TILECFG columns and rows values via pointers.
     Columns are represented by 2 bytes and rows are represented
     by 1 byte.  Column pointer which is *uint8_t needs to be converted
     to *uint16_t pointer.  */
  uint16_t *vec_col_pos = reinterpret_cast<uint16_t*>
    (tilecfg_offset + xstateregs.get () + COLUMN_MEMORY_OFFSET);
  uint8_t *vec_row_pos
    = tilecfg_offset + xstateregs.get () + ROW_MEMORY_OFFSET;

  for (int i = 0; i < NUM_OF_TILES; i++)
    columns_n_rows[i] = {vec_col_pos[i], vec_row_pos[i]};
}
