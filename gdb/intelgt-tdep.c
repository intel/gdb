/* Target-dependent code for the Intel(R) Graphics Technology architecture.

   Copyright (C) 2019-2023 Free Software Foundation, Inc.

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
#include "gdbtypes.h"
#include "target.h"
#include "target-descriptions.h"
#include "value.h"
#include "disasm.h"
#if defined (HAVE_LIBIGA64)
#include "iga.h"
#endif /* defined (HAVE_LIBIGA64)  */
#include "gdbthread.h"
#include "inferior.h"
#include "user-regs.h"
#include "objfiles.h"
#include "observable.h"
#include "block.h"
#include "elf-bfd.h"
#include "infcall.h"
#include "gdbcore.h"
#include "elfnote-file.h"
#include "regset.h"
#include "solist.h"
#include "solib.h"
#include "symfile.h"
#include "xml-tdesc.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <array>
#include <stack>
#include <unordered_map>

/* Address space flags.
   We are assigning the TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1 to the shared
   local memory address space.  */

#define INTELGT_TYPE_INSTANCE_FLAG_SLM TYPE_INSTANCE_FLAG_ADDRESS_CLASS_1
#define INTELGT_SLM_ADDRESS_QUALIFIER "slm"

/* The maximum number of GRF registers to be used when passing function
   arguments.  */
constexpr int INTELGT_MAX_GRF_REGS_FOR_ARGS = 12;

/* The maximum number of GRF registers to be used for the return value.  */
constexpr int INTELGT_MAX_GRF_REGS_FOR_RET = 8;

/* The maximum size in bytes of a promotable struct.  */
constexpr int PROMOTABLE_STRUCT_MAX_SIZE = 16;

/* Intelgt FE stack alignment size in bytes.  */
constexpr int OWORD_SIZE = 16;

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

/* Regnum pair describing the assigned regnum range for a single
   regset.  */

struct regnum_range
{
  int start;
  int end;
};

/* Implicit arguments structure, version 0.
   The lifespan of a structure and the corresponding local ID table is
   the corresponding kernel dispatch.  */
struct implicit_args
{
  uint8_t struct_size;
  uint8_t struct_version;
  uint8_t num_work_dim;
  uint8_t simd_width;
  uint32_t local_size_x;
  uint32_t local_size_y;
  uint32_t local_size_z;
  uint64_t global_size_x;
  uint64_t global_size_y;
  uint64_t global_size_z;
  uint64_t printf_buffer_ptr;
  uint64_t global_offset_x;
  uint64_t global_offset_y;
  uint64_t global_offset_z;
  uint64_t local_id_table_ptr;
  uint32_t group_count_x;
  uint32_t group_count_y;
  uint32_t group_count_z;
  uint64_t rt_global_buffer_ptr;

  /* Disable copy, default others.  */
  implicit_args () = default;
  implicit_args &operator= (const implicit_args &) = delete;
  implicit_args (const implicit_args &) = delete;
  implicit_args (implicit_args &&) = default;
  implicit_args &operator= (implicit_args &&) = default;
};

/* The value type of the implicit arguments cache.  We want to store both
   the implicit arguments structure and its local ID table.  */

typedef std::pair<implicit_args, std::vector<uint16_t>>
  implicit_args_value_pair;

/* Global cache to store implicit args and local IDs.
   Key: stringified "inferior num" + "implicit args address".
   This key is guarantied to be unique during the kernel dispatch.
   However, we currently do not have a means to identify the kernel
   dispatch.  Thus, the cache has to be cleared at every target resume.  */

std::unordered_map<std::string, implicit_args_value_pair> implicit_args_cache;

/* Return the reference to the entry in implicit_args_cache.
   If there is no entry yet, try to read it.
   Use this function to get the implicit arguments struct and local IDs.

   TP thread is used to find the address to the implicit args structure.
   We need to have the access to its registers and build the key to
   the cache based on its inferior number.  */

static implicit_args_value_pair &
intelgt_implicit_args_find_value_pair (gdbarch *gdbarch, thread_info *tp);

/* The 'gdbarch_data' stuff specific for this architecture.  */

static int intelgt_pseudo_register_num (gdbarch*, const char*);
static bool is_a_promotable_small_struct (type *arg_type, int max_size);
static unsigned int get_field_total_memory (type *struct_type,
					    int field_index);

/* Read and write vectors on the stack while considering the SIMD
   vectorization.

   ADDR is the stack memory address.
   VALTYPE is the type of the vector.
   BUFF_READ is a non-NULL pointer to read data from when performing
   stack write.  It is NULL if we are performing a stack read.
   BUFF_WRITE is a non-NULL writable pointer that will contain the data
   read from the stack.  It is NULL if we are performing a stack write.

   The returned value is the stack address right after the vector
   reserved memory.  */

static CORE_ADDR fe_stack_handle_vector (CORE_ADDR addr, type *valtype,
					 const gdb_byte *buff_read,
					 gdb_byte *buff_write,
					 const unsigned int simd_width);

/* Read vector from the stack into BUFF.  */

static CORE_ADDR
fe_stack_read_vector (CORE_ADDR addr, type *valtype, gdb_byte *buff,
		      const unsigned int simd_width)
{
  return fe_stack_handle_vector (addr, valtype, nullptr, buff, simd_width);
}

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API

/* Write vector from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_vector (CORE_ADDR addr, type *valtype, const gdb_byte *buff,
		       const unsigned int simd_width)
{
  return fe_stack_handle_vector (addr, valtype, buff, nullptr, simd_width);
}
#endif /* not USE_WIN32API */

/* Read and write small structures on the stack while considering
   the SIMD vectorization.

   ADDR is the stack memory address.
   VALTYPE is the type of the structure.
   BUFF_READ is a non-NULL pointer to read data from when performing
   stack write.  It is NULL if we are performing a stack read.
   BUFF_WRITE is a non-NULL writable pointer that will contain the data
   read from the stack.  It is NULL if we are performing a stack write.

   The returned value is the stack address right after the structure
   reserved memory.  */

static CORE_ADDR fe_stack_handle_small_struct (CORE_ADDR addr, type *valtype,
					       const gdb_byte *buff_read,
					       gdb_byte *buff_write,
					       const unsigned int simd_width);

/* Read small structure from the stack into BUFF.  */

static CORE_ADDR
fe_stack_read_small_struct (CORE_ADDR addr, type *valtype, gdb_byte *buff,
			    const unsigned int simd_width)
{
  return fe_stack_handle_small_struct (addr, valtype, nullptr, buff,
				       simd_width);
}

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API

/* Write small structure from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_small_struct (CORE_ADDR addr, type *valtype,
			     const gdb_byte *buff,
			     const unsigned int simd_width)
{
  return fe_stack_handle_small_struct (addr, valtype, buff, nullptr,
				       simd_width);
}
#endif /* not USE_WIN32API */

/* Read and write up to 8 bytes on the stack while considering the SIMD
   vectorization.

   ADDR is the stack memory address.
   LEN is the length in bytes to read or write on the stack.
   BUFF_READ is a non-NULL pointer to read data from when performing
   stack write.  It is NULL if we are performing a stack read.
   BUFF_WRITE is a non-NULL writable pointer that will contain the data
   read from the stack.  It is NULL if we are performing a stack write.

   The returned value is the stack address right after the data
   reserved memory.  */

static CORE_ADDR fe_stack_handle_primitive (CORE_ADDR addr, int len,
					    const gdb_byte *buff_read,
					    gdb_byte *buff_write,
					    const unsigned int simd_width);

/* Read up to 8 bytes from the stack into BUFF.  */

static CORE_ADDR
fe_stack_read_primitive (CORE_ADDR addr, int len, gdb_byte *buff,
			 const unsigned int simd_width)
{
  return fe_stack_handle_primitive (addr, len, nullptr, buff, simd_width);
}

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API

/* Write up to 8 bytes from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_primitive (CORE_ADDR addr, int len, const gdb_byte *buff,
			  const unsigned int simd_width)
{
  return fe_stack_handle_primitive (addr, len, buff, nullptr, simd_width);
}
#endif /* not USE_WIN32API */


/* Structure for GRF read / write handling.  */

struct grf_handler
{
public:
  grf_handler (uint32_t reg_size, regcache * regcache, unsigned int simd_width)
      : m_reg_size (reg_size), m_regcache (regcache), m_simd_width (simd_width)
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

  /* Read primitives from GRFs into BUFF.  */
  void read_primitive (int regnum, int len, gdb_byte *buff);

  /* Write primitives from BUFF into GRFs.  */
  void write_primitive (int regnum, int len, const gdb_byte *buff);

private:
  uint32_t m_reg_size;
  regcache *m_regcache;
  const unsigned int m_simd_width;

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

  void handle_primitive (int regnum, const gdb_byte *buff_read,
		       gdb_byte *buff_write, int len);
};

/* Return the machine code of the current elf.  */

static int
intelgt_get_current_machine_code ()
{
  regcache *regcache = get_current_regcache ();
  CORE_ADDR pc = regcache_read_pc (regcache);
  const block *block = block_for_pc (pc);

  if (block == nullptr)
    error (_("Cannot read block for PC 0x%lx."), pc);

  objfile *obj = block_objfile (block);

  if (obj == nullptr)
    error (_("Cannot find object file for block containg PC 0x%lx."), pc);

  bfd *abfd = obj->obfd.get ();
  return get_elf_backend_data (abfd)->elf_machine_code;
}

