/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019-2022 Free Software Foundation, Inc.

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
#include "user-regs.h"
#include <algorithm>

/* Address space flags.
   We are assigning the TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1 to the shared
   local memory address space.  */

#define INTELGT_TYPE_INSTANCE_FLAG_SLM TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1
#define INTELGT_SLM_ADDRESS_QUALIFIER "slm"

/* The maximum number of GRF registers to be used when passing function
   arguments.  */
constexpr int INTELGT_MAX_GRF_REGS_FOR_ARGS = 12;

/* The maximum size in bytes of a promotable struct return value.  */
constexpr int PROMOTABLE_STRUCT_MAX_SIZE_RET = 8;

/* The maximum size in bytes of a promotable struct argument.  */
constexpr int PROMOTABLE_STRUCT_MAX_SIZE_ARG = 16;

/* Intelgt FE stack alignment size in bytes.  */
constexpr int OWORD_SIZE = 16;

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
static int intelgt_pseudo_register_num (gdbarch*, const char*);
static bool is_a_promotable_small_struct (type *arg_type, int max_size);
static unsigned int get_field_total_memory (type *struct_type,
					    int field_index);

/* Structure for GRF read / write handling.  */

struct grf_handler
{
public:
  grf_handler (uint32_t reg_size, regcache * regcache)
      : m_reg_size (reg_size), m_regcache (regcache)
  {
  }

  /* Read small structures from GRFs into BUFF.  */
  void read_small_struct (int regnum, type *valtype, gdb_byte *buff);

  /* Write small structures from BUFF into GRFs.  */
  void write_small_struct (int regnum, type *valtype, const gdb_byte *buff);

  /* Read vectors from GRFs into BUFF.  */
  void read_vector (int regnum, type *valtype, gdb_byte *buff);

  /* Write vectors from BUFF into GRFs.  */
  void write_vector (int regnum, type *valtype, const gdb_byte *buff);

  /* Read integers from GRFs into BUFF.  */
  void read_integer (int regnum, int len, gdb_byte *buff);

  /* Write integers from BUFF into GRFs.  */
  void write_integer (int regnum, int len, const gdb_byte *buff);

private:
  uint32_t m_reg_size;
  regcache *m_regcache;

  /* Read and write small structures to GRF registers while considering
     the SIMD vectorization.

     REGNUM is the index of the first register for data storage.
     BUFF_READ is a non-NULL pointer to read data from when performing
     registers write.  It is NULL when performing registers read.
     BUFF_WRITE is a non-NULL writable pointer that will contain the data
     read from GRFs.  It is a NULL if we are performing registers write.
     VALTYPE is the type of the structure.  */

  void handle_small_struct (int regnum, const gdb_byte *buff_read,
			    gdb_byte *buff_write, type *valtype);

  /* Read and write vector values to GRF registers while considering the SIMD
     vectorization.

     REGNUM is the index of the first register for data storage.
     BUFF_READ is a non-NULL pointer to read data from when performing
     registers write.  It is NULL when performing registers read.
     BUFF_WRITE is a non-NULL writable pointer that will contain the data
     read from GRFs.  It is a NULL if we are performing registers write.
     VALTYPE is the type of the vector.  */

  void handle_vector (int regnum, const gdb_byte *buff_read,
		      gdb_byte *buff_write, type *valtype);

  /* Read and write up to 8 bytes to GRF registers while considering the SIMD
     vectorization.

     REGNUM is the index of the first register for data storage.
     BUFF_READ is a non-NULL pointer to read data from when performing
     registers write.  It is NULL when performing registers read.
     BUFF_WRITE is a non-NULL writable pointer that will contain the data
     read from GRFs.  It is a NULL if we are performing registers write.
     LEN is the length in bytes to read or write on the GRFs.  */

  void handle_integer (int regnum, const gdb_byte *buff_read,
		       gdb_byte *buff_write, int len);
};

struct intelgt_gdbarch_data
{
  /* $emask register number in the regcache.  */
  int emask_regnum = -1;
  /* Register number for the GRF containing function return value.  */
  int retval_regnum = -1;
  /* Register number for the control register.  */
  int cr0_regnum = -1;
  /* Register number for the state register.  */
  int sr0_regnum = -1;
  /* Register number for the instruction base virtual register.  */
  int isabase_regnum = -1;
  /* Assigned regnum ranges for DWARF regsets.  */
  regnum_range regset_ranges[intelgt::regset_count];
  /* Enabled pseudo-register for the current target description.  */
  std::vector<std::string> enabled_pseudo_regs;
  /* Cached $framedesc pseudo-register type.  */
  type *framedesc_type = nullptr;

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

static int
intelgt_pseudo_register_num (gdbarch *arch, const char *name);

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

  /* Register sets follow this format: [BEGIN, END), where BEGIN is inclusive
     and END is exclusive.  */
  constexpr regnum_range dwarf_nums[intelgt::regset_count] = {
    [intelgt::regset_sba] = { 5, 12 },
    [intelgt::regset_grf] = { 16, 272 },
    [intelgt::regset_addr] = { 272, 288 },
    [intelgt::regset_flag] = { 288, 304 },
    [intelgt::regset_acc] = { 304, 320 },
    [intelgt::regset_mme] = { 320, 336 },
  };

