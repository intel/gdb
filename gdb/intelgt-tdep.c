/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019-2024 Free Software Foundation, Inc.

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

#include "arch-utils.h"
#include "arch/intelgt.h"
#include "cli/cli-cmds.h"
#include "dwarf2/frame.h"
#include "frame-unwind.h"
#include "gdbsupport/gdb_obstack.h"
#include "gdbtypes.h"
#include "target.h"
#include "target-descriptions.h"
#include "value.h"
#include "gdbthread.h"
#include "inferior.h"
#include "user-regs.h"
#include <algorithm>

/* Global debug flag.  */
static bool intelgt_debug = false;

/* Print an "intelgt" debug statement.  */

#define intelgt_debug_printf(fmt, ...) \
  debug_prefixed_printf_cond (intelgt_debug, "intelgt", fmt, ##__VA_ARGS__)

/* Regnum pair describing the assigned regnum range for a single
   regset.  START is inclusive, END is exclusive.  */

struct regnum_range
{
  int start;
  int end;
};

/* The encoding for XE version enumerates follows this pattern, which is
   aligned with the IGA encoding.  */

#define XE_VERSION(MAJ, MIN) (((MAJ) << 24) | (MIN))

/* Supported GDB GEN platforms.  */

enum xe_version
{
  XE_INVALID = 0,
  XE_HP = XE_VERSION (1, 1),
  XE_HPG = XE_VERSION (1, 2),
  XE_HPC = XE_VERSION (1, 4),
  XE2 = XE_VERSION (2, 0),
};

/* Helper functions to request and translate the device id/version.  */

[[maybe_unused]] static xe_version get_xe_version (unsigned int device_id);

/* Data specific for this architecture.  */

struct intelgt_gdbarch_tdep : gdbarch_tdep_base
{
  /* $r0 GRF register number.  */
  int r0_regnum = -1;

  /* $ce register number in the regcache.  */
  int ce_regnum = -1;

  /* Register number for the GRF containing function return value.  */
  int retval_regnum = -1;

  /* Register number for the control register.  */
  int cr0_regnum = -1;

  /* Register number for the state register.  */
  int sr0_regnum = -1;

  /* Register number for the instruction base virtual register.  */
  int isabase_regnum = -1;

  /* Register number for the general state base SBA register.  */
  int genstbase_regnum = -1;

  /* Register number for the DBG0 register.  */
  int dbg0_regnum = -1;

  /* Assigned regnum ranges for DWARF regsets.  */
  regnum_range regset_ranges[intelgt::REGSET_COUNT];

  /* Enabled pseudo-registers for the current target description.  */
  std::vector<std::string> enabled_pseudo_regs;

  /* Cached $framedesc pseudo-register type.  */
  type *framedesc_type = nullptr;

  /* Initialize ranges to -1 as "not-yet-set" indicator.  */
  intelgt_gdbarch_tdep ()
  {
    memset (&regset_ranges, -1, sizeof regset_ranges);
  }

  /* Return regnum where frame descriptors are stored.  */
  int framedesc_base_regnum ()
  {
    /* For EM_INTELGT frame descriptors are stored at MAX_GRF - 1.  */
    gdb_assert (regset_ranges[intelgt::REGSET_GRF].end > 1);
    return regset_ranges[intelgt::REGSET_GRF].end - 1;
  }
};

/* The 'register_type' gdbarch method.  */

static type *
intelgt_register_type (gdbarch *gdbarch, int regno)
{
  type *typ = tdesc_register_type (gdbarch, regno);
  return typ;
}

/* Read part of REGNUM at OFFSET into BUFFER.  The length of data to
   read is SIZE.  Consider using this helper function when reading
   subregisters of CR0, SR0, and R0.  */

static void
intelgt_read_register_part (readable_regcache *regcache, int regnum,
			    size_t offset,
			    gdb::array_view<gdb_byte> buffer,
			    const char *error_message)
{
  if (regnum == -1)
    error (_("%s  Unexpected reg num '-1'."), error_message);

  gdbarch *arch = regcache->arch ();
  const char *regname = gdbarch_register_name (arch, regnum);
  int regsize = register_size (arch, regnum);

  if (offset + buffer.size () > regsize)
    error (_("%s[%zu:%zu] is outside the range of %s[%d:0]."),
	   regname, (offset + buffer.size () - 1), offset,
	   regname, (regsize - 1));

  register_status reg_status
    = regcache->cooked_read_part (regnum, offset, buffer);

  if (reg_status == REG_UNAVAILABLE)
    throw_error (NOT_AVAILABLE_ERROR,
		 _("%s  Register %s (%d) is not available."),
		 error_message, regname, regnum);

  if (reg_status == REG_UNKNOWN)
    error (_("%s  Register %s (%d) is unknown."), error_message,
	   regname, regnum);
}

static int
intelgt_pseudo_register_num (gdbarch *arch, const char *name);

/* Convert a DWARF register number to a GDB register number.  This
   function requires the register listing in the target description to
   be in the same order in each regset as the intended DWARF numbering
   order.  Currently this always holds true when gdbserver generates
   the target description.  */

static int
intelgt_dwarf_reg_to_regnum (gdbarch *gdbarch, int num)
{
  constexpr int ip = 0;
  constexpr int ce = 1;

  /* Register sets follow this format: [START, END), where START is
     inclusive and END is exclusive.  */
  constexpr regnum_range dwarf_nums[intelgt::REGSET_COUNT] = {
    [intelgt::REGSET_SBA] = { 5, 12 },
    [intelgt::REGSET_GRF] = { 16, 272 },
    [intelgt::REGSET_ADDR] = { 272, 288 },
    [intelgt::REGSET_FLAG] = { 288, 304 },
    [intelgt::REGSET_ACC] = { 304, 320 },
    [intelgt::REGSET_MME] = { 320, 336 },
  };

  /* Number of SBA registers.  */
  constexpr size_t sba_dwarf_len = dwarf_nums[intelgt::REGSET_SBA].end
    - dwarf_nums[intelgt::REGSET_SBA].start;

  /* Map the DWARF register numbers of SBA registers to their names.
     Base number is dwarf_nums[intelgt::REGSET_SBA].start.  */
  constexpr const char* sba_dwarf_reg_order[sba_dwarf_len] {
    "btbase",
    "scrbase",
    "genstbase",
    "sustbase",
    "blsustbase",
    "blsastbase",
    "scrbase2"
  };

  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (gdbarch);

  if (num == ip)
    return intelgt_pseudo_register_num (gdbarch, "ip");
  if (num == ce)
    return data->ce_regnum;

  for (int regset = 0; regset < intelgt::REGSET_COUNT; ++regset)
    if (num >= dwarf_nums[regset].start && num < dwarf_nums[regset].end)
      {
	if (regset == intelgt::REGSET_SBA)
	  {
	    /* For SBA registers we first find out the name of the register
	       out of DWARF register number and then find the register number
	       corresponding to the name.  */
	    int sba_num = num - dwarf_nums[intelgt::REGSET_SBA].start;
	    const char* name = sba_dwarf_reg_order [sba_num];

	    return user_reg_map_name_to_regnum (gdbarch, name, -1);
	  }
	else
	  {
	    int candidate = data->regset_ranges[regset].start + num
	      - dwarf_nums[regset].start;

	    if (candidate < data->regset_ranges[regset].end)
	      return candidate;
	  }
      }

  return -1;
}

/* Return the PC of the first real instruction.  */

static CORE_ADDR
intelgt_skip_prologue (gdbarch *gdbarch, CORE_ADDR start_pc)
{
  intelgt_debug_printf ("start_pc: %s", paddress (gdbarch, start_pc));
  CORE_ADDR func_addr;

  if (find_pc_partial_function (start_pc, nullptr, &func_addr, nullptr))
    {
      CORE_ADDR post_prologue_pc
       = skip_prologue_using_sal (gdbarch, func_addr);

      intelgt_debug_printf ("post prologue pc: %s",
			    paddress (gdbarch, post_prologue_pc));

      if (post_prologue_pc != 0)
       return std::max (start_pc, post_prologue_pc);
    }

  /* Could not find the end of prologue using SAL.  */
  return start_pc;
}

/* Implementation of gdbarch's return_value method.  */

static enum return_value_convention
intelgt_return_value_as_value (gdbarch *gdbarch, value *function,
			       type *valtype, regcache *regcache,
			       value **read_value, const gdb_byte *writebuf)
{
  gdb_assert_not_reached ("intelgt_return_value_as_value is to be "
			  "implemented later.");
}

/* Callback function to unwind the $framedesc register.  */

static value *
intelgt_dwarf2_prev_framedesc (const frame_info_ptr &this_frame,
			       void **this_cache, int regnum)
{
  gdbarch *gdbarch = get_frame_arch (this_frame);
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (gdbarch);

  int actual_regnum = data->framedesc_base_regnum ();

  /* Unwind the actual GRF register.  */
  return frame_unwind_register_value (this_frame, actual_regnum);
}

/* Architecture-specific register state initialization.  */

static void
intelgt_init_reg (gdbarch *gdbarch, int regnum, dwarf2_frame_state_reg *reg,
		  const frame_info_ptr &this_frame)
{
  int ip_regnum = intelgt_pseudo_register_num (gdbarch, "ip");
  int framedesc_regnum = intelgt_pseudo_register_num (gdbarch, "framedesc");

  if (regnum == ip_regnum)
    reg->how = DWARF2_FRAME_REG_RA;
  else if (regnum == gdbarch_sp_regnum (gdbarch))
    reg->how = DWARF2_FRAME_REG_CFA;
  /* We use special functions to unwind the $framedesc register.  */
  else if (regnum == framedesc_regnum)
    {
      reg->how = DWARF2_FRAME_REG_FN;
      reg->loc.fn = intelgt_dwarf2_prev_framedesc;
    }
}

/* A helper function that returns the value of the ISABASE register.  */

static CORE_ADDR
intelgt_get_isabase (readable_regcache *regcache)
{
  gdbarch *gdbarch = regcache->arch ();
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (gdbarch);
  gdb_assert (data->isabase_regnum != -1);

  uint64_t isabase = 0;
  if (regcache->cooked_read (data->isabase_regnum, &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (isabase) is not available"),
		 data->isabase_regnum);
  return isabase;
}

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, const frame_info_ptr &next_frame)
{
  /* Use ip register here, as IGC uses 32bit values (pc is 64bit).  */
  int ip_regnum = intelgt_pseudo_register_num (gdbarch, "ip");
  CORE_ADDR prev_ip = frame_unwind_register_unsigned (next_frame,
						      ip_regnum);
  intelgt_debug_printf ("prev_ip: %s", paddress (gdbarch, prev_ip));

  /* Program counter is $ip + $isabase.  Read directly from the
     regcache instead of unwinding, as the frame unwind info may
     simply be unavailable.  The isabase register does not change
     during kernel execution, so this must be safe.  */
  regcache *regcache = get_thread_regcache (inferior_thread ());
  CORE_ADDR isabase = intelgt_get_isabase (regcache);

  return isabase + prev_ip;
}

/* Frame unwinding.  */

static void
intelgt_frame_this_id (const frame_info_ptr &this_frame,
		       void **this_prologue_cache, frame_id *this_id)
{
  /* FIXME: Assembly-level unwinding for intelgt is not available at
     the moment.  Stop at the first frame.  */
  *this_id = outer_frame_id;
}

static const struct frame_unwind intelgt_unwinder =
  {
    "intelgt prologue",
    NORMAL_FRAME,			/* type */
    default_frame_unwind_stop_reason,	/* stop_reason */
    intelgt_frame_this_id,		/* this_id */
    nullptr,				/* prev_register */
    nullptr,				/* unwind_data */
    default_frame_sniffer,		/* sniffer */
    nullptr,				/* dealloc_cache */
  };


/* The memory_insert_breakpoint gdbarch method.  */

static int
intelgt_memory_insert_breakpoint (gdbarch *gdbarch, bp_target_info *bp)
{
  intelgt_debug_printf ("req ip: %s", paddress (gdbarch,
						bp->reqstd_address));

  /* Ensure that we have enough space in the breakpoint.  */
  static_assert (intelgt::MAX_INST_LENGTH <= BREAKPOINT_MAX);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = target_read_memory (bp->reqstd_address, inst,
				intelgt::MAX_INST_LENGTH);
  if (err != 0)
    {
      /* We could fall back to reading a full and then a compacted
	 instruction but I think we should rather allow short reads than
	 having the caller try smaller and smaller sizes.  */
      intelgt_debug_printf ("Failed to read memory at %s (%s).",
			    paddress (gdbarch, bp->reqstd_address),
			    strerror (err));
      return err;
    }

  bp->placed_address = bp->reqstd_address;
  bp->shadow_len = intelgt::inst_length (inst);

  /* Make a copy before we set the breakpoint so we can restore the
     original instruction when removing the breakpoint again.

     This isn't strictly necessary but it saves one target access.  */
  memcpy (bp->shadow_contents, inst, bp->shadow_len);

  const bool already = intelgt::set_breakpoint (inst);
  if (already)
    {
      /* Warn if the breakpoint bit is already set.

	 There is still a breakpoint, probably hard-coded, and it should
	 still trigger and we're still able to step over it.  It's just
	 not our breakpoint.  */
      warning (_("Using permanent breakpoint at %s."),
	       paddress (gdbarch, bp->placed_address));

      /* There's no need to write the unmodified instruction back.  */
      return 0;
    }

  err = target_write_raw_memory (bp->placed_address, inst, bp->shadow_len);
  if (err != 0)
    intelgt_debug_printf ("Failed to insert breakpoint at %s (%s).",
			  paddress (gdbarch, bp->placed_address),
			  strerror (err));

  return err;
}

/* The memory_remove_breakpoint gdbarch method.  */

static int
intelgt_memory_remove_breakpoint (gdbarch *gdbarch, struct bp_target_info *bp)
{
  intelgt_debug_printf ("req ip: %s, placed ip: %s",
			paddress (gdbarch, bp->reqstd_address),
			paddress (gdbarch, bp->placed_address));

  /* Warn if we're inserting a permanent breakpoint.  */
  if (intelgt::has_breakpoint (bp->shadow_contents))
    warning (_("Re-inserting permanent breakpoint at %s."),
	     paddress (gdbarch, bp->placed_address));

  /* See comment in mem-break.c on write_inferior_memory.  */
  int err = target_write_raw_memory (bp->placed_address, bp->shadow_contents,
				     bp->shadow_len);
  if (err != 0)
    intelgt_debug_printf ("Failed to remove breakpoint at %s (%s).",
			  paddress (gdbarch, bp->placed_address),
			  strerror (err));

  return err;
}

/* The program_breakpoint_here_p gdbarch method.  */

static bool
intelgt_program_breakpoint_here_p (gdbarch *gdbarch, CORE_ADDR pc)
{
  intelgt_debug_printf ("pc: %s", paddress (gdbarch, pc));

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = target_read_memory (pc, inst, intelgt::MAX_INST_LENGTH);
  if (err != 0)
    {
      /* We could fall back to reading a full and then a compacted
	 instruction but I think we should rather allow short reads than
	 having the caller try smaller and smaller sizes.  */
      intelgt_debug_printf ("Failed to read memory at %s (%s).",
			    paddress (gdbarch, pc), strerror (err));
      return err;
    }

  const bool is_bkpt = intelgt::has_breakpoint (inst);

  intelgt_debug_printf ("%sbreakpoint found.", is_bkpt ? "" : "no ");

  return is_bkpt;
}

/* The 'breakpoint_kind_from_pc' gdbarch method.
   This is a required gdbarch function.  */

static int
intelgt_breakpoint_kind_from_pc (gdbarch *gdbarch, CORE_ADDR *pcptr)
{
  intelgt_debug_printf ("*pcptr: %s", paddress (gdbarch, *pcptr));

  return intelgt::BP_INSTRUCTION;
}

/* The 'sw_breakpoint_from_kind' gdbarch method.  */

static const gdb_byte *
intelgt_sw_breakpoint_from_kind (gdbarch *gdbarch, int kind, int *size)
{
  intelgt_debug_printf ("kind: %d", kind);

  /* We do not support breakpoint instructions.

     We use breakpoint bits in instructions, instead.  See
     intelgt_memory_insert_breakpoint.  */
  *size = 0;
  return nullptr;
}

/* Print one instruction from MEMADDR on INFO->STREAM.  */

static int
intelgt_print_insn (bfd_vma memaddr, struct disassemble_info *info)
{
  /* Disassembler is to be added in a later patch.  */
  return -1;
}

/* Utility function to look up the pseudo-register number by name.  Exact
   amount of pseudo-registers may differ and thus fixed constants can't be
   used for this.  */

static int
intelgt_pseudo_register_num (gdbarch *arch, const char *name)
{
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);
  auto iter = std::find (data->enabled_pseudo_regs.begin (),
			 data->enabled_pseudo_regs.end (), name);
  gdb_assert (iter != data->enabled_pseudo_regs.end ());
  return gdbarch_num_regs (arch) + (iter - data->enabled_pseudo_regs.begin ());
}

