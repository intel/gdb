/* Control-flow Enforcement Technology support for GDB, the GNU debugger.

   Copyright (C) 2018 Free Software Foundation, Inc.

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
#include "x86-cet.h"
#include "demangle.h"
#include "frame.h"
#include "gdbcmd.h"
#include "gdbthread.h"
#include "i386-tdep.h"
#include "i387-tdep.h"
#include "inferior.h"
#include "include/elf/common.h"
#include "inf-ptrace.h"
#include "valprint.h"

#include <sstream>
#include <vector>


/* MSR_IA32_U_CET and MSR_IA32_S_CET bits.  */
#define MSR_CET_SHSTK_EN		(0x1 << 0)
#define MSR_CET_WR_SHSTK_EN		(0x1 << 1)
#define MSR_CET_ENDBR_EN		(0x1 << 2)
#define MSR_CET_LEG_IW_EN		(0x1 << 3)
#define MSR_CET_NO_TRACK_EN		(0x1 << 4)
#define MSR_CET_SUPRESS_DIS		(0x1 << 5)
#define MSR_CET_SUPRESS			(0x1 << 10)
#define MSR_CET_TRACKER			(0x1 << 11)
#define MSR_CET_EB_LEG_BITMAP_BASE	0xfffffffffffff000ULL

extern int safe_read_memory_unsigned_integer (CORE_ADDR memaddr, int len,
					      enum bfd_endian byte_order,
					      ULONGEST *return_value);

/* See x86-cet.h.  */

bool
cet_set_registers (const ptid_t tid, const CORE_ADDR *ssp,
		   const uint64_t *cet_msr)
{
  if (!target_has_execution)
    error (_("No current process: you must name one."));

  auto *regcache = get_thread_regcache_for_ptid (tid);
  const auto *tdep = gdbarch_tdep (regcache->arch ());

  auto regnum = tdep->cet_regnum;
  if (regnum < 0)
    return false;

  regcache_raw_write_unsigned (regcache, regnum, (ULONGEST) *cet_msr);

  ++regnum;
  regcache_raw_write_unsigned (regcache, regnum, *ssp);

  return true;
}

/* See x86-cet.h.  */

bool
cet_get_registers (const ptid_t tid, CORE_ADDR *ssp, uint64_t *cet_msr)
{
  if (!target_has_execution)
    error (_("No current process: you must name one."));

  auto *regcache = get_thread_regcache_for_ptid (tid);
  const auto *tdep = gdbarch_tdep (regcache->arch ());

  auto regnum = tdep->cet_regnum;
  if (regnum < 0)
    return false;

  if (regcache_raw_read_unsigned (regcache, regnum, (ULONGEST *) cet_msr)
      != REG_VALID)
    return false;

  ++regnum;
  if (regcache_raw_read_unsigned (regcache, regnum, ssp) != REG_VALID)
    return false;

  return true;
}

/* See x86-cet.h.  */

bool
shstk_is_enabled (CORE_ADDR *ssp, uint64_t *cet_msr)
{
  if (!cet_get_registers (inferior_ptid, ssp, cet_msr))
    return false;

  return (*cet_msr & MSR_CET_SHSTK_EN);
}

/* Print the information from the CET MSR and the SSP.  */

static void
print_cet_status (const CORE_ADDR *ssp, const uint64_t *cet_msr)
{
  const int ncols = 2, nrows = 10;
  struct ui_out *uiout = current_uiout;

  std::vector<std::string> names
      = { "Shadow Stack:", "Shadow Stack Pointer:",
	  "WR_SHSTK_EN:",  "Indirect Branch Tracking:",
	  "TRACKER:",      "LEG_IW_EN:",
	  "NO_TRACK_EN:",  "SUPRESS_DIS:",
	  "SUPRESS:",      "EB_LEG_BITMAP_BASE:" };

  std::vector<std::string> values
      = { (*cet_msr & MSR_CET_SHSTK_EN) ? "enabled" : "disabled",
	  hex_string_custom (*ssp, 12),
	  (*cet_msr & MSR_CET_WR_SHSTK_EN) ? "enabled" : "disabled",
	  (*cet_msr & MSR_CET_ENDBR_EN) ? "enabled" : "disabled",
	  (*cet_msr & MSR_CET_TRACKER) ? "WAIT_FOR_ENDBRANCH" : "IDLE",
	  (*cet_msr & MSR_CET_LEG_IW_EN) ? "enabled" : "disabled",
	  (*cet_msr & MSR_CET_NO_TRACK_EN) ? "enabled" : "disabled",
	  (*cet_msr & MSR_CET_SUPRESS_DIS) ? "enabled" : "disabled",
	  (*cet_msr & MSR_CET_SUPRESS) ? "enabled" : "disabled",
	  hex_string_custom (*cet_msr & MSR_CET_EB_LEG_BITMAP_BASE, 12) };

  ui_out_emit_table table_emitter (uiout, ncols, nrows, "cet-status");

  uiout->table_header (25, ui_left, "name", "Target Id:");
  uiout->table_header (33, ui_left, "value",
		       target_pid_to_str (inferior_ptid));
  uiout->table_body ();

  for (int i = 0; i < nrows; ++i)
    {
      ui_out_emit_tuple tuple_emitter (uiout, nullptr);
      uiout->field_string ("name", names.at (i).c_str ());
      uiout->field_string ("value", values.at (i).c_str ());
      uiout->text ("\n");
    }
}

