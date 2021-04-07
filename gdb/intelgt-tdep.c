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
#include "target.h"
#include "target-descriptions.h"
#include "value.h"
#if defined (HAVE_LIBIGA64)
#include "iga.h"
#endif /* defined (HAVE_LIBIGA64)  */
#include "gdbthread.h"

/* Feature names.  */

#define GT_FEATURE_GRF		"org.gnu.gdb.intelgt.grf"
#define GT_FEATURE_DEBUG	"org.gnu.gdb.intelgt.debug"
#define GT_FEATURE_ARF9		"org.gnu.gdb.intelgt.arf9"
#define GT_FEATURE_ARF11	"org.gnu.gdb.intelgt.arf11"
#define GT_FEATURE_ARF12	"org.gnu.gdb.intelgt.arf12"

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

/* The 'gdbarch_data' stuff specific for this architecture.  */

static struct gdbarch_data *intelgt_gdbarch_data_handle;

struct intelgt_gdbarch_data
{
  intelgt::arch_info *info;
#if defined (HAVE_LIBIGA64)
  /* libiga context for disassembly.  */
  iga_context_t iga_ctx = nullptr;
#endif
};

static void *
init_intelgt_gdbarch_data (obstack *obstack)
{
  return OBSTACK_ZALLOC (obstack, struct intelgt_gdbarch_data);
}

static intelgt_gdbarch_data *
get_intelgt_gdbarch_data (gdbarch *gdbarch)
{
  return ((intelgt_gdbarch_data *)
	  gdbarch_data (gdbarch, intelgt_gdbarch_data_handle));
}

static intelgt::arch_info *
get_intelgt_arch_info (gdbarch *gdbarch)
{
  return get_intelgt_gdbarch_data (gdbarch)->info;
}

/* The 'register_name' gdbarch method.  */

static const char *
intelgt_register_name (gdbarch *gdbarch, int regno)
{
  dprintf ("regno: %d", regno);

  if (tdesc_has_registers (gdbarch_target_desc (gdbarch)))
    return tdesc_register_name (gdbarch, regno);
  else
    {
      intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);
      if (regno < intelgt_info->num_registers ())
	return intelgt_info->get_register_name (regno);
      else
	return nullptr;
    }
}

/* The 'register_type' gdbarch method.  */

static type *
intelgt_register_type (gdbarch *gdbarch, int regno)
{
  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);
  unsigned short reg_size
    = (intelgt_info->get_register (regno)).size_in_bytes;
  switch (reg_size)
    {
    case 4:
      return builtin_type (gdbarch)->builtin_uint32;
    case 8:
      return builtin_type (gdbarch)->builtin_uint64;
    case 16:
      return builtin_type (gdbarch)->builtin_uint128;
    case 32:
    default:
      return builtin_type (gdbarch)->builtin_uint256;
    }
}

/* Convert a DWARF register number to a GDB register number.  */

static int
intelgt_dwarf_reg_to_regnum (gdbarch *gdbarch, int num)
{
  constexpr unsigned int IP = 0;
  constexpr unsigned int EMask = 1;
  constexpr unsigned int DebugBase = 5;
  constexpr unsigned int DebugLast = 10;
  constexpr unsigned int GRFBase = 16;
  constexpr unsigned int A0Base = 272;
  constexpr unsigned int F0Base = 288;
  constexpr unsigned int Acc0Base = 304;
  constexpr unsigned int Mme0Base = 335;

  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);

  const int grfs = intelgt_info->grf_reg_count ();
  const int addresses = intelgt_info->address_reg_count ();
  const int accumulators = intelgt_info->acc_reg_count ();
  const int flags = intelgt_info->flag_reg_count ();
  const int mmes = intelgt_info->mme_reg_count ();

  if (num < GRFBase)
    {
      if (num == IP)
	return intelgt_info->pc_regnum ();
      if (num == EMask)
	return intelgt_info->emask_regnum ();
      if (num >= DebugBase && num <= DebugLast)
	return intelgt_info->debug_reg_base () - DebugBase + num;
    }
  else if (num >= GRFBase && num < A0Base)
    {
      int regnum = num - GRFBase;
      if (regnum < grfs)
	return regnum;
    }
  else if (num >= A0Base && num < F0Base)
    {
      int regnum = num - A0Base;
      if (regnum < addresses)
	return intelgt_info->address_reg_base () + regnum;
    }
  else if (num >= F0Base && num < Acc0Base)
    {
      int regnum = num - F0Base;
      if (regnum < flags)
	return intelgt_info->flag_reg_base () + regnum;

    }
  else if (num >= Acc0Base && num < Mme0Base)
    {
      int regnum = num - Acc0Base;
      if (regnum < accumulators)
	return intelgt_info->acc_reg_base () + regnum;
    }
  else if (num >= Mme0Base)
    {
      int regnum = num - Mme0Base;
      if (regnum < mmes)
	return intelgt_info->mme_reg_base () + regnum;
    }

  dprintf ("Dwarf regnum %d not recognized", num);
  return -1;
}

