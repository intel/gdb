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
#include "omp.h"

int
main (int argc, char *argv[])
{
  int in_arr[3] = {7, 8, 9};

#pragma omp target teams num_teams(1) thread_limit(1) map(from: in_arr) \
  depend(out: in_arr)
  {
    in_arr[1] = 32;
    in_arr[2] = 10; /* kernel-1-line */
  }

#pragma omp target teams num_teams(1) thread_limit(1) map(tofrom: in_arr) \
  depend(in: in_arr)
  {
    int num1 = in_arr[1];
    int num2 = in_arr[2];
    in_arr[0] = num1 + num2; /* kernel-2-line */
  }

  std::cout << "Result is " << in_arr[0] << std::endl;  /* line-after-kernel */

  return 0;
}