  /* Number of SBA registers.  */
  constexpr size_t sba_dwarf_len = dwarf_nums[intelgt::regset_sba].end
    - dwarf_nums[intelgt::regset_sba].start;

  /* Map the DWARF register numbers of SBA registers to their names.
     Base number is dwarf_nums[intelgt::regset_sba].start.  */
  constexpr const char* sba_dwarf_reg_order[sba_dwarf_len] {
    "btbase",
    "scrbase",
    "genstbase",
    "sustbase",
    "blsustbase",
    "blsastbase",
    "scrbase2"
  };

  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  if (num == ip)
    return intelgt_pseudo_register_num (gdbarch, "ip");
  if (num == emask)
    return data->emask_regnum;

  for (int regset = 0; regset < intelgt::regset_count; ++regset)
    if (num >= dwarf_nums[regset].start && num < dwarf_nums[regset].end)
      {
	if (regset == intelgt::regset_sba)
	  {
	    /* For SBA registers we first find out the name of the register
	       out of DWARF register number and then find the register number
	       corresponding to the name.  */
	    int sba_num = num - dwarf_nums[intelgt::regset_sba].start;
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

/* Return active lanes mask for the specified thread TP.  */

static unsigned int
intelgt_active_lanes_mask (struct gdbarch *gdbarch, thread_info *tp)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  regcache *thread_regcache = get_thread_regcache (tp);

  /* Default to zero if the emask register is not available.  This may
     happen if TP is not available.  */
  ULONGEST emask = 0ull;
  regcache_cooked_read_unsigned (thread_regcache, data->emask_regnum,
				 &emask);

  /* The higher bits of emask are undefined if they are outside the
     dispatch mask range.  Clear them explicitly using the dispatch
     mask, which is at SR0.2.  SR0 elements are 4 byte wide.  */
  uint32_t sr0_2 = 0;
  thread_regcache->raw_read_part (data->sr0_regnum, sizeof (uint32_t) * 2,
				  sizeof (sr0_2), (gdb_byte *) &sr0_2);

  dprintf ("emask: %lx, dmask: %x", emask, sr0_2);

  /* The higher bits of dmask are undefined if they are outside the
     SIMD width.  Clear them explicitly.
     We cannot query the simd-width if the thread is executing.  */
  if (tp->executing)
    return 0;

  unsigned int width = tp->get_simd_width ();
  uint32_t width_mask = 0xffffffff;
  if (width < 32)
    width_mask = ~(width_mask << width);

  dprintf ("width: %d, width_mask: %x", width, width_mask);

  /* With ESIMD, width would be 1.  Skip masking.
     FIXME: Need a solution to address this case.  */
  if (width > 1)
    sr0_2 &= width_mask;

  return emask & sr0_2;
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
  gdb_assert (inferior_ptid != null_ptid);

  int address_size_byte = gdbarch_addr_bit (gdbarch) / 8;

  /* The vectorized return value is stored at this register and onwards.  */
  int retval_regnum = get_intelgt_gdbarch_data (gdbarch)->retval_regnum;
  unsigned int retval_size = register_size (gdbarch, retval_regnum);
  int type_length = TYPE_LENGTH (valtype);
  auto grf = grf_handler (retval_size, regcache);

  /* Promotable structures are returned by value on registers.  */
  if (is_a_promotable_small_struct (valtype, PROMOTABLE_STRUCT_MAX_SIZE_RET))
    {
      if (readbuf != nullptr)
	grf.read_small_struct (retval_regnum, valtype, readbuf);

      return RETURN_VALUE_REGISTER_CONVENTION;
    }

  /* Values greater than 64 bits (64 is specified by ABI) or non-promotable
     structs are stored by reference.  The return value register contains a
     vectorized sequence of memory addresses.  */
  if (type_length > 8 || class_or_union_p (valtype))
    {
      if (readbuf != nullptr)
	{
	  /* Read the address to a temporary buffer.  */
	  CORE_ADDR addr = 0;
	  grf.read_integer (retval_regnum, address_size_byte,
			    (gdb_byte *) &addr);
	  /* Read the value to the resulting buffer.  */
	  target_read_memory (addr, readbuf, type_length);
	}

      return RETURN_VALUE_ABI_RETURNS_ADDRESS;
    }

  /* Return value is stored in the return register.  */
  if (readbuf != nullptr)
    {
      /* Read vector value from GRFs.  */
      if (valtype->is_vector ())
	grf.read_vector (retval_regnum, valtype, readbuf);

      /* Read primitive values from GRFs.  */
      else
	grf.read_integer (retval_regnum, type_length, readbuf);
    }

  return RETURN_VALUE_REGISTER_CONVENTION;
}

static void
intelgt_init_reg (gdbarch *gdbarch, int regnum, dwarf2_frame_state_reg *reg,
		  frame_info *this_frame)
{
  int ip_regnum = intelgt_pseudo_register_num (gdbarch, "ip");
  if (regnum == ip_regnum)
    reg->how = DWARF2_FRAME_REG_RA;
  else if (regnum == gdbarch_sp_regnum (gdbarch))
    reg->how = DWARF2_FRAME_REG_CFA;
}

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, frame_info *next_frame)
{
  /* Use ip register here, as IGC uses 32bit values (pc is 64bit).  */
  int ip_regnum = intelgt_pseudo_register_num (gdbarch, "ip");
  CORE_ADDR prev_ip = frame_unwind_register_unsigned (next_frame,
						      ip_regnum);
  dprintf ("prev_ip: %lx", prev_ip);

  /* Program counter is $ip + $isabase.  Read directly from the
     regcache instead of unwinding, as the frame unwind info may
     simply be unavailable.  The isabase register does not change
     during kernel execution, so this must be safe.  */
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  gdb_assert (data->isabase_regnum != -1);
  uint64_t isabase = 0;
  if (get_current_regcache ()->cooked_read (data->isabase_regnum,
					    &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR,
		 _ ("Register %d (isabase) is not available"),
		 data->isabase_regnum);

  return (CORE_ADDR) isabase + prev_ip;
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

  if (regnum == intelgt_pseudo_register_num (arch, "ip"))
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
					  type_instance_flags type_flags)
{
  if ((type_flags & INTELGT_TYPE_INSTANCE_FLAG_SLM) != 0)
    return INTELGT_SLM_ADDRESS_QUALIFIER;
  else
    return nullptr;
}

/* Implementation of `address_class_name_to_type_flags' gdbarch method,
   as defined in gdbarch.h.  */

static bool
intelgt_address_class_name_to_type_flags (struct gdbarch *gdbarch,
					  const char* name,
					  type_instance_flags *type_flags_ptr)
{
  if (strcmp (name, INTELGT_SLM_ADDRESS_QUALIFIER) == 0)
    {
      *type_flags_ptr = INTELGT_TYPE_INSTANCE_FLAG_SLM;
      return true;
    }
  else
    return false;
}

/* Implementation of `address_space_from_type_flags' gdbarch method,
   as defined in gdbarch.h.  */

static const unsigned int
intelgt_address_space_from_type_flags (struct gdbarch *gdbarch,
				       type_instance_flags type_flags)
{
  if ((type_flags & INTELGT_TYPE_INSTANCE_FLAG_SLM) != 0)
    return 1;
  return 0;
}

/* Utility function to lookup the pseudo-register number by name.  Exact
   amount of pseudo-registers may differ and thus fixed constants can't be
   used for this.  */

static int
intelgt_pseudo_register_num (gdbarch *arch, const char *name)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  auto iter = std::find (data->enabled_pseudo_regs.begin (),
			 data->enabled_pseudo_regs.end (), name);
  gdb_assert (iter != data->enabled_pseudo_regs.end ());
  return gdbarch_num_regs (arch) + (iter - data->enabled_pseudo_regs.begin ());
}

static CORE_ADDR
intelgt_read_pc (readable_regcache *regcache)
{
  gdbarch *arch = regcache->arch ();
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  /* $ip is uint32_t, but uint64_t is used here to comply with cooked_read
     signature.  */
  uint64_t ip;
  int ip_regnum = intelgt_pseudo_register_num (arch, "ip");
  if (regcache->cooked_read (ip_regnum, &ip) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR, _ ("Register %d is not available"),
		 data->cr0_regnum);

  /* Program counter is $ip + $isabase.  */
  gdb_assert (data->isabase_regnum != -1);
  uint64_t isabase;
  if (regcache->cooked_read (data->isabase_regnum, &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR, _ ("Register %d is not available"),
		 data->isabase_regnum);
  return isabase + ip;
}

static void
intelgt_write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  gdbarch *arch = regcache->arch ();
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  /* Program counter is $ip + $isabase, can only modify $ip.  Need
     to ensure that the new value fits within $ip modification rannge
     and propagate the write accordingly.  */
  uint64_t isabase;
  gdb_assert (data->isabase_regnum != -1);
  if (regcache->cooked_read (data->isabase_regnum, &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR, _ ("Register %d is not available"),
		 data->isabase_regnum);
  if (pc < isabase || pc > isabase + UINT32_MAX)
    error ("Can't update $pc to value 0x%lx, out of range", pc);
  /* $ip is uint32_t, but uint64_t is used here to comply with cooked_write
     signature.  */
  uint64_t ip = pc - isabase;
  int ip_regnum = intelgt_pseudo_register_num (arch, "ip");
  regcache->cooked_write (ip_regnum, ip);
}

/* Return the name of pseudo-register REGNUM.  */

static const char *
intelgt_pseudo_register_name (gdbarch *arch, int regnum)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
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
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);

