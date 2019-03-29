/* Native-dependent code for GNU/Linux x86 (i386 and x86-64).

   Copyright (C) 1999-2024 Free Software Foundation, Inc.

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

#include "elf/common.h"
#include "gdbsupport/common-defs.h"
#include "nat/gdb_ptrace.h"
#include "nat/linux-ptrace.h"
#include "nat/x86-cpuid.h"
#include <sys/uio.h>
#include "x86-linux.h"
#include "x86-linux-dregs.h"

/* Per-thread arch-specific data we want to keep.  */

struct arch_lwp_info
{
  /* Non-zero if our copy differs from what's recorded in the
     thread.  */
  int debug_registers_changed;
};

/* See nat/x86-linux.h.  */

void
lwp_set_debug_registers_changed (struct lwp_info *lwp, int value)
{
  if (lwp_arch_private_info (lwp) == NULL)
    lwp_set_arch_private_info (lwp, XCNEW (struct arch_lwp_info));

  lwp_arch_private_info (lwp)->debug_registers_changed = value;
}

/* See nat/x86-linux.h.  */

int
lwp_debug_registers_changed (struct lwp_info *lwp)
{
  struct arch_lwp_info *info = lwp_arch_private_info (lwp);

  /* NULL means either that this is the main thread still going
     through the shell, or that no watchpoint has been set yet.
     The debug registers are unchanged in either case.  */
  if (info == NULL)
    return 0;

  return info->debug_registers_changed;
}

/* See nat/x86-linux.h.  */

void
x86_linux_new_thread (struct lwp_info *lwp)
{
  lwp_set_debug_registers_changed (lwp, 1);
}

/* See nat/x86-linux.h.  */

void
x86_linux_delete_thread (struct arch_lwp_info *arch_lwp)
{
  xfree (arch_lwp);
}

/* See nat/x86-linux.h.  */

void
x86_linux_prepare_to_resume (struct lwp_info *lwp)
{
  x86_linux_update_debug_registers (lwp);
}

/* See nat/x86-linux.h.  */

bool
x86_check_ssp_support (const int tid)
{
  unsigned int eax, ebx, ecx, edx;

  __get_cpuid_count (7, 0, &eax, &ebx, &ecx, &edx);

  if ((ecx & bit_SHSTK) == 0)
    return false;

  /* Further check for NT_X86_SHSTK kernel support.  */
  uint64_t ssp;
  iovec iov;
  iov.iov_base = &ssp;
  iov.iov_len = sizeof (ssp);

  int res = ptrace (PTRACE_GETREGSET, tid, NT_X86_SHSTK, &iov);
  if (res < 0 && errno == EINVAL)
    {
      /* The errno EINVAL for a PTRACE_GETREGSET call indicates that
	 kernel support is not available.  */
      return false;
    }

  return true;
}
