/* This testcase is part of GDB, the GNU debugger.

 Copyright 2018-2021 Free Software Foundation, Inc.

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

#include <stdint.h>

void
ptwrite64 (uint64_t value)
{
  asm volatile ("PTWRITE %0;" : : "b" (value));
}

void
ptwrite32 (uint32_t value)
{
  asm volatile ("PTWRITE %0;" : : "b" (value));
}

int
main (void)
{

  ptwrite64 (0x42);
  ptwrite32 (0x43);

  return 0;
}
