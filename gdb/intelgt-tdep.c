/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019 Free Software Foundation, Inc.

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
#include "gdbsupport/gdb_obstack.h"
#include "target.h"
#include "target-descriptions.h"
#include "value.h"

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
  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);
  unsigned short reg_size
    = (intelgt_info->get_register (regno)).size_in_bytes;
  switch (reg_size)
    {
    case 4:
      return builtin_type (gdbarch)->builtin_uint32;
    case 16:
      return builtin_type (gdbarch)->builtin_uint128;
    case 32:
    default:
      return builtin_type (gdbarch)->builtin_uint256;
    }
}

/* Return the PC of the first real instruction.  */

static CORE_ADDR
intelgt_skip_prologue (gdbarch *gdbarch, CORE_ADDR start_pc)
{
  dprintf ("start_pc: %lx", start_pc);

  /* For now there are no function calls, so no prologues.  */
  return start_pc;
}

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, frame_info_ptr next_frame)
{
  int pc_regnum = gdbarch_pc_regnum (gdbarch);
  CORE_ADDR prev_pc = frame_unwind_register_unsigned (next_frame,
						      pc_regnum);
  dprintf ("prev_pc: %lx", prev_pc);

  return prev_pc;
}

/* Frame unwinding.  */

static void
intelgt_frame_this_id (frame_info_ptr this_frame, void **this_prologue_cache,
		       frame_id *this_id)
{
  /* FIXME: Other tdeps populate and use the cache.  */

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
intelgt_frame_prev_register (frame_info_ptr this_frame,
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
    "intelgt prologue",
    NORMAL_FRAME,			/* type */
    default_frame_unwind_stop_reason,	/* stop_reason */
    intelgt_frame_this_id,		/* this_id */
    intelgt_frame_prev_register,	/* prev_register */
    nullptr,				/* unwind_data */
    default_frame_sniffer,		/* sniffer */
    nullptr,				/* dealloc_cache */
  };

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
intelgt_sw_breakpoint_from_kind (gdbarch *gdbarch,
				 int kind, int *size)
{
  dprintf ("kind: %d", kind);

  intelgt::arch_info *intelgt_info = get_intelgt_arch_info (gdbarch);

  switch (kind)
    {
    case intelgt::BP_INSTRUCTION:
      *size = intelgt_info->breakpoint_inst_length ();
      return intelgt_info->breakpoint_inst ();
    }

  dprintf ("Unrecognized breakpoint kind: %d", kind);

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

  set_gdbarch_breakpoint_kind_from_pc (gdbarch,
				       intelgt_breakpoint_kind_from_pc);
  set_gdbarch_sw_breakpoint_from_kind (gdbarch,
				       intelgt_sw_breakpoint_from_kind);

  if (tdesc_data != nullptr)
    tdesc_use_registers (gdbarch, tdesc, std::move (tdesc_data));

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
