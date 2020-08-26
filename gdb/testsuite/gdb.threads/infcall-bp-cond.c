/* Copyright 2020 Free Software Foundation, Inc.

   This file is part of GDB.

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

#define NUM_THREADS 2

int
is_one (int tid)
{
  return tid == 1;
}

int
return_true ()
{
  return 1;
}

int
return_false ()
{
  return 0;
}

void *
doSmt (void *arg)
{
  int a = 42; /* breakpoint-here  */
}

int main(int argc, char *argv[])
{
  pthread_t threads[NUM_THREADS];
  int args[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++)
    {
      args[i] = i;
      pthread_create (&threads[i], NULL, doSmt, &args[i]);
    }

  pthread_exit(NULL);
}
