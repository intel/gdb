/* This testcase is part of GDB, the GNU debugger.

   Copyright 2026 Free Software Foundation, Inc.

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
#include "sycl-util.cpp"

int
main (int argc, char *argv[])
{
  sycl::queue queue {get_sycl_queue (argc, argv)};
  sycl::range<1> range {64};
  queue.parallel_for (range, [] (sycl::id<1> id)
    {
      int ntrips = 0;
      while (ntrips > 0) /* kernel.spin */
	ntrips -= 1;

      int foo = 0; /* kernel.end */
    });

  queue.wait ();

  return 0;
}