/* Return active lanes mask for the specified thread TP.  */

static unsigned int
intelgt_active_lanes_mask (struct gdbarch *gdbarch, thread_info *tp)
{
  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);
  int regnum_emask = intelgt_info->emask_regnum ();
  regcache *thread_regcache = get_thread_regcache (tp);

  return regcache_raw_get_unsigned (thread_regcache, regnum_emask);
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

  const intelgt::arch_info * const intelgt_info
    = get_intelgt_arch_info (gdbarch);

  bp->placed_address = bp->reqstd_address;
  bp->shadow_len = intelgt_info->inst_length (inst);

  /* Make a copy before we set the breakpoint so we can restore the
     original instruction when removing the breakpoint again.

     This isn't strictly necessary but it saves one target access.  */
  memcpy (bp->shadow_contents, inst, bp->shadow_len);

  const bool already = intelgt_info->set_breakpoint (inst);
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

  const intelgt::arch_info * const intelgt_info
    = get_intelgt_arch_info (gdbarch);

  /* Warn if we're inserting a permanent breakpoint.  */
  if (intelgt_info->has_breakpoint (bp->shadow_contents))
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

  const intelgt::arch_info * const intelgt_info
    = get_intelgt_arch_info (gdbarch);
  const bool is_bkpt = intelgt_info->has_breakpoint (inst);

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

/* Check the tdesc for validity.  */

static intelgt::version
intelgt_version_from_tdesc (const target_desc *tdesc)
{
  if (!tdesc_has_registers (tdesc))
    {
      /* We assume a default feature in this case.  */
      return intelgt::version::Gen9;
    }

  /* We have to have the GRF feature + the debug feature + an ARF feature.  */
  gdb_assert (tdesc_find_feature (tdesc, GT_FEATURE_GRF) != nullptr);
  gdb_assert (tdesc_find_feature (tdesc, GT_FEATURE_DEBUG) != nullptr);

  const tdesc_feature *feature
    = tdesc_find_feature (tdesc, GT_FEATURE_ARF9);
  if (feature != nullptr)
    return intelgt::version::Gen9;

  feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF11);
  if (feature != nullptr)
    return intelgt::version::Gen11;

  feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF12);
  if (feature != nullptr)
    return intelgt::version::Gen12;

  error (_("A supported Intel(R) Graphics Technology feature was not "
	   "found"));
}

/* Initialize architectural information.  The TDESC must be validated
   prior to calling this function.  */

static void
intelgt_initialize_gdbarch_data (const target_desc *tdesc,
				 gdbarch *gdbarch)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  intelgt::version gt_version = intelgt_version_from_tdesc (tdesc);

  data->info = intelgt::arch_info::get_or_create (gt_version);

#if defined (HAVE_LIBIGA64)
  iga_gen_t iga_version = IGA_GEN_INVALID;
  if (gt_version == intelgt::version::Gen9)
    iga_version = IGA_GEN9;
  else if (gt_version == intelgt::version::Gen11)
    iga_version = IGA_GEN11;
  else if (gt_version == intelgt::version::Gen12)
    iga_version = IGA_GEN12p1;

  if (iga_version != IGA_GEN_INVALID)
    {
      const iga_context_options_t options
        = IGA_CONTEXT_OPTIONS_INIT (iga_version);
      iga_context_create (&options, &data->iga_ctx);
    }
#endif
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
  gdb_disassembler *di
    = static_cast<gdb_disassembler *>(info->application_data);
  struct gdbarch *gdbarch = di->arch ();
  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);

  unsigned int full_length = intelgt_info->inst_length_full ();
  unsigned int compact_length = intelgt_info->inst_length_compacted ();

  std::unique_ptr<bfd_byte[]> insn (new bfd_byte[full_length]);

  int status = (*info->read_memory_func) (memaddr, insn.get (),
					  compact_length, info);
  if (status != 0)
    {
      /* Aborts disassembling with a memory_error exception.  */
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }
  if (!intelgt_info->is_compacted_inst ((gdb_byte *) insn.get ()))
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

  iga_context_t iga_ctx
    = get_intelgt_gdbarch_data (gdbarch)->iga_ctx;
  iga_status_t iga_status
    = iga_context_disassemble_instruction (iga_ctx, &dopts, insn.get (),
					   intelgt_disasm_sym_cb,
					   info, &dbuf);
  if (iga_status != IGA_SUCCESS)
    return -1;

  (*info->fprintf_func) (info->stream, "%s", dbuf);

  if (intelgt_info->is_compacted_inst ((gdb_byte *) insn.get ()))
    return compact_length;
  else
    return full_length;
