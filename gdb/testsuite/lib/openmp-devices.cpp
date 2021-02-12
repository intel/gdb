/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020-2021 Free Software Foundation, Inc.

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

#include <iostream>
#include <omp.h>

extern "C"
{
  /* These functions are exported by the runtime and they can be used to
     get all the available devices and the currently used device.  */
  extern char *__tgt_get_device_name (int64_t device_num, char *buffer, size_t size);
  extern char *__tgt_get_device_rtl_name (int64_t device_num, char *buffer, size_t size);
}

using namespace std;

int
main()
{
  int nd = omp_get_num_devices ();
  if (nd < 1)
    {
      cerr << "OpenMP: omp_get_num_devices() call "
	<< "failed with an error code: " << nd << endl;
      return 1;
    }

  cout << "OpenMP: Number of devices is " << nd << endl;

  int dd = omp_get_default_device ();
  if (dd < 0)
    {
      cerr << "OpenMP: omp_get_default_device() call"
	<< "failed with an error code: " << dd << endl;
      return 1;
    }

  char buffer[128];
  if (__tgt_get_device_name (dd, buffer, 128))
    cout << "OpenMP: Default device is " << dd << ". Name: " << buffer << endl;

  for (int i = 0; i < nd; i++)
    {
      const char *dev_name = __tgt_get_device_name (i, buffer, 128);
      if (dev_name == nullptr)
	dev_name = "Unknown";
      cout << "OpenMP: Device " << i << ". Name: " << dev_name << endl;

      const char *rtl_name = __tgt_get_device_rtl_name (i, buffer, 128);
      if (rtl_name == nullptr)
	rtl_name = "Unknown";
      cout << "OpenMP: Device " << i << ". RTL name: " << rtl_name << endl;
    }

  int sum = 0;
  #pragma omp target teams distribute parallel for reduction (+:sum)
  for (int i = 0; i < 100; i++)
    sum += i;

  return 0;
}