/* The "read_pc" gdbarch method.  */

static CORE_ADDR
intelgt_read_pc (readable_regcache *regcache)
{
  gdbarch *arch = regcache->arch ();
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  /* Instruction pointer is stored in CR0.2.  */
  uint32_t ip;
  intelgt_read_register_part (regcache, data->cr0_regnum,
			      sizeof (uint32_t) * 2,
			      gdb::make_array_view ((gdb_byte *) &ip,
						    sizeof (uint32_t)),
			      _("Cannot compute PC."));

  /* Program counter is $ip + $isabase.  */
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
  return isabase + ip;
}

/* The "write_pc" gdbarch method.  */

static void
intelgt_write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  gdbarch *arch = regcache->arch ();
  /* Program counter is $ip + $isabase, can only modify $ip.  Need
     to ensure that the new value fits within $ip modification range
     and propagate the write accordingly.  */
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
  if (pc < isabase || pc > isabase + UINT32_MAX)
    error ("Can't update $pc to value %s, out of range",
	   paddress (arch, pc));

  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  /* Instruction pointer is stored in CR0.2.  */
  uint32_t ip = pc - isabase;
  regcache->cooked_write_part (data->cr0_regnum, sizeof (uint32_t) * 2,
			       gdb::make_array_view ((gdb_byte *) &ip,
						     sizeof (uint32_t)));
}

