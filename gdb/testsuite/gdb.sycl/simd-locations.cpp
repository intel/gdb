/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020 Free Software Foundation, Inc.

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
  sycl::queue queue {get_sycl_queue (argc, argv)};

  in_s in[N] = {};
  out_s out[N] = {};
  cs_s cs[L];

  for (int i = 0; i < N; ++i)
    in[i].a = i;

  for (int i = 0; i < L; ++i)
    cs[i].c = 4;

  uint64_t ops = 0;

  sycl::buffer<in_s> bin (in, sycl::range<1> (N));
  sycl::buffer<cs_s> bcs (cs, sycl::range<1> (L));
  sycl::buffer<out_s> bout (out, sycl::range<1> (N));
  sycl::buffer<uint64_t> bops (&ops, sycl::range<1> (1));

  queue.submit([&](sycl::handler& cgh)
    {
      sycl::accessor<in_s, 1, sycl::access::mode::read,
		     sycl::access::target::device> ain
	= bin.get_access<sycl::access::mode::read,
			 sycl::access::target::device> (cgh);

      sycl::accessor<cs_s, 1, sycl::access::mode::read,
		     sycl::access::target::constant_buffer> acs
	= bcs.get_access<sycl::access::mode::read,
			 sycl::access::target::constant_buffer> (cgh);

      sycl::accessor<out_s, 1, sycl::access::mode::write,
		     sycl::access::target::device> aout
	= bout.get_access<sycl::access::mode::write,
			  sycl::access::target::device> (cgh);

      sycl::accessor<uint64_t, 1, sycl::access::mode::read_write,
		     sycl::access::target::device> aops
	= bops.get_access<sycl::access::mode::read_write,
			  sycl::access::target::device> (cgh);

      sycl::local_accessor<in_s, 1> atmp (sycl::range<1> (N), cgh);

      cgh.parallel_for_work_group (sycl::range<1> (G),
				   sycl::range<1> (L),
				   [=] (sycl::group<1> wg)
	{
	  sycl::private_memory<sycl::id<1>> pgid (wg);
	  uint16_t lcs;

	  wg.parallel_for_work_item ([&] (sycl::h_item<1> wi)
	    {
	      sycl::id<1> gid = wi.get_global_id ();
	      sycl::id<1> lid = wi.get_local_id ();

	      in_s in = ain[gid];
	      uint16_t cs = acs[lid].c;
	      uint32_t ops = aops[0];

	      atmp[gid] = in;
	      pgid (wi) = gid;
	      lcs = cs;

	      ops += 1;
	      aops[0] = ops; /* bp.1 */
	    });

	  wg.parallel_for_work_item ([&] (sycl::h_item<1> wi)
	    {
	      sycl::id<1> gid = pgid (wi);
	      sycl::id<1> lid = wi.get_local_id ();

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

  sycl::accessor<out_s, 1, sycl::access::mode::read,
		 sycl::access::target::host_buffer> aout
    = bout.get_access<sycl::access::mode::read> ();

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
