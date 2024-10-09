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

/* number of threads = DATA_SIZE / SUB_GROUP_SIZE.  */
#define DATA_SIZE 64
#define SUB_GROUP_SIZE 16

int
fourth (int x4, int *y4)
{
  int result = x4 * (*y4);

  /* In this function, one thread tries to read through a nullptr, while
     other threads go spinning.  We expect all the threads to stop due to
     that one faulting read, before they exit.  */

  if (*y4 < SUB_GROUP_SIZE)
    {
      /* Spin a while, before triggering pagefault,
	 to let other threads enter this function.  */
      size_t count = 1e4;
      while (count > 0) count--;
      int *src = nullptr;
      /* Memory access and page fault detection may be asynchronous,
	 so we use 'plus and assign' operator to force the page fault
	 detection at that line.  */
      result += *src;  /* pagefault-line */
    }
  else
    {
      /* Spin a very long time, to let the faulting
	 thread trigger a pagefault.  Counter ensures
	 this does not run infinitely.  */
      size_t count = 1e8;
      while (count > 0) count--;  /* spin-line */
    }

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
  int in[DATA_SIZE];
  int out[DATA_SIZE];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DATA_SIZE; i++)
    in[i] = i + 123;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<1> dataRange {DATA_SIZE};
    sycl::buffer<int, 1> bufferIn {&in[0], dataRange};
    sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto accessorIn = bufferIn.get_access<sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<sycl::access::mode::write> (cgh);

	cgh.parallel_for (dataRange, [=] (sycl::id<1> wiID)
				[[sycl::reqd_sub_group_size (SUB_GROUP_SIZE)]]
	  {
	    int in_elem = accessorIn[wiID];
	    int in_elem2 = wiID;
	    accessorOut[wiID] = first (in_elem, in_elem2); /* kernel-line */
	  });
      });
  }

  return 0;
}
