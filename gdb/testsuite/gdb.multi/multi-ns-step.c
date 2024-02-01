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

/* This program is intended to be started outside of GDB, and then
   attached to by GDB.  It loops for a while, but not forever.  */

#include <unistd.h>
#include <pthread.h>

static void *
thread_func_1 (void *arg)
{
  while (1)
    sleep (1); /* break-here */

  return NULL;
}

static void *
thread_func_2 (void *arg)
{
  while (1)
    sleep (1);

  return NULL;
}

int
main (void)
{
  alarm (30);

  pthread_t thread_1, thread_2;

  pthread_create (&thread_1, NULL, thread_func_1, NULL);
  pthread_create (&thread_2, NULL, thread_func_2, NULL);

  pthread_join (thread_1, NULL);
  pthread_join (thread_2, NULL);

  return 0;
}
