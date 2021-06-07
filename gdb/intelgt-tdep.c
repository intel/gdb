/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019-2021 Free Software Foundation, Inc.

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
#include "arch/intelgt.h"
#include "dwarf2/frame.h"
#include "frame-unwind.h"
#include "gdbcmd.h"
#include "gdb_obstack.h"
#include "gdbtypes.h"
#include "target.h"
#include "target-descriptions.h"
#include "value.h"
#if defined (HAVE_LIBIGA64)
#include "iga.h"
#endif /* defined (HAVE_LIBIGA64)  */
#include "gdbthread.h"
#include "inferior.h"

/* Address space flags.
   We are assigning the TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1 to the shared
   local memory address space.  */

#define INTELGT_TYPE_INSTANCE_FLAG_SLM TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1
#define INTELGT_SLM_ADDRESS_QUALIFIER "slm"

/* Global debug flag.  */
static bool intelgt_debug = false;

#define dprintf(...)						\
  do								\
    {								\
      if (intelgt_debug)					\
	{							\
	  fprintf_unfiltered (gdb_stdlog, "%s: ", __func__);	\
	  fprintf_unfiltered (gdb_stdlog, __VA_ARGS__);		\
	  fprintf_unfiltered (gdb_stdlog, "\n");		\
	}							\
    }								\
  while (0)

/* Regnum pair describing the assigned regnum range for a single
   regset.  */

struct regnum_range
{
  int start;
  int end;
};

/* The 'gdbarch_data' stuff specific for this architecture.  */

static struct gdbarch_data *intelgt_gdbarch_data_handle;

struct intelgt_gdbarch_data
{
  /* $emask register number in the regcache.  */
  int emask_regnum = -1;
  /* Register number for the GRF containing function return value.  */
  int retval_regnum = -1;
  /* Assigned regnum ranges for DWARF regsets.  */
  regnum_range regset_ranges[intelgt::regset_count];

  /* Initialize ranges to -1 as "not-yet-set" indicator.  */
  intelgt_gdbarch_data ()
  {
    memset (&regset_ranges, -1, sizeof regset_ranges);
  }

#if defined (HAVE_LIBIGA64)
  /* libiga context for disassembly.  */
  iga_context_t iga_ctx = nullptr;
#endif
};

static void *
init_intelgt_gdbarch_data (obstack *obstack)
{
  return obstack_new<struct intelgt_gdbarch_data> (obstack);
}

static intelgt_gdbarch_data *
get_intelgt_gdbarch_data (gdbarch *gdbarch)
{
  return ((intelgt_gdbarch_data *)
	  gdbarch_data (gdbarch, intelgt_gdbarch_data_handle));
}

/* Convert a DWARF register number to a GDB register number.  This
   function requires for the register listing in the target
   description to be in the same order in each regeset as the
   intended DWARF numbering order.  Currently this is always
   holds true when gdbserver generates the target description.  */

static int
intelgt_dwarf_reg_to_regnum (gdbarch *gdbarch, int num)
{
  constexpr int ip = 0;
  constexpr int emask = 1;
  constexpr regnum_range dwarf_nums[intelgt::regset_count] = {
    [intelgt::regset_sba] = { 5, 10 },
    [intelgt::regset_grf] = { 16, 271 },
    [intelgt::regset_addr] = { 272, 287 },
    [intelgt::regset_flag] = { 288, 303 },
    [intelgt::regset_acc] = { 304, 319 },
    [intelgt::regset_mme] = { 320, 335 },
  };

  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  if (num == ip)
    return gdbarch_pc_regnum (gdbarch);
  if (num == emask)
    return data->emask_regnum;

  for (int regset = 0; regset < intelgt::regset_count; ++regset)
    if (num >= dwarf_nums[regset].start && num <= dwarf_nums[regset].end)
      {
	int candidate = data->regset_ranges[regset].start + num
			- dwarf_nums[regset].start;
	if (candidate <= data->regset_ranges[regset].end)
	  return candidate;
      }

  return -1;
}

/* Return active lanes mask for the specified thread TP.  */

static unsigned int
intelgt_active_lanes_mask (struct gdbarch *gdbarch, thread_info *tp)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  regcache *thread_regcache = get_thread_regcache (tp);

  return regcache_raw_get_unsigned (thread_regcache, data->emask_regnum);
}

/* Return the PC of the first real instruction.  */

