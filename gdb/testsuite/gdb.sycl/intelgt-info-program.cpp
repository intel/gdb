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
#include "../lib/sycl-util.cpp"

void
do_spin ()
{
  bool spin = true;
  int value = 0;

  while (spin)
    value = 1; /* spinning-line.  */
}

int
break_after_driver_load ()
{
  return 0;
}

int
main (int argc, char *argv[])
{
  do_spin (); /* before-kernel */

  for (auto platform: sycl::platform::get_platforms ())
    platform.get_devices ();

  break_after_driver_load ();

  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  deviceQueue.submit ([&] (sycl::handler &cgh)
    {
      cgh.single_task ([=] ()
	{
	  int dummy = 0;  /* kernel-line */
	});
    });

  deviceQueue.wait_and_throw ();

  return 0;
}