struct intelgt_gdbarch_data
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

  /* Return regnum where frame descriptors are stored.  */

  int
  framedesc_base_regnum ()
  {
    int machine = intelgt_get_current_machine_code ();
    if (machine == EM_INTELGT)
      {
	/* For EM_INTELGT frame descriptors are stored at MAX_GRF - 1.  */
	gdb_assert (regset_ranges[intelgt::regset_grf].end > 1);
	return regset_ranges[intelgt::regset_grf].end - 1;
      }

    if (machine == EM_INTEL_GEN)
      {
	/* For EM_INTEL_GEN frame descriptors are stored at MAX_GRF - 3.  */
	gdb_assert (regset_ranges[intelgt::regset_grf].end > 3);
	return regset_ranges[intelgt::regset_grf].end - 3;
      }

    gdb_assert_not_reached ("Machine code is unknown.");
  }

#if defined (HAVE_LIBIGA64)
  /* libiga context for disassembly.  */
  iga_context_t iga_ctx = nullptr;
  iga_gen_t iga_version = IGA_GEN_INVALID;
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

/* The 'register_type' gdbarch method.  */

static type *
intelgt_register_type (gdbarch *gdbarch, int regno)
{
  type *typ = tdesc_register_type (gdbarch, regno);
  return typ;
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
  constexpr int ce = 1;

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
  if (num == ce)
    return data->ce_regnum;

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

  /* Default to zero if the CE register is not available.  This may
     happen if TP is not available.  */
  ULONGEST ce = 0ull;
  regcache_cooked_read_unsigned (thread_regcache, data->ce_regnum,
				 &ce);

  /* The higher bits of CE are undefined if they are outside the
     dispatch mask range.  Clear them explicitly using the dispatch
     mask, which is at SR0.2.  SR0 elements are 4 byte wide.  */
  uint32_t sr0_2 = 0;
  thread_regcache->raw_read_part (data->sr0_regnum, sizeof (uint32_t) * 2,
				  sizeof (sr0_2), (gdb_byte *) &sr0_2);

  dprintf ("ce: %lx, dmask: %x", ce, sr0_2);

  /* The higher bits of dmask are undefined if they are outside the
     SIMD width.  Clear them explicitly.
     We cannot query the simd-width if the thread is executing.  */
  if (tp->executing ())
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

  return ce & sr0_2;
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
  dprintf ("return type length %ld", valtype->length ());
  gdb_assert (inferior_ptid != null_ptid);

  int address_size_byte = gdbarch_addr_bit (gdbarch) / 8;
  CORE_ADDR function_pc = value_address (function);
  const unsigned int simd_width = get_simd_width_for_pc (function_pc);
  constexpr int max_primitive_size = 8;

  /* The vectorized return value is stored at this register and onwards.  */
  int retval_regnum = get_intelgt_gdbarch_data (gdbarch)->retval_regnum;
  unsigned int retval_size = register_size (gdbarch, retval_regnum);
  int type_length = valtype->length ();
  auto grf = grf_handler (retval_size, regcache, simd_width);
  bool is_promotable_struct
    = is_a_promotable_small_struct (valtype, PROMOTABLE_STRUCT_MAX_SIZE);

  /* Non-promotable structs are stored by reference.  The return value
     register contains a vectorized sequence of memory addresses.  */
  if (class_or_union_p (valtype) && !is_promotable_struct)
    {
      if (readbuf != nullptr)
	{
	  /* Read the address to a temporary buffer.  */
	  CORE_ADDR addr = 0;
	  grf.read_primitive (retval_regnum, address_size_byte,
			      (gdb_byte *) &addr);
	  /* Read the value to the resulting buffer.  */
	  int err = target_read_memory (addr, readbuf, type_length);
	  if (err != 0)
	      error ("Failed to read the returned struct of type %s of "
		     "length %d at address %s.",
		     TYPE_SAFE_NAME (valtype), type_length,
		     paddress (gdbarch, addr));
	}

      return RETURN_VALUE_ABI_RETURNS_ADDRESS;
    }

  /* Promotable structures and vectors are returned by values on registers.
     In case the GRFs space is not sufficient, the return by value takes place
     on the stack, at the end of the caller frame.  */
  if (type_length * simd_width
      <= INTELGT_MAX_GRF_REGS_FOR_RET * retval_size)
    {
      /* Return value can fit in the GRF registers.  */
      if (readbuf == nullptr)
	return RETURN_VALUE_REGISTER_CONVENTION;

      /* Read the return values from GRFs.  */
      if (is_promotable_struct)
	grf.read_small_struct (retval_regnum, valtype, readbuf);
      else if (valtype->is_vector ())
	grf.read_vector (retval_regnum, valtype, readbuf);
      else if (type_length <= max_primitive_size)
	grf.read_primitive (retval_regnum, type_length, readbuf);

      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  else
    {
      /* Return value is returned on the stack.  */
      if (readbuf == nullptr)
	return RETURN_VALUE_ABI_RETURNS_ADDRESS;

      /* The return address of the returned value is deduced from the caller
	 FE_SP.  Return address = FE_SP - (vectorized and aligned return
	 type length).  */
      const int framedesc_regnum
	= intelgt_pseudo_register_num (gdbarch, "framedesc");
      CORE_ADDR addr = 0;
      regcache->cooked_read_part (framedesc_regnum, 24, 8,
				  (gdb_byte *) &addr);

      CORE_ADDR reserved_struct_memory = align_up (type_length * simd_width,
						   OWORD_SIZE);
      if (addr < reserved_struct_memory)
	error ("Invalid stack address of return value: 0x%lx", addr);
      addr -= reserved_struct_memory;

      /* Read the returned value from the stack.  */
      if (is_promotable_struct)
	fe_stack_read_small_struct (addr, valtype, readbuf, simd_width);
      else if (valtype->is_vector ())
	fe_stack_read_vector (addr, valtype, readbuf, simd_width);
      else if (type_length <= max_primitive_size)
	fe_stack_read_primitive (addr, type_length, readbuf, simd_width);

      return RETURN_VALUE_ABI_RETURNS_ADDRESS;
    }
}

/* Callback function to unwind the $framedesc register.  */

static value *
intelgt_dwarf2_prev_framedesc (frame_info_ptr this_frame, void **this_cache,
			       int regnum)
{
  gdbarch *gdbarch = get_frame_arch (this_frame);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  int actual_regnum = data->framedesc_base_regnum ();

  /* Unwind the actual GRF register.  */
  return frame_unwind_register_value (this_frame, actual_regnum);
}

static void
intelgt_init_reg (gdbarch *gdbarch, int regnum, dwarf2_frame_state_reg *reg,
		  frame_info_ptr this_frame)
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

/* The 'unwind_pc' gdbarch method.  */

static CORE_ADDR
intelgt_unwind_pc (gdbarch *gdbarch, frame_info_ptr next_frame)
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
		 _("Register %d (isabase) is not available"),
		 data->isabase_regnum);

  return (CORE_ADDR) isabase + prev_ip;
}

/* Frame unwinding.  */

static void
intelgt_frame_this_id (frame_info_ptr this_frame, void **this_prologue_cache,
		       frame_id *this_id)
{
  /* FIXME: Other tdeps populate and use the cache.  */

  /* Try to use symbol information to get the current start address.  */
  CORE_ADDR func;

  if (get_frame_func_if_available (this_frame, &func))
    {
      /* Use the current PC as a fallback if no symbol info is available.  */
      if (func == 0)
	func = get_frame_pc (this_frame);

      /* FIXME: Because there is no full notion of stack, it
	 should be OK to ignore the SP reg.  Currently, we cannot use SP
	 even if we want to, because SP's size is 16 bytes whereas
	 CORE_ADDR is 8.  */
      *this_id = frame_id_build_unavailable_stack (func);
    }
  else
    *this_id = outer_frame_id;
}

