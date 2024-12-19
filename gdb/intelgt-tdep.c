/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019-2020 Free Software Foundation, Inc.

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
#include "extract-store-integer.h"
#include "frame-unwind.h"
#include "cli/cli-cmds.h"
#include "gdbsupport/gdb_obstack.h"
#include "target.h"
#include "target-descriptions.h"
#include "value.h"
#include "disasm.h"
#if defined (HAVE_LIBIGA64)
#include "iga/iga.h"
#endif /* defined (HAVE_LIBIGA64)  */
#include "gdbthread.h"

/* Feature names.  */

#define GT_FEATURE_GRF		"org.gnu.gdb.intelgt.grf"
#define GT_FEATURE_ARF9		"org.gnu.gdb.intelgt.arf9"

/* Global debug flag.  */
static bool intelgt_debug = false;

#define dprintf(...)						\
  do								\
    {								\
      if (intelgt_debug)					\
	{							\
	  gdb_printf (gdb_stdlog, "%s: ", __func__);		\
	  gdb_printf (gdb_stdlog, __VA_ARGS__);			\
	  gdb_printf (gdb_stdlog, "\n");			\
	}							\
    }								\
  while (0)

/* The 'gdbarch_data' stuff specific for this architecture.  */

struct intelgt_gdbarch_data
{
  intelgt::arch_info *info;
#if defined (HAVE_LIBIGA64)
  /* libiga context for disassembly.  */
  iga_context_t iga_ctx = nullptr;
#endif
};

static const registry<gdbarch>::key<intelgt_gdbarch_data>
    intelgt_gdbarch_data_handle;

static intelgt_gdbarch_data *
get_intelgt_gdbarch_data (gdbarch *gdbarch)
{
  intelgt_gdbarch_data *result = intelgt_gdbarch_data_handle.get (gdbarch);
  if (result == nullptr)
    result = intelgt_gdbarch_data_handle.emplace (gdbarch);
  return result;
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
  type *typ = tdesc_register_type (gdbarch, regno);
  return typ;
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

  /* For now there are no function calls, so no prologues.  */
  return start_pc;
}

/* Implementation of gdbarch's return_value method.  */

static enum return_value_convention
intelgt_return_value (gdbarch *gdbarch, value *function,
		      type *valtype, regcache *regcache,
		      gdb_byte *readbuf, const gdb_byte *writebuf)
{
  gdb_assert_not_reached ("intelgt_return_value is to be implemented later.");
}

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, const frame_info_ptr &next_frame)
{
  int pc_regnum = gdbarch_pc_regnum (gdbarch);
  CORE_ADDR prev_pc = frame_unwind_register_unsigned (next_frame,
						      pc_regnum);
  dprintf ("prev_pc: %lx", prev_pc);

  return prev_pc;
}

/* Frame unwinding.  */

static void
intelgt_frame_this_id (const frame_info_ptr &this_frame,
		       void **this_prologue_cache,
		       frame_id *this_id)
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
intelgt_memory_insert_breakpoint (gdbarch *gdbarch, struct bp_target_info *bp)
{
  dprintf ("req ip: %s", paddress (gdbarch, bp->reqstd_address));

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

  /* We have to have the GRF feature, plus an ARF feature.  */
  gdb_assert (tdesc_find_feature (tdesc, GT_FEATURE_GRF) != nullptr);

  const tdesc_feature *feature
    = tdesc_find_feature (tdesc, GT_FEATURE_ARF9);
  if (feature != nullptr)
    return intelgt::version::Gen9;

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

  if (iga_version != IGA_GEN_INVALID)
    {
      const iga_context_options_t options
	= IGA_CONTEXT_OPTIONS_INIT (iga_version);
      iga_context_create (&options, &data->iga_ctx);
    }
#endif
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
  gdb_disassemble_info *di
    = static_cast<gdb_disassemble_info *>(info->application_data);
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
  iga_disassemble_options_t dopts = IGA_DISASSEMBLE_OPTIONS_INIT ();

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
  gdb_printf (_("\nDisassemble feature not available: libiga64 "
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
  tdesc_arch_data_up tdesc_data = nullptr;
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
	    = tdesc_numbered_register (feature, tdesc_data.get (), i, name);

	  if (!valid)
	    {
	      dprintf ("Register '%s' not found", name);
	      return nullptr;
	    }
	}

      /* Fill in data for ARF registers.  */
      feature = tdesc_find_feature (tdesc, GT_FEATURE_ARF9);

      if (feature != nullptr)
	{
	  dprintf ("Found feature %s", feature->name.c_str ());
	  int i = intelgt_info->grf_reg_count ();
	  for (; i < intelgt_info->num_registers (); i++)
	    {
	      const char *name = intelgt_info->get_register_name (i);
	      int valid
		= tdesc_numbered_register (feature, tdesc_data.get (), i, name);

	      if (!valid)
		{
		  dprintf ("Register '%s' not found", name);
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

  set_gdbarch_skip_prologue (gdbarch, intelgt_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
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

  /* Disassembly.  */
  set_gdbarch_print_insn (gdbarch, intelgt_print_insn);

  set_gdbarch_active_lanes_mask (gdbarch, &intelgt_active_lanes_mask);

  if (tdesc_data != nullptr)
    tdesc_use_registers (gdbarch, tdesc, std::move (tdesc_data));

#if defined (USE_WIN32API)
  set_gdbarch_has_dos_based_file_system (gdbarch, 1);
#endif

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
			   _("When on, Intel(R) Graphics Technology debugging"
			     "is enabled."),
			   nullptr,
			   show_intelgt_debug,
			   &setdebuglist, &showdebuglist);
}
