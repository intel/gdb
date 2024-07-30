/* Self tests for ELF NT_FILE builder/iterator.

   Copyright (C) 2021-2024 Free Software Foundation, Inc.

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
#include "gdbsupport/selftest.h"
#include "selftest-arch.h"
#include "elfnote-file.h"
#include "arch-utils.h"

namespace selftests {

static void
test_write_into_read ()
{
  /* Create some long type.  */
  struct gdbarch_info info;
  gdbarch_info_fill (&info);
  info.bfd_arch_info = bfd_scan_arch ("linux");
  struct gdbarch *arch = gdbarch_find_by_info (info);
  SELF_CHECK (arch != NULL);
  type_allocator alloc (arch);
  struct type *long_type
    = init_integer_type (alloc, gdbarch_long_bit (arch), 0, "long");

  /* Generate mappings.  */
  file_mappings_builder builder (long_type);
  builder.add ({10, 20, 30, "first"})
	 .add ({100, 200, 300, "second"})
	 .add ({30, 20, 10, "third"});
  auto mappings = builder.build ();
  SELF_CHECK (mappings.size () > 0);

  /* Iterate and validate mappings.  */
  bool pre_cb_called = false;
  int cb_called_count = 0;
  iterate_file_mappings (&mappings, long_type,
			 [&] (int count)
			   {
			     pre_cb_called = true;
			     SELF_CHECK (count == 3);
			   },
			 [&] (int i, const file_mapping& item)
			   {
			     ++cb_called_count;
			     switch (i)
			       {
				case 0:
				  SELF_CHECK (item.vaddr == 10);
				  SELF_CHECK (item.size == 20);
				  SELF_CHECK (item.offset == 30);
				  SELF_CHECK (strcmp (item.filename,
						      "first") == 0);
				  break;
				case 1:
				  SELF_CHECK (item.vaddr == 100);
				  SELF_CHECK (item.size == 200);
				  SELF_CHECK (item.offset == 300);
				  SELF_CHECK (strcmp (item.filename,
						      "second") == 0);
				  break;
				case 2:
				  SELF_CHECK (item.vaddr == 30);
				  SELF_CHECK (item.size == 20);
				  SELF_CHECK (item.offset == 10);
				  SELF_CHECK (strcmp (item.filename,
						      "third") == 0);
				  break;
				default:
				  SELF_CHECK (false);
			     }
			 });

  SELF_CHECK (pre_cb_called);
  SELF_CHECK (cb_called_count == 3);
}

}

void _initialize_elfnote_file_selftests ();
void
_initialize_elfnote_file_selftests ()
{
  selftests::register_test
    ("elfnote-file", selftests::test_write_into_read);
}
