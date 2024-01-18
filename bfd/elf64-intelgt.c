/* Intel(R) Graphics Technology-specific support for ELF
   Copyright (C) 2022-2024 Free Software Foundation, Inc.

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
#include "elf/intelgt.h"


static bool
elf64_intelgt_elf_object_p (bfd *abfd)
{
  return bfd_default_set_arch_mach (abfd, bfd_arch_intelgt, bfd_mach_intelgt);
}

static char *
intelgt_elf_write_core_note (bfd *a __attribute__((unused)),
			     char *b __attribute__((unused)),
			     int *c __attribute__((unused)),
			     int d __attribute__((unused)), ...)
{
  bfd_assert ("Use elfcore_write_note directly instead.", 0);
  return NULL;
}

static bool
intelgt_elf_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  /* Do not overwrite the core signal if it
     has already been set by another thread.  */
  if (elf_tdata (abfd)->core->signal == 0)
    elf_tdata (abfd)->core->signal = bfd_get_32 (abfd, note->descdata + 8);
  elf_tdata (abfd)->core->lwpid = bfd_get_64 (abfd, note->descdata);

  return _bfd_elfcore_make_pseudosection (
      abfd, ".reg", note->descsz - 16,
      note->descpos + 16);
}

static bool
intelgt_elf_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  elf_tdata (abfd)->core->command = _bfd_elfcore_strndup (
      abfd, note->descdata, strlen (note->descdata));

  return _bfd_elfcore_make_pseudosection (
      abfd, ".note.intelgt", note->descsz,
      note->descpos);
 }

#define TARGET_LITTLE_SYM		    intelgt_elf64_vec
#define TARGET_LITTLE_NAME		    "elf64-intelgt"
#define ELF_ARCH			    bfd_arch_intelgt
#define ELF_MACHINE_CODE		    EM_INTELGT
#define ELF_OSABI			    0
#define ELF_MAXPAGESIZE			    0x40000000

#define elf_backend_object_p		    elf64_intelgt_elf_object_p

#define elf_backend_write_core_note	    intelgt_elf_write_core_note
#define elf_backend_grok_prstatus	    intelgt_elf_grok_prstatus
#define elf_backend_grok_psinfo		    intelgt_elf_grok_psinfo

#define bfd_elf64_bfd_reloc_type_lookup     bfd_default_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup     _bfd_norelocs_bfd_reloc_name_lookup

#include "elf64-target.h"
