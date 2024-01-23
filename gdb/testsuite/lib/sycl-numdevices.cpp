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

/* Utility file for SYCL test programs to get number of available
   devices.  */

#include <sycl/sycl.hpp>
#include <string>

int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      std::cout << "Usage: " << argv[0]
		<< " <cpu|gpu|accelerator>" << std::endl;
      exit (1);
    }

  sycl::info::device_type type;
  std::string str = argv[1];
  if (str == "gpu")
    type = sycl::info::device_type::gpu;
  else if (str == "cpu")
    type = sycl::info::device_type::cpu;
  else if (str == "accelerator")
    type = sycl::info::device_type::accelerator;
  else
    {
      std::cout << "Unknown device type " << str << std::endl;
      exit (0);
    }

  const std::vector<sycl::device> devices
    = sycl::device::get_devices (type);

  bool devices_same_driver = std::all_of(devices.begin(), devices.end(),
					 [&devices] (const sycl::device &d)
    {
      return d.get_info<sycl::info::device::driver_version> ()
	== devices.front ().get_info<sycl::info::device::driver_version> ();
    });

  if (!devices_same_driver) /* devices-driver-check */
    {
      std::cerr << "failure: found devices use different drivers" << std::endl;
      return -1;
    }

  const unsigned long num_devices = devices.size ();

  std::cout << "SYCL: Number of devices: " << num_devices << std::endl;

  return 0;
}
