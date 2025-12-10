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
#include <iostream>
#include <cstdlib>
#include "../lib/sycl-util.cpp"
#include <sycl/ext/intel/experimental/grf_size_properties.hpp>

template<int GrfSize>
void run_kernel (sycl::queue& q)
{
  namespace syclex = sycl::ext::oneapi::experimental;
  namespace intelex = sycl::ext::intel::experimental;

  int data = 0;
  sycl::buffer<int, 1> buf (&data, sycl::range<1> {1});
  sycl::range<1> ndRange = { 1 };

  q.submit ([&] (sycl::handler& cgh)
    {
      auto acc = buf.get_access<sycl::access::mode::write> (cgh);

      syclex::properties kernel_properties {intelex::grf_size<GrfSize>};
      syclex::launch_config kernel_config (ndRange, kernel_properties);

      syclex::parallel_for (cgh, kernel_config, [=] (sycl::id<1> wiID)
	{
	  int num = 0;
	  int *src = nullptr;
	  num += *src; /* kernel-pagefault-line */
	  acc[0] = num;
	});
    });

  q.wait ();
}

int
main (int argc, char *argv[])
{
  const char *value = std::getenv ("TEST_GRF_SIZE");
  if ((value == nullptr)
      || ((std::string (value) != "128")
	  && (std::string (value) != "256")))
    {
      std::cerr << "TEST_GRF_SIZE must be set to '128' or '256'"
		<< std::endl;
      return 1;
    }

  sycl::queue q{ get_sycl_queue (argc, argv) };

  if (std::string (value) == "128")
    run_kernel<128> (q);
  else
    run_kernel<256> (q);

  return 0;
}
