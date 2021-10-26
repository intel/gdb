/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2021 Free Software Foundation, Inc.

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

/* Utility file for SYCL test programs to enable explicit selection of
   a SYCL device.  Include this file in each SYCL test program.  */

#include <CL/sycl.hpp>
#include <iostream>
#include <vector>

static cl::sycl::info::device_type
get_device_type (std::string &type_arg)
{
  cl::sycl::info::device_type type;

  if (type_arg.compare ("host") == 0)
    type = cl::sycl::info::device_type::host;
  else if (type_arg.compare ("cpu") == 0)
    type = cl::sycl::info::device_type::cpu;
  else if (type_arg.compare ("gpu") == 0)
    type = cl::sycl::info::device_type::gpu;
  else if (type_arg.compare ("accelerator") == 0)
    type = cl::sycl::info::device_type::accelerator;
  else
    {
      std::cout << "SYCL: Unrecognized device type '"
		<< type_arg << "'" << std::endl;
      exit (1);
    }

  return type;
}

static std::vector<cl::sycl::device>
get_sycl_devices (int argc, char *argv[])
{
  if (argc <= 2)
    {
      std::cout << "Usage: " << argv[0]
		<< " <host|cpu|gpu|accelerator>"
		<< " <device name substring>" << std::endl;
      exit (1);
    }

  std::string type_arg {argv[1]};
  std::string name_arg {argv[2]};

  cl::sycl::info::device_type type = get_device_type (type_arg);

  std::vector<cl::sycl::device> devices
    = cl::sycl::device::get_devices (type);

  std::vector<cl::sycl::device> filtered_devices;
  for (const cl::sycl::device &device : devices)
    {
      std::string dev_name
	= device.get_info<cl::sycl::info::device::name> ();
      std::string platform_name
	= device.get_platform ().get_info<cl::sycl::info::platform::name> ();
      std::string version
	= device.get_info<cl::sycl::info::device::driver_version> ();

      if (dev_name.find (name_arg) != std::string::npos)
	filtered_devices.push_back (device);
    }

  if (filtered_devices.empty ())
    {
      std::cout << "SYCL: Could not select a device" << std::endl;
      exit (1);
    }

  return filtered_devices;
}

static void
print_device (const cl::sycl::device &device)
{
  std::string dev_name
    = device.get_info<cl::sycl::info::device::name> ();
  std::string platform_name
    = device.get_platform ().get_info<cl::sycl::info::platform::name> ();
  std::string version
    = device.get_info<cl::sycl::info::device::driver_version> ();

  std::cout << "SYCL: Using device: [" << dev_name << "]"
	    << " from [" << platform_name << "]"
	    << " version [" << version << "]" << std::endl;
}

static cl::sycl::queue
get_sycl_queue (int argc, char *argv[])
{
  cl::sycl::device device = get_sycl_devices (argc, argv)[0];
  print_device (device);

  return cl::sycl::queue {device}; /* return-sycl-queue */
}