  if (strcmp (name, "framedesc") == 0)
    {
      if (data->framedesc_type != nullptr)
	return data->framedesc_type;
      type *frame = arch_composite_type (arch, "frame_desc", TYPE_CODE_STRUCT);
      append_composite_type_field (frame, "return_ip", bt->builtin_uint32);
      append_composite_type_field (frame, "return_emask", bt->builtin_uint32);
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
				    readable_regcache *regcache, int regnum)
{
  const char *name = intelgt_pseudo_register_name (arch, regnum);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  type *regtype = register_type (arch, regnum);
  int regsize = register_size (arch, regnum);

  value *result = allocate_value (regtype);
  VALUE_LVAL (result) = lval_register;
  VALUE_REGNUM (result) = regnum;
  gdb_byte *result_buf = value_contents_writeable (result);

  if (strcmp (name, "framedesc") == 0)
    {
      /* Frame description is stored in GRF count-3.  */
      gdb_assert (data->regset_ranges[intelgt::regset_grf].end > 3);
      int grf_num = data->regset_ranges[intelgt::regset_grf].end - 3;
      value *grf = regcache->cooked_read_value (grf_num);
      if (!value_entirely_available (grf))
	throw_error (NOT_AVAILABLE_ERROR, _ ("Register %d is not available"),
		     grf_num);
      gdb_assert (regsize <= register_size (arch, grf_num));
      memcpy (result_buf, value_contents_raw (grf), regsize);
      return result;
    }
  else if (strcmp (name, "ip") == 0)
    {
      /* Instruction pointer is stored in CR0.2.  */
      gdb_assert (data->cr0_regnum != -1);
      value *cr0 = regcache->cooked_read_value (data->cr0_regnum);
      if (!value_entirely_available (cr0))
	throw_error (NOT_AVAILABLE_ERROR, _ ("Register %d is not available"),
		     data->cr0_regnum);
      /* CR0 elements are 4 byte wide.  */
      gdb_assert (regsize + 8 <= register_size (arch, data->cr0_regnum));
      memcpy (result_buf, value_contents_raw (cr0) + 8, regsize);
      return result;
    }

  return nullptr;
}

/* Write the value of a pseudo-register REGNUM.  */

static void
intelgt_pseudo_register_write (gdbarch *arch,
			       regcache *regcache, int regnum,
			       const gdb_byte *buf)
{
  const char *name = intelgt_pseudo_register_name (arch, regnum);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);

