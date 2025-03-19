/* This testcase is part of GDB, the GNU debugger.

   Copyright 2025 Free Software Foundation, Inc.

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

#ifdef _MBCS
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static int
get_dim (sycl::id<1> wi, int index)
{
  return wi[index];
}

static int
fourth (int x4, int y4)
{
  return x4 + y4; /* fourth-loc.  */
}

static int
third (int x3, int y3)
{
  return fourth (x3 + 5, y3 - 3) + 30; /* third-loc.  */
}

static int
second (int x2, int y2)
{
  return third (x2 + 5, y2 - 3) + 30; /* second-loc.  */
}

static int
first (int x1, int y1)
{
  int result = second (x1 + 5, y1 - 3); /* first-loc.  */
  return result + 30;
}


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

  /* Sleep for 10 seconds to ensure 'gdbserver-ze' started, the remote
     connection is setup and GDB is attached before submitting the workload
     to the GPU.  */
#ifdef _MBCS
    Sleep (10000);
#else
    sleep (10);
#endif

  q.parallel_for<class kernel> (size, [=] (sycl::id<1> wiID)
    {
      int dim0 = get_dim (wiID, 0); /* kernel-first-line.  */
      int elem = in[wiID] + 5;
      int elem2 = in[wiID] - 10;
      out[wiID] = first (elem, elem2) + 100; /* outer-loc.  */
    });

  q.wait ();

  return 0;
}
