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
! along with this program.  If not, see <http://www.gnu.org/licenses/> .

!DIR$ ATTRIBUTES NOINLINE  :: fourth
function fourth (x4, y4) result(z4) ! entry-fourth-loc
  !$omp declare target (fourth)
  integer, intent(in) :: x4, y4
  integer :: z4
  z4 = x4 * y4  ! ordinary-fourth-loc
end function fourth

function third (x3, y3) result(z3)
  !$omp declare target (third, fourth)
  integer, intent(in) :: x3, y3
  integer :: z3
  z3 = fourth (x3 + 5, y3 * 3) + 30 ! ordinary-third-loc
end function third

function second (x2, y2) result(z2)
  !$omp declare target (second, third)
  integer, intent(in) :: x2, y2
  integer :: z2
  z2 = third (x2 + 5, y2 * 3) + 30 ! ordinary-second-loc
end function second

function first (x1, y1) result(z1)
  !$omp declare target (first, second)
  integer, intent(in) :: x1, y1
  integer  :: z1
  z1 = second (x1 + 5, y1 * 3) ! ordinary-first-loc
  z1 = z1 + 30 ! kernel-function-return
end function first

!DIR$ ATTRIBUTES FORCEINLINE :: inlined_second
function inlined_second (x, y) result(z)
  !$omp declare target (inlined_second)
  integer, intent(in):: x, y
  integer, volatile :: z

  z = x * y  ! inlined-inner-loc
end function inlined_second

!DIR$ ATTRIBUTES FORCEINLINE :: inlined_first
function inlined_first (num1, num2) result(total)
  !$omp declare target (inlined_first, inlined_second)
  integer, intent(in) :: num1, num2
  integer :: total
  total = inlined_second (num1 + 4, num2 * 3) ! inlined-middle-loc
  total = total + 30
end function inlined_first

program call_stack
  integer, dimension(0:2) :: data = (/ 7, 8, 9 /)
  integer :: ten, five, fifteen ! ten-d1 five-d1 fifteen-d1

  !$omp target teams num_teams(1) thread_limit(1) &
  !$omp& map(tofrom: data) private(ten, five, fifteen)
    ten = data(1) + 2
    five = data(2) - 4
    fifteen = ten + five
    data(0) = first (fifteen + 1, 3) ! ordinary-outer-loc
    data(1) = inlined_first (10, 2) ! inlined-outer-loc
    data(2) = first (3, 4); ! another-call
  !$omp end target teams

  !$omp single
    write(*,*)  "Result is ", data(0), " ", data(1), " ", data(2), "\n"
    ! Expected: 210 114 114
  !$omp end single

end program call_stack
