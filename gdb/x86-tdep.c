/* Target-dependent code for X86-based targets.

   Copyright (C) 2018-2022 Free Software Foundation, Inc.

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

#include "defs.h"
#include "i386-tdep.h"
#include "x86-tdep.h"
#include "symtab.h"

const char *x86_cet_names[X86_NUM_CET_REGS] = { "cet_u", "pl3_ssp" };

/* See x86-tdep.h.  */

bool
x86_is_cet_regnum (struct gdbarch *gdbarch, const int regnum)
{
  if (gdbarch == nullptr)
    return false;

  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  gdb_assert (tdep != nullptr);

  if (tdep->cet_regnum < 0)
    return false;

  return (regnum >= tdep->cet_regnum
	  && regnum < (tdep->cet_regnum + tdep->num_cet_regs));
}

/* See x86-tdep.h.  */

void
x86_supply_cet (regcache *regcache, const uint64_t buf[X86_NUM_CET_REGS])
{
  if (buf == nullptr)
    return;

  const struct gdbarch_tdep *tdep = gdbarch_tdep (regcache->arch ());
  gdb_assert (tdep != nullptr);

  if (tdep->cet_regnum < 0)
    return;

  for (int i = 0; i < X86_NUM_CET_REGS; ++i)
    regcache->raw_supply (tdep->cet_regnum + i, &buf[i]);
}

/* See x86-tdep.h.  */

void
x86_collect_cet (const regcache *regcache,
		 uint64_t buf[X86_NUM_CET_REGS])
{
  if (buf == nullptr)
    return;

  const struct gdbarch_tdep *tdep = gdbarch_tdep (regcache->arch ());
  gdb_assert (tdep != nullptr);

  if (tdep->cet_regnum < 0)
    return;

  for (int i = 0; i < X86_NUM_CET_REGS; ++i)
    regcache->raw_collect (tdep->cet_regnum + i, &buf[i]);
}

/* Check whether NAME is included in NAMES[LO] (inclusive) to NAMES[HI]
   (exclusive).  */

static bool
x86_is_thunk_register_name (const char *name, const char * const *names,
			    int lo, int hi)
{
  int reg;
  for (reg = lo; reg < hi; ++reg)
    if (strcmp (name, names[reg]) == 0)
      return true;

  return false;
}

/* See x86-tdep.h.  */

bool
x86_in_indirect_branch_thunk (CORE_ADDR pc, const char * const *register_names,
			      int lo, int hi)
{
  struct bound_minimal_symbol bmfun = lookup_minimal_symbol_by_pc (pc);
  if (bmfun.minsym == nullptr)
    return false;

  const char *name = bmfun.minsym->linkage_name ();
  if (name == nullptr)
    return false;

  /* Check the indirect return thunk first.  */
  if (strcmp (name, "__x86_return_thunk") == 0)
    return true;

  /* Then check a family of indirect call/jump thunks.  */
  static const char thunk[] = "__x86_indirect_thunk";
  static const size_t length = sizeof (thunk) - 1;
  if (strncmp (name, thunk, length) != 0)
    return false;

  /* If that's the complete name, we're in the memory thunk.  */
  name += length;
  if (*name == '\0')
    return true;

  /* Check for suffixes.  */
  if (*name++ != '_')
    return false;

  if (x86_is_thunk_register_name (name, register_names, lo, hi))
    return true;

  return false;
}
