/* This testcase is part of GDB, the GNU debugger.

   Copyright 2025-2026 Free Software Foundation, Inc.

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

#include <pthread.h>
#include <stdio.h>

#define NUM_THREAD 5

/* Barrier to ensure all threads are created before any reaches
   the breakpoint.  */
static pthread_barrier_t barrier;

void *
thread_function (void *arg)
{
  volatile int x = * (int *) arg;

  pthread_barrier_wait (&barrier);

  printf ("Thread <%d> executing\n", x); /* thread-filter-bp */

  return NULL;
}

int
main (int argc, char **argv)
{
  pthread_t threads[NUM_THREAD];
  int args[NUM_THREAD];
  int i;

  pthread_barrier_init (&barrier, NULL, NUM_THREAD + 1);

  for (i = 0; i < NUM_THREAD; i++)
    {
      args[i] = i;
      pthread_create (&threads[i], NULL, thread_function, &args[i]);
    }

  pthread_barrier_wait (&barrier);

  for (i = 0; i < NUM_THREAD; i++)
    pthread_join (threads[i], NULL);

  pthread_barrier_destroy (&barrier);

  return 0;
}
