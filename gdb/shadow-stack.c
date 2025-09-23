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
#include "annotate.h"
#include "stack.h"
#include "solib.h"
#include "event-top.h"
#include "cli/cli-style.h"

class regcache;
struct backtrace_cmd_options;

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
			     const unsigned int count,
			     const ssp_update_direction direction)
{
  bool increment = gdbarch_stack_grows_down (gdbarch)
		   ? direction == ssp_update_direction::outer
		     : direction == ssp_update_direction::inner;

  const int element_size
    = gdbarch_shadow_stack_element_size_aligned (gdbarch);
  if (increment)
    return ssp + count * element_size;
  else
    return ssp - count * element_size;
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
    = update_shadow_stack_pointer (gdbarch, *ssp, 1,
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
	    = update_shadow_stack_pointer (gdbarch, ssp, 1,
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

/* Return true, if PC is in the middle of a statement.  Note that in the
   middle of a statement PC range includes sal.end (SAL.PC, SAL.END].
   Return false, if
   - SAL.IS_STMT is false
   - there is no location information associated with this SAL, which
   could happen in case of inlined functions
   - PC is not in the range (SAL.PC, SAL.END].
   This function is similar to stack.c:frame_show_address but is used
   to determine if we are in the middle of a statement only, not to decide
   if we should print a frame's address.  */

static bool
pc_in_middle_of_statement (CORE_ADDR pc, symtab_and_line sal)
{
  if (sal.is_stmt == false)
    return false;

  /* If there is a line number, but no PC, then there is no location
     information associated with this sal.  The only way that should
     happen is for the call sites of inlined functions (SAL comes from
     find_sal_for_pc).  Otherwise, we would have some PC range if the SAL
     came from a line table.  However, as we don't have a frame for this
     function we cannot assert (in contrast to frame_show_address).  */
  if (sal.line != 0 && sal.pc == 0 && sal.end == 0)
    return false;

  return pc > sal.pc && pc <= sal.end;
}

/* Attempt to obtain the function name based on the symbol of PC.  */

static gdb::unique_xmalloc_ptr<char>
find_pc_funname (CORE_ADDR pc)
{
  symbol *func = find_pc_function (pc);
  if (func != nullptr)
    return find_symbol_funname (func);

  gdb::unique_xmalloc_ptr<char> funname;
  bound_minimal_symbol msymbol = lookup_minimal_symbol_by_pc (pc);
  if (msymbol.minsym != nullptr)
    funname = make_unique_xstrdup (msymbol.minsym->print_name ());

  return funname;
}

/* Print information of shadow stack frame info FRAME.  The output is
   formatted according to PRINT_WHAT.  For the meaning of PRINT_WHAT, see
   enum print_what comments in frame.h.  Note that PRINT_WHAT is overridden,
   if PRINT_OPTIONS.print_frame_info != print_frame_info_auto.  */

static void
do_print_shadow_stack_frame_info
  (ui_out *uiout, gdbarch *gdbarch,
   const frame_print_options &fp_opts,
   const shadow_stack_frame_info &frame, print_what print_what)
{
  std::string frame_type;
  if (gdbarch_is_no_return_shadow_stack_address_p (gdbarch)
      && gdbarch_is_no_return_shadow_stack_address (gdbarch,
						    frame,
						    frame_type))
    {
      gdb_assert (!frame_type.empty ());

      /* It is possible, for the x86 architecture for instance, that an
	 element on the shadow stack is not a return address.  We still
	 want to print the address in that case but no further
	 information.  */
      ui_out_emit_tuple tuple_emitter (uiout, "shadow-stack-frame");
      uiout->text ("#");
      uiout->field_fmt_signed (2, ui_left, "level", frame.level);

      uiout->field_string
	("func", frame_type, metadata_style.style ());
      uiout->text ("\n");
      gdb_flush (gdb_stdout);
      return;
    }

  if (fp_opts.print_frame_info != print_frame_info_auto)
    {
      /* Use the specific frame information desired by the user.  */
      print_what
	= *print_frame_info_to_print_what (fp_opts.print_frame_info);
    }

  /* In contrast to find_frame_sal which is used for the ordinary backtrace
     command, PC always points at the return instruction (which is *after* the
     call instruction).  Since we want to get the line containing the call
     (because the call is where the user thinks the program is), we pass 1 here
     as second argument.  */
  symtab_and_line sal = find_pc_line (frame.value, 1);

  if (should_print_location (print_what) || sal.symtab == nullptr)
    {
      gdb::unique_xmalloc_ptr<char> funname = find_pc_funname (frame.value);

      { /* Extra scope to print frame tuple.  */
	ui_out_emit_tuple tuple_emitter (uiout, "shadow-stack-frame");

	annotate_shadowstack_frame_begin (frame.level, gdbarch,
					  frame.value);

	uiout->text ("#");
	uiout->field_fmt_signed (2, ui_left, "level", frame.level);

	annotate_shadowstack_frame_address ();

	/* On x86 there can be a shadow stack token at bit 63.  For x32,
	   the address size is only 32 bit.  Thus, we still must use
	   gdbarch_shadow_stack_element_size_aligned (and not
	   gdbarch_addr_bit) to determine the width of the address to be
	   printed.  */
	const int element_size
	 = gdbarch_shadow_stack_element_size_aligned (gdbarch);

	uiout->field_string
	  ("addr", hex_string_custom (frame.value, element_size * 2),
	   address_style.style ());

	annotate_shadowstack_frame_address_end ();

	uiout->text (" in ");
	print_funname (uiout, funname, true);

	if (print_what != SHORT_LOCATION && sal.symtab != nullptr)
	  print_filename (uiout, sal, true);

	if (print_what != SHORT_LOCATION
	    && (funname == nullptr || sal.symtab == nullptr)
	    && sal.pspace != nullptr)
	  {
	    const char *lib = solib_name_from_address (sal.pspace,
						       frame.value);
	    if (lib != nullptr)
	      print_lib (uiout, lib, true);
	  }
      } /* Extra scope to print frame tuple.  */

      uiout->text ("\n");
    }

  if (print_what == SRC_LINE || print_what == SRC_AND_LOC)
    {
      bool mid_statement = pc_in_middle_of_statement (frame.value, sal);

      /* While for the ordinary backtrace printing of pc is based on
	 MID_STATEMENT determined by stack.c:frame_show_address and the
	 and the print configuration, for shadow stack backtrace we always
	 print the pc/address on the shadow stack.  */
      bool print_address = true;
      print_source (uiout, gdbarch, frame.value, sal, print_address,
		    mid_statement, "");
    }

  annotate_shadowstack_frame_end ();
  gdb_flush (gdb_stdout);
}

/* Redirect output to a temporary buffer for the duration of
   do_print_shadow_stack_frame_info.  */

static void
print_shadow_stack_frame_info
  (gdbarch *gdbarch, const frame_print_options &fp_opts,
   const shadow_stack_frame_info &frame, print_what print_what)
{
  do_with_buffered_output
    (do_print_shadow_stack_frame_info, current_uiout, gdbarch,
     fp_opts, frame, print_what);
}

/* Extract a char array which can be used for printing a reasonable
   error message for REASON.  REASON must not be NO_ERROR if passed
   to this function.  */

static const char *
ssp_unwind_stop_reason_to_err_string (ssp_unwind_stop_reason reason)
{
  switch (reason)
    {
    case ssp_unwind_stop_reason::memory_read_error:
      return _("shadow stack memory read failure");
    }

  gdb_assert_not_reached ("invalid unwind stop reason.");
}

/* Read the memory at shadow stack pointer SSP and assign it to
   RETURN_VALUE.  In case we cannot read the memory, set REASON to
   ssp_unwind_stop_reason::memory_read_error and return false.  */

static bool
read_shadow_stack_memory (gdbarch *gdbarch, CORE_ADDR ssp,
			  CORE_ADDR &return_value,
			  ssp_unwind_stop_reason *reason)
{
  /* On x86 there can be a shadow stack token at bit 63.  For x32, the
     address size is only 32 bit.  Thus, we still must use
     gdbarch_shadow_stack_element_size_aligned (and not gdbarch_addr_bit)
     to read the full element for x32 as well.  */
  const int element_size
    = gdbarch_shadow_stack_element_size_aligned (gdbarch);

  const bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  if (!safe_read_memory_unsigned_integer (ssp, element_size, byte_order,
					  &return_value))
    {
      *reason = ssp_unwind_stop_reason::memory_read_error;
      return false;
    }

  return true;
}

/*  If possible, return the starting shadow stack frame info needed to handle
    COUNT outermost frames.  FRAME should point to the innermost (newest)
    element of the shadow stack.  RANGE is the shadow stack memory range
    [start_address, end_address) corresponding to FRAME's shadow stack pointer.
    If COUNT is bigger than the number of elements on the shadow stack, return
    FRAME.  In case of failure, assign an appropriate ssp_unwind_stop_reason in
    FRAME->UNWIND_stop_REASON.  */

static std::optional<shadow_stack_frame_info>
get_trailing_outermost_shadow_stack_frame_info
  (gdbarch *gdbarch, const std::pair<CORE_ADDR, CORE_ADDR> range,
   const ULONGEST count, shadow_stack_frame_info &frame)
{
  gdb_assert (gdbarch_get_shadow_stack_size_p (gdbarch));

  const long shadow_stack_size
    = gdbarch_get_shadow_stack_size (gdbarch,
				     std::optional<CORE_ADDR> (frame.ssp),
				     range);

  /* We should only get here in case shadow stack is enabled for the
     current thread.  */
  gdb_assert (shadow_stack_size >= 0);

  const long level = shadow_stack_size - count;

  /* COUNT exceeds the number of elements on the shadow stack.  Return the
     starting shadow stack frame info FRAME.  */
  if (level <= 0)
    return std::optional<shadow_stack_frame_info> (frame);

  CORE_ADDR new_ssp = update_shadow_stack_pointer
    (gdbarch, frame.ssp, level, ssp_update_direction::outer);

  if (gdbarch_stack_grows_down (gdbarch))
    gdb_assert (new_ssp < range.second);
  else
    gdb_assert (new_ssp >= range.first);

  CORE_ADDR new_value;
  if (!read_shadow_stack_memory (gdbarch, new_ssp, new_value,
				 &frame.unwind_stop_reason))
    return {};

  return std::optional<shadow_stack_frame_info>
    ({new_ssp, new_value, (unsigned long) level,
      ssp_unwind_stop_reason::no_error});
}

std::optional<shadow_stack_frame_info>
shadow_stack_frame_info::unwind_prev_shadow_stack_frame_info
  (gdbarch *gdbarch, std::pair<CORE_ADDR, CORE_ADDR> range)
{
  /* If the user's backtrace limit has been exceeded, stop.  We must
     add two to the current level; one of those accounts for
     backtrace_limit being 1-based and the level being 0-based, and the
     other accounts for the level of the new frame instead of the level
     of the current frame.  */
  if (this->level + 2 > user_set_backtrace_options.backtrace_limit)
    return {};

  CORE_ADDR new_ssp
    = update_shadow_stack_pointer (gdbarch, this->ssp, 1,
				   ssp_update_direction::outer);

   if (gdbarch_top_addr_empty_shadow_stack_p (gdbarch)
       && gdbarch_top_addr_empty_shadow_stack (gdbarch, new_ssp, range))
     return {};
   else if (gdbarch_stack_grows_down (gdbarch))
    {
      /* The shadow stack grows downwards.  */
      if (new_ssp >= range.second)
	{
	  /* We reached the outermost element of the shadow stack.  */
	  return {};
	}
      /* We updated new_ssp towards the outermost element of the shadow
	 stack before, and new_ssp must be pointing to shadow stack
	 memory.  */
      gdb_assert (new_ssp > range.first);
    }
  else
    {
      /* The shadow stack grows upwards.  */
      if (new_ssp < range.first)
	{
	  /* We reached the outermost element of the shadow stack.  */
	  return {};
	}
      /* We updated new_ssp towards the outermost element of the shadow
	 stack before, and new_ssp must be pointing to shadow stack
	 memory.  */
      gdb_assert (new_ssp <= range.second);
    }

  CORE_ADDR new_value;
  if (!read_shadow_stack_memory (gdbarch, new_ssp, new_value,
				 &this->unwind_stop_reason))
    return {};

  return std::optional<shadow_stack_frame_info>
    ({new_ssp, new_value, this->level + 1, ssp_unwind_stop_reason::no_error});
}

/* Print all elements on the shadow stack or just the innermost COUNT_EXP
   frames.  */

void
backtrace_shadow_command (const frame_print_options &fp_opts,
			  const char *count_exp, int from_tty)
{
  if (!target_has_stack ())
    error (_("No shadow stack."));

  gdbarch *gdbarch = get_current_arch ();
  if (!gdbarch_address_in_shadow_stack_memory_range_p (gdbarch)
      || !gdbarch_top_addr_empty_shadow_stack_p (gdbarch)
      || !gdbarch_get_shadow_stack_size_p (gdbarch)
      || gdbarch_ssp_regnum (gdbarch) == -1)
    error (_("Printing of the shadow stack backtrace is not supported for"
	     " the current target."));

  regcache *regcache = get_thread_regcache (inferior_thread ());
  bool shadow_stack_enabled = false;

  std::optional<CORE_ADDR> start_ssp
    = gdbarch_get_shadow_stack_pointer (gdbarch, regcache,
					shadow_stack_enabled);

  if (!start_ssp.has_value () || !shadow_stack_enabled)
    error (_("Shadow stack is not enabled for the current thread."));

  /* Check if START_SSP points to a shadow stack memory range and use
     the returned range to determine when to stop unwinding.
     Note that a shadow stack memory range can change, due to shadow stack
     switches for instance on x86 for an inter-privilege far call or when
     calling an interrupt/exception handler at a higher privilege level.
     Shadow stack for userspace is supported for amd64 linux starting with
     Linux kernel v6.6.  However, shadow stack switches are not supported
     due to missing kernel space support.  We therefore implement this
     command without support for shadow stack switches for now.  */
  bool is_top_addr_empty_shadow_stack = false;
  std::pair<CORE_ADDR, CORE_ADDR> range;
  if (!gdbarch_address_in_shadow_stack_memory_range (gdbarch, *start_ssp,
						     &range))
    {
      /* For x86, if the current shadow stack pointer does not point to
	 shadow stack memory but is valid, the shadow stack is empty.  */
      is_top_addr_empty_shadow_stack = true;
    }

  if (!is_top_addr_empty_shadow_stack)
    {
      /* For ARM's Guarded Control Stack, the shadow stack can be empty
	 even though START_SSP points to shadow stack memory range.  */
      is_top_addr_empty_shadow_stack
	= gdbarch_top_addr_empty_shadow_stack_p (gdbarch)
	  && gdbarch_top_addr_empty_shadow_stack (gdbarch, *start_ssp, range);
    }

  if (is_top_addr_empty_shadow_stack)
    {
      gdb_printf (_("The shadow stack is empty.\n"));
      return;
    }

  /* Extract the first shadow stack frame info (level 0).  */
  ssp_unwind_stop_reason reason = ssp_unwind_stop_reason::no_error;
  std::optional<shadow_stack_frame_info> current;
  CORE_ADDR new_value;

  if (read_shadow_stack_memory (gdbarch, *start_ssp, new_value, &reason))
    current = {*start_ssp, new_value, 0, ssp_unwind_stop_reason::no_error};

  std::optional<shadow_stack_frame_info> trailing = current;

  LONGEST count = -1;
  if (current.has_value () && count_exp != nullptr)
    {
      count = parse_and_eval_long (count_exp);
      /* If count is negative, update trailing with the shadow stack frame
	 info from which we should start printing.  */
      if (count < 0)
	{
	  trailing = get_trailing_outermost_shadow_stack_frame_info
		       (gdbarch, range, std::abs (count), *current);

	  if (!trailing.has_value ())
	    reason = current->unwind_stop_reason;
	}
    }

  if (!trailing.has_value ())
    {
      gdb_assert (reason != ssp_unwind_stop_reason::no_error);

      error (_("Cannot print shadow stack backtrace: %s.\n"),
	     ssp_unwind_stop_reason_to_err_string (reason));
    }

  current = trailing;
  for (current = trailing; current.has_value () && count != 0; count--)
    {
      QUIT;

      print_shadow_stack_frame_info (gdbarch, fp_opts, *current, LOCATION);

      trailing = current;
      current = current->unwind_prev_shadow_stack_frame_info (gdbarch, range);
    }

  /* If we've stopped before the end, mention that.  */
  if (current.has_value () && from_tty)
    gdb_printf (_("(More shadow stack frames follow...)\n"));

  /* Due to the loop above, trailing always has a value at this point.  */
  gdb_assert (trailing.has_value ());

  /* If we've run out of shadow stack frames, and the reason appears to
     be an error condition, print it.  */
  if (!current.has_value ()
      && trailing->unwind_stop_reason > ssp_unwind_stop_reason::no_error)
    gdb_printf (_("Shadow stack backtrace stopped at shadow stack " \
		  "pointer %s due to: %s.\n"),
		paddress (gdbarch, trailing->ssp),
		ssp_unwind_stop_reason_to_err_string
		  (trailing->unwind_stop_reason));
}

