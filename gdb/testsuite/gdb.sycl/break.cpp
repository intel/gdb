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

static int
foo ()
{ /* foo.entry */
  int a = 0; /* foo.entry */
  int b = 1; /* foo.1 */
  return a + b;
}

static int
bar ()
{ /* bar.entry */
  int a = 0; /* bar.entry */
  int b = 1; /* bar.1 */
  return a + b;
}

struct SingleTask
{
  /* Test that we're not leaking host breakpoints into the kernel.  */
  SingleTask ()
    {
      int a = 0; /* host.1 */
    }

  void operator () () const /* kernel.single_task */
    { /* kernel.single_task */
      int a = 0; /* kernel.single_task */
      int b = 0; /* kernel.1 */
      int c = foo ();
      int d = 0; /* kernel.2 */
      int e = bar ();
    }

  /* Test that we're not leaking kernel breakpoints into the host.  */
  ~SingleTask ()
    {
    }
};

struct ParallelFor
{
  /* Test that we're not leaking host breakpoints into the kernel.  */
  ParallelFor ()
    {
      int a = 0; /* host.2 */
    }

  void operator () (sycl::nd_item<1> item) const /* kernel.parallel_for */
    { /* kernel.parallel_for */
      sycl::id<1> id = item.get_global_id (); /* kernel.parallel_for */
      int gid = id.get (0);

      int a = 0; /* kernel.3 */
      int b = 0; /* kernel.4 */
      int c = 0; /* kernel.5 */
      int d = 0; /* kernel.6 */
      int e = 0; /* kernel.7 */
      int f = 0; /* kernel.8 */
      int g = 0; /* kernel.9 */
    }

  /* Test that we're not leaking kernel breakpoints into the host.  */
  ~ParallelFor ()
    {
    }
};

int
main (int argc, char *argv[])
{
  sycl::queue queue {get_sycl_queue (argc, argv)};

  SingleTask single_task;
  for (int i = 0; i < 2; i++)
    {
      queue.single_task (single_task);
      queue.wait ();
    }

  /* Test that breakpoints do not leak into or out of lambda kernels.  */
  int a = 0; /* host.3 */
  queue.single_task ([] ()
    {
      int a = 0;
      int b = 0; /* lambda.1 */
    });
  queue.wait ();

  ParallelFor parallel_for;
  sycl::nd_range<1> range {2, 1};
  for (int i = 0; i < 2; i++)
    {
      queue.parallel_for (range, parallel_for);
      queue.wait ();
      int a = 0; /* host.4 */
    }

  return 0;
}
