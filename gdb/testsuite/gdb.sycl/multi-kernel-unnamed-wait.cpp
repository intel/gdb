/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023-2024 Free Software Foundation, Inc.

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
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 128;

  int in[DIM0];
  int out[DIM0];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DIM0; i++)
    in[i] = i + 123;

  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  sycl::range<1> dataRange {DIM0};

  /* Submit kernel 100 times.  */
  for (unsigned int i = 0; i < 100; i++)
    {
      sycl::buffer<int, 1> bufferIn {&in[0], dataRange};
      sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

      deviceQueue.submit ([&] (sycl::handler& cgh) /* line-before-kernel */
	{
	  auto accessorIn
	    = bufferIn.get_access<sycl::access::mode::read> (cgh);
	  auto accessorOut
	    = bufferOut.get_access<sycl::access::mode::write> (cgh);

	  cgh.parallel_for (dataRange, [=] (sycl::id<1> wiID)
	    {
	      int dim0 = wiID[0]; /* kernel-first-line */
	      int in_elem = accessorIn[dim0];
	      accessorOut[dim0] = in_elem + 100; /* kernel-last-line */
	    });
	});

      deviceQueue.wait_and_throw ();
    }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    if (out[i] != in[i] + 100)
      {
	std::cout << "Element " << i << " is " << out[i] << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */
  return 0;
}
