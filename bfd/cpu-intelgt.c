/* BFD support for the Intel(R) Graphics Technology architecture.
   Copyright 2019 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

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

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

static void *
bfd_arch_intelgt_fill (bfd_size_type count,
		       bool is_bigendian ATTRIBUTE_UNUSED,
		       bool code)
{
  void *fill = bfd_malloc (count);

  if (fill != NULL)
    {
      /* nop on gen is 0x7e.  */
      memset (fill, code ? 0x7e : 0, count);
    }

  return fill;
}

const bfd_arch_info_type bfd_intelgt_arch =
  {
    64, /* 64 bits in a word.  */
    64, /* 64 bits in an address.  */
    8,  /* 8 bits in a byte.  */
    bfd_arch_intelgt, /* Architecture.  */
    bfd_mach_intelgt, /* Machine number.  */
    "intelgt", /* Architecture name.  */
    "intelgt", /* Printable name.  */
    3, /* Section alignment power.  */
    true, /* Default machine for this architecture.  */
    bfd_default_compatible, /* Check for compatibility.  */
    bfd_default_scan, /* Check for an arch and mach hit.  */
    bfd_arch_intelgt_fill, /* Allocate and fill bfd.  */
    NULL, /* Pointer to next.  */
    0 /* Maximum offset of a reloc from the start of an insn.  */
  };