static CORE_ADDR
intelgt_skip_prologue (gdbarch *gdbarch, CORE_ADDR start_pc)
{
  dprintf ("start_pc: %lx", start_pc);
  CORE_ADDR func_addr;

  if (find_pc_partial_function (start_pc, nullptr, &func_addr, nullptr))
    {
      CORE_ADDR post_prologue_pc
       = skip_prologue_using_sal (gdbarch, func_addr);

      dprintf ("post prologue pc: %lx", post_prologue_pc);

      if (post_prologue_pc != 0)
       return std::max (start_pc, post_prologue_pc);
    }

  /* Could not find the end of prologue using SAL.  */
  return start_pc;
}

/* Implementation of gdbarch's return_value method.  */

static enum return_value_convention
intelgt_return_value (gdbarch *gdbarch, value *function,
		      type *valtype, regcache *regcache,
		      gdb_byte *readbuf, const gdb_byte *writebuf)
{
  dprintf ("return type length %ld", TYPE_LENGTH (valtype));
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  gdb_assert (inferior_ptid != null_ptid);

  int address_size_byte = gdbarch_addr_bit (gdbarch) / 8;

  /* The vectorized return value is stored at this register and onwards.  */
  int retval_regnum = get_intelgt_gdbarch_data (gdbarch)->retval_regnum;
  unsigned int retval_size = register_size (gdbarch, retval_regnum);
  int type_length = TYPE_LENGTH (valtype);
  int simd_lane = inferior_thread ()->current_simd_lane ();

  if (type_length > 8 || class_or_union_p (valtype))
    {
      /* Values greater than 64 bits (64 is specified by ABI) or structs
	 are stored by reference.  The return value register contains a
	 vectorized sequence of memory addresses.  */
      if (readbuf != nullptr)
	{
	  CORE_ADDR offset = address_size_byte * simd_lane;
	  /* One retval register contains that many addresses.  */
	  int n_addresses_in_retval_reg = retval_size / address_size_byte;

	  /* Find at which register the return value address is stored
	     for the current SIMD lane.  */
	  while (offset > retval_size)
	    {
	      /* The register RETVAL_REGNUM does not contain the return value
		 for the current SIMD lane.  Decrease the offset by the size of
		 addresses stored in this register and move to the next
		 register.  */
	      offset -= n_addresses_in_retval_reg* address_size_byte;
	      retval_regnum++;
	    }

	  /* Read the address to a temporary buffer.  The address is stored
	     in RETVAL_REGNUM with OFFSET.  */
	  gdb_byte buf[address_size_byte];
	  regcache->cooked_read_part (retval_regnum, offset,
				      address_size_byte, buf);
	  CORE_ADDR addr = extract_unsigned_integer (buf, address_size_byte,
						     byte_order);
	  /* Read the value to the resulting buffer.  */
	  target_read_memory (addr, readbuf, type_length);
	}

      return RETURN_VALUE_ABI_RETURNS_ADDRESS;
    }

  /* Return value is stored in the return register.  */
  if (readbuf != nullptr)
    {
      if (TYPE_VECTOR (valtype) == 1)
	{
	  /* Vectors on GRF are stored with Structure of Arrays (SoA) layout.
	     E.g. the vector v[4] when vectorized accross SIMD lanes will have
	     the following layout:
	     v[3] v[3]...v[3] v[2] v[2]...v[2] v[1] v[1]...v[1] v[0] v[0]...v[0]
	     To get the complete vector, we need to read the whole register.  */

	  /* Length of an element in the vector.  */
	  int target_type_length = TYPE_LENGTH (TYPE_TARGET_TYPE (valtype));

	  /* Number of elements in the vector.  */
	  int n_elements_to_read = type_length / target_type_length;

	  /* Number of elements, which we have already found.  */
	  int n_done_elements = 0;

	  /* Buffer to read the register.  */
	  gdb_byte reg_buf[retval_size];

	  /* Offset at the read register buffer.  */
	  int reg_offset;

	  while (n_done_elements != n_elements_to_read)
	    {
	      regcache->cooked_read (retval_regnum, reg_buf);

	      /* The register has the format (read from right to left):
		 next elements... v[n_done_elements]... v[n_done_elements]
		 We set initial offset to the v[n_done_elements] from
		 the current SIMD lane.  Then we loop through the rest of
		 the read register and take next elements of the vector.
		 We find them by increasing this offset by 8 bytes at every
		 iteration, until the register is completed.  */
	      reg_offset = target_type_length * simd_lane;

	      while (n_done_elements != n_elements_to_read
		     && reg_offset < retval_size)
		{
		  /* Offset for the current element at the resulting
		     buffer.  */
		  int val_offset = n_done_elements * target_type_length;

		  /* Copy the current element to the resulting buffer
		     to the correct position.  */
		  memcpy (readbuf + val_offset, reg_buf + reg_offset,
			  target_type_length);

		  n_done_elements++;
		  reg_offset += 8;
		}

	      /* If we are not yet finished, at the next iteration we will
		 read the next register.  */
	      retval_regnum++;
	    }
	}
      else
	{
	  /* The return value takes a contiguous chunk in GRF.  */

	  CORE_ADDR offset = type_length * simd_lane;
	  /* One retval register contains that many values.  */
	  int n_values_in_retval_reg = retval_size / type_length;

	  /* Find at which register the return value is stored
	     for the current SIMD lane.  */
	  while (offset > retval_size)
	    {
	      /* The register RETVAL_REGNUM does not contain the return value
		 for the current SIMD lane.  Decrease the offset by the size of
		 data stored in this register and move to the next register.  */
	      offset -= n_values_in_retval_reg * type_length;
	      retval_regnum++;
	    }

	  /* Read the final value from the register with the remaining
	     offset.  */
	  regcache->cooked_read_part (retval_regnum, offset, type_length,
				      readbuf);
	}
    }

  return RETURN_VALUE_REGISTER_CONVENTION;
}

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, frame_info *next_frame)
{
  int pc_regnum = gdbarch_pc_regnum (gdbarch);
  CORE_ADDR prev_pc = frame_unwind_register_unsigned (next_frame,
						      pc_regnum);
  dprintf ("prev_pc: %lx", prev_pc);

  return prev_pc;
}

