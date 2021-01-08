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

#ifndef NAT_X86_LINUX_AMX_H
#define NAT_X86_LINUX_AMX_H

#include "gdbsupport/gdb_assert.h"

#include <utility>
#include <vector>

/* TILECFG register.
   0       palette
   1       start_row
   2-15    reserved, must be zero
   16-17   tile0.colsb Tile 0 bytes per row.
   18-19   tile1.colsb Tile 1 bytes per row.
   20-21   tile2.colsb Tile 2 bytes per row.
   ...     (sequence continues)
   30-31   tile7.colsb Tile 7 bytes per row.
   32-47   reserved, must be zero
   48      tile0.rows Tile 0 rows.
   49      tile1.rows Tile 1 rows.
   50      tile2.rows Tile 2 rows.
   ...     (sequence continues)
   55      tile7.rows Tile 7 rows.
   56-63   reserved, must be zero.  */

/* TILECFG class representing the AMX Tilecfg register.  */

class tilecfg_reg
{
public:
  static const uint8_t NUM_OF_TILES = 8;
  static const uint8_t COLUMN_MEMORY_OFFSET = 16;
  static const uint8_t ROW_MEMORY_OFFSET = 48;
  static const uint8_t MAX_BYTES_PER_TILE_ROW = 64;
  static const uint16_t MAX_BYTES_PER_TILE = 1024;

  uint8_t palette = 0;
  uint8_t start_row = 0;

  tilecfg_reg ()
      : columns_n_rows (
	  std::vector<std::pair<uint16_t, uint8_t>> (NUM_OF_TILES, { 0, 0 }))
  {
  }

  /* Construct it from raw tilecfg data.  */
  explicit tilecfg_reg (uint8_t *raw_tilecfg);

  ~tilecfg_reg () noexcept = default;
  tilecfg_reg (const tilecfg_reg &t) = default;
  tilecfg_reg (tilecfg_reg &&t) noexcept = default;

  tilecfg_reg &operator= (tilecfg_reg &&t) noexcept = default;

  /* Get Number of configured bytes per row for tile p.  */
  inline uint16_t
  bytes_per_row (uint8_t p) const
  {
    gdb_assert (columns_n_rows.size () > p);
    return columns_n_rows[p].first;
  }

  /* Get Number of configured rows for tile p.  */
  inline uint8_t
  rows (uint8_t p) const
  {
    gdb_assert (columns_n_rows.size () > p);
    return columns_n_rows[p].second;
  }

  inline uint8_t
  num_of_tiles () const
  {
    return columns_n_rows.size ();
  }

  bool
  operator== (const tilecfg_reg &t) const
  {
    return palette == t.palette && start_row == t.start_row
	   && columns_n_rows == t.columns_n_rows;
  }

  bool
  operator!= (const tilecfg_reg &t) const
  {
    return !(*this == t);
  }

private:
  std::vector<std::pair<uint16_t, uint8_t>> columns_n_rows;
};

#endif /* NAT_X86_LINUX_AMX_H */