/* Return the name of pseudo-register REGNUM.  */

static const char *
intelgt_pseudo_register_name (gdbarch *arch, int regnum)
{
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);
  int base_num = gdbarch_num_regs (arch);
  if (regnum < base_num
      || regnum >= base_num + data->enabled_pseudo_regs.size ())
    error ("Invalid pseudo-register regnum %d", regnum);
  return data->enabled_pseudo_regs[regnum - base_num].c_str ();
}

/* Return the GDB type object for the "standard" data type of data in
   pseudo-register REGNUM.  */

static type *
intelgt_pseudo_register_type (gdbarch *arch, int regnum)
{
  const char *name = intelgt_pseudo_register_name (arch, regnum);
  const struct builtin_type *bt = builtin_type (arch);
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  if (strcmp (name, "framedesc") == 0)
    {
      if (data->framedesc_type != nullptr)
	return data->framedesc_type;
      type *frame = arch_composite_type (arch, "frame_desc", TYPE_CODE_STRUCT);
      append_composite_type_field (frame, "return_ip", bt->builtin_uint32);
      append_composite_type_field (frame, "return_callmask",
				   bt->builtin_uint32);
      append_composite_type_field (frame, "be_sp", bt->builtin_uint32);
      append_composite_type_field (frame, "be_fp", bt->builtin_uint32);
      append_composite_type_field (frame, "fe_fp", bt->builtin_uint64);
      append_composite_type_field (frame, "fe_sp", bt->builtin_uint64);
      data->framedesc_type = frame;
      return frame;
    }
  else if (strcmp (name, "ip") == 0)
    return bt->builtin_uint32;

  return nullptr;
}

