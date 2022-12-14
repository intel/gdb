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

static void
push_long (gdb::byte_vector *vec, type *long_type, ULONGEST value)
{
  gdb_byte buf[sizeof (ULONGEST)];
  pack_long (buf, long_type, value);
  vec->insert (vec->end (), &buf[0], &buf[long_type->length ()]);
}

/* See elfnote-file.h.  */

file_mappings_builder::file_mappings_builder (type *long_type)
{
  this->file_count = 0;
  this->long_type = long_type;
  /* Reserve space for the count.  */
  this->data.resize (long_type->length ());
  /* We always write the page size as 1 since we have no good way to
     determine the correct value.  */
  push_long (&this->data, this->long_type, 1);
}

/* See elfnote-file.h.  */

file_mappings_builder &
file_mappings_builder::add (const file_mapping &mapping)
{
  ++this->file_count;
  push_long (&this->data, this->long_type, mapping.vaddr);
  push_long (&this->data, this->long_type, mapping.vaddr + mapping.size);
  push_long (&this->data, this->long_type, mapping.offset);
  const char* p = mapping.filename;
  do
    {
      this->filenames.push_back (*p);
    }
  while (*p++ != '\0');

  return *this;
}

/* See elfnote-file.h.  */

gdb::byte_vector
file_mappings_builder::build ()
{
  if (this->file_count == 0)
    return gdb::byte_vector ();

  /* Write the count to the reserved space.  */
  pack_long (this->data.data (), this->long_type, this->file_count);

  /* Copy the filenames to the main buffer.  */
  this->data.insert (this->data.end (), this->filenames.begin (),
		     this->filenames.end ());

  return std::move (this->data);
}
