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
#include <iostream>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  sycl::queue q {get_sycl_queue (argc, argv)};
  auto device = q.get_device ();
  auto numSlices
    = device.get_info<sycl::ext::intel::info::device::gpu_slices> ();
  auto numSubslicesPerSlice
    = device.get_info<sycl::ext::intel::info::device
	::gpu_subslices_per_slice> ();
  auto numEUsPerSubslice
    = device.get_info<sycl::ext::intel::info::device
	::gpu_eu_count_per_subslice> ();
  auto numThreadsPerEU
    = device.get_info<sycl::ext::intel::info::device
	::gpu_hw_threads_per_eu> ();
  const uint32_t num_cores
    = (numSlices * numSubslicesPerSlice * numEUsPerSubslice);
  const uint32_t total_threads = (num_cores * numThreadsPerEU);

  size_t DIM0 = num_cores * num_cores;

  int *in = sycl::malloc_shared<int> (DIM0, q);
  int *out = sycl::malloc_shared<int> (DIM0, q);

  if ((in == nullptr) || (out == nullptr))
    {
      if (in != nullptr)
	sycl::free (in, q);
      if (out != nullptr)
	sycl::free (out, q);

      std::cerr << "failed to allocate shared memory" << std::endl;
      return -1;
    }

  /* Initialize the input.  */
  for (size_t i = 0; i < DIM0; i++)
    in[i] = i + 123;

  sycl::range<1> size {DIM0};
  q.parallel_for<class kernel> (size, [=] (sycl::id<1> wiID)
    {
      int in_elem = in[wiID] + 100; /* kernel-line-break */
      unsigned int max = 20000 * total_threads;
      while (max > 0)
	{
	  out[wiID] = in_elem;
	  max--;
	}
    });

  q.wait ();

  /* Verify the output.  */
  for (size_t i = 0; i < DIM0; i++)
    if (out[i] != in[i] + 100)
      {
	std::cerr << "Element " << i << " is " << out[i] << std::endl;
	sycl::free (in, q);
	sycl::free (out, q);
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */

  sycl::free (in, q);
  sycl::free (out, q);

  return 0;
}