/* Read the value of a pseudo-register REGNUM.  */

static struct value *
intelgt_pseudo_register_read_value (gdbarch *arch,
				    const frame_info_ptr &next_frame,
				    int pseudo_regnum)
{
  const char *name = intelgt_pseudo_register_name (arch, pseudo_regnum);
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  if (strcmp (name, "framedesc") == 0)
    {
      int grf_num = data->framedesc_base_regnum ();
      return pseudo_from_raw_part (next_frame, pseudo_regnum, grf_num, 0);
    }
  else if (strcmp (name, "ip") == 0)
    {
      int regsize = register_size (arch, pseudo_regnum);
      /* Instruction pointer is stored in CR0.2.  */
      gdb_assert (data->cr0_regnum != -1);
      /* CR0 elements are 4 byte wide.  */
      gdb_assert (regsize + 8 <= register_size (arch, data->cr0_regnum));

      return pseudo_from_raw_part (next_frame, pseudo_regnum,
				   data->cr0_regnum, 8);
    }

  return nullptr;
}

/* Write the value of a pseudo-register REGNUM.  */

static void
intelgt_pseudo_register_write (gdbarch *arch,
			       const frame_info_ptr &next_frame,
			       int pseudo_regnum,
			       gdb::array_view<const gdb_byte> buf)
{
  const char *name = intelgt_pseudo_register_name (arch, pseudo_regnum);
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  if (strcmp (name, "framedesc") == 0)
    {
      int grf_num = data->framedesc_base_regnum ();
      int grf_size = register_size (arch, grf_num);
      int desc_size = register_size (arch, pseudo_regnum);
      gdb_assert (grf_size >= desc_size);
      pseudo_to_raw_part (next_frame, buf, grf_num, 0);
    }
  else if (strcmp (name, "ip") == 0)
    {
      /* Instruction pointer is stored in CR0.2.  */
      gdb_assert (data->cr0_regnum != -1);
      int cr0_size = register_size (arch, data->cr0_regnum);

      /* CR0 elements are 4 byte wide.  */
      int reg_size = register_size (arch, pseudo_regnum);
      gdb_assert (reg_size + 8 <= cr0_size);
      pseudo_to_raw_part (next_frame, buf, data->cr0_regnum, 8);
    }
  else
    error ("Pseudo-register %s is read-only", name);
}

