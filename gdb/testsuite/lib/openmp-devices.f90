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


program devices
  use omp_lib, ONLY : omp_get_default_device, omp_get_num_devices
  external tgt_get_device_name, tgt_get_rtl_name

  integer :: dd, nd, sum, i, ret_val
  integer, parameter :: N = 100

  nd = omp_get_num_devices()
  if(nd < 1) then
    call exit (1)
  end if

  print*, "OpenMP: number of devices is ", nd

  dd = omp_get_default_device()
  if (dd < 0) then
    print*, "omp_get_default_device() call failed with an error code: ", nd
    call exit (1)
  end if

  sum = 0
  !$omp target teams distribute parallel do reduction (+:sum)
    do i = 1, N
      sum = sum + i
    end do
  !$omp end target teams distribute parallel do

end program devices
