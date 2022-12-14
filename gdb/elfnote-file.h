/* Utilities for working with ELF binaries built on top of
   libbfd.  Main criteria for putting them here rather in libbfd
   is usage of C++ features and/or GDB own concepts.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef _ELFUTILS_H
#define _ELFUTILS_H

#include "defs.h"
#include "gdbtypes.h"
#include "obstack.h"

class file_mappings_builder
{
private:
  /* Buffer used for packing numbers.  */
  gdb_byte buf[sizeof (ULONGEST)];
  /* Number of files mapped.  */
  ULONGEST file_count;
  /* The filename obstack.  */
  auto_obstack filenames;
  /* The obstack for the main part of the data.  */
  auto_obstack data;
  /* The architecture's "long" type.  */
  type *long_type;

public:
  /* Constructor.  */
  file_mappings_builder (type *long_type);

  /* Adds a new mapping to a currently created note.  */
  file_mappings_builder& add (ULONGEST vaddr, ULONGEST size, ULONGEST offset,
			      const char *filename);

  /* Finalizes creation of the note data and releases the data buffer.  */
  void *build (int *size);
};
#endif