/* Called by tdesc_use_registers each time a new regnum
   is assigned.  Used to track down assigned numbers for
   any important regnums.  */

static int
intelgt_unknown_register_cb (gdbarch *arch, tdesc_feature *feature,
			     const char *reg_name, int possible_regnum)
{
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (arch);

  /* First, check if this a beginning of a not yet tracked regset
     assignment.  */

  for (int regset = 0; regset < intelgt::REGSET_COUNT; ++regset)
    {
      if (data->regset_ranges[regset].start == -1
	  && feature->name == intelgt::DWARF_REGSET_FEATURES[regset])
	{
	  data->regset_ranges[regset].start = possible_regnum;
	  data->regset_ranges[regset].end
	      = feature->registers.size () + possible_regnum;
	  break;
	}
    }

  /* Second, check if it is any specific individual register that
     needs to be tracked.  */

  if (strcmp ("r0", reg_name) == 0)
    data->r0_regnum = possible_regnum;
  else if (strcmp ("r26", reg_name) == 0)
    data->retval_regnum = possible_regnum;
  else if (strcmp ("cr0", reg_name) == 0)
    data->cr0_regnum = possible_regnum;
  else if (strcmp ("sr0", reg_name) == 0)
    data->sr0_regnum = possible_regnum;
  else if (strcmp ("isabase", reg_name) == 0)
    data->isabase_regnum = possible_regnum;
  else if (strcmp ("ce", reg_name) == 0)
    data->ce_regnum = possible_regnum;
  else if (strcmp ("genstbase", reg_name) == 0)
    data->genstbase_regnum = possible_regnum;
  else if (strcmp ("dbg0", reg_name) == 0)
    data->dbg0_regnum = possible_regnum;

  return possible_regnum;
}

