! Copyright 2020 Free Software Foundation, Inc.
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
!
! Tests OpenMP Asynchronous target with Tasks.

program multi_kernel_async
  integer :: data1 = 11
  integer :: data2 = 22
  integer :: item, total

  !$omp task shared(item)
  !$omp target teams num_teams(1) thread_limit(1) map(tofrom: data1) &
  !$omp private(item)
    item = data1 + 100
    data1 = item  ! kernel-1-line
  !$omp end target teams
  !$omp end task

  !$omp task shared(item)
  !$omp target teams num_teams(1) thread_limit(1) map(tofrom: data2) &
  !$omp private(item)
    item = data2 + 200
    data2 = item  ! kernel-2-line
  !$omp end target teams
  !$omp end task

  !$omp taskwait
  !$omp single
    total  = data1 + data2; ! post-kernel-line
  !$omp end single
end program multi_kernel_async
