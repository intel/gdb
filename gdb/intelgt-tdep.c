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
#include "block.h"
#include "elf-bfd.h"
#include "infcall.h"
#include <algorithm>
#include <array>

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

/* Write vector from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_vector (CORE_ADDR addr, type *valtype, const gdb_byte *buff,
		       const unsigned int simd_width)
{
  return fe_stack_handle_vector (addr, valtype, buff, nullptr, simd_width);
}

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

/* Write small structure from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_small_struct (CORE_ADDR addr, type *valtype,
			     const gdb_byte *buff,
			     const unsigned int simd_width)
{
  return fe_stack_handle_small_struct (addr, valtype, buff, nullptr,
				       simd_width);
}

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

/* Write up to 8 bytes from BUFF into the stack.  */

static CORE_ADDR
fe_stack_write_primitive (CORE_ADDR addr, int len, const gdb_byte *buff,
			  const unsigned int simd_width)
{
  return fe_stack_handle_primitive (addr, len, buff, nullptr, simd_width);
}

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
};

/* Helper functions to request and translate the device id/version.  */

static xe_version get_xe_version (unsigned int device_id);
static uint32_t get_device_id (inferior *inferior);
static uint32_t get_device_id (gdbarch *gdbarch);

/* Intelgt memory handler to manage memory allocation and releasing of
   a target memory region.  We are using a linked list to keep track of
   memory blocks and serve the ALLOC request with the first-fit approach.

   This class is currently used to manage memory allocations of the scratch
   debug area.  */

class target_memory_allocator
{
  struct data_block
  {
    data_block (CORE_ADDR addr, size_t size, bool reserved, data_block *next)
      : addr (addr), size (size), reserved (reserved), next (next)
    {}
    /* Disable copying.  */
    data_block &operator= (const data_block &) = delete;
    data_block (const data_block &) = delete;

    /* Merge the NEXT block into this block and delete NEXT.  */
    void merge_with_next ()
    {
      if (next != nullptr)
	{
	  gdb_assert (!reserved && !next->reserved);

	  data_block *next_blk = next;
	  size += next_blk->size;
	  next = next_blk->next;
	  delete next_blk;
	}
      else
	dprintf ("Cannot apply merge to the last block.");
    }

    CORE_ADDR addr;
    size_t size;
    bool reserved;
    data_block *next;
  };

public:
  target_memory_allocator (CORE_ADDR start, size_t size)
  {
    blocks_list = new data_block (start, size, false, nullptr);
  }

  ~target_memory_allocator ()
  {
    /* Free up the list.  */
    data_block *head = blocks_list;
    while (head != nullptr)
      {
	data_block *current_blk = head;
	head = head->next;
	delete current_blk;
      }
  }

  /* Disable copying and delete default constructor.  */
  target_memory_allocator &operator= (const target_memory_allocator &)
    = delete;
  target_memory_allocator (const target_memory_allocator &) = delete;
  target_memory_allocator () = delete;

  /* Return the first fitting free block.  */
  CORE_ADDR alloc (size_t size) const
  {
    data_block *head = blocks_list;
    while (head != nullptr)
      {
	/* We found a larger fit block, split it.  */
	if (!head->reserved && (head->size > size))
	  {
	    data_block *new_free_block
	      = new data_block (head->addr + size,
				head->size - size, false, head->next);
	    head->size = size;
	    head->reserved = true;
	    head->next = new_free_block;
	    break;
	  }
	else if (!head->reserved && head->size == size)
	  {
	    /* No need to create a new block, just re-use this one.  */
	    head->reserved = true;
	    break;
	  }

	head = head->next;
      }

    if (head == nullptr)
      error (_("Failed to allocate %" PRIu64
	       " bytes in the debug scratch area."), (uint64_t) size);

    return head->addr;
  }

