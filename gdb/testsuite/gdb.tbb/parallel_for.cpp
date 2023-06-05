/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022-2023 Free Software Foundation, Inc.

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

#include <vector>
#include <iostream>
#include <numeric>

#include "oneapi/tbb/blocked_range.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/parallel_reduce.h"

using namespace oneapi::tbb;

int
main (int argc, char **argv)
{
  const size_t DIM0 = 100000;
  auto vals = std::vector<double> (DIM0);

  /* Parallel data initialization.  */
  parallel_for (blocked_range<size_t> (0, DIM0), [&] (blocked_range<size_t>& r)
    {
      for (size_t i = r.begin (); i < r.end (); ++i)
	vals[i] = i * 0.00001; /* bp-line-1 */
    });

  /* Parallel reduce operation. */
  using range_t = blocked_range<std::vector<double>::iterator>;
  double total = parallel_reduce (range_t (vals.begin (), vals.end ()), 0.0,
    [&] (const range_t &r, double init)
      {
	return std::accumulate (r.begin (), r.end (), init); /* bp-line-2 */
      },
    std::plus<double> ());

  return 0; /* return line */
}