/* Helper function to translate the device id to a device version.  */

static xe_version
get_xe_version (unsigned int device_id)
{
  xe_version device_xe_version = XE_INVALID;
  switch (device_id)
    {
      case 0x4F80:
      case 0x4F81:
      case 0x4F82:
      case 0x4F83:
      case 0x4F84:
      case 0x4F85:
      case 0x4F86:
      case 0x4F87:
      case 0x4F88:
      case 0x5690:
      case 0x5691:
      case 0x5692:
      case 0x5693:
      case 0x5694:
      case 0x5695:
      case 0x5696:
      case 0x5697:
      case 0x5698:
      case 0x56A0:
      case 0x56A1:
      case 0x56A2:
      case 0x56A3:
      case 0x56A4:
      case 0x56A5:
      case 0x56A6:
      case 0x56A7:
      case 0x56A8:
      case 0x56A9:
      case 0x56B0:
      case 0x56B1:
      case 0x56B2:
      case 0x56B3:
      case 0x56BA:
      case 0x56BB:
      case 0x56BC:
      case 0x56BD:
      case 0x56C0:
      case 0x56C1:
      case 0x56C2:
      case 0x56CF:
      case 0x7D40:
      case 0x7D45:
      case 0x7D67:
      case 0x7D41:
      case 0x7D55:
      case 0x7DD5:
	device_xe_version = XE_HPG;
	break;

      case 0x0201:
      case 0x0202:
      case 0x0203:
      case 0x0204:
      case 0x0205:
      case 0x0206:
      case 0x0207:
      case 0x0208:
      case 0x0209:
      case 0x020A:
      case 0x020B:
      case 0x020C:
      case 0x020D:
      case 0x020E:
      case 0x020F:
      case 0x0210:
	device_xe_version = XE_HP;
	break;

      case 0x0BD0:
      case 0x0BD4:
      case 0x0BD5:
      case 0x0BD6:
      case 0x0BD7:
      case 0x0BD8:
      case 0x0BD9:
      case 0x0BDA:
      case 0x0BDB:
      case 0x0B69:
      case 0x0B6E:
	device_xe_version = XE_HPC;
	break;

      case 0x6420:
      case 0x64A0:
      case 0x64B0:
      case 0xE202:
      case 0xE20B:
      case 0xE20C:
      case 0xE20D:
      case 0xE212:
	device_xe_version = XE2;
	break;
    }

  return device_xe_version;
}

