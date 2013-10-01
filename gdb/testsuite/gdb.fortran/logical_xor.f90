! Copyright 2013 Free Software Foundation, Inc.
!
! Contributed by Intel Corp. <christoph.t.weinmann@intel.com>
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

subroutine sub
  logical :: val_a = .true., val_b = .false.
  logical val_c

  val_c = xor(val_a, val_b)
  return    !stop_here
end

program prog
  implicit none
  call sub
end
