/* This testcase is part of GDB, the GNU debugger.

   Copyright 2024 Free Software Foundation, Inc.

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
#include <iostream>
#include "../lib/sycl-util.cpp"
#include <sycl/ext/intel/experimental/grf_size_properties.hpp>

int
main (int argc, char *argv[])
{
  namespace syclex = sycl::ext::oneapi::experimental;
  namespace intelex = sycl::ext::intel::experimental;

  int a = 0;
  int b = 0;

  sycl::queue q {get_sycl_queue (argc, argv)};
  sycl::buffer<int, 1> bufa (&a, sycl::range<1> {1});
  sycl::buffer<int, 1> bufb (&b, sycl::range<1> {1});

  q.submit ([&] (sycl::handler &cgh)
    {
      auto acc = bufa.get_access<sycl::access::mode::write> (cgh);

      syclex::properties kernel_properties {intelex::grf_size<128>};

      cgh.parallel_for (sycl::range<1> {1}, kernel_properties,
			[=] (sycl::id<1> wiID)
			{
			  acc[wiID] = wiID; /* kernel-1-line */
			});
    });

  q.wait ();

  q.submit ([&] (sycl::handler &cgh)
    {
      auto acc = bufb.get_access<sycl::access::mode::write> (cgh);

      syclex::properties kernel_properties {intelex::grf_size<256>};

      cgh.parallel_for (sycl::range<1> {1}, kernel_properties,
			[=] (sycl::id<1> wiID)
			{
			  acc[wiID] = wiID + 1; /* kernel-2-line */
			});
    });

  q.wait ();

  return 0;
}
