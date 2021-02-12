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

program parallel_for_2D
  integer , parameter :: DIM_0 = 128, DIM_1 = 64
  integer :: i, j, in_elem, in_elem2, in_elem3
  integer, dimension(0:DIM_0, 0:DIM_1) :: in_arr, out_arr

  ! Initialize the input
  val = 123
  do i = 0, DIM_0 - 1
    do j = 0, DIM_1 - 1
      in_arr(i,j) = val
      val = val + 1
    end do
  end do

  !$omp target data map(to: in_arr) map(from: out_arr)
  !$omp target teams num_teams(DIM_0) thread_limit(DIM_1)
  !$omp distribute parallel do private(in_elem, in_elem2, in_elem3)
  do i = 0, DIM_0 - 1
    do j = 0, DIM_1 - 1
      in_elem = in_arr(i, j) ! kernel-first-line
      in_elem2 = i
      in_elem3 = j
      ! Negate the value, write into the transpositional location.
      out_arr(j, i) = -1 * in_elem ! kernel-last-line
    end do
  end do
  !$omp end distribute parallel do
  !$omp end target teams
  !$omp end target data

  ! Verify the output.
  do i = 0, DIM_0 - 1
    do j = 0, DIM_1 - 1
      if (in_arr(i, j) .ne. -out_arr(j, i)) then
        write(*,*) "Element (", i, ", ", j, ") is ", out_arr(j, i)
        write(*,*) " but expected is ", in_arr(i, j), "\n"
        call abort
      end if
    end do
  end do

  write(*,*) "Correct", "\n" ! end-marker
end program parallel_for_2D