  if (strcmp (name, "framedesc") == 0)
    {
      /* Frame description is stored in GRF count-3.  */
      gdb_assert (data->regset_ranges[intelgt::regset_grf].end > 3);
      int grf_num = data->regset_ranges[intelgt::regset_grf].end - 3;
      int grf_size = register_size (arch, grf_num);
      int desc_size = register_size (arch, regnum);
      gdb_assert (grf_size >= desc_size);
      regcache->raw_write (grf_num, buf);
    }
  else if (strcmp (name, "ip") == 0)
    {
      /* Instruction pointer is stored in CR0.2.  */
      gdb_assert (data->cr0_regnum != -1);
      int cr0_size = register_size (arch, data->cr0_regnum);
      gdb_byte reg_buf[cr0_size];
      regcache->raw_read (data->cr0_regnum, reg_buf);

      /* CR0 elements are 4 byte wide.  */
      int reg_size = register_size (arch, regnum);
      gdb_assert (reg_size + 8 <= cr0_size);
      memcpy (reg_buf + 8, buf, reg_size);
      regcache->raw_write (data->cr0_regnum, reg_buf);
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
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);

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

  if (strcmp ("r26", reg_name) == 0)
    data->retval_regnum = possible_regnum;
  else if (strcmp ("cr0", reg_name) == 0)
    data->cr0_regnum = possible_regnum;
  else if (strcmp ("sr0", reg_name) == 0)
    data->sr0_regnum = possible_regnum;
  else if (strcmp ("isabase", reg_name) == 0)
    data->isabase_regnum = possible_regnum;
  else if (strcmp ("emask", reg_name) == 0)
    data->emask_regnum = possible_regnum;

  return possible_regnum;
}

/* Check if a small struct can be promoted.  Struct arguments less than or
   equal to 128-bits and only containing primitive element types are passed by
   value as a vector of bytes, and are stored in the SoA (structure of arrays)
   format on GRFs.  Similarly for struct return values less than or equal
   to 64-bits and containing only primitive element types.  */

static bool
is_a_promotable_small_struct (type *arg_type, int max_size)
{
  if (!class_or_union_p (arg_type))
    return false;

  /* The struct is not promoted if it is larger than MAX_SIZE.  */
  if (TYPE_LENGTH (arg_type) > max_size)
    return false;

  int n_fields = arg_type->num_fields ();
  for (int field_idx = 0; field_idx < n_fields; ++field_idx)
    {
      type *field_type = check_typedef (arg_type->field (field_idx).type ());

      if (field_type->code () != TYPE_CODE_INT
	  && field_type->code () != TYPE_CODE_BOOL
	  && field_type->code () != TYPE_CODE_ENUM
	  && field_type->code () != TYPE_CODE_FLT
	  && field_type->code () != TYPE_CODE_PTR)
	return false;
    }

  return true;
}

/* Return the total memory, in bytes, used to store a field within a struct,
   which is the sum of the actual size of the field and the added padding.
   The padding could be between fields (intra-padding) or at the end of the
   struct (inter-padding).  */

