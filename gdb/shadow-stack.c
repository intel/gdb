/* Manage a shadow stack pointer for GDB, the GNU debugger.

   Copyright (C) 2024-2026 Free Software Foundation, Inc.
   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "arch-utils.h"
#include "gdbcore.h"
#include "extract-store-integer.h"
#include "frame.h"
#include "frame-unwind.h"
#include "shadow-stack.h"

class regcache;

enum class ssp_update_direction
{
  /* Update ssp towards the oldest (outermost) element of the shadow
     stack.  */
  outer = 0,

  /* Update ssp towards the most recent (innermost) element of the
     shadow stack.  */
  inner
};

/* Return a new shadow stack pointer which is incremented or decremented
   by one element dependent on DIRECTION.  */

static CORE_ADDR
update_shadow_stack_pointer (gdbarch *gdbarch, CORE_ADDR ssp,
			     const ssp_update_direction direction)
{
  bool increment = gdbarch_stack_grows_down (gdbarch)
		   ? direction == ssp_update_direction::outer
		     : direction == ssp_update_direction::inner;

  if (increment)
    return ssp + gdbarch_shadow_stack_element_size_aligned (gdbarch);
  else
    return ssp - gdbarch_shadow_stack_element_size_aligned (gdbarch);
}

/* See shadow-stack.h.  */

void shadow_stack_push (regcache *regcache, const CORE_ADDR new_addr)
{
  gdbarch *gdbarch = regcache->arch ();
  if (!gdbarch_address_in_shadow_stack_memory_range_p (gdbarch)
      || gdbarch_ssp_regnum (gdbarch) == -1)
    return;

  bool shadow_stack_enabled;
  std::optional<CORE_ADDR> ssp
    = gdbarch_get_shadow_stack_pointer (gdbarch, regcache,
					shadow_stack_enabled);
  if (!ssp.has_value () || !shadow_stack_enabled)
    return;

  const CORE_ADDR new_ssp
    = update_shadow_stack_pointer (gdbarch, *ssp,
				   ssp_update_direction::inner);

  /* If NEW_SSP does not point to shadow stack memory, we assume the
     stack is full.  */
  if (!gdbarch_address_in_shadow_stack_memory_range (gdbarch,
						     new_ssp,
						     nullptr))
    error (_("No space left on the shadow stack."));

  /* On x86 there can be a shadow stack token at bit 63.  For x32, the
     address size is only 32 bit.  Always write back the full element
     size to include the shadow stack token.  */
  const int element_size
    = gdbarch_shadow_stack_element_size_aligned (gdbarch);

  const bfd_endian byte_order = gdbarch_byte_order (gdbarch);

  write_memory_unsigned_integer (new_ssp, element_size, byte_order,
				 (ULONGEST) new_addr);

  regcache_raw_write_unsigned (regcache,
			       gdbarch_ssp_regnum (gdbarch),
			       new_ssp);
}

/* See shadow-stack.h.  */

value *
dwarf2_prev_ssp (const frame_info_ptr &this_frame, void **this_cache,
		 int regnum)
{
  value *v = frame_unwind_got_register (this_frame, regnum, regnum);
  gdb_assert (v != nullptr);

  gdbarch *gdbarch = get_frame_arch (this_frame);

  if (gdbarch_address_in_shadow_stack_memory_range_p (gdbarch)
      && v->entirely_available () && !v->optimized_out ())
    {
      const int size = register_size (gdbarch, regnum);
      bfd_endian byte_order = gdbarch_byte_order (gdbarch);
      CORE_ADDR ssp = extract_unsigned_integer
	(v->contents_all ().data (), size, byte_order);

      /* Only if the current shadow stack pointer SSP points to shadow
	 stack memory a valid previous shadow stack pointer can be
	 calculated.  */
      std::pair<CORE_ADDR, CORE_ADDR> range;
      if (gdbarch_address_in_shadow_stack_memory_range (gdbarch, ssp, &range))
	{
	  /* Note that a shadow stack memory range can change, due to
	     shadow stack switches for instance on x86 for an inter-
	     privilege far call or when calling an interrupt/exception
	     handler at a higher privilege level.  Shadow stack for
	     userspace is supported for amd64 linux starting with
	     Linux kernel v6.6.  However, shadow stack switches are not
	     supported due to missing kernel space support.  We therefore
	     implement this unwinder without support for shadow stack
	     switches for now.  */
	  const CORE_ADDR new_ssp
	    = update_shadow_stack_pointer (gdbarch, ssp,
					   ssp_update_direction::outer);

	  /* On x86, if NEW_SSP points to the end outside of RANGE
	     (NEW_SSP == RANGE.SECOND), it indicates that NEW_SSP is
	     valid, but the shadow stack is empty.  In contrast, for
	     ARM's Guarded Control Stack, if NEW_SSP points to the end
	     of RANGE, it means that the shadow stack feature is
	     disabled.  */
	  bool is_top_addr_empty_shadow_stack
	    = gdbarch_top_addr_empty_shadow_stack_p (gdbarch)
	      && gdbarch_top_addr_empty_shadow_stack (gdbarch, new_ssp, range);

	  /* Validate NEW_SSP.  This may depend on both
	     IS_TOP_ADDR_EMPTY_SHADOW_STACK and the gdbarch hook (e.g., x86),
	     or on the hook only (e.g., ARM).  */
	  if (is_top_addr_empty_shadow_stack
	      || gdbarch_address_in_shadow_stack_memory_range (gdbarch,
							       new_ssp,
							       &range))
	    return frame_unwind_got_address (this_frame, regnum, new_ssp);
	}
    }

  /* Return a value which is marked as unavailable, in case we could not
     calculate a valid previous shadow stack pointer.  */
  value *retval
    = value::allocate_register (get_next_frame_sentinel_okay (this_frame),
				regnum, register_type (gdbarch, regnum));
  retval->mark_bytes_unavailable (0, retval->type ()->length ());
  return retval;
}
