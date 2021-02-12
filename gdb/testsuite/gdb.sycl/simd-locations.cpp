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

#include <CL/sycl.hpp>
#include "../lib/sycl-util.cpp"
#include <iostream>

#define G 2
#define L 8
#define N (G * L)

struct in_s {
  uint16_t reserved;
  uint16_t a;
  uint16_t b;
};

struct out_s {
  uint16_t reserved[5];
  uint16_t a;
  uint16_t c;
};

struct cs_s {
  uint16_t reserved[4];
  uint16_t c;
};

int
main (int argc, char *argv[])
{
  cl::sycl::queue queue {get_sycl_queue (argc, argv)};

  in_s in[N] = {};
  out_s out[N] = {};
  cs_s cs[L];

  for (int i = 0; i < N; ++i)
    in[i].a = i;

  for (int i = 0; i < L; ++i)
    cs[i].c = 4;

  uint64_t ops = 0;

  cl::sycl::buffer<in_s> bin (in, cl::sycl::range<1> (N));
  cl::sycl::buffer<cs_s> bcs (cs, cl::sycl::range<1> (L));
  cl::sycl::buffer<out_s> bout (out, cl::sycl::range<1> (N));
  cl::sycl::buffer<uint64_t> bops (&ops, cl::sycl::range<1> (1));

  queue.submit([&](sycl::handler& cgh)
    {
      cl::sycl::accessor<in_s, 1, cl::sycl::access::mode::read,
			 cl::sycl::access::target::global_buffer> ain
	= bin.get_access<cl::sycl::access::mode::read,
			 cl::sycl::access::target::global_buffer> (cgh);

      cl::sycl::accessor<cs_s, 1, cl::sycl::access::mode::read,
			 cl::sycl::access::target::constant_buffer> acs
	= bcs.get_access<cl::sycl::access::mode::read,
			 cl::sycl::access::target::constant_buffer> (cgh);

      cl::sycl::accessor<out_s, 1, cl::sycl::access::mode::write,
			 cl::sycl::access::target::global_buffer> aout
	= bout.get_access<cl::sycl::access::mode::write,
			 cl::sycl::access::target::global_buffer> (cgh);

      cl::sycl::accessor<uint64_t, 1, cl::sycl::access::mode::read_write,
			 cl::sycl::access::target::global_buffer> aops
	= bops.get_access<cl::sycl::access::mode::read_write,
			  cl::sycl::access::target::global_buffer> (cgh);

      cl::sycl::accessor<in_s, 1, cl::sycl::access::mode::read_write,
			 cl::sycl::access::target::local>
	atmp (cl::sycl::range<1> (N), cgh);

      cgh.parallel_for_work_group (cl::sycl::range<1> (G),
				   cl::sycl::range<1> (L),
				   [=] (cl::sycl::group<1> wg)
	{
	  cl::sycl::private_memory<cl::sycl::id<1>> pgid (wg);
	  uint16_t lcs;

	  wg.parallel_for_work_item ([&] (cl::sycl::h_item<1> wi)
	    {
	      cl::sycl::id<1> gid = wi.get_global_id ();
	      cl::sycl::id<1> lid = wi.get_local_id ();

	      in_s in = ain[gid];
	      uint16_t cs = acs[lid].c;
	      uint32_t ops = aops[0];

	      atmp[gid] = in;
	      pgid (wi) = gid;
	      lcs = cs;

	      ops += 1;
	      aops[0] = ops; /* bp.1 */
	    });

	  wg.parallel_for_work_item ([&] (cl::sycl::h_item<1> wi)
	    {
	      cl::sycl::id<1> gid = pgid (wi);
	      cl::sycl::id<1> lid = wi.get_local_id ();

	      uint16_t in = atmp[gid].a;
	      uint16_t cs = lcs;
	      uint32_t ops = aops[0];
	      out_s out;

	      out.a = in;
	      out.c = in + cs;
	      aout[gid] = out;

	      ops += 1;
	      aops[0] = ops; /* bp.2 */
	    });
	});
    });

  cl::sycl::accessor<out_s, 1, cl::sycl::access::mode::read,
		     cl::sycl::access::target::host_buffer> aout
    = bout.get_access<cl::sycl::access::mode::read> ();

  int errcode = 0;
  for (int i = 0; i < N; ++i)
    {
      if (aout[i].a != i || aout[i].c != (i + 4))
	{
	  std::cerr << "out[" << i << "] = (" << aout[i].a << ", "
		    << aout[i].c << "), expected (" << i << ", " << i + 4
		    << ")" << std::endl;
	  errcode = 1;
	}
    }

  return errcode;
}
