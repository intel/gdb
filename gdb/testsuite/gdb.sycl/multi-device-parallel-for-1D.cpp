/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2022 Free Software Foundation, Inc.

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

static int
get_transformed (int data, int device_idx)
{
  /* Any complicated operation.  */
  return data * 3 + 11 * (device_idx + 1);
}

int
main (int argc, char *argv[])
{
  using namespace cl::sycl;

  std::vector<device> devices = get_sycl_devices (argc, argv);
  const unsigned long num_devices = devices.size ();

  constexpr size_t DIM0 = 64;

  if (num_devices < 2) /* num-devices-check */
    {
      std::cerr << "failure: could not find multiple devices" << std::endl;
      return -1;
    }

  std::vector<int> in (DIM0 * num_devices);
  std::vector<int> out (DIM0 * num_devices);

  std::vector<queue> queues;
  /* Initialize the input.  */
  for (int i = 0; i < num_devices; ++i)
    {
      /* Initialize the input for ith device as device number starting from 1:
       { dev_num, dev_num, ... (DIM0 - 3 times) ..., dev_num }.  */
      std::fill_n (in.begin () + (i * DIM0), DIM0, i + 1);
      queues.emplace_back (queue (devices[i]));
    }

  {
    range<1> data_range {DIM0};
    std::vector<std::pair<buffer<int, 1>,
			  buffer<int, 1>>> buffers;
    /* Initialize buffers.  */
    for (int i = 0; i < num_devices; ++i)
      buffers.emplace_back (buffer<int, 1> {&in[i * DIM0], data_range},
			    buffer<int, 1> {&out[i * DIM0], data_range});

    for (int dev_idx = 0; dev_idx < num_devices; ++dev_idx)
      {
	std::cout << "Pushing task to dev " << dev_idx << std::endl;
	queues[dev_idx].submit ([&] (handler &cgh) /* line-before-kernel */
	  {
	    auto accessorIn
	      = buffers[dev_idx].first.get_access<access::mode::read> (cgh);
	    auto accessorOut
	      = buffers[dev_idx].second.get_access<access::mode::write> (cgh);

	    cgh.parallel_for (data_range, [=] (id<1> wiID)
	      {
		int in_elem = accessorIn[wiID]; /* kernel-first-line */
		accessorOut[wiID]
		  = get_transformed (in_elem, dev_idx); /* kernel-last-line */
	      });
	  });
      }

    for (auto &queue : queues)
      queue.wait ();
  }

  for (int i = 0; i < (num_devices * DIM0); ++i) /* check-marker */
    {
      unsigned int dev = i / DIM0;
      if (get_transformed (in[i], dev) != out[i])
	{
	  std::cout << "Element " << i << " is " << out[i] << " expected "
		    << get_transformed (in[i], dev) << std::endl;
	  return 1;
	}
    }

  std::cout << "Correct" << std::endl;
  return 0; /* end-marker */
}
