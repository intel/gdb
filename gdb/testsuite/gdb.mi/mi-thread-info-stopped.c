/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023-2024 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define NUM 4

volatile int should_spin = 1;

static void
something ()
{
}

static void
spin ()
{
  while (should_spin)
    usleep (1);
}

static void *
work (void *arg)
{
  int id = *((int *) arg);

  /* Sleep a bit to give the other threads a chance to run.  */
  usleep (1);

  if (id % 2 == 0)
    something (); /* break-here */
  else
    spin ();

  pthread_exit (NULL);
}

int
main ()
{
  pthread_t threads[NUM];
  void *thread_result;
  int ids[NUM];

  for (int i = 0; i < NUM; i++)
    {
      ids[i] = i + 2;
      pthread_create (&threads[i], NULL, work, &(ids[i]));
    }

  sleep (10);
  should_spin = 0;

  for (int i = 0; i < NUM; i++)
    pthread_join (threads[i], &thread_result);

  return 0;
}