static unsigned int
get_field_total_memory (type *struct_type, int field_index)
{
  field *fields = struct_type->fields ();
  type *field_type = check_typedef (struct_type->field (field_index).type ());
  int field_len = TYPE_LENGTH (field_type);
  int current_pos = fields[field_index].loc.bitpos / 8;

  /* Determine the memory occupation of the field (field size + padding).  */
  unsigned int total_memory = field_len;
  if (field_index < struct_type->num_fields () - 1)
    {
      int next_pos = fields[field_index + 1].loc.bitpos / 8;
      total_memory = next_pos - current_pos;
    }
  else
    total_memory = (TYPE_LENGTH (struct_type) - current_pos);

  return total_memory;
}

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API

/* Return the number of registers required to store an argument.  ARG_TYPE is
   the type of the argument.  */

static unsigned int
get_argument_required_registers (gdbarch *gdbarch, type *arg_type)
{
  const int len = TYPE_LENGTH (arg_type);
  const unsigned int simd_width = inferior_thread ()->get_simd_width ();
  const int address_size_byte = gdbarch_addr_bit (gdbarch) / 8;
  /* We need to know the size of a GRF register.  The retval register is a GRF,
     so just use its size.  */
  const int intelgt_register_size = register_size (
    gdbarch, get_intelgt_gdbarch_data (gdbarch)->retval_regnum);
  unsigned int required_registers = 1;

  /* Compute the total required memory.  */
  unsigned int required_memory = 0;
  if (class_or_union_p (arg_type)
      && !(is_a_promotable_small_struct (arg_type,
					 PROMOTABLE_STRUCT_MAX_SIZE_ARG)))
    required_memory = simd_width * address_size_byte;
  else
    required_memory = simd_width * len;

  /* Compute the number of the required registers to store the variable.  */
  required_registers = required_memory / intelgt_register_size;
  if (required_memory % intelgt_register_size != 0)
    required_registers++;

  return required_registers;
}

/* Intelgt implementation of the "value_arg_coerce" method.  */

static value *
intelgt_value_arg_coerce (gdbarch *gdbarch, value *arg,
			  type *param_type, int is_prototyped)
{
  /* Intelgt target accepts arguments less than the width of an
     integer (32-bits).  No need to do anything.  */

  type *arg_type = check_typedef (value_type (arg));
  type *type = param_type ? check_typedef (param_type) : arg_type;

  return value_cast (type, arg);
}

/* Intelgt implementation of the "dummy_id" method.  */

static struct frame_id
intelgt_dummy_id (struct gdbarch *gdbarch, frame_info *this_frame)
{
  /* Extract the front-end frame pointer from the "framedesc" register.
     The size of the framedesc.fe_fp is 8 bytes with an offset of 16.  */
  int framedesc_regnum = intelgt_pseudo_register_num (gdbarch, "framedesc");
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);

  gdb_assert (register_size (gdbarch, framedesc_regnum) <= 64);
  gdb_byte buf[64];
  get_frame_register (this_frame, framedesc_regnum, buf);
  CORE_ADDR fe_fp = extract_unsigned_integer (buf + 16, 8, byte_order);

  return frame_id_build (fe_fp, get_frame_pc (this_frame));
}

/* Intelgt implementation of the "return_in_first_hidden_param_p" method.  */

static int
intelgt_return_in_first_hidden_param_p (gdbarch *gdbarch, type *type)
{
  /* Vector return values greater than 64-bits and non-promotable structure
     return values are converted to be passed by reference as the first
     argument in the arguments list of the function.  */
  return (
    (class_or_union_p (type)
     && !is_a_promotable_small_struct (type, PROMOTABLE_STRUCT_MAX_SIZE_RET))
    || (type->is_vector () && TYPE_LENGTH (type) > 8));
}

/* Adjust the address upwards (direction of stack growth) so that the stack is
   always aligned.  According to the spec, the FE stack should be
   OWORD aligned.  */

static CORE_ADDR
intelgt_frame_align (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  return align_up (addr, OWORD_SIZE);
}

/* Intelgt implementation of the "unwind_sp" method.  The FE_SP
   is being considered.  */

static CORE_ADDR
intelgt_unwind_sp (gdbarch *gdbarch, frame_info *next_frame)
{
  /* Extract the front-end stack pointer from the "framedesc" register.
     The size of the framedesc.fe_sp is 8 bytes with an offset of 24.  */
  int framedesc_regnum = intelgt_pseudo_register_num (gdbarch, "framedesc");
  value *unwound_framedesc
    = frame_unwind_register_value (next_frame, framedesc_regnum);
  gdb_byte *raw_bytes = value_contents_raw (unwound_framedesc);
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR fe_sp = extract_unsigned_integer (raw_bytes + 24, 8, byte_order);

  return fe_sp;
}

/* Intelgt implementation of the "push_dummy_call" method.  */

