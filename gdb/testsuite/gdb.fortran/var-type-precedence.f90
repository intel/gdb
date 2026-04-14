! Copyright 2026 Free Software Foundation, Inc.
!
! This program is free software; you can redistribute it and/or modify
! it under the terms of the GNU General Public License as published by
! the Free Software Foundation; either version 3 of the License, or
! (at your option) any later version.
!
! This program is distributed in the hope that it will be useful,
! but WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
! GNU General Public License for more details.
!
! You should have received a copy of the GNU General Public License
! along with this program.  If not, see <http://www.gnu.org/licenses/>.

! Source code for var-type-precedence test to verify if Fortran variable
! name conflicting with a type name from a C shared library is handled
! properly.

program test
  use iso_c_binding
  implicit none

  interface
    subroutine fortran_var_type_order_test () bind (C)
    end subroutine fortran_var_type_order_test
  end interface

  ! Declare variables with names that conflict with types in C library.
  integer, dimension (-2:2) :: fortran_var_c_type_conflict

  ! Call C library function to ensure it's linked.
  call fortran_var_type_order_test ()

  fortran_var_c_type_conflict = 1

  print *, "" ! break-here
  print *, fortran_var_c_type_conflict

end program test
