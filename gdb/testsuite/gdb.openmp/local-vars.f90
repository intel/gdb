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

program local_vars
  integer :: glob = 0
  integer, target :: a
  integer*8 :: b = 3
  integer*2 :: c
  integer, pointer :: pa

  !$omp target teams num_teams(1) thread_limit(1) &
  !$omp& map(from:glob) private(a, b, c, pa)
  pa => a                 ! kernel-line-1
  a = 0                   ! kernel-line-2
  c = 2                   ! kernel-line-3
  glob = 5                ! kernel-line-4
  c = glob + a + b        ! kernel-line-5
  a = a + 1               ! kernel-line-6
  pa = 0                  ! kernel-line-7
  a = a + 1               ! kernel-line-8
  !$omp end target teams

  !$omp single
  glob = 10 ! line-after-kernel
  !$omp end single
end program local_vars ! return-stmt