static CORE_ADDR
intelgt_push_dummy_call (gdbarch *gdbarch, value *function, regcache *regcache,
			 CORE_ADDR bp_addr, int nargs, value **args,
			 CORE_ADDR sp,
			 function_call_return_method return_method,
			 CORE_ADDR struct_addr)
{
  const unsigned int simd_width = inferior_thread ()->get_simd_width ();
  const int current_lane = inferior_thread ()->current_simd_lane ();
  /* The retval register (r26) is the first GRF register to be used
     for passing arguments.  */
  const int retval_regnum = get_intelgt_gdbarch_data (gdbarch)->retval_regnum;
  const unsigned int retval_regsize = register_size (gdbarch, retval_regnum);
  const int framedesc_regnum = intelgt_pseudo_register_num (gdbarch,
							    "framedesc");
  /* ADDRESS_SIZE is the size of an address in bytes.  */
  const int address_size = gdbarch_addr_bit (gdbarch) / 8;
  CORE_ADDR fe_sp = sp;

  /* Set the return address register to point to the entry point of
     the program, where a breakpoint lies in wait.  The return address
     register is the framedesc.return_ip which is located at the first four
     bytes of "framedesc".  */
  regcache->cooked_write_part (framedesc_regnum, 0, 4, (gdb_byte *) &bp_addr);

  /* Push all struct objects (except for promoted structs) to the stack
     and save the corresponding addresses.  */
  std::vector<CORE_ADDR> obj_addrs;
  for (int index = 0; index < nargs; ++index)
    {
      type *arg_type = check_typedef (value_type (args[index]));
      /* TYPE_LENGTH returns the size of the argument in bytes.  */
      int len = TYPE_LENGTH (arg_type);

      /* For argument structs, a maximum size of 128-bits (16-bytes)
	 is used for the promotion check.  */
      if (class_or_union_p (arg_type)
	  && !is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE_ARG))
	{
	  const gdb_byte *val = value_contents (args[index]);

	  obj_addrs.push_back (fe_sp + current_lane * len);
	  int err = target_write_memory (fe_sp + current_lane * len,
					 val, len);
	  if (err != 0)
	    error ("Target failed to write on the stack: "
		   "arg %d of type %s", index, arg_type->name ());

	  fe_sp += align_up (len * simd_width, OWORD_SIZE);
	}
    }

  /* Copying arguments into registers.  The current IGC implementation
     uses a maximum of 12 GRF registers to pass arguments, which are r26 and
     onwards.  The rest of the arguments are pushed to the FE stack.  */
  int obj_index = 0;
  int regnum = retval_regnum;
  auto grf = grf_handler (retval_regsize, regcache);

  for (int argnum = 0; argnum < nargs; ++argnum)
    {
      type *arg_type = check_typedef (value_type (args[argnum]));
      /* Compute the required number of registers to store the argument.  */
      int required_registers
	= get_argument_required_registers (gdbarch, arg_type);
      /* LEN is the size of the argument in bytes.  */
      int len = TYPE_LENGTH (arg_type);
      const gdb_byte *val = value_contents (args[argnum]);

      /* If the argument can fit into the remaining GRFs then it needs to be
	 copied there.  */
      if (required_registers + regnum
	  <= retval_regnum + INTELGT_MAX_GRF_REGS_FOR_ARGS)
	{
	  /* First available GRF register to write data into.  */
	  int target_regnum = regnum;

	  if (is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE_ARG))
	    grf.write_small_struct (target_regnum, arg_type, val);

	  /* The argument has been pushed to the FE stack, and its
	     reference needs to be passed to the register.  */
	  else if (class_or_union_p (arg_type))
	    grf.write_integer (target_regnum, address_size,
			       (const gdb_byte *) &obj_addrs[obj_index++]);

	  /* Write vector elements to GRFs.  */
	  else if (arg_type->is_vector ())
	    grf.write_vector (target_regnum, arg_type, val);

	  /* Write primitive values to GRFs.  */
	  else if (len <= 8)
	    grf.write_integer (target_regnum, len, val);

	  else
	    error ("unexpected type %s of arg %d", arg_type->name (), argnum);

	  /* Move to the next available register.  */
	  regnum += required_registers;
	}
      else
	{
	  /* Push the argument to the FE stack when it does not fit
	     in the space left within GRFs.  */

	  if (is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE_ARG))
	    {
	      /* Promotable structures are stored in the stack with SoA layout.
		 Example:
		 s.a s.a... s.a  s.b s.b... s.b  s.c s.c... s.c.  */

	      int n_fields = arg_type->num_fields ();
	      field *fields = arg_type->fields ();

	      /* Loop over all structure fields.  */
	      for (int field_idx = 0; field_idx < n_fields; ++field_idx)
		{
		  type *field_type = check_typedef (
		    arg_type->field (field_idx).type ());
		  int field_len = TYPE_LENGTH (field_type);

		  /* Determine the offset of the field within the struct
		     in bytes.  */
		  int current_pos = fields[field_idx].loc.bitpos / 8;

		  int err
		    = target_write_memory (fe_sp + current_lane * field_len,
					   val + current_pos, field_len);
		  if (err != 0)
		    error ("Target failed to write on the stack: "
			   "arg %d of type %s", argnum, arg_type->name ());

		  /* Update the stack pointer for the next field while
		     considering the structure intra/inter-padding.  */
		  int mem_occupation = simd_width * get_field_total_memory (
		    arg_type, field_idx);
		  fe_sp += mem_occupation;
		}

	      /* Align the entire stack.  */
	      fe_sp = align_up (fe_sp, OWORD_SIZE);
	    }
	  else if (class_or_union_p (arg_type))
	    {
	      /* The object has been previously pushed to the stack, now push
		 its saved address to be aligned with the rest of the
		 arguments in the stack.  */
	      gdb_byte *obj_addr = (gdb_byte *) &obj_addrs[obj_index++];
	      int err
		= target_write_memory (fe_sp + current_lane * address_size,
				       obj_addr, address_size);
	      if (err != 0)
		error ("Target failed to write on the stack: "
		       "arg %d of type %s", argnum, arg_type->name ());

	      fe_sp += align_up (address_size * simd_width, OWORD_SIZE);
	    }
	  else if (arg_type->is_vector ())
	    {
	      /* Vectors are copied to stack with the SoA layout.  */

	      /* Length in bytes of an element in the vector.  */
	      int target_type_len = TYPE_LENGTH (TYPE_TARGET_TYPE (arg_type));
	      /* Number of elements in the vector.  */
	      int n_elements = len / target_type_len;

	      for (int element_idx = 0; element_idx < n_elements;
		   ++element_idx)
		{
		  int lane_offset = current_lane * target_type_len;

		  int total_offset
		    = lane_offset + element_idx * target_type_len * simd_width;

		  /* Location of the element in the vector.  */
		  const gdb_byte *element_addr
		    = val + element_idx * target_type_len;

		  int err = target_write_memory (fe_sp + total_offset,
						 element_addr,
						 target_type_len);

		  if (err != 0)
		    error ("Target failed to write on the stack: "
			   "arg %d of type %s", argnum, arg_type->name ());
		}

	      /* Align the entire stack.  */
	      fe_sp = align_up (fe_sp + len * simd_width, OWORD_SIZE);
	    }
	  else if (len <= 8)
	    {
	      int err = target_write_memory (fe_sp + current_lane * len,
					     val, len);
	      if (err != 0)
		error ("Target failed to write on the stack: "
		       "arg %d of type %s", argnum, arg_type->name ());

	      fe_sp += align_up (len * simd_width, OWORD_SIZE);
	    }
	  else
	    error ("unexpected type %s of arg %d", arg_type->name (), argnum);
	}
    }

  /* Update the FE frame pointer (framedesc.fe_fp).  */
  regcache->cooked_write_part (framedesc_regnum, 16, 8, (gdb_byte *) &fe_sp);
  /* Update the FE stack pointer (framedesc.fe_sp).  */
  regcache->cooked_write_part (framedesc_regnum, 24, 8, (gdb_byte *) &fe_sp);
  return fe_sp;
}
#endif /* not USE_WIN32API */