  void free (CORE_ADDR addr) const
  {
    data_block *head = blocks_list;
    data_block *prev_head = nullptr;
    while (head != nullptr)
      {
	/* The memory address does not belong to any block.  */
	if (addr < head->addr)
	  {
	    dprintf ("Cannot find the corresponding allocated memory in "
		     "scratch area: Addr %s",
		     paddress (current_inferior ()->arch (), addr));
	    head = nullptr;
	    break;
	  }

	if (head->addr == addr)
	  {
	    /* No need to do anything, the block is already free.  */
	    if (!head->reserved)
	      internal_error (_("Double free from the debug scratch area "
				"detected: Addr %s"),
			      paddress (current_inferior ()->arch (), addr));

	    head->reserved = false;
	    /* Merge adjacent free blocks.  */
	    if ((head->next != nullptr) && !head->next->reserved)
	      head->merge_with_next ();
	    if ((prev_head != nullptr) && !prev_head->reserved)
	      prev_head->merge_with_next ();
	    break;
	  }

	prev_head = head;
	head = head->next;
      }

    if (head == nullptr)
      internal_error (_("Failed to free memory from the debug scratch area: "
			"Addr %s"),
		      paddress (current_inferior ()->arch (), addr));
  }

private:
  /* Linked list of blocks ordered by increasing address.  */
  data_block *blocks_list;
};

/* The 'gdbarch_data' stuff specific for this architecture.  */

struct intelgt_gdbarch_data
{
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
  /* Assigned regnum ranges for DWARF regsets.  */
  regnum_range regset_ranges[intelgt::regset_count];
  /* Enabled pseudo-register for the current target description.  */
  std::vector<std::string> enabled_pseudo_regs;
  /* Cached $framedesc pseudo-register type.  */
  type *framedesc_type = nullptr;
  /* Debug area memory manager.  */
  target_memory_allocator *scratch_area = nullptr;

  /* Initialize ranges to -1 as "not-yet-set" indicator.  */
  intelgt_gdbarch_data ()
  {
    memset (&regset_ranges, -1, sizeof regset_ranges);
  }

  /* Return regnum where frame descriptors are stored.  */