/* Architecture initialization.  */

static gdbarch *
intelgt_gdbarch_init (gdbarch_info info, gdbarch_list *arches)
{
  /* If there is already a candidate, use it.  */
  arches = gdbarch_list_lookup_by_info (arches, &info);
  if (arches != nullptr)
    return arches->gdbarch;

  const target_desc *tdesc = info.target_desc;
  gdbarch *gdbarch
    = gdbarch_alloc (&info,
		     gdbarch_tdep_up (new intelgt_gdbarch_tdep));
  intelgt_gdbarch_tdep *data
    = gdbarch_tdep<intelgt_gdbarch_tdep> (gdbarch);

  /* Initialize register info.  */
  set_gdbarch_num_regs (gdbarch, 0);
  set_gdbarch_register_name (gdbarch, tdesc_register_name);

  if (tdesc_has_registers (tdesc))
    {
      tdesc_arch_data_up tdesc_data = tdesc_data_alloc ();

      /* First assign register numbers to all registers.  The
	 callback function will record any relevant metadata
	 about it in the intelgt_gdbarch_data instance to be
	 inspected after.  */

      tdesc_use_registers (gdbarch, tdesc, std::move (tdesc_data),
			   intelgt_unknown_register_cb);

      /* Now check the collected metadata to ensure that all
	 mandatory pieces are in place.  */

      if (data->ce_regnum == -1)
	error ("Debugging requires $ce provided by the target");
      if (data->retval_regnum == -1)
	error ("Debugging requires return value register to be provided by "
	       "the target");
      if (data->cr0_regnum == -1)
	error ("Debugging requires control register to be provided by "
	       "the target");
      if (data->sr0_regnum == -1)
	error ("Debugging requires state register to be provided by "
	       "the target");

      /* Unconditionally enabled pseudo-registers:  */
      data->enabled_pseudo_regs.push_back ("ip");
      data->enabled_pseudo_regs.push_back ("framedesc");

      set_gdbarch_num_pseudo_regs (gdbarch, data->enabled_pseudo_regs.size ());
      set_gdbarch_pseudo_register_read_value (
	  gdbarch, intelgt_pseudo_register_read_value);
      set_gdbarch_pseudo_register_write (gdbarch,
					 intelgt_pseudo_register_write);
      set_tdesc_pseudo_register_type (gdbarch, intelgt_pseudo_register_type);
      set_tdesc_pseudo_register_name (gdbarch, intelgt_pseudo_register_name);
      set_gdbarch_read_pc (gdbarch, intelgt_read_pc);
      set_gdbarch_write_pc (gdbarch, intelgt_write_pc);
    }

  /* Populate gdbarch fields.  */
  set_gdbarch_ptr_bit (gdbarch, 64);
  set_gdbarch_addr_bit (gdbarch, 64);
  set_gdbarch_long_bit (gdbarch, 64);

  set_gdbarch_register_type (gdbarch, intelgt_register_type);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, intelgt_dwarf_reg_to_regnum);

  set_gdbarch_skip_prologue (gdbarch, intelgt_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_greaterthan);
  set_gdbarch_unwind_pc (gdbarch, intelgt_unwind_pc);
  dwarf2_append_unwinders (gdbarch);
  frame_unwind_append_unwinder (gdbarch, &intelgt_unwinder);

  set_gdbarch_return_value_as_value (gdbarch, intelgt_return_value_as_value);

  set_gdbarch_memory_insert_breakpoint (gdbarch,
					intelgt_memory_insert_breakpoint);
  set_gdbarch_memory_remove_breakpoint (gdbarch,
					intelgt_memory_remove_breakpoint);
  set_gdbarch_program_breakpoint_here_p (gdbarch,
					 intelgt_program_breakpoint_here_p);
  set_gdbarch_breakpoint_kind_from_pc (gdbarch,
				       intelgt_breakpoint_kind_from_pc);
  set_gdbarch_sw_breakpoint_from_kind (gdbarch,
				       intelgt_sw_breakpoint_from_kind);
  dwarf2_frame_set_init_reg (gdbarch, intelgt_init_reg);

  /* Disassembly.  */
  set_gdbarch_print_insn (gdbarch, intelgt_print_insn);

  return gdbarch;
}

/* Dump the target specific data for this architecture.  */

static void
intelgt_dump_tdep (gdbarch *gdbarch, ui_file *file)
{
  /* Implement target-specific print output if and
     when gdbarch_tdep is defined for this architecture.  */
}

static void
show_intelgt_debug (ui_file *file, int from_tty,
		    cmd_list_element *c, const char *value)
{
  gdb_printf (file, _("Intel(R) Graphics Technology debugging is "
		      "%s.\n"), value);
}

void _initialize_intelgt_tdep ();
void
_initialize_intelgt_tdep ()
{
  gdbarch_register (bfd_arch_intelgt, intelgt_gdbarch_init,
		    intelgt_dump_tdep);

  /* Debugging flag.  */
  add_setshow_boolean_cmd ("intelgt", class_maintenance, &intelgt_debug,
			   _("Set Intel(R) Graphics Technology debugging."),
			   _("Show Intel(R) Graphics Technology debugging."),
			   _("When on, Intel(R) Graphics Technology "
			     "debugging is enabled."),
			   nullptr,
			   show_intelgt_debug,
			   &setdebuglist, &showdebuglist);
}