/* Frame unwinding.  */

static void
intelgt_frame_this_id (frame_info *this_frame, void **this_prologue_cache,
		       frame_id *this_id)
{
  /* FIXME: Other tdeps populate and use the cache.  */
  dprintf ("this_frame: %p", this_frame);

  /* Try to use symbol information to get the current start address.  */
  CORE_ADDR func = get_frame_func (this_frame);

  /* Use the current PC as a fallback if no symbol info is available.  */
  if (func == 0)
    func = get_frame_pc (this_frame);

  /* FIXME: Because there is no full notion of stack, it
     should be OK to ignore the SP reg.  Currently, we cannot use SP
     even if we want to, because SP's size is 16 bytes whereas
     CORE_ADDR is 8.  */
  *this_id = frame_id_build_unavailable_stack (func);
}

static value *
intelgt_frame_prev_register (frame_info *this_frame,
			     void **this_prologue_cache, int regnum)
{
  dprintf ("regnum %d", regnum);

  gdbarch *arch = get_frame_arch (this_frame);
  /* FIXME: Do the values below exist in an ABI?  */
  constexpr int STORAGE_REG_RET_PC = 1;
  constexpr int STORAGE_REG_SP = 125;

  if (regnum == gdbarch_pc_regnum (arch))
    return frame_unwind_got_register (this_frame, regnum,
				      STORAGE_REG_RET_PC);
  else if (regnum == gdbarch_sp_regnum (arch))
    return frame_unwind_got_register (this_frame, regnum,
				      STORAGE_REG_SP);
  else
    return frame_unwind_got_register (this_frame, regnum, regnum);
}

static const struct frame_unwind intelgt_unwinder =
  {
    NORMAL_FRAME,			/* type */
    default_frame_unwind_stop_reason,	/* stop_reason */
    intelgt_frame_this_id,		/* this_id */
    intelgt_frame_prev_register,	/* prev_register */
    nullptr,				/* unwind_data */
    default_frame_sniffer,		/* sniffer */
    nullptr,				/* dealloc_cache */
  };


/* The memory_insert_breakpoint gdbarch method.  */

