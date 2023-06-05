/* This testcase is part of GDB, the GNU debugger.

   Copyright 2021-2023 Free Software Foundation, Inc.

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

struct fat_pointer
{
  int *data;
  int *associated;
  int *size;
};

int one[] = {1};
int zero1[] = {0};
int zero2[] = {0};
int four[] = {4};
int data[] = {11, 22, 33, 44};

struct fat_pointer fp_associated = {data, one, four};
struct fat_pointer fp_not_associated = {0, zero1, zero2};

int
main ()
{
  return 0;
}