static value *
intelgt_frame_prev_register (frame_info_ptr this_frame,
			     void **this_prologue_cache, int regnum)
{
  dprintf ("regnum %d", regnum);

  gdbarch *arch = get_frame_arch (this_frame);
  /* FIXME: Do the values below exist in an ABI?  */
  constexpr int STORAGE_REG_RET_PC = 1;
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  int STORAGE_REG_SP = data->framedesc_base_regnum ();

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
   space to which they are pointing.  Tags are encoded into bits [61:63] of
   an address:

   000/111 - global,
   001 - private,
   010 - local (SLM)

   Variables in SLM will not have tagged addresses.  They will be marked
   in DWARF and have a instance_flag because of that.  To communicate to
   gdbserver that it should access SLM, we tag the address here.  This was
   chosen as relying on the type system and RSP to communicate that, was
   problematic for generic pointers (as types are cached and shared).  */
static CORE_ADDR
intelgt_pointer_to_address (gdbarch *gdbarch,
			    type *type, const gdb_byte *buf)
{
  CORE_ADDR addr = extract_unsigned_integer (buf, type->length (),
					     gdbarch_byte_order (gdbarch));

  /* Tag slm addresses.  This will be used by gdbserver to figure out which
     address space to read.  */
  if ((type->instance_flags () & INTELGT_TYPE_INSTANCE_FLAG_SLM) != 0)
    {
      constexpr CORE_ADDR addr_space_bits = (CORE_ADDR) 0x7 << 61;
      constexpr CORE_ADDR slm_mask = (CORE_ADDR) 0x2 << 61;
      addr = (addr & ~addr_space_bits) | slm_mask;
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
  gdb_disassemble_info *di
    = static_cast<gdb_disassemble_info *>(info->application_data);
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
  gdb_printf (_("\nDisassemble feature not available: libiga64 "
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

/* Implementation of `address_class_type_flags' gdbarch method.

   This method maps DW_AT_address_class attributes to a
   type_instance_flag_value.  */
static type_instance_flags
intelgt_address_class_type_flags (int byte_size, int dwarf2_addr_class)
{
  /* The value 1 of the DW_AT_address_class attribute corresponds to SLM.  */
  if (dwarf2_addr_class == 1)
    return INTELGT_TYPE_INSTANCE_FLAG_SLM;
  return 0;
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

static unsigned int
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
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (ip) is not available"),
		 data->cr0_regnum);

  /* Program counter is $ip + $isabase.  */
  gdb_assert (data->isabase_regnum != -1);
  uint64_t isabase;
  if (regcache->cooked_read (data->isabase_regnum, &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (isabase) is not available"),
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
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (isabase) is not available"),
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
				    readable_regcache *regcache, int regnum)
{
  const char *name = intelgt_pseudo_register_name (arch, regnum);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);
  type *regtype = register_type (arch, regnum);
  int regsize = register_size (arch, regnum);

  value *result = allocate_value (regtype);
  VALUE_LVAL (result) = lval_register;
  VALUE_REGNUM (result) = regnum;
  gdb_byte *result_buf = value_contents_writeable (result).data ();

  if (strcmp (name, "framedesc") == 0)
    {
      int grf_num = data->framedesc_base_regnum ();
      value *grf = regcache->cooked_read_value (grf_num);
      if (!value_entirely_available (grf))
	throw_error (NOT_AVAILABLE_ERROR,
		     _("Register %d (framedesc) is not available"),
		     grf_num);
      gdb_assert (regsize <= register_size (arch, grf_num));
      memcpy (result_buf, value_contents_raw (grf).data (), regsize);
      return result;
    }
  else if (strcmp (name, "ip") == 0)
    {
      /* Instruction pointer is stored in CR0.2.  */
      gdb_assert (data->cr0_regnum != -1);
      value *cr0 = regcache->cooked_read_value (data->cr0_regnum);
      if (!value_entirely_available (cr0))
	throw_error (NOT_AVAILABLE_ERROR,
		     _("Register %d (cr0) is not available"),
		     data->cr0_regnum);
      /* CR0 elements are 4 byte wide.  */
      gdb_assert (regsize + 8 <= register_size (arch, data->cr0_regnum));
      memcpy (result_buf, value_contents_raw (cr0).data () + 8, regsize);
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
      int grf_num = data->framedesc_base_regnum ();
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

  return possible_regnum;
}

/* Build the NT_PRPSINFO for IntelGT.  */

static char *
intelgt_make_prpsinfo (bfd *obfd, char *obuf, int *obufsize,
		       uint32_t device_id, uint32_t vendor_id,
		       const char *fname, const char *xml)
{
  int fname_len = strlen (fname) + 1;
  int xml_len = strlen (xml) + 1;
  int note_len = 4 + 4 + fname_len + xml_len;
  char *data = (char *) calloc (note_len, 1);
  if (data == nullptr)
    error ("Could not allocate space for the PRPSINFO note");

  bfd_put_32 (obfd, device_id, data);
  bfd_put_32 (obfd, vendor_id, data + 4);
  strcpy (data + 8, fname);
  strcpy (data + 8 + fname_len, xml);
  gdb_assert (data[note_len - 1] == 0);
  return elfcore_write_note (obfd, obuf, obufsize, "CORE", NT_PRPSINFO, data,
			     note_len);
}

/* Build the NT_PRSTATUO for IntelGT.  */

static char *
intelgt_make_prstatus (bfd *obfd, char *obuf, int *obufsize, uint64_t tid,
		       uint32_t signal, uint32_t slm, const char *regs,
		       int regs_size)
{
  int note_len = 8 + 4 + 4 + regs_size;
  char *data = (char *) calloc (note_len, 1);
  bfd_put_64 (obfd, tid, data);
  bfd_put_32 (obfd, signal, data + 8);
  bfd_put_32 (obfd, slm, data + 12);
  memcpy (data + 16, regs, regs_size);
  return elfcore_write_note (obfd, obuf, obufsize, "CORE", NT_PRSTATUS, data,
			     note_len);
}

/* Implement the "iterate_over_regset_sections" gdbarch method.
   This is used for both writing regsets to a core file and
   later reading it back in GDB.  */

static void
intelgt_iterate_over_regset_sections (struct gdbarch *gdbarch,
				      iterate_over_regset_sections_cb *cb,
				      void *cb_data,
				      const struct regcache *regcache)
{
  /* Generate register maps from the target description.  There may
     be a more elegant way to automatically supply/collect all known
     registers directly from a regcache but at the moment of implementing
     this it was not found.  */
  std::vector<struct regcache_map_entry> intelgt_regmap;
  int count = gdbarch_num_regs (gdbarch);
  int total_size = 0;

  /* Supply/collect all known features using a single ".reg" section.
     Content is fully dynamic and based on the target description
     embedded in the core file itself, thus there is no need to
     distinguish different register groups here.  */
  for (int reg = 0; reg < count; ++reg)
    {
      int size = register_size (gdbarch, reg);
      total_size += size;
      intelgt_regmap.push_back ({ 1, reg, size });
    }

  intelgt_regmap.push_back ({ 0, 0, 0 });

  struct regset intelgt_regset
      = { intelgt_regmap.data (), regcache_supply_regset,
	  regcache_collect_regset };

  cb (".reg", total_size, total_size, &intelgt_regset, NULL, cb_data);
}

/* Structure for passing information from
   intelgt_collect_thread_registers via an iterator to
   intelgt_collect_regset_section_cb.  */

struct intelgt_collect_regset_section_cb_data
{
  const struct regcache *regcache;
  bfd *obfd;
  char *note_data;
  int *note_size;
  unsigned long lwp;
  enum gdb_signal stop_signal;
  int abort_iteration;
};

/* Writes an ELF note with a register values for a single
   GDB section.  Right now we put all registers into a single
   ".reg" pseudo-section so this is expected to be called
   only once.  */

static void
intelgt_collect_regset_section_cb (const char *sect_name, int supply_size,
				   int collect_size,
				   const struct regset *regset,
				   const char *human_name, void *cb_data)
{
  struct intelgt_collect_regset_section_cb_data *data
    = (struct intelgt_collect_regset_section_cb_data *) cb_data;

  if (data->abort_iteration)
    return;

  gdb_assert (regset->collect_regset);

  char *buf = (char *) xmalloc (collect_size);
  regset->collect_regset (regset, data->regcache, -1, buf, collect_size);

  gdb_assert (strcmp (sect_name, ".reg") == 0);
  data->note_data = (char *) intelgt_make_prstatus (
      data->obfd, data->note_data, data->note_size, data->lwp,
      gdb_signal_to_host (data->stop_signal), 0 /* slm */, buf, collect_size);

  xfree (buf);

  if (data->note_data == NULL)
    data->abort_iteration = 1;
}

/* Records the thread's register state for the corefile note
   section.  */

static char *
intelgt_collect_thread_registers (const struct regcache *regcache,
				  ptid_t ptid, bfd *obfd,
				  char *note_data, int *note_size,
				  enum gdb_signal stop_signal)
{
  struct gdbarch *gdbarch = regcache->arch ();
  struct intelgt_collect_regset_section_cb_data data;

  data.regcache = regcache;
  data.obfd = obfd;
  data.note_data = note_data;
  data.note_size = note_size;
  data.stop_signal = stop_signal;
  data.abort_iteration = 0;
  data.lwp = ptid.lwp ();

  gdbarch_iterate_over_regset_sections (gdbarch,
					intelgt_collect_regset_section_cb,
					&data, regcache);
  return data.note_data;
}

/* Arguments used by `intelgt_corefile_write_thread`.  Some of them
   (primarily note_data/note_size) will be updated during the
   iteration and later used by the caller.  */

struct intelgt_corefile_thread_data
{
  struct gdbarch *gdbarch;
  bfd *obfd;
  char *note_data;
  int *note_size;
  enum gdb_signal stop_signal;
};

/* Writes ELF note data for a single thread.  Does so by collecting all
   registers from the regcache and packing those into a buffer according to a
   regmap.  */

static void
intelgt_corefile_write_thread (struct thread_info *info,
			       struct intelgt_corefile_thread_data *args)
{
  struct regcache *regcache;
  regcache = get_thread_arch_regcache (info->inf->process_target (),
				       info->ptid, args->gdbarch);

  target_fetch_registers (regcache, -1);

  args->note_data = intelgt_collect_thread_registers (
      regcache, info->ptid, args->obfd, args->note_data, args->note_size,
      args->stop_signal);
}

/* Build the note section for a corefile, and return it in a malloc buffer.  */

__attribute__((unused)) /* See FIXME in intelgt_gdbarch_init funciton.  */
static gdb::unique_xmalloc_ptr<char>
intelgt_make_corefile_notes (struct gdbarch *gdbarch, bfd *obfd,
			     int *note_size)
{
  gdb::unique_xmalloc_ptr<char> note_data;
  const char *xml = tdesc_get_features_xml (gdbarch_target_desc (gdbarch));
  note_data.reset (intelgt_make_prpsinfo (obfd, note_data.release (),
					  note_size, 0, 0,
					  "GPU", xml + 1));
  try
    {
      update_thread_list ();
    }
  catch (const gdb_exception_error &e)
    {
      exception_print (gdb_stderr, e);
      return note_data;
    }

  struct intelgt_corefile_thread_data thread_args;
  thread_args.gdbarch = gdbarch;
  thread_args.obfd = obfd;
  thread_args.note_data = note_data.release ();
  thread_args.note_size = note_size;
  /* In case of `gcore` there is no signal: */
  thread_args.stop_signal = GDB_SIGNAL_0;

  for (thread_info *thread : current_inferior ()->threads ())
    intelgt_corefile_write_thread (thread, &thread_args);

  /* File (compute kernel) mappings.  */

  struct type *long_type
      = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch), 0, "long");
  file_mappings_builder mapping_builder (long_type);

  uint64_t count = 0;

  for (struct so_list *so : current_program_space->solibs ())
    {
      if (so->so_name[0] == '\0')
	continue;
      ++count;
      ULONGEST so_addr = count << 50;

      /* Find the kernel ELF on the local filesystem and copy it
	into the core file memory: */
      void *content = nullptr;
      ULONGEST so_size = 0;
      if (so->begin != 0 && so->end != 0)
      {
	gdb_assert (so->end > so->begin);
	so_size = so->end - so->begin;
	content = xmalloc (so_size);
	read_memory (so->begin, (gdb_byte *)content, so_size);
      }
      else
      {
	FILE *so_file = fopen (so->so_name, "r");
	if (so_file == nullptr)
	  error ("Could not open the file %s", so->so_name);
	fseek (so_file, 0L, SEEK_END);
	so_size = ftell (so_file);
	gdb_assert (so_size > 0);
	fseek (so_file, 0, SEEK_SET);
	content = xmalloc (so_size);
	ULONGEST rsize = fread (content, 1, so_size, so_file);
	fclose (so_file);
	if (rsize != so_size)
	  error ("Failed to read %s", so->so_name);
      }

      flagword flags = SEC_ALLOC | SEC_HAS_CONTENTS | SEC_LOAD;
      asection *osec = bfd_make_section_anyway_with_flags (obfd,
							   "load-bin-gt",
							   flags);
      if (osec == nullptr)
       {
	 error ("Failed to create a section: %s",
		bfd_errmsg (bfd_get_error ()));
	 continue;
       }
      bfd_set_section_size (osec, so_size);
      bfd_set_section_vma (osec, so_addr);
      bfd_set_section_lma (osec, 0);
      if (!bfd_set_section_userdata (osec, content))
	error ("Failed to set section contents: %s",
	       bfd_errmsg (bfd_get_error ()));

      mapping_builder.add (
	 { so_addr, so_size, so->addr_low, so->so_name });
    }

  gdb::byte_vector file_note_data = mapping_builder.build ();
  thread_args.note_data = elfcore_write_file_note (obfd,
						   thread_args.note_data,
						   thread_args.note_size,
						   file_note_data.data (),
						   file_note_data.size ());

  note_data.reset (thread_args.note_data);
  return note_data;
}

static int
intelgt_core_load_hook (struct gdbarch *gdbarch, bfd *abfd)
{
  asection *section = bfd_get_section_by_name (abfd, ".note.linuxcore.file");
  if (section == nullptr)
    return -1;

  type *long_type
      = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch), 0, "long");

  size_t note_size = bfd_section_size (section);
  gdb::def_vector<gdb_byte> contents (note_size);
  if (!bfd_get_section_contents (core_bfd, section, contents.data (), 0,
				 note_size))
    {
      warning (_("could not get core note contents"));
      return -1;
    }

  iterate_file_mappings (
      &contents, long_type,
      [=] (int count)
       {
       },
      [=] (int i, const file_mapping &fm)
       {
	 gdb_bfd_ref_ptr mem_bfd (gdb_bfd_open_from_target_memory (
	       fm.vaddr, fm.size, "elf64-intelgt"));
	 gdb_printf (_("Loading object file embedded into "
			    "the core at 0x%lx-0x%lx\n"),
	       fm.vaddr, fm.vaddr + fm.size);
	 if (!bfd_check_format (mem_bfd.get (), bfd_object))
	   error (_("Got object file from the core but "
		    "can't read symbols: %s."),
		  bfd_errmsg (bfd_get_error ()));

	 section_addr_info sai;
	 bfd_section *sec;
	 for (sec = mem_bfd->sections; sec != NULL; sec = sec->next)
	   if ((bfd_section_flags (sec) & (SEC_ALLOC | SEC_LOAD)) != 0)
	     sai.emplace_back (bfd_section_vma (sec) + fm.offset,
			       bfd_section_name (sec), sec->index);

	 objfile *objf = symbol_file_add_from_bfd (mem_bfd, fm.filename,
						   0, &sai,
						   OBJF_SHARED, NULL);
	 current_program_space->add_target_sections (objf);
	 reinit_frame_cache ();
       });

  return 0;
}

/* Core file may contain pid values in different formats, depending on a setup.
   Check which one is available and adapt text representation accordingly.  */

static std::string
intelgt_core_pid_to_str (struct gdbarch *gdbarch, ptid_t ptid)
{
  if (ptid.lwp () != 0)
    return string_printf ("LWP %ld", ptid.lwp ());
  else
    return normal_pid_to_str (ptid);
}

/* Core file may embed a target description information about registers
   used during the crashed program execution.  Use it if present.  */

static const struct target_desc *
intelgt_core_read_description (struct gdbarch *gdbarch,
			       struct target_ops *target, bfd *abfd)
{
  asection *section = bfd_get_section_by_name (abfd, ".note.intelgt");
   if (section == nullptr)
    return nullptr;
  size_t note_size = bfd_section_size (section);
  gdb::def_vector<char> contents (note_size, 0);
  if (!bfd_get_section_contents (abfd, section, contents.data (), 0,
				 note_size))
    return nullptr;
  gdb_assert (contents[contents.size () - 1] == 0);
  /* Skip fname.  */
  size_t fname_len = strlen (contents.data () + 8) + 1;
  auto tdesc = string_read_description_xml (contents.data () + 8 + fname_len);
  if (tdesc == nullptr)
    error ("Can't handle intelgt core file with a missing target description");
  return tdesc;
}

/* Filter VMAs to be dumped to a core file.  Currently nothing
   is written - .text will come from an embedded full binary and
   there is no good way to get small enough useful set of user pages
   to keep.  Core files generated by the system will have more
   memory regions though.  */

static int
intelgt_find_memory_regions (struct gdbarch *gdbarch,
			     find_memory_region_ftype func, void *obfd)
{
  return 0;
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
  if (arg_type->length () > max_size)
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
  int field_len = field_type->length ();
  int current_pos = fields[field_index].loc_bitpos () / 8;

  /* Determine the memory occupation of the field (field size + padding).  */
  unsigned int total_memory = field_len;
  if (field_index < struct_type->num_fields () - 1)
    {
      int next_pos = fields[field_index + 1].loc_bitpos () / 8;
      total_memory = next_pos - current_pos;
    }
  else
    total_memory = (struct_type->length () - current_pos);

  return total_memory;
}

/* Inferior calls is still not fully supported on Windows.  */
#ifndef USE_WIN32API

/* Return the number of registers required to store an argument.  ARG_TYPE is
   the type of the argument.  */

static unsigned int
get_argument_required_registers (gdbarch *gdbarch, type *arg_type)
{
  const int len = arg_type->length ();
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
					 PROMOTABLE_STRUCT_MAX_SIZE)))
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
intelgt_dummy_id (struct gdbarch *gdbarch, frame_info_ptr this_frame)
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
  /* Non-promotable structure return values are converted
     to be passed by reference as the first argument in the arguments
     list of the function.  */
  return (
    class_or_union_p (type)
    && !is_a_promotable_small_struct (type, PROMOTABLE_STRUCT_MAX_SIZE));
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
intelgt_unwind_sp (gdbarch *gdbarch, frame_info_ptr next_frame)
{
  /* Extract the front-end stack pointer from the "framedesc" register.
     The size of the framedesc.fe_sp is 8 bytes with an offset of 24.  */
  int framedesc_regnum = intelgt_pseudo_register_num (gdbarch, "framedesc");
  value *unwound_framedesc
    = frame_unwind_register_value (next_frame, framedesc_regnum);
  gdb_byte *raw_bytes = value_contents_raw (unwound_framedesc).data ();
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR fe_sp = extract_unsigned_integer (raw_bytes + 24, 8, byte_order);

  return fe_sp;
}

