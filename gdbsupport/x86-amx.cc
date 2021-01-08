/* AMX TILECFG register handling for  GNU/Linux x86 (i386 and x86-64).

   Copyright (C) 2022 Free Software Foundation, Inc.

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

#include "common-defs.h"
#include "x86-amx.h"

tilecfg_reg::tilecfg_reg (uint8_t *raw_tilecfg) : tilecfg_reg ()
{
  if (raw_tilecfg == nullptr)
    return; /* Use default values.  */

  palette = raw_tilecfg[0];
  start_row = raw_tilecfg[1];

  /* Read TILECFG columns and rows values via pointers.
     Columns are represented by 2 bytes and rows are represented
     by 1 byte.  Column pointer which is *uint8_t needs to be converted
     to *uint16_t pointer.  */
  uint16_t *vec_col_pos
      = reinterpret_cast<uint16_t *> (raw_tilecfg + COLUMN_MEMORY_OFFSET);
  uint8_t *vec_row_pos = raw_tilecfg + ROW_MEMORY_OFFSET;

  for (int i = 0; i < NUM_OF_TILES; i++)
    columns_n_rows[i] = { vec_col_pos[i], vec_row_pos[i] };
}
