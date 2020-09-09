/* x86 XSAVE extended state functions.

   Copyright (C) 2020 Free Software Foundation, Inc.

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


#include "common-defs.h"
#include "x86-xstate.h"

x86_extended_feature
get_x86_extended_feature (unsigned int feature)
{
  switch (feature)
  {
    case X86_XSTATE_AVX_ID:
    case X86_XSTATE_BNDREGS_ID:
    case X86_XSTATE_BNDCFG_ID:
    case X86_XSTATE_K_ID:
    case X86_XSTATE_ZMM_H_ID:
    case X86_XSTATE_ZMM_ID:
    case X86_XSTATE_PKRU_ID:
	unsigned int eax = 0, ebx = 0, unused;
	__get_cpuid_count (0x0D, feature, &eax, &ebx, &unused, &unused);
	return {feature, eax, ebx};
  }
  gdb_assert_not_reached ("Unexpected feature");
}

unsigned int
get_x86_xstate_size (uint64_t xcr0)
{
  x86_extended_feature ef;
  if (HAS_PKRU (xcr0))
    ef = get_x86_extended_feature (X86_XSTATE_PKRU_ID);
  else if (HAS_AVX512 (xcr0))
    ef = get_x86_extended_feature (X86_XSTATE_ZMM_ID);
  else if (HAS_MPX (xcr0))
    ef = get_x86_extended_feature (X86_XSTATE_BNDCFG_ID);
  else if (HAS_AVX (xcr0))
    ef = get_x86_extended_feature (X86_XSTATE_AVX_ID);
  else
    return X86_XSTATE_SSE_SIZE;

  return ef.size + ef.offset;
}
