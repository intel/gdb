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

#include <iostream>
#include <omp.h>

#pragma omp declare target
int
second (int x, int y)
{
  return x * y; /* ordinary-inner-loc */
}

int
first (int num1, int num2)
{
  int total = second (num1 + 4, num2 * 3); /* ordinary-middle-loc */
  return total + 30; /* kernel-function-return */
}

__attribute__((always_inline))
int
inlined_second (int x, int y)
{
  return x * y; /* inlined-inner-loc */
}

__attribute__((always_inline))
int
inlined_first (int num1, int num2)
{
  int total = inlined_second (num1 + 4, num2 * 3); /* inlined-middle-loc */
  return total + 30;
}
#pragma omp end declare target

int
main (int argc, char *argv[])
{
  int data[3] = {7, 8, 9};

#pragma omp target data map(tofrom: data)
#pragma omp target teams num_teams(1) thread_limit(1)
  {
    int ten = data[1] + 2;
    int five = data[2] - 4;
    int fifteen = ten + five;
    data[0] = first (fifteen + 1, 3); /* ordinary-outer-loc */
    data[1] = inlined_first (10, 2); /* inlined-outer-loc */
    data[2] = first (3, 4); /* another-call */
  }

#pragma omp single
  {
    std::cout << "Result is " << data[0] << " "
	      << data[1] << " " << data[2] << std::endl; /* line-after-kernel */
    /* Expected: 210 114 114 */
  }

  return 0; /* end-of-program */
}
