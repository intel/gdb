! Copyright 2020-2021 Free Software Foundation, Inc.
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

function update_val (val, offset) result (r)
  !$omp declare target (update_val)
  integer, intent(in) :: val, offset
  integer :: r
  r = val + offset
end function

program parallel_for_1D
  integer , parameter :: DIM0 = 1024
  integer :: i, in_elem, in_elem2, in_elem3
  integer, dimension(0:DIM0) :: in_arr, out_arr

  ! Initialize the input
  do i = 0, DIM0 - 1
    in_arr(i) = i + 123
  end do

  !$omp target teams map(to: in_arr) map(from: out_arr)
  !$omp distribute parallel do private(in_elem, in_elem2, in_elem3)
  do i = 0, DIM0 - 1
    in_elem = update_val (in_arr(i), 100); ! kernel-first-line
    in_elem2 = in_arr(i) + 200; ! kernel-second-line
    in_elem3 = in_elem + 300;
    out_arr(i) = in_elem; ! kernel-last-line
  end do
  !$omp end distribute parallel do
  !$omp end target teams

  ! Verify the output.
  do i = 0,  DIM0 - 1
    if (out_arr(i) .ne. in_arr(i) + 100) then
      write(*,*) "Element ", i, " is ", out_arr(i)
      write(*,*) " but expected is ", (in_arr(i) + 100), "\n"
      call abort
    end if
  end do

  write(*,*) "Correct", "\n" ! end-marker
end program parallel_for_1D
