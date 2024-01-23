! Copyright 2023-2024 Free Software Foundation, Inc.
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
! along with this program.  If not, see <http://www.gnu.org/licenses/> .

! Source code for func-trampoline.exp.

integer(kind=4) function second(x, y)
  integer(kind=4), intent(in) :: x
  integer(kind=4), intent(in) :: y

  second = x * y ! second-breakpt
end function

integer(kind=4) function first(num1, num2)
  integer(kind=4), intent(in) :: num1
  integer(kind=4), intent(in) :: num2

  first = second (num1 + 4, num2 * 3) ! first-breakpt
end function

program func_trampoline
  integer(kind=4) :: total

  total = first(16, 3) ! main-outer-loc

  write(*,*)  "Result is ", total, "\n"
  ! Expected: 180
end program func_trampoline
