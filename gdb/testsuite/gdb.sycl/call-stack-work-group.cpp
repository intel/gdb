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

#include "CL/sycl.hpp"
#include "../lib/sycl-util.cpp"

constexpr int g_items_1d = 8;
constexpr int g_items_2d = 4;
constexpr int g_items_3d = 2;
constexpr int l_items_1d = 4;
constexpr int l_items_2d = 2;
constexpr int l_items_3d = 1;
constexpr int gr_range_1d = (g_items_1d / l_items_1d);
constexpr int gr_range_2d = (g_items_2d / l_items_2d);
constexpr int gr_range_3d = (g_items_3d / l_items_3d);
constexpr int gl_items_total = (g_items_1d * g_items_2d * g_items_3d);
constexpr int l_items_total = (l_items_1d * l_items_2d * l_items_3d);
constexpr int gr_range_total = (gl_items_total / l_items_total);

int
main (int argc, char *argv[])
{
  cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  deviceQueue.submit ([&] (cl::sycl::handler &cgh)
    {
      cgh.parallel_for_work_group (
	cl::sycl::range<3> (gr_range_1d, gr_range_2d, gr_range_3d),
	cl::sycl::range<3> (l_items_1d, l_items_2d, l_items_3d),
	[=] (cl::sycl::group<3> group)
	  {
	    group.parallel_for_work_item ([&] (cl::sycl::h_item<3> itemID)
	      {
		int localId0 = itemID.get_local_id (0); /* work-item-location */
	      });
	  });
    });

  deviceQueue.wait();
}