  int
  framedesc_base_regnum ()
  {
    /* For EM_INTELGT frame descriptors are stored at MAX_GRF - 1.  */
    gdb_assert (regset_ranges[intelgt::regset_grf].end > 1);
    return regset_ranges[intelgt::regset_grf].end - 1;
  }

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

/* Per-inferior cached data for the Intelgt target.  */

struct intelgt_inferior_data
{
  /* Device target id.  */
  uint32_t device_id = 0u;
};

static const registry<inferior>::key<intelgt_inferior_data>
  intelgt_inferior_data_handle;

/* Fetch the per-inferior data.  */

static intelgt_inferior_data *
get_intelgt_inferior_data (inferior *inf)
{
  intelgt_inferior_data *inf_data = intelgt_inferior_data_handle.get (inf);
  if (inf_data == nullptr)
    inf_data = intelgt_inferior_data_handle.emplace (inf);

  return inf_data;
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

static bool is_a_promotable_small_struct (type *arg_type, int max_size);

/* Implementation of gdbarch's return_value method.  */

static enum return_value_convention
intelgt_return_value (gdbarch *gdbarch, value *function,
		      type *valtype, regcache *regcache,
		      gdb_byte *readbuf, const gdb_byte *writebuf)
{
  dprintf ("return type length %ld", valtype->length ());
  gdb_assert (inferior_ptid != null_ptid);

  int address_size_byte = gdbarch_addr_bit (gdbarch) / 8;
  CORE_ADDR function_pc = function->address ();
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
intelgt_dwarf2_prev_framedesc (const frame_info_ptr &this_frame,
			       void **this_cache, int regnum)
{
  gdbarch *gdbarch = get_frame_arch (this_frame);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

  int actual_regnum = data->framedesc_base_regnum ();

  /* Unwind the actual GRF register.  */
  return frame_unwind_register_value (this_frame, actual_regnum);
}

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
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
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
  dprintf ("prev_ip: %lx", prev_ip);

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
		       void **this_prologue_cache,
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
intelgt_frame_prev_register (const frame_info_ptr &this_frame,
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
  iga_disassemble_options_t dopts = IGA_DISASSEMBLE_OPTIONS_INIT ();
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
  /* $ip is uint32_t, but uint64_t is used here to comply with cooked_read
     signature.  */
  uint64_t ip;
  int ip_regnum = intelgt_pseudo_register_num (arch, "ip");
  if (regcache->cooked_read (ip_regnum, &ip) != REG_VALID)
    throw_error (NOT_AVAILABLE_ERROR,
		 _("Register %d (ip) is not available"),
		 ip_regnum);

  /* Program counter is $ip + $isabase.  */
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
  return isabase + ip;
}

static void
intelgt_write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  gdbarch *arch = regcache->arch ();
  /* Program counter is $ip + $isabase, can only modify $ip.  Need
     to ensure that the new value fits within $ip modification rannge
     and propagate the write accordingly.  */
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
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
				    const frame_info_ptr &next_frame,
				    int pseudo_regnum)
{
  const char *name = intelgt_pseudo_register_name (arch, pseudo_regnum);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);

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
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (arch);

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
  else if (strcmp ("ce", reg_name) == 0)
    data->ce_regnum = possible_regnum;

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

  type *arg_type = check_typedef (arg->type ());
  type *type = param_type ? check_typedef (param_type) : arg_type;

  return value_cast (type, arg);
}

/* Intelgt implementation of the "dummy_id" method.  */

static struct frame_id
intelgt_dummy_id (struct gdbarch *gdbarch, const frame_info_ptr &this_frame)
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
intelgt_unwind_sp (gdbarch *gdbarch, const frame_info_ptr &next_frame)
{
  /* Extract the front-end stack pointer from the "framedesc" register.
     The size of the framedesc.fe_sp is 8 bytes with an offset of 24.  */
  int framedesc_regnum = intelgt_pseudo_register_num (gdbarch, "framedesc");
  value *unwound_framedesc
    = frame_unwind_register_value (next_frame, framedesc_regnum);
  gdb_byte *raw_bytes = unwound_framedesc->contents_raw ().data ();
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR fe_sp = extract_unsigned_integer (raw_bytes + 24, 8, byte_order);

  return fe_sp;
}

/* Read the debug area info and initialize SCRATCH_AREA in intelgt data.  */

static void
intelgt_init_scratch_area (gdbarch *gdbarch)
{
  /* Layout of the debug area header.  */
  struct debug_area_header
  {
    char magic[8] = "";
    uint64_t reserved_1 = 0;
    uint8_t version = 0;
    uint8_t pgsize = 0;
    uint8_t size = 0;
    uint8_t reserved_2 = 0;
    uint16_t scratch_begin = 0;
    uint16_t scratch_end = 0;
  } dbg_header;

  regcache *regcache = get_thread_regcache (inferior_thread ());
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
  int err = target_read_memory (isabase, (gdb_byte *)&dbg_header,
				sizeof dbg_header);
  if (err != 0)
    error (_("Target failed to read the debug area header at %s"),
	   paddress (gdbarch, isabase));

  if (strcmp (dbg_header.magic, "dbgarea") != 0)
    error (_("Failed to find scratch debug area at %s"),
	   paddress (gdbarch, isabase));

  if (dbg_header.version != 0)
    error (_("Unknown version of debug area header."));

  /* Initialize SCRATCH_AREA.  */
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  data->scratch_area = new target_memory_allocator (
    isabase + dbg_header.scratch_begin,
    dbg_header.scratch_end - dbg_header.scratch_begin);
}

/* Return a pointer to the scratch area object.  */

static target_memory_allocator *
get_scratch_area (gdbarch *gdbarch)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  if (data->scratch_area == nullptr)
    {
      intelgt_init_scratch_area (gdbarch);
      if (data->scratch_area == nullptr)
	error (_("Device scratch area is needed for this operation but could "
		 "not be found."));
    }

  return data->scratch_area;
}

/* Intelgt implementation of the dummy frame dtor.  This function will be
   called when a dummy frame is removed or an error is thrown during the
   infcall flow.

   In this dtor, we free up the scratch memory that we used to inject the
   CALLA instruction in "intelgt_push_dummy_code".  */

static void
intelgt_infcall_dummy_dtor (void *data, int unused)
{
  /* Do not error out if any exception is thrown.  */
  try
    {
      auto infcall_cleanup_data = (std::pair<gdbarch *, CORE_ADDR> *) data;
      gdbarch *gdbarch = infcall_cleanup_data->first;
      CORE_ADDR calla_addr = infcall_cleanup_data->second;
      delete infcall_cleanup_data;

      target_memory_allocator *scratch_area = get_scratch_area (gdbarch);
      scratch_area->free (calla_addr);
    }
  catch (const gdb_exception_error &e)
    {
      exception_print (gdb_stderr, e);
    }
}