/* The "info cet status" command.  */

static void
cet_status_cmd (const char *args, int from_tty)
{
  uint64_t cet_msr;
  CORE_ADDR ssp;
  if (!cet_get_registers (inferior_ptid, &ssp, &cet_msr))
    {
      warning (_("Failed to fetch CET registers."));
      return;
    }

  print_cet_status (&ssp, &cet_msr);
}

/*  Retrieve the mapped memory regions [ADDR_LOW, ADDR_HIGH) for a given
    address ADDR in memory space of process TID by reading the process
    information from its pseudo-file system.  */

static bool
cet_get_shstk_mem_range (const CORE_ADDR addr, struct mem_range *range)
{
  if (!target_has_execution)
    error (_("No current process: you must name one."));

  if (current_inferior ()->fake_pid_p)
    error (_("Can't determine the current process's PID."));

  auto pid = current_inferior ()->pid;

  /* Construct the memory-map file's name and read the file's content.  */
  std::string filename{ "/proc/" + std::to_string (pid) + "/maps" };
  gdb::unique_xmalloc_ptr<char> map
    = target_fileio_read_stralloc (nullptr, filename.c_str ());
  if (map == nullptr)
    {
      warning (_("Unable to open file '%s'"), filename.c_str ());
      return false;
    }

  /* Parse the memory-map file line-by-line and look for the memory range which
     ADDR belongs to. Each line of the memory-map file starts with the format
     "<map_low>-<map_high>".  */
  std::istringstream map_file_strm (map.get ());
  std::string line;
  while (std::getline (map_file_strm, line))
    {
      CORE_ADDR map_low, map_high;
      const char *p = line.c_str ();
      map_low = strtoulst (p, &p, 16);
      if (*p == '-')
	p++;
      map_high = strtoulst (p, &p, 16);

      struct mem_range tmp_range
      {
	map_low, static_cast<int> (map_high - map_low)
      };
      if (address_in_mem_range (addr, &tmp_range))
	{
	  *range = tmp_range;
	  return true;
	}
    }

  return false;
}