/* See GRF_HANDLER declaration.  */

void
grf_handler::read_small_struct (int regnum, type *valtype, gdb_byte *buff)
{
  handle_small_struct (regnum, nullptr, buff, valtype);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::write_small_struct (int regnum, type *valtype,
				 const gdb_byte *buff)
{
  handle_small_struct (regnum, buff, nullptr, valtype);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::read_vector (int regnum, type *valtype, gdb_byte *buff)
{
  handle_vector (regnum, nullptr, buff, valtype);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::write_vector (int regnum, type *valtype, const gdb_byte *buff)
{
  handle_vector (regnum, buff, nullptr, valtype);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::read_integer (int regnum, int len, gdb_byte *buff)
{
  handle_integer (regnum, nullptr, buff, len);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::write_integer (int regnum, int len, const gdb_byte *buff)
{
  handle_integer (regnum, buff, nullptr, len);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::handle_small_struct (int regnum, const gdb_byte *buff_read,
				  gdb_byte *buff_write, type *valtype)
{
  /* The vectorized return value is stored at this register and onwards.  */
  const int simd_lane = inferior_thread ()->current_simd_lane ();
  const unsigned int simd_width = inferior_thread ()->get_simd_width ();

  /* Small structures are stored in the GRF registers with SoA
     layout.  Example:
     s.a s.a... s.a  s.b s.b... s.b  s.c s.c... s.c.  */

  int reg_offset = 0;
  int target_regnum = regnum;
  int n_fields = valtype->num_fields ();
  field *fields = valtype->fields ();

  /* Loop over all structure fields.  */
  for (int field_idx = 0; field_idx < n_fields; ++field_idx)
    {
      /* FIELD_REG_OFFSET and FIELD_REGNUM are the local register
	 offset and the register number for writing the current
	 field.  */
      int field_reg_offset = reg_offset;
      int field_regnum = target_regnum;

      type *field_type = check_typedef (valtype->field (field_idx).type ());
      int field_len = TYPE_LENGTH (field_type);

      /* Total field size after SIMD vectorization.  */
      int mem_occupation = simd_width * get_field_total_memory (
	valtype, field_idx);

      int lane_offset = simd_lane * field_len;

      field_regnum += (reg_offset + lane_offset) / m_reg_size;
      field_reg_offset = (reg_offset + lane_offset) % m_reg_size;

      /* Prepare the TARGET_REGNUM and the REG_OFFSET for
	 the next field.  */
      target_regnum += (reg_offset + mem_occupation) / m_reg_size;
      reg_offset = (reg_offset + mem_occupation) % m_reg_size;

      /* Determine the offset of the field within the struct
	 in bytes.  */
      int current_pos = fields[field_idx].loc.bitpos / 8;

      /* Read from the corresponding part of register.  */
      if (buff_write != nullptr)
	m_regcache->cooked_read_part (field_regnum, field_reg_offset,
				      field_len, buff_write + current_pos);

      /* Write to the corresponding part of register.  */
      else if (buff_read != nullptr)
	m_regcache->cooked_write_part (field_regnum, field_reg_offset,
				       field_len, buff_read + current_pos);
    }
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::handle_vector (int regnum, const gdb_byte *buff_read,
			    gdb_byte *buff_write, type *valtype)
{
  const int current_lane = inferior_thread ()->current_simd_lane ();
  const unsigned int simd_width = inferior_thread ()->get_simd_width ();
  int target_regnum = regnum;

  /* Vectors are stored in GRFs with the Structure of Arrays (SoA) layout.  */

  int len = TYPE_LENGTH (valtype);
  /* Length in bytes of an element in the vector.  */
  int element_len = TYPE_LENGTH (TYPE_TARGET_TYPE (valtype));
  /* Number of elements in the vector.  */
  int n_elements = len / element_len;

  for (int element_idx = 0; element_idx < n_elements; ++element_idx)
    {
      int lane_offset = current_lane * element_len;
      int total_offset
	  = lane_offset + element_idx * element_len * simd_width;
      int reg_offset = total_offset % m_reg_size;

      /* Move to read / write on the right register.  */
      target_regnum = regnum + total_offset / m_reg_size;

      /* Read from the corresponding part of register.  */
      if (buff_write != nullptr)
	m_regcache->cooked_read_part (target_regnum, reg_offset, element_len,
				      buff_write + element_idx * element_len);

      /* Write to the corresponding part of register.  */
      else if (buff_read != nullptr)
	m_regcache->cooked_write_part (target_regnum, reg_offset, element_len,
				       buff_read + element_idx * element_len);
    }
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::handle_integer (int regnum, const gdb_byte *buff_read,
			     gdb_byte *buff_write, int len)
{
  const int current_lane = inferior_thread ()->current_simd_lane ();
  int lane_offset = current_lane * len;
  int reg_offset = lane_offset % m_reg_size;

  /* Move to read / write on the right register.  */
  int target_regnum = regnum + lane_offset / m_reg_size;

  /* Read from from the corresponding part of the register.  */
  if (buff_write != nullptr)
    m_regcache->cooked_read_part (target_regnum, reg_offset, len, buff_write);

  /* Write to the corresponding part of the register.  */
  else if (buff_read != nullptr)
    m_regcache->cooked_write_part (target_regnum, reg_offset, len, buff_read);
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
  iga_gen_t iga_version = IGA_GEN_INVALID;

  const std::string &tdesc_gen_major
    = tdesc_find_device_info_attribute (tdesc, "gen_major");
  const std::string &tdesc_gen_minor
    = tdesc_find_device_info_attribute (tdesc, "gen_minor");

  if (!tdesc_gen_major.empty ())
    {
      dprintf (_("device: Gen%s.%s"), tdesc_gen_major.c_str (),
	       tdesc_gen_minor.c_str ());

      if (tdesc_gen_major == "9")
	iga_version = IGA_GEN9;
      else if (tdesc_gen_major == "11")
	iga_version = IGA_GEN11;
      else if (tdesc_gen_major == "12")
	{
	  if (!tdesc_gen_minor.empty ())
	    {
	      if (tdesc_gen_minor == "0")
		iga_version = IGA_XE;
	      else if (tdesc_gen_minor == "1")
		iga_version = IGA_XE;
	      else if (tdesc_gen_minor == "5")
		iga_version = IGA_XE_HP;
	      else if (tdesc_gen_minor == "71")
		iga_version = IGA_XE_HPG;
	      else if (tdesc_gen_minor == "72")
		iga_version = IGA_XE_HPC;
	    }
	  else
	    dprintf (_("Failed to read minor version from Gen12 device."));
	}
      else
	dprintf (_("Unknown major version %s from device."),
		 tdesc_gen_major.c_str ());
    }
  else
    dprintf (_("Failed to read major version from device."));


  const iga_context_options_t options = IGA_CONTEXT_OPTIONS_INIT (iga_version);
  iga_context_create (&options, &data->iga_ctx);
#endif

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

      if (data->emask_regnum == -1)
	error ("Debugging requires $emask provided by the target");
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
  set_gdbarch_can_leave_breakpoints (gdbarch, true);
  dwarf2_frame_set_init_reg (gdbarch, intelgt_init_reg);

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

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API
  /* Enable inferior call support.  */
  set_gdbarch_push_dummy_call (gdbarch, intelgt_push_dummy_call);
  set_gdbarch_unwind_sp (gdbarch, intelgt_unwind_sp);
  set_gdbarch_frame_align (gdbarch, intelgt_frame_align);
  set_gdbarch_return_in_first_hidden_param_p (
    gdbarch, intelgt_return_in_first_hidden_param_p);
  set_gdbarch_value_arg_coerce (gdbarch, intelgt_value_arg_coerce);
  set_gdbarch_dummy_id (gdbarch, intelgt_dummy_id);
#endif /* not USE_WIN32API */

  set_gdbarch_is_inferior_device (gdbarch, true);
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
