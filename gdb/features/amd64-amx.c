/* Copyright (C) 2022 Free Software Foundation, Inc.

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

#include "gdbsupport/tdesc.h"
#include "gdbsupport/x86-amx.h"
#include <string>

/* This function is NOT auto generated from xml.  Create the AMX feature
   based on the current state of the TILECFG register.  The register
   contains columns and rows information.  */

static int
create_feature_i386_64bit_amx (target_desc *result, long regnum,
			       tilecfg_reg *amx_tilecfg)
{
  tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.i386.amx");
  tdesc_type *element_type;

  tdesc_create_reg (feature, "tilecfg", regnum++, 1, nullptr, 512, "uint512");

  /* Some tiles can have no data.  Still, we want to show them in the
     output.  Default values with one row and one column are taken
     for this case.  Since each cell can have 4 bytes we assume the
     default value of columns to be 4.  */
  uint16_t columns = 64;
  uint8_t rows = 16;
  uint8_t num_of_tiles = tilecfg_reg::NUM_OF_TILES;
  if (amx_tilecfg != nullptr)
    num_of_tiles = amx_tilecfg->num_of_tiles ();

  for (uint8_t i = 0; i < num_of_tiles; i++)
    {
      if (amx_tilecfg != nullptr)
	{
	  columns = amx_tilecfg->bytes_per_row (i);
	  rows = amx_tilecfg->rows (i);
	  if (columns == 0)
	    columns = 64;
	  if (rows == 0)
	    rows = 16;
	}

      element_type = tdesc_named_type (feature, "int8");
      element_type = tdesc_create_vector (feature, "column_i8",
	element_type, columns);
      tdesc_type *matrix_i8_type = tdesc_create_vector (feature,
	"matrix_i8", element_type, rows);

      element_type = tdesc_named_type (feature, "uint8");
      element_type = tdesc_create_vector (feature, "column_ui8",
	element_type, columns);
      tdesc_type *matrix_ui8_type = tdesc_create_vector (feature,
	"matrix_ui8", element_type, rows);

      element_type = tdesc_named_type (feature, "int32");
      element_type = tdesc_create_vector (feature, "column_i32",
	element_type, columns / 4);
      tdesc_type *matrix_i32_type = tdesc_create_vector (feature,
	"matrix_i32", element_type, rows);

      element_type = tdesc_named_type (feature, "bfloat16");
      element_type = tdesc_create_vector (feature, "column_bf16",
	element_type, columns / 2);
      tdesc_type *matrix_bf16_type = tdesc_create_vector (feature,
	"matrix_bf16", element_type, rows);

      element_type = tdesc_named_type (feature, "ieee_single");
      element_type = tdesc_create_vector (feature, "column_fp32",
	element_type, columns / 4);
      tdesc_type *matrix_fp32_type = tdesc_create_vector (feature,
	"matrix_fp32", element_type, rows);

      std::string tile_name = "tile" + std::to_string (i);
      tdesc_type_with_fields *type_with_fields = tdesc_create_union (feature,
	tile_name.c_str ());

      tdesc_add_field (type_with_fields, "m_int8", matrix_i8_type);
      tdesc_add_field (type_with_fields, "m_uint8", matrix_ui8_type);
      tdesc_add_field (type_with_fields, "m_int32", matrix_i32_type);
      tdesc_add_field (type_with_fields, "m_bf16", matrix_bf16_type);
      tdesc_add_field (type_with_fields, "m_fp32", matrix_fp32_type);

      std::string tmm_name = "tmm" + std::to_string (i);
      tdesc_create_reg (feature, tmm_name.c_str (), regnum++, 1, nullptr,
	rows * columns * num_of_tiles, tile_name.c_str ());
    }

  return regnum;
}
