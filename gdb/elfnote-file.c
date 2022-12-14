/* See elfutils.h

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

#include "elfnote-file.h"
#include "elf-bfd.h"
#include "value.h"

/* See elfnote-file.h.  */

file_mappings_builder::file_mappings_builder (type *long_type)
{
  this->file_count = 0;
  this->long_type = long_type;
  /* Reserve space for the count.  */
  obstack_blank (&this->data, long_type->length ());
  /* We always write the page size as 1 since we have no good way to
     determine the correct value.  */
  pack_long (this->buf, long_type, 1);
  obstack_grow (&this->data, this->buf, long_type->length ());
}

/* See elfnote-file.h.  */

file_mappings_builder &
file_mappings_builder::add (ULONGEST vaddr, ULONGEST size,
			    ULONGEST offset, const char *filename)
{
  ++this->file_count;
  int length = this->long_type->length ();

  pack_long (this->buf, this->long_type, vaddr);
  obstack_grow (&this->data, this->buf, length);
  pack_long (this->buf, this->long_type, vaddr + size);
  obstack_grow (&this->data, this->buf, length);
  pack_long (this->buf, this->long_type, offset);
  obstack_grow (&this->data, this->buf, length);

  obstack_grow_str0 (&this->filenames, filename);

  return *this;
}

/* See elfnote-file.h.  */

void *
file_mappings_builder::build (int *size)
{
  if (this->file_count == 0)
    {
      *size = 0;
      return nullptr;
    }

  /* Write the count to the obstack.  */
  pack_long ((gdb_byte *) obstack_base (&this->data), this->long_type,
	     this->file_count);

  /* Copy the filenames to the data obstack.  */
  int filesize = obstack_object_size (&this->filenames);
  obstack_grow (&this->data, obstack_base (&this->filenames), filesize);

  *size = obstack_object_size (&this->data);
  return obstack_base (&this->data);
}
