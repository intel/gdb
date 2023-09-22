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

/* Utility file for SYCL test programs to get list of available devices.  */

#include <sycl/sycl.hpp>
#include <unordered_set>
#include <mpi.h>

static std::string
get_backend_name (sycl::backend backend_arg)
{
  std::string backend_name;

  if (backend_arg == sycl::backend::opencl)
    backend_name = "opencl";
  else if (backend_arg ==  sycl::backend::ext_oneapi_level_zero)
    backend_name = "ext_oneapi_level_zero";
  else
    {
      std::cout << "IMPI/SYCL: Unrecognized backend" << std::endl;
      backend_name = "";
    }

  return backend_name;
}

static std::string
get_device_type (sycl::info::device_type type)
{
  std::string type_name;

  if (type == sycl::info::device_type::cpu)
    type_name = "cpu";
  else if (type == sycl::info::device_type::gpu)
    type_name = "gpu";
  else if (type == sycl::info::device_type::accelerator)
    type_name = "accelerator";
  else
    {
      std::cout << "IMPI/SYCL: Unrecognized device type" << std::endl;
      type_name = "";
    }

  return type_name;
}

int
main (int argc, char *argv[])
{
  MPI_Init (&argc, &argv);

  const std::vector<sycl::device> devices
    = sycl::device::get_devices (sycl::info::device_type::all);

  if (devices.empty ())
    {
      std::cout << "IMPI/SYCL: Could not find any device" << std::endl;
      MPI_Finalize ();
      exit (1);
    }

  std::unordered_set<std::string> device_types;

  for (const sycl::device &device : devices)
    {
      const std::string dev_name
	= device.get_info<sycl::info::device::name> ();
      const std::string version
	= device.get_info<sycl::info::device::driver_version> ();
      const std::string backend_name
	= get_backend_name (device.get_backend ());

      const std::string type
	= get_device_type (device.get_info<sycl::info::device::device_type> ());

      if (backend_name != "")
	device_types.insert (dev_name + ";" + backend_name + ";" + version + ";" + type);
    }

  std::cout << "IMPI/SYCL: List of Target devices: [";
  int index = 0;
  for (const std::string &dev : device_types)
    {
      index++;
      std::cout << dev;
      if (index < device_types.size ())
	std::cout << ",";
    }
  std::cout << "]" << std::endl;

  MPI_Finalize ();
  return 0;
}
