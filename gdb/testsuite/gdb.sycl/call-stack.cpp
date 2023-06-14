/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2023 Free Software Foundation, Inc.

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

#include <sycl/sycl.hpp>
#include <iostream>
#include "../lib/sycl-util.cpp"

int
fourth (int x4, int y4)
{
  return x4 * y4; /* ordinary-fourth-loc */
}

int
third (int x3, int y3)
{
  return fourth (x3 + 5, y3 * 3) + 30; /* ordinary-third-loc */
}

int
second (int x2, int y2)
{
  return third (x2 + 5, y2 * 3) + 30; /* ordinary-second-loc */
}

int
first (int x1, int y1)
{
  int result = second (x1 + 5, y1 * 3); /* ordinary-first-loc */
  return result + 30; /* kernel-function-return */
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
  int result = inlined_second (num1 + 5, num2 * 3); /* inlined-middle-loc */
  return result + 30;
}

int
main (int argc, char *argv[])
{
  int data[3] = {7, 8, 9};

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {3}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto numbers = buf.get_access<sycl::access::mode::read_write> (cgh);

	cgh.single_task ([=] ()
	  {
	    int ten = numbers[1] + 2;
	    int four = numbers[2] - 5;
	    int fourteen = ten + four;
	    numbers[0] = first (fourteen + 1, 3); /* ordinary-outer-loc */
	    numbers[1] = inlined_first (10, 2); /* inlined-outer-loc */
	    numbers[2] = first (3, 4); /* another-call */
	  });
      });
  }

  std::cout << "Result is " << data[0] << " "
	    << data[1] << " " << data[2] << std::endl;
  /* Expected: 210 120 126 */

  return 0; /* end-of-program */
}
