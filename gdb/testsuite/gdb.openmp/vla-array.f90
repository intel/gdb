! Copyright 2009-2021 Free Software Foundation, Inc.
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

subroutine vla_array_func (arr_vla1, arr_vla2, arr2)
  !$omp declare target (vla_array_func)
  character (len=*):: arr_vla1 (:)
  character (len=*):: arr_vla2
  character (len=9):: arr2 (:)

  arr_vla2 = "aryvla"             ! breakpt
end subroutine vla_array_func

program vla_array_main
interface
  subroutine vla_array_func (arr_vla1, arr_vla2, arr2)
    !$omp declare target (vla_array_func)
    character (len=*):: arr_vla1 (:)
    character (len=*):: arr_vla2
    character (len=9):: arr2 (:)
  end subroutine vla_array_func
end interface
  character (len=9) :: arr1 (3)
  character (len=6) :: arr2
  character (len=12) :: arr3 (5)

  arr1 = 'vlaaryvla'
  arr2 = 'vlaary'
  arr3 = 'vlaaryvlaary'

  !$omp target map(tofrom: arr1, arr2, arr3)
  !$omp teams num_teams(1) thread_limit(1)
    call vla_array_func (arr3, arr2, arr1)
  !$omp end target teams
end program vla_array_main
