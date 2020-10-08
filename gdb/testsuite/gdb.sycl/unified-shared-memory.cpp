/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020 Free Software Foundation, Inc.

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

#include <CL/sycl.hpp>
#include <iostream>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 32;

  cl::sycl::queue q {get_sycl_queue (argc, argv)};
  int *in = cl::sycl::malloc_shared<int> (DIM0, q);
  int *out = cl::sycl::malloc_shared<int> (DIM0, q);

  if ((in == nullptr) || (out == nullptr))
    {
      if (in != nullptr)
	cl::sycl::free(in, q);
      if (out != nullptr)
	cl::sycl::free(out, q);

      std::cerr << "failed to allocate shared memory" << std::endl;
      return -1;
    }

  /* Initialize the input.  */
  for (size_t i = 0; i < DIM0; i++)
    in[i] = i + 123;

  cl::sycl::range<1> size {DIM0};
  auto e = q.parallel_for<class kernel> (size, [=] (cl::sycl::id<1> wiID)
	     {
	       int dim0 = wiID[0]; /* kernel-first-line */
	       int in_elem = in[wiID];
	       out[wiID] = in_elem;
	       out[wiID] += 100; /* kernel-last-line */
	     });

  e.wait ();

  /* Verify the output.  */
  for (size_t i = 0; i < DIM0; i++)
    if (out[i] != in[i] + 100)
      {
	std::cerr << "Element " << i << " is " << out[i] << std::endl;
	cl::sycl::free(in, q);
	cl::sycl::free(out, q);
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */

  cl::sycl::free(in, q);
  cl::sycl::free(out, q);

  return 0;
}