/* Intelgt implementation of the "push_dummy_code" method.

   In this function, we are injecting a CALLA instruction in the debug area.
   We set the REAL_PC to start executing from the injected instruction,
   which will then force the function to return to the next address, and that
   would be the BP_ADDR.  */

static CORE_ADDR
intelgt_push_dummy_code (gdbarch *gdbarch, CORE_ADDR sp, CORE_ADDR funaddr,
			 value **args, int nargs, type *value_type,
			 CORE_ADDR *real_pc, CORE_ADDR *bp_addr,
			 regcache *regcache,
			 dummy_frame_dtor_ftype **arch_dummy_dtor,
			 void **dtor_data)
{
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);
  target_memory_allocator *scratch_area = get_scratch_area (gdbarch);

  /* Allocate memory for two instructions in the scratch area.  The first is
     for the CALLA, and the second is the return address, where GDB inserts
     a breakpoint.  */
  CORE_ADDR calla_addr = scratch_area->alloc (2 * intelgt::MAX_INST_LENGTH);

  /* Set the dummy frame dtor right after scratch memory allocation,
     so that it gets called for any exception.  */
  auto *infcall_cleanup_data
    = new std::pair<struct gdbarch *, CORE_ADDR> (gdbarch, calla_addr);
  *arch_dummy_dtor = intelgt_infcall_dummy_dtor;
  *dtor_data = infcall_cleanup_data;

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

  /* Make sure to have a cleared buffer for the CALLA instruction
     and the return breakpoint.  */
  gdb_byte buff[2 * intelgt::MAX_INST_LENGTH];
  memset (buff, 0, sizeof (buff));

  /* Construct the dummy CALLA instruction.  */
  gdb_byte *calla_inst = buff;

  constexpr uint32_t calla_opcode = 0x2b;
  calla_inst[0] = calla_opcode;

  thread_info *current_thread = inferior_thread ();

  /* Compute the DEVICE_GEN from the DEVICE_ID, so that we can determine
     the correct encoding for some fields of the instruction.  */
  uint32_t device_id = get_device_id (current_thread->inf);
  xe_version device_version = get_xe_version (device_id);
  switch (device_version)
    {
    case XE_HP:
    case XE_HPG:
      calla_inst[2] = exec_size;
      break;
    case XE_HPC:
      calla_inst[2] = exec_size << 2;
      break;
    default:
      error (_("Unsupported device id 0x%x"), device_id);
    }

  /* We are building r<framedesc-regnum>.0, set RegFile to GRF, and use
     sub-register 0.  */
  calla_inst[6] = 0x04;
  /* Destination register number for the CALLA instruction.  */
  uint32_t dst_reg = data->framedesc_base_regnum ();
  calla_inst[7] = dst_reg;

  /* Determine the jump IP from function address.
     FUNADDR = JIP + $isabase.  */
  CORE_ADDR isabase = intelgt_get_isabase (regcache);
  CORE_ADDR jump_ip = funaddr - isabase;

  /* Store the JIP in the last 4 bytes of the CALLA instruction.  */
  bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  store_unsigned_integer (calla_inst + intelgt::MAX_INST_LENGTH - 4, 4,
			  byte_order, (uint32_t) jump_ip);

  /* Use the NOP instruction for the return breakpoint.  */
  constexpr uint32_t nop_opcode = 0x60;
  gdb_byte *nop_inst = buff + intelgt::MAX_INST_LENGTH;
  nop_inst[0] = nop_opcode;

  /* Inject the dummy CALLA instruction and the breakpoint in the
     reserved space.  */
  int err = target_write_memory (calla_addr, buff, sizeof (buff));
  if (err != 0)
    error ("Target failed to inject a dummy calla instruction at 0x%lx",
	   calla_addr);

  /* Update the REAL_PC to execute the CALLA, which would make the function
     return to the next address.  Use that address as the BP_ADDR.  */
  *real_pc = calla_addr;
  *bp_addr = calla_addr + intelgt::MAX_INST_LENGTH;

  return sp;
}

