// { dg-options "-std=gnu++17" }
// { dg-require-string-conversions "" }

// Copyright (C) 2013-2024 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

namespace element_access_front_back {

static void
test01 ()
{
  gdb::string_view str("ramifications");
  const gdb::string_view cstr("melodien");

  VERIFY( str.front() == 'r' );
  VERIFY( str.back() == 's' );
  VERIFY( cstr.front() == 'm' );
  VERIFY( cstr.back() == 'n' );
}

static int
main ()
{
  test01();

  return 0;
}

} // namespace element_access_front_back
