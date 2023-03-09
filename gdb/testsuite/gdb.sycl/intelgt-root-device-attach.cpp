/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023 Free Software Foundation, Inc.

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

/* This is a SYCL program that partitions devices according to the
   affinity domain and then submits a workload to each sub-device.  */

#include <sycl/sycl.hpp>
#include <iostream>
#include "../lib/sycl-util.cpp"

static void
run (sycl::queue &device_queue)
{
  sycl::device device = device_queue.get_device ();
  std::cout << "SYCL: Submitting to ";
  print_device (device);
  std::cout << std::endl;

  sycl::range data_range {1};

  device_queue.submit ([&] (sycl::handler& cgh)
    {
      cgh.parallel_for (data_range, [=] (sycl::id<1> index)
	{
	  index[0] = 101; /* kernel-line-1 */
	});
    });
}

int
main (int argc, char *argv[])
{
  std::vector<sycl::device> devices = get_sycl_devices (argc, argv);
  unsigned long num_devices = devices.size ();
  if (num_devices == 0)
    {
      std::cerr << "SYCL: No devices found."
		<< std::endl;
      return -1;
    }

  std::vector<sycl::device> sub_devices;
  for (sycl::device root: devices)
    {
      auto constexpr strategy
	= sycl::info::partition_property::partition_by_affinity_domain;
      auto constexpr affinity
	= sycl::info::partition_affinity_domain::numa;
      auto num_max_subdevices =
	root.get_info<sycl::info::device::partition_max_sub_devices> ();

      if (num_max_subdevices == 0)
	{
	  std::cout << "SYCL: No subdevices found in ";
	  print_device (root);
	  std::cout << "; skipping." << std::endl;
	}
      else
	{
	  std::cout << "SYCL: Partitioning ";
	  print_device (root);
	  std::cout << "; has " << num_max_subdevices << " subdevices."
		    << std::endl;

	  for (sycl::device sub: root.create_sub_devices<strategy>(affinity))
	    sub_devices.push_back (sub);
	}
    }

  unsigned long num_sub_devices = sub_devices.size ();
  if (num_sub_devices == 0) /* num-devices-check */
    {
      std::cerr << "SYCL: No subdevices found."
		<< std::endl;
      return -1;
    }

  std::vector<sycl::queue> queues;
  for (const sycl::device &device : sub_devices)
    queues.push_back (sycl::queue {device});

  std::cout << "SYCL: Submitting tasks." << std::endl; /* pre-submission */

  for (sycl::queue &queue: queues)
    run (queue);

  std::cout << "SYCL: Submitted tasks." << std::endl; /* post-submission */

  for (sycl::queue &queue: queues)
    queue.wait_and_throw ();

  std::cout << "SYCL: Done." << std::endl;
  return 0; /* end-marker */
}
