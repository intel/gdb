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

program multi_kernel_async
  integer :: data1 = 11
  integer :: data2 = 22
  integer :: item, total

  !$omp target map(to: data1)
  !$omp teams num_teams(1) thread_limit(1) private(item)
    item = data1 + 100 ! kernel-1-line
  !$omp end teams
  !$omp end target

  !$omp target map(to: data2)
  !$omp teams num_teams(1) thread_limit(1) private(item)
    item = data2 + 200 ! kernel-2-line
  !$omp end teams
  !$omp end target

  total  = data1 + data2; ! post-kernel-line
  !$omp taskwait
end program multi_kernel_async
