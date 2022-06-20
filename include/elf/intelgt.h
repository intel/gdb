/* Copyright (C) 2022 Free Software Foundation, Inc.

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

/* This file holds definitions specific to the IntelGT ABI.  */

#ifndef __INTELGT_H_
#define __INTELGT_H_

#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_intelgt_reloc_type)
  RELOC_NUMBER (R_ZE_NONE,			     0)
  /* 64-bit address.  */
  RELOC_NUMBER (R_ZE_SYM_ADDR,			     1)
  /* 32-bit address or lower 32-bit of a 64-bit address.  */
  RELOC_NUMBER (R_ZE_SYM_ADDR_32,		     2)
  /* Higher 32bits of a 64-bit address.  */
  RELOC_NUMBER (R_ZE_SYM_ADDR32_HI,		     3)
  /* 32-bit field of payload offset of per-thread data.  */
  RELOC_NUMBER (R_PER_THREAD_PAYLOAD_OFFSET_32,      4)
END_RELOC_NUMBERS (R_ZE_max)

#endif /* __INTELGT_H_ */
