/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2020 Free Software Foundation, Inc.

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
  int data1 = 11;
  int data2 = 22;

#pragma omp target teams num_teams(1) thread_limit(1) \
	    map(tofrom: data1) depend(out: data1) nowait
  {
    int item = data1 + 100;
    data1 = item; /* kernel-1-line */
  }

#pragma omp target teams num_teams(1) thread_limit(1) \
	    map(tofrom: data2) depend(out: data2) nowait
  {
    int item = data2 + 200;
    data2 = item;  /* kernel-2-line */
  }

#pragma omp single
#pragma omp task depend(in: data1, data2)
  int total = data1 + data2; /* post-kernel-line */

  return 0;
}
