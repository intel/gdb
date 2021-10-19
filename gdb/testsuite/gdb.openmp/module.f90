! Copyright 2021 Free Software Foundation, Inc.
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

! Do not use simple single-letter names as GDB can pick up unexpected
! symbols from system libraries.

module module_sub
contains
  subroutine sub1 (val_a)
    !$omp declare target
    integer :: val_a
    val_a = val_a + 10  ! args-check
  end subroutine sub1
end module module_sub

module module_declarations
  integer :: var_d = 11
  integer :: var_c = 12
end module module_declarations

program module_omp
  use module_declarations
  use module_sub

  !$omp target map(tofrom: var_d, var_c)
  !$omp teams num_teams(1) thread_limit(1)
    var_d = var_d           ! locals-check
            
    call sub1 (var_d)
    call sub1 (var_c)
  !$omp end target teams
end program