/* This stack is used to store the instructions at the entry point of
   the kernel when an inferior call takes place, so that we can restore
   the original contents after the infcall completes.  We need stack here
   to keep track of the entry point instructions for nested inferior
   calls.
   The stack entry is a pair of the byte array which holds the entry
   instruction and a bool flag, which shows the instruction validity.
   The instruction may be invalid in case we failed to find the kernel
   entry point or read the memory from this point.  */

std::stack<std::pair<std::array<gdb_byte, intelgt::MAX_INST_LENGTH>, bool>>
entry_inst_stack;

/* Determine the kernel start address.  */

static bool
get_kernel_entry (CORE_ADDR pc, CORE_ADDR &entry_addr)
{
  /* TODO: this needs to be changed once we have kernel start address
     available in registers.  Until then find the section which contains
     the PC and take its address.  */
  obj_section *s = find_pc_section (pc);
  if (s == nullptr)
    return false;

  entry_addr = s->addr ();
  return true;
}

/* Intelgt implementation of the "push_dummy_code" method.

   In this function, we are injecting a CALLA instruction at the entry point
   address of the kernel.  We set the REAL_PC to start executing from the
   injected instruction, which will then force the function
   to return to the next instruction address, and that would be the
   BP_ADDR.  */

static CORE_ADDR
intelgt_push_dummy_code (gdbarch *gdbarch, CORE_ADDR sp, CORE_ADDR funaddr,
			 value **args, int nargs, type *value_type,
			 CORE_ADDR *real_pc, CORE_ADDR *bp_addr,
			 regcache *regcache)
{
  /* We push to the ENTRY_INST_STACK unconditionally, to avoid branching,
     since the post-clean up is called on every error.
     At first, the instruction is marked invalid (FALSE).  */
  std::array<gdb_byte, intelgt::MAX_INST_LENGTH> entry_inst;
  entry_inst_stack.push (std::make_pair (std::move (entry_inst), false));

  CORE_ADDR entry_addr = 0;
  if (!get_kernel_entry (funaddr, entry_addr))
    error ("Failed to get kernel starting address.");

  int err = 0;
  /* Save the original instruction data in ENTRY_INST_STACK so we can restore
     it back after running the infcall.  */
  err = target_read_memory (entry_addr, entry_inst_stack.top ().first.data (),
			    intelgt::MAX_INST_LENGTH);
  if (err != 0)
    error ("Target failed to read memory at 0x%lx", entry_addr);

  /* We have read the entry instruction.  Mark it valid.  */
  entry_inst_stack.top ().second = true;

  /* Compute the execution size from SIMD_WIDTH, below is the EXEC_SIZE
     encoding according to the spec.
     000b = 1 Channels
     001b = 2 Channels
     010b = 4 Channels
     011b = 8 Channels
     100b = 16 Channels
     101b = 32 Channels.  */
  const uint32_t simd_width = get_simd_width_for_pc (funaddr);
  uint32_t exec_size = 0;
  while ((simd_width >> exec_size) > 1)
    exec_size++;

  /* Make sure that 2^EXEC_SIZE = SIMD_WIDTH.  */
  gdb_assert (1 << exec_size == simd_width);

  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  /* Construct the dummy CALLA instruction.  */
  gdb_byte calla_inst[intelgt::MAX_INST_LENGTH];
  memset (calla_inst, 0, intelgt::MAX_INST_LENGTH);

  constexpr uint32_t calla_opcode = 0x2b;
  calla_inst[0] = calla_opcode;
#if defined (HAVE_LIBIGA64)
  calla_inst[2] = data->iga_version >= IGA_XE_HPC ? exec_size << 2 : exec_size;
#else
  error ("Inferior call feature is not available: libiga64 is missing.");
#endif /* defined (HAVE_LIBIGA64)  */

  /* Location of the CallMask field in the framedesc register (subreg 0)
     and the Src0.IsImm field (bit 46).  */
  constexpr uint32_t dst_subreg = 0x04;
  calla_inst[6] = dst_subreg;
  /* Destination register number for the CALLA instruction.  */
  uint32_t dst_reg = data->framedesc_base_regnum ();
  calla_inst[7] = dst_reg;

  /* Determine the jump IP from function address.
     FUNADDR = JIP + $isabase.  */
  gdb_assert (data->isabase_regnum != -1);
  uint64_t isabase = 0, jump_ip = 0;
  if (regcache->cooked_read (data->isabase_regnum, &isabase) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (isabase) is not available"),
		 data->isabase_regnum);
  jump_ip = funaddr - isabase;

  /* Store the JIP in the last 4 bytes of the CALLA instruction.  */
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  store_unsigned_integer (calla_inst + intelgt::MAX_INST_LENGTH - 4, 4,
			  byte_order, (uint32_t) jump_ip);

  /* Inject the dummy CALLA instruction.  */
  err = target_write_memory (entry_addr, calla_inst, intelgt::MAX_INST_LENGTH);
  if (err != 0)
    error ("Target failed to inject a dummy calla instruction at 0x%lx",
	   entry_addr);

  /* Update the REAL_PC to execute the CALLA, which would make the function
     return to the next address.  Use that address BP_ADDR.  */
  *real_pc = entry_addr;
  *bp_addr = entry_addr + intelgt::MAX_INST_LENGTH;

  return sp;
}

