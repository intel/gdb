/* Copyright 2021-2022 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <omp.h>
#include <complex>
#include <iostream>
#include <cstring>

struct some_struct
{
  float a, b;
};

class base_one
{
  int num1 = 1;
  int num2 = 2;
  int num3 = 3;
};

class base_two
{
  const char *string = "Something in C++";
  float val = 3.5;
};

class derived_type : public base_one, base_two
{
private:
  int xxx = 9;
  float yyy = 10.5;
};

extern void mixed_func_1d (int a, float b, double c, const char *string);
static void mixed_func_1i (derived_type obj);

extern "C"
{
  /* The entry point back into Fortran, target.  */
  extern void mixed_func_1e_ ();

#pragma omp declare target
  void
  mixed_func_1c (int a, float b, double c, _Complex float d)
  {
    const char *string = "this is a string from C++";
    mixed_func_1d (a, b, c, string);
  }
#pragma omp end declare target

  extern void
  mixed_func_1h ()
  {
    derived_type *obj = new derived_type();

    #pragma omp target teams num_teams(1) thread_limit(1) map(to: obj)
    {
      mixed_func_1i (*obj);
    }

    delete obj;
  }
}

#pragma omp declare target
void
mixed_func_1d (int a, float b, double c, const char *string)
{
  std::cout << string << std::endl;
  mixed_func_1e_ ();
}

static void
mixed_func_1i (derived_type obj)
{
  mixed_func_1e_ ();
}
#pragma omp end declare target
