/* This testcase is part of GDB, the GNU debugger.

   Copyright 2021 Free Software Foundation, Inc.

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

static std::vector<int> input;
static std::vector<int> output;

static void
run (cl::sycl::queue &device_queue, int index)
{
  /* A device picks an element from the input array according to its
     index, and writes to the output array at the same index.  */
  cl::sycl::range<1> data_range {1};
  cl::sycl::buffer<int, 1> buffer_in {&(input.data ()[index]), data_range};
  cl::sycl::buffer<int, 1> buffer_out {&(output.data ()[index]), data_range};

  device_queue.submit ([&] (cl::sycl::handler& cgh)  /* line-before-kernel */
    {
      auto acc_in
	= buffer_in.get_access<cl::sycl::access::mode::read> (cgh);
      auto acc_out
	= buffer_out.get_access<cl::sycl::access::mode::write> (cgh);

      cgh.single_task<class simple_kernel> ([=] ()
	{
	  int point = acc_in[0];
	  int a = 111; /* kernel-line-1 */
	  acc_out[0] = point + 100;
	  int b = 222; /* kernel-line-2 */
	});
    });
}

int
main (int argc, char *argv[])
{
  std::vector<cl::sycl::device> devices
    = get_sycl_devices (argc, argv);
  unsigned long num_devices = devices.size ();

  if (num_devices < 2) /* num-devices-check */
    {
      std::cerr << "failure: could not find multiple devices" << std::endl;
      return -1;
    }

  std::vector<cl::sycl::queue> queues;
  int index = 1;
  for (const cl::sycl::device &device : devices)
    {
      print_device (device);
      queues.push_back (cl::sycl::queue {device});
      /* Also fill the data.  */
      input.push_back (index);
      output.push_back (0);
      index++;
    }

  std::cout << "Submitting tasks" << std::endl; /* pre-submission */

  index = 0;
  for (cl::sycl::queue &queue: queues)
    {
      run (queue, index);
      index++;
    }

  std::cout << "Submitted tasks" << std::endl; /* post-submission */

  for (cl::sycl::queue queue: queues)
    queue.wait ();

  /* Verify the output.  */
  for (int i = 0; i < num_devices; i++)
    if (output[i] != input[i] + 100)
      {
	std::cout << "failure: output[" << i << "] is "
		  << output[i] << std::endl;
	return -1;
      }

  std::cout << "success" << std::endl;
  return 0; /* end-marker */
}