static int
intelgt_memory_insert_breakpoint (gdbarch *gdbarch, struct bp_target_info *bp)
{
  dprintf ("req ip: %s", paddress (gdbarch, bp->reqstd_address));

  /* Ensure that we have enough space in the breakpoint.  */
  gdb_static_assert (intelgt::MAX_INST_LENGTH <= BREAKPOINT_MAX);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = target_read_memory (bp->reqstd_address, inst,
				intelgt::MAX_INST_LENGTH);
  if (err != 0)
    {
      /* We could fall back to reading a full and then a compacted
	 instruction but I think we should rather allow short reads than
	 having the caller try smaller and smaller sizes.  */
      dprintf ("Failed to read memory at %s (%s).",
	       paddress (gdbarch, bp->reqstd_address), strerror (err));
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
    dprintf ("Failed to insert breakpoint at %s (%s).",
	     paddress (gdbarch, bp->placed_address), strerror (err));

  return err;
}

/* The memory_remove_breakpoint gdbarch method.  */

static int
intelgt_memory_remove_breakpoint (gdbarch *gdbarch, struct bp_target_info *bp)
{
  dprintf ("req ip: %s, placed ip: %s",
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
    dprintf ("Failed to remove breakpoint at %s (%s).",
	     paddress (gdbarch, bp->placed_address), strerror (err));

  return err;
}

/* The program_breakpoint_here_p gdbarch method.  */

static bool
intelgt_program_breakpoint_here_p (gdbarch *gdbarch, CORE_ADDR pc)
{
  dprintf ("pc: %s", paddress (gdbarch, pc));

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int err = target_read_memory (pc, inst, intelgt::MAX_INST_LENGTH);
  if (err != 0)
    {
      /* We could fall back to reading a full and then a compacted
	 instruction but I think we should rather allow short reads than
	 having the caller try smaller and smaller sizes.  */
      dprintf ("Failed to read memory at %s (%s).",
	       paddress (gdbarch, pc), strerror (err));
      return err;
    }

  const bool is_bkpt = intelgt::has_breakpoint (inst);

  dprintf ("%sbreakpoint found.", is_bkpt ? "" : "no ");

  return is_bkpt;
}

/* The 'breakpoint_kind_from_pc' gdbarch method.
   This is a required gdbarch function.  */

static int
intelgt_breakpoint_kind_from_pc (gdbarch *gdbarch, CORE_ADDR *pcptr)
{
  dprintf ("*pcptr: %lx", *pcptr);

  return intelgt::BP_INSTRUCTION;
}

/* The 'sw_breakpoint_from_kind' gdbarch method.  */

static const gdb_byte *
intelgt_sw_breakpoint_from_kind (gdbarch *gdbarch, int kind, int *size)
{
  dprintf ("kind: %d", kind);

  /* We do not support breakpoint instructions.

     We use breakpoint bits in instructions, instead.  See
     intelgt_memory_insert_breakpoint.  */
  *size = 0;
  return nullptr;
}

/* Generic pointers are tagged in order to preserve the address
   space to which they are pointing.  Tags are encoded into [61:63] bits of
   an address:
   000/111 - global,
   001 - private,
   010 - local (SLM).  */
static CORE_ADDR
intelgt_pointer_to_address (gdbarch *gdbarch,
			    type *type, const gdb_byte *buf)
{
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR addr
    = extract_unsigned_integer (buf, TYPE_LENGTH (type), byte_order);

  unsigned long tag = addr >> 56;
  switch (tag)
    {
      /* Private.  */
    case 0x20ul:
      /* Global.  */
    case 0xe0ul:
      /* Mask out the tag, we want an address to the global address
	 space (first 8 bits are 0s).  */
      addr &= ~(((0x0ul << 8) - 1) << 56);
      break;
    default:
      if (tag != 0x0ul)
	dprintf (_("Address tag '%lx' not resolved."), tag);
    }

  return addr;
}

#if defined (HAVE_LIBIGA64)
/* Map CORE_ADDR to symbol names for jump labels in an IGA disassembly.  */

static const char *
intelgt_disasm_sym_cb (int addr, void *ctx)
{
  disassemble_info *info = (disassemble_info *) ctx;
  symbol *sym = find_pc_function (addr + (uintptr_t) info->private_data);
  return sym ? sym->linkage_name () : nullptr;
}
#endif /* defined (HAVE_LIBIGA64)  */

/* Print one instruction from MEMADDR on INFO->STREAM.  */

static int
intelgt_print_insn (bfd_vma memaddr, struct disassemble_info *info)
{
  unsigned int full_length = intelgt::inst_length_full ();
  unsigned int compact_length = intelgt::inst_length_compacted ();

  std::unique_ptr<bfd_byte[]> insn (new bfd_byte[full_length]);

  int status = (*info->read_memory_func) (memaddr, insn.get (),
					  compact_length, info);
  if (status != 0)
    {
      /* Aborts disassembling with a memory_error exception.  */
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }
  if (!intelgt::is_compacted_inst ((gdb_byte *) insn.get ()))
    {
      status = (*info->read_memory_func) (memaddr, insn.get (),
					  full_length, info);
      if (status != 0)
	{
	  /* Aborts disassembling with a memory_error exception.  */
	  (*info->memory_error_func) (status, memaddr, info);
	  return -1;
	}
    }

#if defined (HAVE_LIBIGA64)
  char *dbuf;
  iga_disassemble_options_t dopts = IGA_DISASSEMBLE_OPTIONS_INIT();
  gdb_disassembler *di
    = static_cast<gdb_disassembler *>(info->application_data);
  struct gdbarch *gdbarch = di->arch ();

  iga_context_t iga_ctx
    = get_intelgt_gdbarch_data (gdbarch)->iga_ctx;
  iga_status_t iga_status
    = iga_context_disassemble_instruction (iga_ctx, &dopts, insn.get (),
					   intelgt_disasm_sym_cb,
					   info, &dbuf);
  if (iga_status != IGA_SUCCESS)
    return -1;

  (*info->fprintf_func) (info->stream, "%s", dbuf);

  if (intelgt::is_compacted_inst ((gdb_byte *) insn.get ()))
    return compact_length;
  else
    return full_length;
#else
  printf_filtered (_("\nDisassemble feature not available: libiga64 "
		     "is missing.\n"));
  return -1;
#endif /* defined (HAVE_LIBIGA64)  */
}

/* Implementation of `address_class_type_flags_to_name' gdbarch method
   as defined in gdbarch.h.  */

static const char*
intelgt_address_class_type_flags_to_name (struct gdbarch *gdbarch,
					  int type_flags)
{
  if ((type_flags & INTELGT_TYPE_INSTANCE_FLAG_SLM) != 0)
    return INTELGT_SLM_ADDRESS_QUALIFIER;
  else
    return nullptr;
}

/* Implementation of `address_class_name_to_type_flags' gdbarch method,
   as defined in gdbarch.h.  */

static int
intelgt_address_class_name_to_type_flags (struct gdbarch *gdbarch,
					  const char* name,
					  int *type_flags_ptr)
{
  if (strcmp (name, INTELGT_SLM_ADDRESS_QUALIFIER) == 0)
    {
      *type_flags_ptr = INTELGT_TYPE_INSTANCE_FLAG_SLM;
      return 1;
    }
  else
    return 0;
}

/* Implementation of `address_space_from_type_flags' gdbarch method,
   as defined in gdbarch.h.  */

static const unsigned int
intelgt_address_space_from_type_flags (struct gdbarch *gdbarch,
				       int type_flags)
{
  if ((type_flags & INTELGT_TYPE_INSTANCE_FLAG_SLM) != 0)
    return 1;
  return 0;
}

/* Called by tdesc_use_registers each time a new regnum
   is assigned.  Used to track down assigned numbers for
   any important regnums.  */

static int
intelgt_unknown_register_cb (struct gdbarch *gdbarch, tdesc_feature *feature,
			     const char *reg_name, int possible_regnum)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  /* First, check if this a beginning of a not yet tracked regset
     assignment.  */

  for (int regset = 0; regset < intelgt::regset_count; ++regset)
    {
      if (data->regset_ranges[regset].start == -1
	  && feature->name == intelgt::dwarf_regset_features[regset])
	{
	  data->regset_ranges[regset].start = possible_regnum;
	  data->regset_ranges[regset].end
	      = feature->registers.size () + possible_regnum;
	  break;
	}
    }

  /* Second, check if it is any specific individual register that
     needs to be tracked.  */

  if (strcmp ("sp", reg_name) == 0)
    set_gdbarch_sp_regnum (gdbarch, possible_regnum);
  else if (strcmp ("ip", reg_name) == 0)
    set_gdbarch_pc_regnum (gdbarch, possible_regnum);
  else if (strcmp ("r26", reg_name) == 0)
    data->retval_regnum = possible_regnum;
  else if (strcmp ("emask", reg_name) == 0)
    data->emask_regnum = possible_regnum;

  return possible_regnum;
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
  gdbarch *gdbarch = gdbarch_alloc (&info, nullptr);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

#if defined (HAVE_LIBIGA64)
  /* There is currently no way to know on GDB side what GEN exactly it is
     working with.  Some testing has shown that using GEN9 for all supported
     platforms works at least for commonly used instructions.  Should be updated
     once remote protocol allows to report the used GEN version.  */
  iga_gen_t iga_version = IGA_GEN9;
  const iga_context_options_t options = IGA_CONTEXT_OPTIONS_INIT (iga_version);
  iga_context_create (&options, &data->iga_ctx);
#endif

  /* Initialize register info.  */
  set_gdbarch_num_regs (gdbarch, 0);
  set_gdbarch_register_name (gdbarch, tdesc_register_name);

  if (tdesc_has_registers (tdesc))
    {
      tdesc_arch_data *tdesc_data = tdesc_data_alloc ();

      /* First assign register numbers to all registers.  The
	 callback function will record any relevant metadata
	 about it in the intelgt_gdbarch_data instance to be
	 inspected after.  */

      tdesc_use_registers (gdbarch, tdesc, tdesc_data,
			   intelgt_unknown_register_cb);

      /* Now check the collected metadata to ensure that all
	 mandatory pieces are in place.  */

      if (gdbarch_sp_regnum (gdbarch) == -1)
	error ("Debugging requires $sp to be provided by the target");
      if (gdbarch_pc_regnum (gdbarch) == -1)
	error ("Debugging requires $ip to be provided by the target");
      if (data->emask_regnum == -1)
	error ("Debugging requires $emask provided by the target");
      if (data->retval_regnum == -1)
	error ("Debugging requires return value register to be provided by "
	       "the target");
    }

  /* Populate gdbarch fields.  */
  set_gdbarch_ptr_bit (gdbarch, 64);
  set_gdbarch_addr_bit (gdbarch, 64);

  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, intelgt_dwarf_reg_to_regnum);

  set_gdbarch_skip_prologue (gdbarch, intelgt_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_greaterthan);
  set_gdbarch_unwind_pc (gdbarch, intelgt_unwind_pc);
  dwarf2_append_unwinders (gdbarch);
  frame_unwind_append_unwinder (gdbarch, &intelgt_unwinder);

  set_gdbarch_return_value (gdbarch, intelgt_return_value);

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
  set_gdbarch_can_step_over_breakpoint (gdbarch, 1);
  set_gdbarch_pointer_to_address (gdbarch, intelgt_pointer_to_address);

  /* Disassembly */
  set_gdbarch_print_insn (gdbarch, intelgt_print_insn);

  set_gdbarch_active_lanes_mask (gdbarch, &intelgt_active_lanes_mask);

#if defined (USE_WIN32API)
  set_gdbarch_has_dos_based_file_system (gdbarch, 1);
#endif

  set_gdbarch_address_class_name_to_type_flags
    (gdbarch, intelgt_address_class_name_to_type_flags);
  set_gdbarch_address_class_type_flags_to_name
    (gdbarch, intelgt_address_class_type_flags_to_name);
  set_gdbarch_address_space_from_type_flags
    (gdbarch, intelgt_address_space_from_type_flags);

  return gdbarch;
}

/* Dump the target specific data for this architecture.  */

static void
intelgt_dump_tdep (gdbarch *gdbarch, ui_file *file)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  if (tdep == nullptr)
    return;

  /* Implement target-specific print output if and
     when gdbarch_tdep is defined for this architecture.  */
}

static void
show_intelgt_debug (ui_file *file, int from_tty,
		    cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("Intel(R) Graphics Technology debugging is "
			    "%s.\n"), value);
}

void _initialize_intelgt_tdep ();
void
_initialize_intelgt_tdep ()
{
  intelgt_gdbarch_data_handle
    = gdbarch_data_register_pre_init (init_intelgt_gdbarch_data);

  gdbarch_register (bfd_arch_intelgt, intelgt_gdbarch_init,
		    intelgt_dump_tdep);

  /* Debugging flag.  */
  add_setshow_boolean_cmd ("intelgt", class_maintenance, &intelgt_debug,
			   _("Set Intel(R) Graphics Technology debugging."),
			   _("Show Intel(R) Graphics Technology debugging."),
			   _("When on, Intel(R) Graphics Technology debugging"
			     "is enabled."),
			   nullptr,
			   show_intelgt_debug,
			   &setdebuglist, &showdebuglist);
}
