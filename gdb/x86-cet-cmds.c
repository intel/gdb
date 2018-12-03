/* Control-flow Enforcement Technology Command Set for GDB, the GNU debugger.

   Copyright (C) 2018-2022 Free Software Foundation, Inc.

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
#include "command.h"
#include "cli/cli-cmds.h"
#include "gdbsupport/errors.h"
#include "inferior.h"
#include "i386-tdep.h"
#include "gdbarch.h"
#include "gdbcore.h"
#include "ui-out.h"
#include "memrange.h"
#include "utils.h"
#include "valprint.h"

#undef getline
#include <string>
#include <vector>

/* Print the information from the CET MSR and the SSP.  */

static void
print_cet_status (const CORE_ADDR *ssp, const uint64_t *cet_msr)
{
  const int ncols = 2, nrows = 10;

  const std::vector<std::string> names
    = { "Shadow Stack:", "Shadow Stack Pointer:",
	"WR_SHSTK_EN:",  "Indirect Branch Tracking:",
	"TRACKER:",      "LEG_IW_EN:",
	"NO_TRACK_EN:",  "SUPRESS_DIS:",
	"SUPRESS:",      "EB_LEG_BITMAP_BASE:" };

  const std::vector<std::string> values
    = { (*cet_msr & MSR_CET_SHSTK_EN) ? "enabled" : "disabled",
	hex_string_custom (*ssp, 12),
	(*cet_msr & MSR_CET_WR_SHSTK_EN) ? "enabled" : "disabled",
	(*cet_msr & MSR_CET_ENDBR_EN) ? "enabled" : "disabled",
	(*cet_msr & MSR_CET_TRACKER) ? "WAIT_FOR_ENDBRANCH" : "IDLE",
	(*cet_msr & MSR_CET_LEG_IW_EN) ? "enabled" : "disabled",
	(*cet_msr & MSR_CET_NO_TRACK_EN) ? "enabled" : "disabled",
	(*cet_msr & MSR_CET_SUPPRESS_DIS) ? "enabled" : "disabled",
	(*cet_msr & MSR_CET_SUPPRESS) ? "enabled" : "disabled",
	hex_string_custom (*cet_msr & MSR_CET_EB_LEG_BITMAP_BASE, 12) };

  ui_out_emit_table table_emitter (current_uiout, ncols, nrows, "cet-status");

  current_uiout->table_header (25, ui_left, "name", "Target Id:");
  current_uiout->table_header (33, ui_left, "value",
			       target_pid_to_str (inferior_ptid));
  current_uiout->table_body ();

  for (int i = 0; i < nrows; ++i)
    {
      ui_out_emit_tuple tuple_emitter (current_uiout, nullptr);
      current_uiout->field_string ("name", names.at (i).c_str ());
      current_uiout->field_string ("value", values.at (i).c_str ());
      current_uiout->text ("\n");
    }
}

/* Get the CET specific registers.  Print the reason in case CET registers are
   not available.  */

static bool
cet_get_registers (CORE_ADDR *ssp, uint64_t *cet_msr)
{
  if (!target_has_execution ())
    error (_("No current process: you must name one."));

  regcache *regcache = get_current_regcache ();
  const i386_gdbarch_tdep *tdep
    = (i386_gdbarch_tdep *) gdbarch_tdep (regcache->arch ());

  if (tdep == nullptr || tdep->cet_msr_regnum < 0)
    {
      printf_filtered (_("CET is not supported by the current target.\n"));
      return false;
    }

  if (regcache_raw_read_unsigned
       (regcache, tdep->cet_msr_regnum, (ULONGEST *) cet_msr)
      != REG_VALID)
    {
      /* In case we have HW support and the registers are not available we
      assume that the kernel does not support CET.  */
      printf_filtered (_("CET is not supported by the current kernel.\n"));
      return false;
    }

  if (tdep->ssp_regnum > 0)
    {
      if (regcache_raw_read_unsigned (regcache, tdep->ssp_regnum, ssp)
	  != REG_VALID)
	{
	  printf_filtered (_("CET shadow stack is not supported by the current"
			     " kernel.\n"));
	  return false;
	}
    }

  return true;
}

/* The "info cet status" command.  */

static void
cet_status_cmd (const char *args, int from_tty)
{
  uint64_t cet_msr = 0x0;
  CORE_ADDR ssp = 0x0;
  if (!cet_get_registers (&ssp, &cet_msr))
    return;

  print_cet_status (&ssp, &cet_msr);
}

/* Represents a frame in shadow stack.
   Shadow stack frames contain the Program Counter (PC).  Far-calls additionally
   store the Code Segment (CS) and the current Shadow Stack Pointer (SPP).  */

struct shstk_frame_info
{
  /* The code segment register.  */
  CORE_ADDR cs = 0;