/* Intelgt implementation of the "post_infcall" method.

   In this function, we restore the entry point original instruction that we
   used to inject a CALLA instruction in intelgt_push_dummy_code.  */

static void
intelgt_post_infcall (gdbarch *gdbarch, CORE_ADDR funaddr)
{
  /* There always should be an entry instruction restore buffer.  */
  gdb_assert (!entry_inst_stack.empty ());
  CORE_ADDR entry_addr = 0;
  if (get_kernel_entry (funaddr, entry_addr))
    {
      /* Restore the entry instruction only if its restore value
	 is valid.  */
      if (entry_inst_stack.top ().second)
	{
	  int err = target_write_memory (entry_addr,
					 entry_inst_stack.top ().first.data (),
					 intelgt::MAX_INST_LENGTH);
	  if (err != 0)
	    warning ("Target failed to restore instruction at 0x%lx",
		     entry_addr);
	}
    }
  else
    warning ("Failed to get kernel starting address.");

  entry_inst_stack.pop ();
}

/* Intelgt implementation of the "push_dummy_call" method.  */

static CORE_ADDR
intelgt_push_dummy_call (gdbarch *gdbarch, value *function, regcache *regcache,
			 CORE_ADDR bp_addr, int nargs, value **args,
			 CORE_ADDR sp,
			 function_call_return_method return_method,
			 CORE_ADDR struct_addr)
{
  CORE_ADDR function_pc = value_address (function);
  const unsigned int simd_width = get_simd_width_for_pc (function_pc);
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

  /* Determine the reserved space for the returned struct.  This includes
     large vectors that do not fit into available return GRFs.  */
  CORE_ADDR reserved_struct_memory = 0;
  if (return_method == return_method_struct)
    reserved_struct_memory = fe_sp - struct_addr;

  /* Structures returned by values need to be allocated after arguments.
     Use the reserved space for pushing arguments, and then reallocate
     it again at the end of the frame for the return value.  */
  fe_sp = align_up (fe_sp - reserved_struct_memory, OWORD_SIZE);

  /* Push all struct objects (except for promoted structs) to the stack
     and save the corresponding addresses.  */
  std::vector<CORE_ADDR> obj_addrs;
  for (int index = 0; index < nargs; ++index)
    {
      type *arg_type = check_typedef (value_type (args[index]));
      /* Type's length is the size of the argument in bytes.  */
      int len = arg_type->length ();

      /* For argument structs, a maximum size of 128-bits (16-bytes)
	 is used for the promotion check.  */
      if (class_or_union_p (arg_type)
	  && !is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE))
	{
	  const gdb_byte *val = value_contents (args[index]).data ();

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
  auto grf = grf_handler (retval_regsize, regcache, simd_width);

  for (int argnum = 0; argnum < nargs; ++argnum)
    {
      type *arg_type = check_typedef (value_type (args[argnum]));
      /* Compute the required number of registers to store the argument.  */
      int required_registers
	= get_argument_required_registers (gdbarch, arg_type);
      /* LEN is the size of the argument in bytes.  */
      int len = arg_type->length ();
      const gdb_byte *val = value_contents (args[argnum]).data ();

      /* If the argument can fit into the remaining GRFs then it needs to be
	 copied there.  */
      if (required_registers + regnum
	  <= retval_regnum + INTELGT_MAX_GRF_REGS_FOR_ARGS)
	{
	  /* First available GRF register to write data into.  */
	  int target_regnum = regnum;

	  if (is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE))
	    grf.write_small_struct (target_regnum, arg_type, val);

	  /* The argument has been pushed to the FE stack, and its
	     reference needs to be passed to the register.  */
	  else if (class_or_union_p (arg_type))
	    grf.write_primitive (target_regnum, address_size,
			       (const gdb_byte *) &obj_addrs[obj_index++]);

	  /* Write vector elements to GRFs.  */
	  else if (arg_type->is_vector ())
	    grf.write_vector (target_regnum, arg_type, val);

	  /* Write primitive values to GRFs.  */
	  else if (len <= 8)
	    grf.write_primitive (target_regnum, len, val);

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
					    PROMOTABLE_STRUCT_MAX_SIZE))
	    fe_sp = fe_stack_write_small_struct (fe_sp, arg_type, val,
						 simd_width);
	  else if (class_or_union_p (arg_type))
	    {
	      /* The object has been previously pushed to the stack, now push
		 its saved address to be aligned with the rest of the
		 arguments in the stack.  */
	      gdb_byte *obj_addr = (gdb_byte *) &obj_addrs[obj_index++];
	      fe_sp = fe_stack_write_primitive (fe_sp, address_size, obj_addr,
						simd_width);
	    }
	  else if (arg_type->is_vector ())
	    fe_sp = fe_stack_write_vector (fe_sp, arg_type, val, simd_width);

	  else if (len <= 8)
	    fe_sp = fe_stack_write_primitive (fe_sp, len, val, simd_width);

	  else
	    error ("unexpected type %s of arg %d", arg_type->name (), argnum);
	}
    }

  /* Reallocate space for structures returned by values.  */
  fe_sp = align_up (fe_sp + reserved_struct_memory, OWORD_SIZE);

  /* Update the FE frame pointer (framedesc.fe_fp).  */
  regcache->cooked_write_part (framedesc_regnum, 16, 8, (gdb_byte *) &fe_sp);
  /* Update the FE stack pointer (framedesc.fe_sp).  */
  regcache->cooked_write_part (framedesc_regnum, 24, 8, (gdb_byte *) &fe_sp);
  return fe_sp;
}

