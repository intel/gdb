/* Copyright (C) 2021 Free Software Foundation, Inc.

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
#include "nat/x86-linux-amx.h"
#include <string>

/* This function is NOT auto generated from xml.  Create the AMX feature
   based on the current state of the TILECFG register.  The register
   contains columns and rows information.  */

static int
create_feature_i386_64bit_amx (struct target_desc *result, long regnum,
  tilecfg_reg *amx_tilecfg)
{
  struct tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.i386.amx");
  tdesc_type *element_type;

  tdesc_create_reg (feature, "tilecfg", regnum++, 1, NULL, 512, "uint512");

  /* Some tiles can have no data.  Still, we want to show them in the
     output.  Default values with one row and one column are taken
     for this case.  Since each cell can have 4 bytes we assume the
     default value of columns to be 4.  */
  uint16_t columns = 64;
  uint8_t rows = 16;
  uint8_t numOfRegs = tilecfg_reg::NUM_OF_TILES;
  if (amx_tilecfg != nullptr)
    numOfRegs = amx_tilecfg->numOfTiles ();

  for (uint8_t i = 0; i < numOfRegs; i++)
    {
      if (amx_tilecfg != nullptr)
	{
	  columns = amx_tilecfg->getColumn (i);
	  rows = amx_tilecfg->getRow (i);
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

      std::string tileName = "tile" + std::to_string (i);
      tdesc_type_with_fields *type_with_fields = tdesc_create_union (feature,
	tileName.c_str ());

      tdesc_add_field (type_with_fields, "m_int8", matrix_i8_type);
      tdesc_add_field (type_with_fields, "m_int32", matrix_i32_type);
      tdesc_add_field (type_with_fields, "m_bf16", matrix_bf16_type);

      std::string tmmName = "tmm" + std::to_string (i);
      tdesc_create_reg (feature, tmmName.c_str (), regnum++, 1, NULL,
	rows * columns * numOfRegs, tileName.c_str ());
    }

  return regnum;
}