#else
  printf_filtered (_("\nDisassemble feature not available: libiga64 "
		     "is missing.\n"));
  return -1;
#endif /* defined (HAVE_LIBIGA64)  */
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
  intelgt_initialize_gdbarch_data (tdesc, gdbarch);
  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);

  /* Populate tdesc_data if registers are available.  */
  tdesc_arch_data *tdesc_data = nullptr;
  if (tdesc_has_registers (tdesc))
    {
      tdesc_data = tdesc_data_alloc ();

      /* Fill in data for GRF registers.  */
      const tdesc_feature *feature
	= tdesc_find_feature (tdesc, GT_FEATURE_GRF);
      for (int i = 0; i < intelgt_info->grf_reg_count (); i++)
	{
	  const char *name = intelgt_info->get_register_name (i);
	  int valid
	    = tdesc_numbered_register (feature, tdesc_data, i, name);

	  if (!valid)
	    {
	      dprintf ("GRF register %d '%s' not found", i, name);
	      tdesc_data_cleanup (tdesc_data);
	      return nullptr;
	    }
	}

      /* Fill in data for the virtual debug registers.  */
      feature = tdesc_find_feature (tdesc, GT_FEATURE_DEBUG);
      int start = intelgt_info->debug_reg_base ();
      int count = intelgt_info->debug_reg_count ();
      for (int i = start; i < start + count; i++)
	{
	  const char *name = intelgt_info->get_register_name (i);
	  int valid
	    = tdesc_numbered_register (feature, tdesc_data, i, name);

	  if (!valid)
	    {
	      dprintf ("Debug register %d '%s' not found", i, name);
	      tdesc_data_cleanup (tdesc_data);
	      return nullptr;
	    }
	}

      /* Fill in data for ARF registers.  */
      feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF9);

      if (feature == nullptr) /* Try again.  */
	feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF11);

      if (feature == nullptr) /* Try again.  */
	feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF12);

      if (feature != nullptr)
	{
	  dprintf ("Found feature %s", feature->name.c_str ());
	  int i = intelgt_info->address_reg_base ();
	  for (; i < intelgt_info->num_registers (); i++)
	    {
	      const char *name = intelgt_info->get_register_name (i);
	      int valid
		= tdesc_numbered_register (feature, tdesc_data, i, name);

	      if (!valid)
		{
		  dprintf ("ARF register %d '%s' not found", i, name);
		  tdesc_data_cleanup (tdesc_data);
		  return nullptr;
		}
	    }
	}
    }

  /* Populate gdbarch fields.  */
  set_gdbarch_ptr_bit (gdbarch, 64);
  set_gdbarch_addr_bit (gdbarch, 64);

  set_gdbarch_num_regs (gdbarch, intelgt_info->num_registers ());
  dprintf ("PC regnum: %d, SP regnum: %d, EMASK regnum: %d",
	   intelgt_info->pc_regnum (), intelgt_info->sp_regnum (),
	   intelgt_info->emask_regnum ());
  set_gdbarch_pc_regnum (gdbarch, intelgt_info->pc_regnum ());
  set_gdbarch_sp_regnum (gdbarch, intelgt_info->sp_regnum ());
  set_gdbarch_register_name (gdbarch, intelgt_register_name);
  set_gdbarch_register_type (gdbarch, intelgt_register_type);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, intelgt_dwarf_reg_to_regnum);

  set_gdbarch_skip_prologue (gdbarch, intelgt_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
  set_gdbarch_unwind_pc (gdbarch, intelgt_unwind_pc);
  dwarf2_append_unwinders (gdbarch);
  frame_unwind_append_unwinder (gdbarch, &intelgt_unwinder);

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

  if (tdesc_data != nullptr)
    tdesc_use_registers (gdbarch, tdesc, tdesc_data);

#if defined (USE_WIN32API)
  set_gdbarch_has_dos_based_file_system (gdbarch, 1);
#endif

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
