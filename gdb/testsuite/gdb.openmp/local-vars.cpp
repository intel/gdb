/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020-2021 Free Software Foundation, Inc.

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
#include <iostream>
#include <assert.h>

int
main ()
{
  unsigned int glob = 0;  /* line-before-kernel */

  #pragma omp target teams num_teams(1) thread_limit(1) map(from:glob)
  {
    int a;
    long long b = 3;     /* kernel-line-1 */
    unsigned short c;
    int* pa = &a;        /* kernel-line-2 */
    a = 0;               /* kernel-line-3 */
    c = 2;               /* kernel-line-4 */
    glob = 5;            /* kernel-line-5 */
    c = glob + a + b;    /* kernel-line-6 */
    a++;                 /* kernel-line-7 */
    *pa = 0;             /* kernel-line-8 */
    a++;                 /* kernel-line-9 */
  }

  #pragma omp single
  {
    std::cout << "Glob value is " << glob << std::endl;  /* line-after-kernel */
    if (glob == 0)
       assert (0);
  }
  return 0; /* return-stmt */
}