/* Represents a frame in shadow stack.
   Shadow stack frames contain the Program Counter (PC). Far-calls additionally
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
cet_unwind_shstk_frame (const struct mem_range *shstk_mem_range,
			CORE_ADDR *shstk_addr, shstk_frame_info *shstk_info)
{
  /* Check against memory range SHSTK_MEM_RANGE of shadow stack address
     space.  */
  if (!address_in_mem_range (*shstk_addr, shstk_mem_range))
    return false;

  const int addr_size = gdbarch_addr_bit (target_gdbarch ()) / TARGET_CHAR_BIT;
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch ());

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
     the possible previous shadow stack pointer (ssp_addr + 3 * addr_size).  */
  CORE_ADDR pc_val = 0, cs_val = 0;
  if (ssp_val == (ssp_addr + 3 * addr_size))
    {
      /* Read the PC value.  */
      ssp_addr += addr_size;
      if (!safe_read_memory_unsigned_integer (ssp_addr, addr_size, byte_order,
					      &pc_val))
	{
	  warning (
	      _("Unable to read the memory address %lx in shadow stack."),
	      ssp_addr);
	  return false;
	}

      /* Read the CS value.  */
      ssp_addr += addr_size;
      if (!safe_read_memory_unsigned_integer (ssp_addr, addr_size, byte_order,
					      &cs_val))
	{
	  warning (
	      _("Unable to read the memory address %lx in shadow stack."),
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
  *shstk_addr = ssp_addr + addr_size;

  return true;
}

/* Print the symbolic representation (symbol name, file name and line number)
   of ADDR under the label FIELD_LABEL in UIOUT.  */
static void
print_cet_address_symbolic (const CORE_ADDR addr, struct ui_out *uiout,
			    const std::string field_label)
{
  /* Symbol name at ADDR.  */
  std::string name;

  /* File name and line number of the symbol at ADDR.  */
  std::string filename;
  int unmapped = 0, offset = 0, line = 0;

  /* Read the symbol info at ADDR.  */
  if (build_address_symbolic (target_gdbarch (), addr, true, true, &name, &offset,
			      &filename, &line, &unmapped)
      == 0)
    {
      if (!filename.empty ())
	{
	  /* Print the file name if one is found.  */
	  if (line != -1)
	    /* Print the line number if one is found.  */
	    uiout->field_fmt (field_label.c_str (), "%s at %s:%d",
			      name.c_str (), filename.c_str (), line);
	  else
	    uiout->field_fmt (field_label.c_str (), "%s in %s", name.c_str (),
			      filename.c_str ());
	}
      else
	/* Print only the symbol name if no file name is found.  */
	uiout->field_fmt (field_label.c_str (), "%s", name.c_str ());
    }
  else
    uiout->field_string (field_label.c_str (), "<unavailable>");

  return;
}

/* Print the shadow stack backtrace.  */

static void
print_cet_shstk_backtrace (const std::vector<shstk_frame_info> *shstk_frames)
{
  if (shstk_frames->empty ())
    {
      printf_filtered (_("No shadow stack frame to print.\n"));
      return;
    }

  /* Three columns for shadow stack frame's level, value and symbolic address
     of the value.  */
  const int ncols = 3;

  /* One row for each shadow stack frame.  */
  const int nrows = shstk_frames->size ();

  auto *uiout = current_uiout;
  ui_out_emit_table table_emitter (uiout, ncols, nrows, "shstk-bt");

  /* Setup the table header.  */
  const auto level_width = std::to_string (nrows).length () + 1;
  uiout->table_header (level_width, ui_left, "lvl", " ");
  const auto addr_width = gdbarch_addr_bit (target_gdbarch ()) <= 32 ? 10 : 18;
  uiout->table_header (addr_width, ui_left, "shstk-val", "Address");
  uiout->table_header (25, ui_left, "shstk-sym", "Symbol");

  /* Setup the table body.  */
  uiout->table_body ();

  int level = 0;
  for (const auto &frame : *shstk_frames)
    {
      ui_out_emit_tuple tuple_emitter (uiout, nullptr);

      /* Print the frame level.  */
      const std::string level_str{ "#" + std::to_string (level) };
      uiout->field_string ("lvl", level_str.c_str ());

      /* Print the shadow stack's value.  */
      std::string pc_str{ print_core_address (target_gdbarch (), frame.pc) };
      if (frame.cs != 0)
	pc_str = std::string{ hex_string_custom (frame.cs, 4) } + ":" + pc_str;
      uiout->field_string ("shstk-val", pc_str.c_str ());

      /* Print the symbolic representation of the shadow stack's value.  */
      print_cet_address_symbolic (frame.pc, uiout, "shstk-sym");

      uiout->text ("\n");

      ++level;
    }
}

/* Fully unwind the shadow stack starting from the current shadow stack
   pointer.  Returns an empty vector if it could not retrieve either the shadow
   stack pointer or its memory range or it fails to unwind any frame at the
   current shadow stack pointer.  */

static std::vector<shstk_frame_info>
cet_get_shstk_frames ()
{
  /* Read the current shadow stack pointer address SSP.  */
  uint64_t msr;
  CORE_ADDR ssp;
  if (!cet_get_registers (inferior_ptid, &ssp, &msr))
    {
      warning (_("Unable to get the shadow stack pointer address!"));
      return {};
    }

  /* Read the memory range allocated for the shadow stack.
     The range is used as a stop criteria for unwinding process.
     The memory range is read once and passed as arguments to unwinding
     function to avoid repetitive calculation of the range in each unwinding
     call.  */
  struct mem_range shstk_mem_range;
  if (!cet_get_shstk_mem_range (ssp, &shstk_mem_range))
    {
      warning (_("Unable to get the shadow stack address range!"));
      return {};
    }

  /* The first 64-bit value of shadow stack address space is the supervisor
     shadow stack token. We skip it for unwinding the shadow stack since it
     is setup by the supervisor when creating the shadow stacks to be used on
     inter-privilege call transfers.  */
  shstk_mem_range.length -= 0x8;

  /* Unwind the shadow stack until we reach the boundaries of SHSTK_MEM_RANGE
     memory region. After each iteration SSP will point to the beginning of
     the next shadow stack frame.  */
  shstk_frame_info shstk_val;
  std::vector<shstk_frame_info> shstk_frames;
  while (cet_unwind_shstk_frame (&shstk_mem_range, &ssp, &shstk_val))
    shstk_frames.push_back (shstk_val);

  return shstk_frames;
}

/* The "info cet backtrace" command.  */

static void
info_cet_shstk_backtrace_cmd (const char *args, int from_tty)
{
  auto shstk_frames = cet_get_shstk_frames ();
  print_cet_shstk_backtrace (&shstk_frames);
}

/* Command lists for info CET commands.  */

static struct cmd_list_element *info_cet_cmdlist;

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
		  &info_cet_cmdlist, "info cet ", 1, &infolist);

  add_cmd ("status", class_info, cet_status_cmd,
	   _("Show the status information of CET.\n"), &info_cet_cmdlist);

  add_cmd ("backtrace", class_info, info_cet_shstk_backtrace_cmd, _("\
Print backtrace of shadow stack for the current running process.\n\
To print the source filename and line number in the backtrace,\n\
the \"symbol-filename\" option of the print command should be toggled on.\n\
(See \"show print symbol-filename\")\n"),
	   &info_cet_cmdlist);

  add_alias_cmd ("bt", "backtrace", class_info, 1, &info_cet_cmdlist);
}
