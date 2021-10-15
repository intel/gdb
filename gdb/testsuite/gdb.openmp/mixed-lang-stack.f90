! Copyright 2009-2021 Free Software Foundation, Inc.
!
! This program is free software; you can redistribute it and/or modify
! it under the terms of the GNU General Public License as published by
! the Free Software Foundation; either version 2 of the License, or
! (at your option) any later version.
!
! This program is distributed in the hope that it will be useful,
! but WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
! GNU General Public License for more details.
!
! You should have received a copy of the GNU General Public License
! along with this program.  If not, see <http://www.gnu.org/licenses/>.

module type_module
  use, intrinsic :: iso_c_binding, only: c_int, c_float, c_double
  type, bind(C) :: MyType
     real(c_float) :: a
     real(c_float) :: b
  end type MyType
end module type_module

program mixed_stack_main
  implicit none

  ! Set up some locals.
  INTEGER :: a, b, c
  a = 1

  ! Call a Fortran function, on host.
  call mixed_func_1a

  write(*,*) "All done"
end program mixed_stack_main

subroutine mixed_func_1a ()
  use type_module
  implicit none

  TYPE(MyType) :: obj
  complex(kind=4) :: d
  d = cmplx (4.0, 5.0)

  !$omp target map(to: d)
  !$omp teams num_teams(1) thread_limit(1)
    ! Call a Fortran function, on target.
    call mixed_func_1b (1, 2.0, 3D0, d)
  !$omp end teams
  !$omp end target

  ! Call a Fortran function on host.
  call mixed_func_1f (obj)
end subroutine mixed_func_1a

subroutine mixed_func_1b(a, b, c, d)
  !$omp declare target(mixed_func_1b, mixed_func_1c)

  integer :: a
  real(kind=4) :: b
  real(kind=8) :: c
  complex(kind=4) :: d

  interface
     subroutine mixed_func_1c (a, b, c, d) bind(C)
       use, intrinsic :: iso_c_binding, only: c_int, c_float, c_double
       use, intrinsic :: iso_c_binding, only: c_float_complex, c_char
       use type_module
       implicit none
       integer(c_int), value, intent(in) :: a
       real(c_float), value, intent(in) :: b
       real(c_double), value, intent(in) :: c
       complex(c_float_complex), value, intent(in) :: d
     end subroutine mixed_func_1c
  end interface

  ! Call a C++ function (via an extern "C" wrapper), on target.
  call mixed_func_1c (a, b, c, d)
end subroutine mixed_func_1b

subroutine mixed_func_1f (g)
  use type_module
  TYPE(MyType) :: g

  interface
     subroutine mixed_func_1h () bind(C)
       implicit none
     end subroutine mixed_func_1h
  end interface

  write(*,*) "Value for a: ", g%a

  ! Call a C++ function (via an extern "C" wrapper), on host.
  call mixed_func_1h ()
end subroutine mixed_func_1f

subroutine breakpt ()
  !$omp declare target(breakpt)
  INTEGER :: sum = 10, dummy = 0
  dummy = dummy + (sum * 10)
end subroutine breakpt

! This subroutine is called from the C++ code.
subroutine mixed_func_1e ()
  !$omp declare target(mixed_func_1e, breakpt)
  call breakpt
end subroutine mixed_func_1e