  /* The program control register.  */
  CORE_ADDR pc = 0;

  /* The shadow stack pointer address.  */
  CORE_ADDR ssp = 0;
};

/* Retrieve the shadow stack frame value SHSTK_INFO at SHSTK_ADDR and unwind
   the shadow stack by setting the SHSTK_ADDR to the previous frame.  */

static bool
cet_unwind_shstk_frame (const mem_range *shstk_mem_range,
			CORE_ADDR *shstk_addr, shstk_frame_info *shstk_info)
{
  /* Check against memory range SHSTK_MEM_RANGE of shadow stack address
     space.  */
  if (!address_in_mem_range (*shstk_addr, shstk_mem_range))
    return false;

  struct gdbarch *gdbarch = target_gdbarch ();
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);

  const int shstk_addr_byte_align = gdbarch_shstk_addr_byte_align (gdbarch);
  const int addr_size = gdbarch_addr_bit (gdbarch) / TARGET_CHAR_BIT;

  /* Reading the memory at the shadow stack pointer address.
     We create a copy of the SHSTK_ADDR to keep the original value
     intact in case of failure in unwinding process.  */
  CORE_ADDR ssp_addr = *shstk_addr, ssp_val;
  if (!safe_read_memory_unsigned_integer (ssp_addr, addr_size, byte_order,
					  &ssp_val))
    {
      warning (_("Unable to read the memory address %lx in shadow stack."),
	       ssp_addr);
      return false;
    }

  /* In case of a far-call, CS, PC and the current shadow stack pointer
     are pushed on the shadow stack while for a near-call only the PC value is
     pushed.  Hence, to distinguish between far-call and near-call frames,
     we check if the value of current shadow stack pointer is equivalent to
     the possible previous shadow stack pointer
     (ssp_addr + 3 * shstk_addr_byte_align).  */
  CORE_ADDR pc_val = 0, cs_val = 0;
  if (ssp_val == (ssp_addr + 3 * shstk_addr_byte_align))
    {
      /* Read the PC value.  */
      ssp_addr += shstk_addr_byte_align;
      if (!safe_read_memory_unsigned_integer (ssp_addr, addr_size,
					      byte_order, &pc_val))
	{
	  warning (_("Unable to read the memory address %lx in shadow stack."),
		   ssp_addr);
	  return false;
	}

      /* Read the CS value.  */
      ssp_addr += shstk_addr_byte_align;
      if (!safe_read_memory_unsigned_integer (ssp_addr, addr_size,
					      byte_order, &cs_val))
	{
	  warning (_("Unable to read the memory address %lx in shadow stack."),
		   ssp_addr);
	  return false;
	}
    }
  else
    {
      /* Set the PC value.  */
      pc_val = ssp_val;
    }

  /* Store the shadow stack frame info.  */
  shstk_info->cs = cs_val;
  shstk_info->pc = pc_val;
  shstk_info->ssp = *shstk_addr;

  /* Update the shadow stack pointer to point to the previous frame.
     After unwinding the innermost frame, the SSP_ADDR will point to
     boundary of SHSTK_MEM_RANGE and therefore the next unwinding call will
     fail.  */
  *shstk_addr = ssp_addr + shstk_addr_byte_align;

  return true;
}

/* Print the symbolic representation (symbol name, file name and line number)
   of ADDR under the label FIELD_LABEL in current_uiout.  */
static void
print_cet_address_symbolic (const CORE_ADDR addr,
			    const std::string& field_label)
{
  /* Symbol name at ADDR.  */
  std::string name;

  /* File name and line number of the symbol at ADDR.  */
  std::string filename;
  int unmapped = 0, offset = 0, line = 0;

  /* Read the symbol info at ADDR.  */
  if (build_address_symbolic (target_gdbarch (), addr, true, true, &name,
			      &offset, &filename, &line, &unmapped)
      == 0)
    {
      if (!filename.empty ())
	{
	  /* Print the file name if one is found.  */
	  if (line != -1)
	    {
	      /* Print the line number if one is found.  */
	      current_uiout->field_fmt (field_label.c_str (), "%s at %s:%d",
					name.c_str (), filename.c_str (), line);
	    }
	  else
	    current_uiout->field_fmt (field_label.c_str (), "%s in %s",
				      name.c_str (), filename.c_str ());
	}
      else
	{
	  /* Print only the symbol name if no file name is found.  */
	  current_uiout->field_fmt (field_label.c_str (), "%s", name.c_str ());
	}
    }
  else
    current_uiout->field_string (field_label.c_str (), "<unavailable>");

  return;
}

/* Print the shadow stack backtrace.  */

