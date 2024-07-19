/* This testcase is part of GDB, the GNU debugger.

   Copyright 2024 Free Software Foundation, Inc.

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
#include "../lib/sycl-util.cpp"

int
fourth (int x4, int *y4)
{
  int result = x4 * (*y4);
  int *src = nullptr;
  result += *src;  /* pagefault-line */
  return result;  /* line after pagefault */
}

int
third (int x3, int y3)
{
  return fourth (x3 + 5, &y3);  /* func-third */
}

int
second (int x2, int y2)
{
  return third (x2 + 5, y2);  /* func-second */
}

int
first (int x1, int y1)
{
  int result = second (x1 + 5, y1);  /* func-first */
  return result;
}

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 64;

  int in[DIM0];
  int out[DIM0];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DIM0; i++)
    in[i] = i + 123;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<1> dataRange {DIM0};
    sycl::buffer<int, 1> bufferIn {&in[0], dataRange};
    sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto accessorIn = bufferIn.get_access<sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<sycl::access::mode::write> (cgh);

	cgh.parallel_for<class kernel> (dataRange, [=] (sycl::id<1> wiID)
					[[sycl::reqd_sub_group_size (16)]]
	  {
	    int in_elem = accessorIn[wiID];
	    int in_elem2 = wiID;
	    accessorOut[wiID] = first (in_elem, in_elem2); /* kernel-line */
	  });
      });
  }

  return 0;
}
