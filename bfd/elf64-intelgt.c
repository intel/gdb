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

#define MINUS_ONE (~ (bfd_vma) 0)

#define INTELGT_ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static bool
elf64_intelgt_elf_object_p (bfd *abfd)
{
  return bfd_default_set_arch_mach (abfd, bfd_arch_intelgt, bfd_mach_intelgt);
}

/* Map BFD relocs to the IntelGT relocs.  */
struct elf_reloc_map
{
  bfd_reloc_code_real_type bfd_reloc_val;
  unsigned char elf_reloc_val;
};

static const struct elf_reloc_map elf64_intelgt_reloc_map[] =
{
  { BFD_RELOC_64, R_ZE_SYM_ADDR },
  { BFD_RELOC_32, R_ZE_SYM_ADDR_32 },
  { BFD_RELOC_ZE_SYM_ADDR32_HI, R_ZE_SYM_ADDR32_HI },
  { BFD_RELOC_ZE_PER_THREAD_PAYLOAD_OFFSET_32,
    R_PER_THREAD_PAYLOAD_OFFSET_32 },
};

static reloc_howto_type elf64_intelgt_howto_table[] =
{
  HOWTO (R_ZE_NONE,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_ZE_NONE",		/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false),		/* pcrel_offset */
  HOWTO (R_ZE_SYM_ADDR,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned,	/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_ZE_SYM_ADDR",	/* name */
	 false,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 false),		/* pcrel_offset */
  HOWTO (R_ZE_SYM_ADDR_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned,	/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_ZE_SYM_ADDR_32",	/* name */
	 false,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 false),		/* pcrel_offset */
  HOWTO (R_ZE_SYM_ADDR32_HI,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned,	/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_ZE_SYM_ADDR32_HI",	/* name */
	 false,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 false),		/* pcrel_offset */
  HOWTO (R_PER_THREAD_PAYLOAD_OFFSET_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned,	/* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_PER_THREAD_PAYLOAD_OFFSET_32",	/* name */
	 false,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 false),		/* pcrel_offset */
};

/* Given a BFD reloc type, return a HOWTO structure.  */
static reloc_howto_type *
elf64_intelgt_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 bfd_reloc_code_real_type code)
{
  unsigned int i;

  for (i = 0; i < INTELGT_ARRAY_SIZE (elf64_intelgt_reloc_map); i++)
  {
    struct elf_reloc_map reloc_map  = elf64_intelgt_reloc_map[i];

    if (reloc_map.bfd_reloc_val == code)
      return &elf64_intelgt_howto_table[reloc_map.elf_reloc_val];
  }

  return NULL;
}

/* Given relocation NAME, find its HOWTO structure.  */
static reloc_howto_type *
elf64_intelgt_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 const char *r_name)
{
  unsigned int i;

  for (i = 0; i < INTELGT_ARRAY_SIZE (elf64_intelgt_howto_table); i++)
    if (elf64_intelgt_howto_table[i].name != NULL
	&& strcasecmp (elf64_intelgt_howto_table[i].name, r_name) == 0)
      return &elf64_intelgt_howto_table[i];

  return NULL;
}

/* Sets HOWTO of the BFD_RELOC to the entry of howto table based
   on the type of ELF_RELOC.  */
static bool
elf64_info_to_howto (bfd *abfd, arelent *bfd_reloc,
		     Elf_Internal_Rela *elf_reloc)
{
  unsigned int r_type = ELF32_R_TYPE (elf_reloc->r_info);
  bfd_reloc->howto = &elf64_intelgt_howto_table[r_type];

  if (bfd_reloc->howto == NULL)
    {
      /* xgettext:c-format */
      _bfd_error_handler (_("%pB: unsupported relocation type %#x"),
			  abfd, r_type);
      return false;
    }
  return true;
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
  elf_tdata (abfd)->core->lwpid = bfd_get_64 (abfd, note->descdata);
  elf_tdata (abfd)->core->signal = bfd_get_32 (abfd, note->descdata + 8);

  return _bfd_elfcore_make_pseudosection (
      abfd, ".reg", note->descsz - 16,
      note->descpos + 16);
}

static bool
intelgt_elf_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  elf_tdata (abfd)->core->command = _bfd_elfcore_strndup (
      abfd, note->descdata + 8, strlen (note->descdata + 8));

  return _bfd_elfcore_make_pseudosection (
      abfd, ".note.intelgt", note->descsz,
      note->descpos);
 }

#define ELF_MAXPAGESIZE		    0x40000000

#define TARGET_LITTLE_SYM		    intelgt_elf64_vec
#define TARGET_LITTLE_NAME		    "elf64-intelgt"
#define ELF_ARCH			    bfd_arch_intelgt
#define ELF_MACHINE_CODE		    EM_INTELGT

#define ELF_OSABI			    0

#define elf64_bed			    elf64_intelgt_bed

#define elf_backend_object_p		    elf64_intelgt_elf_object_p

#define elf_backend_want_plt_sym	    0

#define elf_backend_write_core_note	    intelgt_elf_write_core_note
#define elf_backend_grok_prstatus	    intelgt_elf_grok_prstatus
#define elf_backend_grok_psinfo		    intelgt_elf_grok_psinfo

#define bfd_elf64_bfd_reloc_type_lookup     elf64_intelgt_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup     elf64_intelgt_reloc_name_lookup
#define elf_info_to_howto		    elf64_info_to_howto

#include "elf64-target.h"
