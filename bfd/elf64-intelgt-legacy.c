/* Intel(R) Graphics Technology-specific support for ELF
   Copyright (C) 2019-2024 Free Software Foundation, Inc.
   Copyright (C) 2024 Intel Corporation

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
#include "elf-bfd.h"

#include "elf/common.h"

static bool
elf64_intelgt_legacy_elf_object_p (bfd *abfd)
{
  bfd_default_set_arch_mach (abfd, bfd_arch_intelgt,
			     bfd_mach_intelgt);
  return true;
}

#define ELF_MAXPAGESIZE			    0x200000

#define TARGET_LITTLE_SYM		    intelgt_legacy_elf64_vec
#define TARGET_LITTLE_NAME		    "elf64-intelgt_legacy"
#define ELF_ARCH			    bfd_arch_intelgt
#define ELF_MACHINE_CODE		    EM_INTEL_GEN

#define	ELF_OSABI			    0

#define elf64_bed			    elf64_intelgt_bed

#define elf_backend_object_p		    elf64_intelgt_legacy_elf_object_p

#define elf_backend_want_plt_sym	    0

#define bfd_elf64_bfd_reloc_type_lookup	    bfd_default_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup     _bfd_norelocs_bfd_reloc_name_lookup

#include "elf64-target.h"