static void
print_cet_shstk_backtrace ()
{
  /* Read the current shadow stack pointer address SSP.  */
  CORE_ADDR ssp;
  i386_cet_get_shstk_pointer (target_gdbarch (), &ssp);

  /* Read the memory range allocated for the shadow stack.
     The range is used as a stop criteria for unwinding process.
     The memory range is read once and passed as arguments to unwinding
     function to avoid repetitive calculation of the range in each unwinding
     call.  */
  mem_range shstk_mem_range;
  if (!i386_cet_get_shstk_mem_range (ssp, &shstk_mem_range))
    {
      warning (_("Unable to get the shadow stack address range!"));
      return;
    }

  /* The first 64-bit value of shadow stack address space is the supervisor
     shadow stack token.  We skip it for unwinding the shadow stack since it
     is setup by the supervisor when creating the shadow stacks to be used on
     inter-privilege call transfers.  */
  shstk_mem_range.length -= 0x8;

  /* Unwind the first frame.  */
  shstk_frame_info frame;
  if (!cet_unwind_shstk_frame (&shstk_mem_range, &ssp, &frame))
    {
      printf_filtered (_("No shadow stack frame to print.\n"));
      return;
    }

  /* Setup the table header: Three columns for shadow stack frame's level, value
     and symbolic address of the value.  Note that we do not use
     ui_out_emit_table here, as we unwind each frame and then print immediately.
     Due to that the total number of rows is not known beforehand.  */
  ui_out_emit_tuple tuple_emitter_header (current_uiout, "shstk-bt-header");

  /* Level column.  */
  current_uiout->text ("   ");

  /* Address value column.  */
  const int addr_len = gdbarch_addr_bit (target_gdbarch ()) <= 32 ? 10 : 18;
  const std::string address_header = "Address";
  const std::string wsp_width (addr_len - address_header.length () + 1, ' ');
  current_uiout->text (address_header + wsp_width);

  /* Symbolic address column.  */
  current_uiout->text ("Symbol\n");

  /* Unwind the shadow stack and print each frame until we reach the boundaries
     of SHSTK_MEM_RANGE memory region.  After each iteration SSP will point to
     the beginning of the next shadow stack frame.  */
  unsigned int level = 0;
  do {
       ui_out_emit_tuple tuple_emitter (current_uiout, "frame");

       /* Print the frame level.  */
       current_uiout->text ("#");
       current_uiout->field_fmt_signed (1, ui_left, "level", level);

       /* Print the shadow stack's value.  */
       std::string pc_str{ print_core_address (target_gdbarch (), frame.pc) };
       if (frame.cs != 0)
	 pc_str = std::string{ hex_string_custom (frame.cs, 4) } + ":" + pc_str;
       current_uiout->field_string ("shstk-val", pc_str.c_str ());
       current_uiout->text (" ");

       /* Print the symbolic representation of the shadow stack's value.  */
       print_cet_address_symbolic (frame.pc, "shstk-sym");

       current_uiout->text ("\n");

       ++level;
    } while (cet_unwind_shstk_frame (&shstk_mem_range, &ssp, &frame));
}

/* The "info cet backtrace" command.  */

static void
info_cet_shstk_backtrace_cmd (const char *args, int from_tty)
{
  shstk_status state = i386_cet_shstk_state ();
  if (state == SHSTK_DISABLED_HW)
    {
      printf_filtered (_("The CET shadow stack is not supported by the current"
			 " target.\n"));
      return;
    }
  else if (state == SHSTK_DISABLED_KERNEL)
    {
      printf_filtered (_("The CET shadow stack is not supported by the current"
			 " kernel.\n"));
      return;
    }
  else if (state == SHSTK_DISABLED_SW)
    {
      printf_filtered (_("The CET shadow stack is not enabled.\n"));
      return;
    }

  print_cet_shstk_backtrace ();
}

/* Command lists for info CET commands.  */

static cmd_list_element *info_cet_cmdlist;

/* The "info cet" command.  */

static void
info_cet_cmd (const char *args, int from_tty)
{
  help_list (info_cet_cmdlist, "info cet ", all_commands, gdb_stdout);
}

void _initialize_cet_commands ();
void
_initialize_cet_commands ()
{
  add_prefix_cmd ("cet", class_info, info_cet_cmd,
		  _("Control-flow enforcement info commands."),
		  &info_cet_cmdlist, 1, &infolist);

  add_cmd ("status", class_info, cet_status_cmd,
	   _("Show the status information of CET."), &info_cet_cmdlist);

  cmd_list_element *backtrace_cmd
    = add_cmd ("backtrace", class_info, info_cet_shstk_backtrace_cmd, _("\
Print backtrace of shadow stack for the current process.\n\
To print the source filename and line number in the backtrace,\n\
the \"symbol-filename\" option of the print command should be toggled on.\n\
(See \"show print symbol-filename\")"),
	       &info_cet_cmdlist);

  add_alias_cmd ("bt", backtrace_cmd, class_info, 1, &info_cet_cmdlist);
}