/* Intelgt implementation of the "reserve_stack_space" method.  The SIMD
   width needs to be considered when reserving memory for VALUE_TYPE.  */

static CORE_ADDR
intelgt_reserve_stack_space (gdbarch *gdbarch, const type *value_type,
			     CORE_ADDR &sp)
{
  const unsigned int simd_width = inferior_thread ()->get_simd_width ();

  /* Make sure the stack is aligned.  */
  sp = align_up (sp, OWORD_SIZE);
  CORE_ADDR struct_addr = sp;
  sp = align_up (sp + value_type->length () * simd_width, OWORD_SIZE);

  return struct_addr;
}

/* Intelgt implementation of the "get_inferior_call_return_value" method.  */

static value *
intelgt_get_inferior_call_return_value (gdbarch *gdbarch,
					call_return_meta_info *ri)
{
  value *retval = nullptr;

  retval = allocate_value (ri->value_type);
  gdbarch_return_value (ri->gdbarch, ri->function, ri->value_type,
			get_current_regcache (),
			value_contents_raw (retval).data (), nullptr);

  gdb_assert (retval != nullptr);
  return retval;
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
grf_handler::read_primitive (int regnum, int len, gdb_byte *buff)
{
  handle_primitive (regnum, nullptr, buff, len);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::write_primitive (int regnum, int len, const gdb_byte *buff)
{
  handle_primitive (regnum, buff, nullptr, len);
}

/* See GRF_HANDLER declaration.  */

void
grf_handler::handle_small_struct (int regnum, const gdb_byte *buff_read,
				  gdb_byte *buff_write, type *valtype)
{
  /* The vectorized return value is stored at this register and onwards.  */
  const int simd_lane = inferior_thread ()->current_simd_lane ();

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
      int field_len = field_type->length ();

      /* Total field size after SIMD vectorization.  */
      int mem_occupation = m_simd_width * get_field_total_memory (
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
      int current_pos = fields[field_idx].loc_bitpos () / 8;

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
  int target_regnum = regnum;

  /* Vectors are stored in GRFs with the Structure of Arrays (SoA) layout.  */

  int len = valtype->length ();
  /* Length in bytes of an element in the vector.  */
  int element_len = valtype->target_type ()->length ();
  /* Number of elements in the vector.  */
  int n_elements = len / element_len;

  for (int element_idx = 0; element_idx < n_elements; ++element_idx)
    {
      int lane_offset = current_lane * element_len;
      int total_offset
	  = lane_offset + element_idx * element_len * m_simd_width;
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
grf_handler::handle_primitive (int regnum, const gdb_byte *buff_read,
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

static CORE_ADDR
fe_stack_handle_vector (CORE_ADDR addr, type *valtype,
			const gdb_byte *buff_read, gdb_byte *buff_write,
			const unsigned int simd_width)
{
  gdb_assert (valtype->is_vector ());
  gdb_assert ((buff_read == nullptr) != (buff_write == nullptr));

  /* Vectors are copied to stack with the SoA layout.  */

  const int current_lane = inferior_thread ()->current_simd_lane ();
  int len = valtype->length ();
  CORE_ADDR fe_addr = addr;

  /* Length in bytes of an element in the vector.  */
  int target_type_len = valtype->target_type ()->length ();
  /* Number of elements in the vector.  */
  int n_elements = len / target_type_len;

  for (int element_idx = 0; element_idx < n_elements; ++element_idx)
    {
      int lane_offset = current_lane * target_type_len;

      int total_offset
	  = lane_offset + element_idx * target_type_len * simd_width;

      if (buff_read != nullptr)
	{
	  /* Location of the element in the vector.  */
	  const gdb_byte *element_addr
	      = buff_read + element_idx * target_type_len;
	  int err = target_write_memory (fe_addr + total_offset, element_addr,
					 target_type_len);
	  if (err != 0)
	    error ("Target failed to write vector on the stack: "
		   "type %s of length %d",
		   valtype->name (), len);
	}
      else if (buff_write != nullptr)
	{
	  /* Location of the element in the vector.  */
	  gdb_byte *element_addr = buff_write
				   + element_idx * target_type_len;
	  int err = target_read_memory (fe_addr + total_offset, element_addr,
					 target_type_len);
	  if (err != 0)
	    error ("Target failed to read vector from the stack: "
		   "type %s of length %d",
		   valtype->name (), len);
	}
    }

  /* Align the stack.  */
  fe_addr = align_up (fe_addr + len * simd_width, OWORD_SIZE);
  return fe_addr;
}

static CORE_ADDR
fe_stack_handle_primitive (CORE_ADDR addr, int len, const gdb_byte *buff_read,
			   gdb_byte *buff_write,
			   const unsigned int simd_width)
{
  gdb_assert (len <= 8);
  gdb_assert ((buff_read == nullptr) != (buff_write == nullptr));

  const int current_lane = inferior_thread ()->current_simd_lane ();
  CORE_ADDR fe_addr = addr;

  if (buff_read != nullptr)
    {
      int err
	  = target_write_memory (fe_addr + current_lane * len, buff_read, len);
      if (err != 0)
	error ("Target failed to write bytes on the stack: "
	       "length %d to address 0x%lx", len, addr);
    }
  else if (buff_write != nullptr)
    {
      int err
	= target_read_memory (fe_addr + current_lane * len, buff_write, len);
      if (err != 0)
	error ("Target failed to read bytes from the stack: "
	       "length %d from address 0x%lx", len, addr);
    }

  /* Align the stack.  */
  fe_addr += align_up (len * simd_width, OWORD_SIZE);
  return fe_addr;
}

static CORE_ADDR
fe_stack_handle_small_struct (CORE_ADDR addr, type *valtype,
			      const gdb_byte *buff_read, gdb_byte *buff_write,
			      const unsigned int simd_width)
{
  gdb_assert (is_a_promotable_small_struct (valtype,
					    PROMOTABLE_STRUCT_MAX_SIZE));
  gdb_assert ((buff_read == nullptr) != (buff_write == nullptr));

  /* Promotable structures are stored in the stack with SoA layout.
     Example:
     s.a s.a... s.a  s.b s.b... s.b  s.c s.c... s.c.  */

  const int current_lane = inferior_thread ()->current_simd_lane ();

  int n_fields = valtype->num_fields ();
  field *fields = valtype->fields ();
  CORE_ADDR fe_addr = addr;

  /* Loop over all structure fields.  */
  for (int field_idx = 0; field_idx < n_fields; ++field_idx)
    {
      type *field_type = check_typedef (valtype->field (field_idx).type ());
      int field_len = field_type->length ();

      /* Determine the offset of the field within the struct
	 in bytes.  */
      int current_pos = fields[field_idx].loc_bitpos () / 8;

      if (buff_read != nullptr)
	{
	  /* Write the current field on the stack.  */
	  int err = target_write_memory (fe_addr + current_lane * field_len,
					 buff_read + current_pos, field_len);
	  if (err != 0)
	    error ("Target failed to write struct on the stack: "
		   "type %s of length %lu",
		   valtype->name (), valtype->length ());
	}
      else if (buff_write != nullptr)
	{
	  /* Write the current field on the stack.  */
	  int err = target_read_memory (fe_addr + current_lane * field_len,
					 buff_write + current_pos, field_len);
	  if (err != 0)
	    error ("Target failed to read struct from the stack: "
		   "type %s of length %lu",
		   valtype->name (), valtype->length ());
	}

      /* Update the stack pointer for the next field while
	 considering the structure intra/inter-padding.  */
      int mem_occupation
	= simd_width * get_field_total_memory (valtype, field_idx);
      fe_addr += mem_occupation;
    }

  /* Align the stack.  */
  fe_addr = align_up (fe_addr, OWORD_SIZE);

  return fe_addr;
}

#if defined (HAVE_LIBIGA64)
/* Translate the DEVICE_ID to an IGA version.  */

static iga_gen_t
get_iga_version (unsigned int device_id)
{
  iga_gen_t device_iga_version = IGA_GEN_INVALID;
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
      case 0x56C0:
      case 0x56C1:
      case 0x56CF:
	device_iga_version = IGA_XE_HPG;
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
	device_iga_version = IGA_XE_HP;
	break;

      case 0x0BD0:
      case 0x0BD5:
      case 0x0BD6:
      case 0x0BD7:
      case 0x0BD8:
      case 0x0BD9:
      case 0x0BDA:
      case 0x0BDB:
	device_iga_version = IGA_XE_HPC;
	break;
    }

  return device_iga_version;
}
#endif /* defined (HAVE_LIBIGA64)  */

/* Error out if the register STATUS is not valid.
   REG_NUM is register number added to the final error message.
   ERROR_MESSAGE is prepended to the final error message.  */

static void
intelgt_check_reg_status (register_status status, int reg_num,
			  const std::string &error_message)
{
  if (status < REG_VALID)
    error (_("%s Register %d has status %d."), error_message.c_str (),
	   reg_num, status);
}

namespace intelgt_implicit_args
{
/* A helper function to parse the fields of the implicit args structure.
   ENTRY is the reference to the field, we return the parsed value here.
   OFFSET is the reference to the offset of the field in BUFFER.  After
   parsing the ENTRY, it gets incremented by the SIZEOF (ENTRY).
   BUF is the buffer from where the structure is parsed.
   BYTE_ORDER is arch specific.
   STRUCT_SIZE is the size of the whole struct.  Used for validation.  */

template <typename T>
static void
parse_arg (T &entry, size_t &offset, const gdb_byte *buf,
	   bfd_endian byte_order, uint8_t struct_size)
{
  if (offset + sizeof (entry) > struct_size)
    error (_("Implicit argument parsing failed: (offset %ld + field size %ld) "
	     "is greater than the read struct size %d."),
	   offset, sizeof (entry), struct_size);
  if (offset + sizeof (entry) > sizeof (implicit_args))
    error (_("Implicit argument parsing failed: (offset %ld + field size %ld) "
	     "is greater than the expected struct size %ld."),
	   offset, sizeof (entry), sizeof (implicit_args));
  entry = extract_unsigned_integer (buf + offset, sizeof (entry), byte_order);
  offset += sizeof (entry);
}

/* Parse BUF into the implicit ARGS struct.  The result is written
   to the implicit ARGS.
   Note, the current layout corresponds to the version 0
   of the implicit arguments structure.
   Error out if the structure could not be parsed.  */

static void
parse_struct (implicit_args &args, const gdb_byte *buf,
	      const bfd_endian byte_order)
{
  args.struct_size = extract_unsigned_integer (buf, sizeof (args.struct_size),
					       byte_order);
  /* It could happen that the struct has some fields not yet known to
     the debugger.  Ignore them and continue.  */
  if (args.struct_size > sizeof (implicit_args))
    dprintf ("Implicit arguments have greater size (%d) than expected (%ld).",
	     args.struct_size, sizeof (implicit_args));

  size_t offset = sizeof (args.struct_size);
  const uint8_t struct_size = args.struct_size;
  parse_arg (args.struct_version, offset, buf, byte_order, struct_size);
  parse_arg (args.num_work_dim, offset, buf, byte_order, struct_size);
  parse_arg (args.simd_width, offset, buf, byte_order, struct_size);
  parse_arg (args.local_size_x, offset, buf, byte_order, struct_size);
  parse_arg (args.local_size_y, offset, buf, byte_order, struct_size);
  parse_arg (args.local_size_z, offset, buf, byte_order, struct_size);
  parse_arg (args.global_size_x, offset, buf, byte_order, struct_size);
  parse_arg (args.global_size_y, offset, buf, byte_order, struct_size);
  parse_arg (args.global_size_z, offset, buf, byte_order, struct_size);
  parse_arg (args.printf_buffer_ptr, offset, buf, byte_order, struct_size);
  parse_arg (args.global_offset_x, offset, buf, byte_order, struct_size);
  parse_arg (args.global_offset_y, offset, buf, byte_order, struct_size);
  parse_arg (args.global_offset_z, offset, buf, byte_order, struct_size);
  parse_arg (args.local_id_table_ptr, offset, buf, byte_order, struct_size);
  try
    {
      /* We do not require the following fields to be present.  Do not error
	 out if they are missing.  */
      parse_arg (args.group_count_x, offset, buf, byte_order, struct_size);
      parse_arg (args.group_count_y, offset, buf, byte_order, struct_size);
      parse_arg (args.group_count_z, offset, buf, byte_order, struct_size);
      parse_arg (args.rt_global_buffer_ptr, offset, buf, byte_order,
		 struct_size);
    }
  catch (const gdb_exception_error &e)
    {
      dprintf ("%s", e.message->c_str ());
    }
}

/* Return the address of the implicit args structure.  */

static CORE_ADDR
get_address (gdbarch *gdbarch, thread_info *tp)
{
  /* The implicit arguments address is stored as r0.0[31:6] as a
     general state offset.  */

  regcache *regcache = get_thread_regcache (tp);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  CORE_ADDR implicit_args_address = 0;
  register_status reg_status
    = regcache->cooked_read_part (data->r0_regnum, 0, sizeof (uint32_t),
				  (gdb_byte *) &implicit_args_address);
  std::string error_msg = _("Cannot read implicit arguments.");
  intelgt_check_reg_status (reg_status, data->r0_regnum, error_msg);

  /* Mask out the lowest 6 bits.  */
  implicit_args_address &= ~0x3f;

  /* Adjust with genstbase.  */
  CORE_ADDR genstbase;
  reg_status = regcache->cooked_read (data->genstbase_regnum, &genstbase);
  intelgt_check_reg_status (reg_status, data->genstbase_regnum, error_msg);
  implicit_args_address += genstbase;

  return implicit_args_address;
}

/* Construct the key for the thread TP and the implicit args
   address ADDRESS.  */

static std::string
make_key (gdbarch *gdbarch, thread_info *tp, CORE_ADDR address)
{
  /* Construct the key in the cache.  */
  return (std::to_string (tp->inf->num) + std::to_string (address));
}

/* Construct the key for the thread TP.  */

static std::string
make_key (gdbarch *gdbarch, thread_info *tp)
{
  CORE_ADDR address = get_address (gdbarch, tp);

  return make_key (gdbarch, tp, address);
}

/* Heuristic check that the implicit args structure is valid.
   Error out if the implicit ARGS have an unexpected value.  */

static void
check_valid (const implicit_args &args)
{
  /* The current implementation corresponds to the layout
     defined for version 0.  */
  if (args.struct_version != 0)
    error (_("Implicit arguments struct_version is not expected %d"),
	   args.struct_version);

  /* We require fields up to local_id_table_ptr.  */
  if (args.struct_size <= 80)
    error (_("Implicit arguments struct_size is not expected %d"),
	   args.struct_size);

  /* We expect SIMD width be only 1, 8, 16, or 32.  */
  if (args.simd_width != 1 && args.simd_width != 8
      && args.simd_width != 16 && args.simd_width != 32)
    error (_("Implicit arguments simd_width is not expected %d"),
	   args.simd_width);

  /* The number of dimensions could be 1, 2, or 3.  */
  if (args.num_work_dim == 0 || args.num_work_dim > 3)
    error (_("Implicit arguments num_work_dim is not expected %d"),
	   args.num_work_dim);
}

/* Get the overall number of uint16_t elements in a single local id entry.
   This includes the placeholder ("reserved") values for SIMD 8 case.
   For different SIMD widths the local ID entry in the table looks as:
   SIMD-1:

     struct local_id {
       uint16_t lx;
       uint16_t ly;
       uint16_t lz;
     };

   SIMD-8:

    struct local_id {
      uint16_t lx[8];
      uint16_t <reserved>[8];
      uint16_t ly[8];
      uint16_t <reserved>[8];
      uint16_t lz[8];
      uint16_t <reserved>[8];
    };

  SIMD-16 and SIMD-32:

    struct local_id {
      uint16_t lx[<simd-width>];
      uint16_t ly[<simd-width>];
      uint16_t lz[<simd-width>];
    };

  We store local ID table as a flat list, so we do not care about its
  internal structure.

  Return the flat vector size of a single (x, y, z) entry.  */

static unsigned int
local_id_entry_length (unsigned int simd_width)
{
  switch (simd_width)
    {
    case 1:
      return 3;
    case 8:
      return 8 * 2 * 3;
    case 16:
      return 16 * 3;
    case 32:
      return 32 * 3;
    default:
      error (_("Unexpected SIMD width %d."), simd_width);
    }
}

/* Read and parse the local ID table corresponding to the passed
   IMPLICIT_ARGS struct.  Return the flat vector of decoded elements,
   including the "reserved" parts in-between (e.g., for SIMD 8).

   The local ID table is a table of different combinations of work item
   local IDs within a workgroup.  These combinations are the same for all
   workgroups.

   An entry of the local ID table is a struct with 3 or 6 fields,
   depending on the SIMD width.
   See the comment to local_id_vector_size for the details.

   We store the local ID table as a flat vector, to simplify the representation
   for different SIMD widths.  */

static std::vector<uint16_t>
read_local_id_table (gdbarch *gdbarch, thread_info *tp,
		     const implicit_args &args, bfd_endian byte_order)
{
  /* Read the number of threads in a workgroup from r0.2[31:24].
     Local ID table has that many entries.  */
  uint8_t local_id_table_length;
  regcache *regcache = get_thread_regcache (tp);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  register_status reg_status
    = regcache->cooked_read_part (data->r0_regnum,
				  2 * sizeof (uint32_t) + 3 * sizeof (uint8_t),
				  sizeof (uint8_t),
				  (gdb_byte *) &local_id_table_length);
  intelgt_check_reg_status (reg_status, data->r0_regnum,
			    _("Cannot read number of elements in local ID "
			      "table."));
  dprintf ("Number of elements in local ID table: %u", local_id_table_length);

  /* The vector-length of one entry in the local ID table.  */
  const size_t local_id_entry_len = local_id_entry_length (args.simd_width);

  /* The number of uint16_t elements to read for the complete
     local ID table.  */
  const size_t elements_to_read = local_id_table_length * local_id_entry_len;

  /* Buffer to read the raw local ID table.  */
  const size_t bytes_to_read = sizeof (uint16_t) * elements_to_read;
  std::vector<gdb_byte> local_ids_raw (bytes_to_read);
  int err = target_read_memory (args.local_id_table_ptr,
				local_ids_raw.data (), bytes_to_read);
  if (err != 0)
    error (_("Cannot read local ID table at address 0x%lx of size %ld."),
	   args.local_id_table_ptr, bytes_to_read);

  /* The parsed local ID table.  */
  std::vector<uint16_t> local_ids (elements_to_read);
  for (int i = 0; i < elements_to_read; i++)
    local_ids[i]
      = extract_unsigned_integer (local_ids_raw.data () + i * sizeof (uint16_t),
				  sizeof (uint16_t), byte_order);

  return local_ids;
}

/* Read the implicit args struct for the thread TP.  If the final struct
   is valid, store it in the global cache.  */

static void
read_args (gdbarch *gdbarch, thread_info *tp)
{
  const bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR args_address = get_address (gdbarch, tp);

  const std::string cache_key = make_key (gdbarch, tp, args_address);
  /* We should not re-read the same implicit arguments.  */
  gdb_assert (implicit_args_cache.count (cache_key) == 0);

  /* Read the whole struct with the size we expect.  */
  gdb_byte buf[sizeof (implicit_args)];
  if (target_read_memory (args_address, buf, sizeof (implicit_args)) != 0)
    error (_("Could not read implicit args structure of size %ld "
	     "at address 0x%lx."),
	   sizeof (implicit_args), args_address);

  implicit_args implicit_args;
  parse_struct (implicit_args, buf, byte_order);
  /* Heuristic sanity check of the struct.  */
  check_valid (implicit_args);

  /* Now read the local IDs flat sequence.  */
  std::vector<uint16_t> local_ids
    = read_local_id_table (gdbarch, tp, implicit_args, byte_order);

  /* Cache both implicit args and local IDs.  */
  implicit_args_cache[cache_key]
    = std::make_pair (std::move (implicit_args), std::move (local_ids));
}
} /* namespace intelgt_implicit_args.  */

static implicit_args_value_pair &
intelgt_implicit_args_find_value_pair (gdbarch *gdbarch, thread_info *tp)
{
  if (tp->is_unavailable ())
    error (_("Cannot read implicit arguments of unavailable thread."));

  const std::string key = intelgt_implicit_args::make_key (gdbarch, tp);

  /* If the implicit args were not yet read, try to read it.  This can happen
     if the ID is being evaluated in the BP condition, so the normal stop
     event has not yet occured.  */
  if (implicit_args_cache.count (key) == 0)
    intelgt_implicit_args::read_args (gdbarch, tp);
  gdb_assert (implicit_args_cache.count (key) > 0);

  return implicit_args_cache[key];
}

/* We clear the cached values every time the target is truly resumed,
   even if the stop was not shown to a user, e.g., after BP condition
   was not met.  */

static void
intelgt_on_target_resumed_internal (process_stratum_target *target,
				    ptid_t ptid)
{
  dprintf ("Clear implicit arguments cache.");
  implicit_args_cache.clear ();
}

/* Return workgroup coordinates of the specified thread TP.  */

static std::array<uint32_t, 3>
intelgt_thread_workgroup (struct gdbarch *gdbarch, thread_info *tp)
{
  std::string err_msg = _("Cannot read thread workgroup.");

  if (tp->is_unavailable ())
    error ("%s", err_msg.c_str ());

  std::array<uint32_t, 3> workgroup;

  /* The workgroup coordinates are stored as { r0.1, r0.6, r0.7 }.  */
  regcache *regcache = get_thread_regcache (tp);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  register_status reg_status;
  reg_status = regcache->cooked_read_part (data->r0_regnum,
					   1 * sizeof (uint32_t),
					   sizeof (uint32_t),
					   (gdb_byte *) &workgroup[0]);
  intelgt_check_reg_status (reg_status, data->r0_regnum, err_msg);

  reg_status = regcache->cooked_read_part (data->r0_regnum,
					   6 * sizeof (uint32_t),
					   sizeof (uint32_t),
					   (gdb_byte *) &workgroup[1]);
  intelgt_check_reg_status (reg_status, data->r0_regnum, err_msg);

  reg_status = regcache->cooked_read_part (data->r0_regnum,
					   7 * sizeof (uint32_t),
					   sizeof (uint32_t),
					   (gdb_byte *) &workgroup[2]);
  intelgt_check_reg_status (reg_status, data->r0_regnum, err_msg);

  return workgroup;
}

/* Compute the local ID coordinates within a workgroup for a given
   thread TP.  */

static std::array<uint32_t, 3>
intelgt_workitem_local_id (gdbarch *gdbarch, thread_info *tp)
{
  std::string err_msg = _("Cannot read work item local ID.");
  if (tp->is_unavailable ())
    error (_("%s"), err_msg.c_str ());

  const implicit_args_value_pair &value_pair
    = intelgt_implicit_args_find_value_pair (gdbarch, tp);
  const implicit_args &implicit_args = value_pair.first;
  const std::vector<uint16_t> &local_ids = value_pair.second;

  /* Index of thread TP within the local ID table, stored in r0.2[7:0].  */
  uint8_t tid;
  regcache *regcache = get_thread_regcache (tp);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  register_status reg_status
    = regcache->cooked_read_part (data->r0_regnum,
				  2 * sizeof (uint32_t), sizeof (uint8_t),
				  (gdb_byte *) &tid);
  intelgt_check_reg_status (reg_status, data->r0_regnum, err_msg);

  /* Local ID entry size.  */
  unsigned int local_id_len
    = intelgt_implicit_args::local_id_entry_length (implicit_args.simd_width);
  gdb_assert (local_id_len % 3 == 0);

  std::array<uint32_t, 3> local_id;
  int lane = tp->current_simd_lane ();
  local_id[0] = local_ids[tid * local_id_len + 0 * local_id_len / 3 + lane];
  local_id[1] = local_ids[tid * local_id_len + 1 * local_id_len / 3 + lane];
  local_id[2] = local_ids[tid * local_id_len + 2 * local_id_len / 3 + lane];

  return local_id;
}

/* Compute the global ID coordinates for a given thread TP.  */

static std::array<uint32_t, 3>
intelgt_workitem_global_id (gdbarch *gdbarch, thread_info *tp)
{
  if (tp->is_unavailable ())
    error (_("Cannot read work item global ID of unavailable thread."));

  std::array<uint32_t, 3> local_id = intelgt_workitem_local_id (gdbarch, tp);
  std::array<uint32_t, 3> group = intelgt_thread_workgroup (gdbarch, tp);
  const implicit_args &implicit_args
    = intelgt_implicit_args_find_value_pair (gdbarch, tp).first;

  std::array<uint32_t, 3> global_id;
  global_id[0] = group[0] * implicit_args.local_size_x + local_id[0];
  global_id[1] = group[1] * implicit_args.local_size_y + local_id[1];
  global_id[2] = group[2] * implicit_args.local_size_z + local_id[2];

  return global_id;
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

  if (tdesc != nullptr)
    {
      const std::string &tdesc_device_id
	= tdesc_find_device_info_attribute (tdesc, "target_id");
      if (tdesc_device_id.empty ())
	warning (_("Failed to read target id from device."));
      else
	{
	  iga_version
	    = get_iga_version (std::stoi (tdesc_device_id, nullptr, 16));
	  if (iga_version == IGA_GEN_INVALID)
	    warning (_("Intel GT device id is unrecognized"));
	}
    }

  /* Take the best guess in case IGA_VERSION is still invalid.  */
  if (iga_version == IGA_GEN_INVALID)
    iga_version = IGA_XE_HPC;

  data->iga_version = iga_version;
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

  /* Core file support.  */
  set_gdbarch_gcore_bfd_target (gdbarch, "elf64-intelgt");
  set_gdbarch_find_memory_regions (gdbarch, intelgt_find_memory_regions);
  // FIXME: Uncomment the following line to allow core file generation
  // on intelgt targets via `gcore` command.
  // set_gdbarch_make_corefile_notes (gdbarch, intelgt_make_corefile_notes);
  set_gdbarch_core_load_hook (gdbarch, intelgt_core_load_hook);
  set_gdbarch_iterate_over_regset_sections
    (gdbarch, intelgt_iterate_over_regset_sections);
  set_gdbarch_core_pid_to_str (gdbarch, intelgt_core_pid_to_str);
  set_gdbarch_core_read_description
    (gdbarch, intelgt_core_read_description);

#if defined (USE_WIN32API)
  set_gdbarch_has_dos_based_file_system (gdbarch, 1);
#endif

  set_gdbarch_address_class_name_to_type_flags
    (gdbarch, intelgt_address_class_name_to_type_flags);
  set_gdbarch_address_class_type_flags_to_name
    (gdbarch, intelgt_address_class_type_flags_to_name);
  set_gdbarch_address_space_from_type_flags
    (gdbarch, intelgt_address_space_from_type_flags);
  set_gdbarch_address_class_type_flags
    (gdbarch, intelgt_address_class_type_flags);

  set_gdbarch_is_inferior_device (gdbarch, true);
  set_gdbarch_thread_workgroup (gdbarch, intelgt_thread_workgroup);
  set_gdbarch_workitem_local_id (gdbarch, intelgt_workitem_local_id);
  set_gdbarch_workitem_global_id (gdbarch, intelgt_workitem_global_id);

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
  set_gdbarch_call_dummy_location (gdbarch, AT_CUSTOM_POINT);
  set_gdbarch_reserve_stack_space (gdbarch, intelgt_reserve_stack_space);
  set_gdbarch_push_dummy_code (gdbarch, intelgt_push_dummy_code);
  set_gdbarch_post_infcall (gdbarch, intelgt_post_infcall);
  set_gdbarch_get_inferior_call_return_value (
    gdbarch, intelgt_get_inferior_call_return_value);
#endif /* not USE_WIN32API */

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

  /* We need to invalidate the cache at every target resume, as we do not know,
     whether the cached implicit arguments are valid at the next stop.
     We need to do that even when the stop was not shown to the user,
     e.g., after a breakpoint condition was evaluated to false and the stop
     did not occur.  */
  gdb::observers::target_resumed_internal.attach
    (intelgt_on_target_resumed_internal, "intelgt");
}