/* Intelgt implementation of the "push_dummy_call" method.  */

static CORE_ADDR
intelgt_push_dummy_call (gdbarch *gdbarch, value *function, regcache *regcache,
			 CORE_ADDR bp_addr, int nargs, value **args,
			 CORE_ADDR sp,
			 function_call_return_method return_method,
			 CORE_ADDR struct_addr)
{
  CORE_ADDR function_pc = function->address ();
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
      type *arg_type = check_typedef (args[index]->type ());
      /* Type's length is the size of the argument in bytes.  */
      int len = arg_type->length ();

      /* For argument structs, a maximum size of 128-bits (16-bytes)
	 is used for the promotion check.  */
      if (class_or_union_p (arg_type)
	  && !is_a_promotable_small_struct (arg_type,
					    PROMOTABLE_STRUCT_MAX_SIZE))
	{
	  const gdb_byte *val = args[index]->contents ().data ();

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
      type *arg_type = check_typedef (args[argnum]->type ());
      /* Compute the required number of registers to store the argument.  */
      int required_registers
	= get_argument_required_registers (gdbarch, arg_type);
      /* LEN is the size of the argument in bytes.  */
      int len = arg_type->length ();
      const gdb_byte *val = args[argnum]->contents ().data ();

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

  retval = value::allocate (ri->value_type);
  intelgt_return_value (ri->gdbarch, ri->function, ri->value_type,
			get_thread_regcache (inferior_thread ()),
			retval->contents_raw ().data (), nullptr);

  gdb_assert (retval != nullptr);
  return retval;
}

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

/* Helper function to return the device id using the inferior.  */

[[maybe_unused]]
static uint32_t
get_device_id (inferior *inferior)
{
  intelgt_inferior_data *inf_data = get_intelgt_inferior_data (inferior);
  if (inf_data->device_id == 0u)
    inf_data->device_id = get_device_id (inferior->arch ());

  return inf_data->device_id;
}

/* Helper function to return the device id using GDBARCH.  */

static uint32_t
get_device_id (gdbarch *gdbarch)
{
  const target_desc *tdesc = gdbarch_target_desc (gdbarch);
  const tdesc_device *device_info = tdesc_device_info (tdesc);
  if (!device_info->target_id.has_value ())
    error (_("A target id for the device is required."));

  return *device_info->target_id;
}

/* Helper function to translate the device id to a device version.  */

[[maybe_unused]]
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
  gdbarch *gdbarch = gdbarch_alloc (&info, nullptr);
  intelgt_gdbarch_data *data = get_intelgt_gdbarch_data (gdbarch);

#if defined (HAVE_LIBIGA64)
  iga_gen_t iga_version = IGA_GEN_INVALID;

  if (tdesc != nullptr)
    {
      const tdesc_device *device_info = tdesc_device_info (tdesc);
      if (!(device_info->vendor_id.has_value ()
	    && device_info->target_id.has_value ()))
	{
	  warning (_("Device vendor id and target id not found."));
	  gdbarch_free (gdbarch);
	  return nullptr;
	}

      uint32_t vendor_id = *device_info->vendor_id;
      uint32_t device_id = *device_info->target_id;
      if (vendor_id != 0x8086)
	{
	  warning (_("Device not recognized: vendor id=0x%04x,"
		     " device id=0x%04x"), vendor_id, device_id);
	  gdbarch_free (gdbarch);
	  return nullptr;
	}
      else
	{
	  iga_version = (iga_gen_t) get_xe_version (device_id);
	  if (iga_version == IGA_GEN_INVALID)
	    warning (_("Intel GT device id is unrecognized: ID 0x%04x"),
		     device_id);
	}
    }

  /* Take the best guess in case IGA_VERSION is still invalid.  */
  if (iga_version == IGA_GEN_INVALID)
    iga_version = IGA_XE_HPC;

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
  set_gdbarch_can_leave_breakpoints (gdbarch, true);
  dwarf2_frame_set_init_reg (gdbarch, intelgt_init_reg);

  /* Disassembly.  */
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

  set_gdbarch_is_inferior_device (gdbarch, true);

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
  set_gdbarch_get_inferior_call_return_value (
    gdbarch, intelgt_get_inferior_call_return_value);

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
