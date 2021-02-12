/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2021 Free Software Foundation, Inc.

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

#include <omp.h>

int
main ()
{
  int ten, four, fourteen;
  int x = 7;
  int y = 8;
  int z = 9;  /* line-before-kernel */

#pragma omp target teams num_teams(1) thread_limit(1) map(tofrom: x, y, z)
  {
    ten = y + 2;              /* kernel-line-1 */
    four = z - 5;             /* kernel-line-2 */
    fourteen = ten + four;    /* kernel-line-3 */
    z = fourteen * 3;             /* kernel-line-4 */
  }

#pragma omp single
  {
    z = 3; /* line-after-kernel */
  }

  return 0; /* return-stmt */
}
