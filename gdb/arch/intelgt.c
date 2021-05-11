/* Copyright (C) 2019-2021 Free Software Foundation, Inc.

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

#include "gdbsupport/common-defs.h"
#include "intelgt.h"
#include <stdlib.h>

namespace intelgt {

gt_register::gt_register (std::string name, reg_group group,
			  unsigned short local_index,
			  unsigned short size_in_bytes)
  : name {std::move (name)}, group {group}, local_index {local_index},
    size_in_bytes {size_in_bytes}
{
  /* empty.  */
}

/* See intelgt.h.  */
arch_info::arch_info (unsigned int num_grfs, unsigned int num_addresses,
		      unsigned int num_accumulators, unsigned int num_flags,
		      unsigned int num_mmes, unsigned int num_debug)
  : num_grfs {num_grfs}, num_addresses {num_addresses},
    num_accumulators {num_accumulators}, num_flags {num_flags},
    num_mmes {num_mmes}, num_debug {num_debug}
{}

/* See intelgt.h.  */

unsigned int
arch_info::num_registers ()
{
  return regs.size ();
}

/* See intelgt.h.  */

unsigned int
arch_info::grf_reg_count () const
{
  return num_grfs;
}

/* See intelgt.h.  */

unsigned int
arch_info::address_reg_count () const
{
  return num_addresses;
}

/* See intelgt.h.  */

unsigned int
arch_info::acc_reg_count () const
{
  return num_accumulators;
}

/* See intelgt.h.  */

unsigned int
arch_info::flag_reg_count () const
{
  return num_flags;
}

/* See intelgt.h.  */

unsigned int
arch_info::mme_reg_count () const
{
  return num_mmes;
}

/* See intelgt.h.  */

unsigned int
arch_info::debug_reg_count () const
{
  return num_debug;
}

/* See intelgt.h.  */

const gt_register &
arch_info::get_register (int index)
{
  return regs[index];
}

/* See intelgt.h.  */

const char *
arch_info::get_register_name (int index)
{
  return regs[index].name.c_str ();
}

/* Architectural info for Gen 9.  */

class arch_info_gen9 : public arch_info
{
public:

  arch_info_gen9 ();

  virtual unsigned int inst_length_compacted () const override;

  virtual unsigned int inst_length_full () const override;

  virtual unsigned int inst_length (const gdb_byte inst[]) const override;

  virtual unsigned int max_reg_size () override;

  virtual bool is_compacted_inst (const gdb_byte inst[]) const override;

  virtual int pc_regnum () const override;

  virtual int sp_regnum () const override;

  virtual int emask_regnum () const override;

  virtual int retval_regnum () const override;

  virtual unsigned int address_reg_base () const override;

  virtual unsigned int acc_reg_base () const override;

  virtual unsigned int flag_reg_base () const override;

  virtual unsigned int mme_reg_base () const override;

  virtual unsigned int debug_reg_base () const override;

  virtual bool set_breakpoint (gdb_byte inst[]) const override;

  virtual bool clear_breakpoint (gdb_byte inst[]) const override;

  virtual bool has_breakpoint (const gdb_byte inst[]) const override;

  virtual int breakpoint_bit_offset (const gdb_byte inst[]) const override;
};

arch_info_gen9::arch_info_gen9 ()
  : arch_info (128, 1, 10, 2, 8, 11)
{
  gdb_assert (regs.size () == 0);

  /* Add GRF registers.  */
  std::string r = "r";
  for (int i = 0; i < grf_reg_count (); i++)
    regs.emplace_back (r + std::to_string (i), reg_group::Grf, i, 32);

  /* Add virtual debug registers.  */
  regs.emplace_back ("emask", reg_group::Debug, 0, 4);
  regs.emplace_back ("iemask", reg_group::Debug, 1, 4);
  regs.emplace_back ("btbase", reg_group::Debug, 2, 8);
  regs.emplace_back ("scrbase", reg_group::Debug, 3, 8);
  regs.emplace_back ("genstbase", reg_group::Debug, 4, 8);
  regs.emplace_back ("sustbase", reg_group::Debug, 5, 8);
  regs.emplace_back ("blsustbase", reg_group::Debug, 6, 8);
  regs.emplace_back ("blsastbase", reg_group::Debug, 7, 8);
  regs.emplace_back ("isabase", reg_group::Debug, 8, 8);
  regs.emplace_back ("iobase", reg_group::Debug, 9, 8);
  regs.emplace_back ("dynbase", reg_group::Debug, 10, 8);

  /* Add ARF registers.  Entries here must be listed in the exact
     same order as the features file.  */
  regs.emplace_back ("a0", reg_group::Address, 0, 32);
  regs.emplace_back ("acc0", reg_group::Accumulator, 0, 32);
  regs.emplace_back ("acc1", reg_group::Accumulator, 1, 32);
  regs.emplace_back ("acc2", reg_group::Accumulator, 2, 32);
  regs.emplace_back ("acc3", reg_group::Accumulator, 3, 32);
  regs.emplace_back ("acc4", reg_group::Accumulator, 4, 32);
  regs.emplace_back ("acc5", reg_group::Accumulator, 5, 32);
  regs.emplace_back ("acc6", reg_group::Accumulator, 6, 32);
  regs.emplace_back ("acc7", reg_group::Accumulator, 7, 32);
  regs.emplace_back ("acc8", reg_group::Accumulator, 8, 32);
  regs.emplace_back ("acc9", reg_group::Accumulator, 9, 32);
  regs.emplace_back ("f0", reg_group::Flag, 0, 4);
  regs.emplace_back ("f1", reg_group::Flag, 1, 4);
  regs.emplace_back ("ce", reg_group::ChannelEnable, 0, 4);
  regs.emplace_back ("sp", reg_group::StackPointer, 0, 16);
  regs.emplace_back ("sr0", reg_group::State, 0, 16);
  regs.emplace_back ("cr0", reg_group::Control, 0, 16);
  regs.emplace_back ("ip", reg_group::InstructionPointer, 0, 4);
  regs.emplace_back ("tdr", reg_group::ThreadDependency, 0, 16);
  regs.emplace_back ("tm0", reg_group::Timestamp, 0, 16);
  regs.emplace_back ("mme0", reg_group::Mme, 0, 32);
  regs.emplace_back ("mme1", reg_group::Mme, 1, 32);
  regs.emplace_back ("mme2", reg_group::Mme, 2, 32);
  regs.emplace_back ("mme3", reg_group::Mme, 3, 32);
  regs.emplace_back ("mme4", reg_group::Mme, 4, 32);
  regs.emplace_back ("mme5", reg_group::Mme, 5, 32);
  regs.emplace_back ("mme6", reg_group::Mme, 6, 32);
  regs.emplace_back ("mme7", reg_group::Mme, 7, 32);
};

unsigned int
arch_info_gen9::inst_length_compacted () const
{
  return 8;
}

unsigned int
arch_info_gen9::inst_length_full () const
{
  return 16;
}

unsigned int
arch_info_gen9::inst_length (const gdb_byte inst[]) const
{
  return (is_compacted_inst (inst)
	  ? inst_length_compacted ()
	  : inst_length_full ());
}

unsigned int
arch_info_gen9::max_reg_size ()
{
  return 256 / 8;
}

bool
arch_info_gen9::is_compacted_inst (const gdb_byte inst[]) const
{
  /* Check the CmptCtrl flag (bit 29).  */
  return inst[3] & 0x20;
}

int
arch_info_gen9::pc_regnum () const
{
  return address_reg_base () + 17;
}

int
arch_info_gen9::sp_regnum () const
{
  return address_reg_base () + 14;
}

int
arch_info_gen9::emask_regnum () const
{
  return debug_reg_base ();
}

int
arch_info_gen9::retval_regnum () const
{
  /* GRF r26.  */
  return 26;
}

unsigned int
arch_info_gen9::address_reg_base () const
{
  return debug_reg_base () + debug_reg_count ();
}

unsigned int
arch_info_gen9::acc_reg_base () const
{
  return address_reg_base () + address_reg_count ();
}

unsigned int
arch_info_gen9::flag_reg_base () const
{
  return acc_reg_base () + acc_reg_count ();
}

unsigned int
arch_info_gen9::mme_reg_base () const
{
  return pc_regnum () + 3;
}

unsigned int
arch_info_gen9::debug_reg_base () const
{
  return grf_reg_count ();
}


/* Get the bit at POS in INST.  */

static bool
get_inst_bit (const gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  return (byte & mask) != 0;
}

/* Set the bit at POS in INST.  */

static bool
set_inst_bit (gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  const bool old = (byte & mask) != 0;
  inst[idx] |= mask;

  return old;
}

/* Clear the bit at POS in INST.  */

static bool
clear_inst_bit (gdb_byte inst[], int pos)
{
  if (pos < 0 || (MAX_INST_LENGTH * 8) <= pos)
    internal_error (__FILE__, __LINE__, _("bad bit offset: %d"), pos);

  const int idx = pos >> 3;
  const int off = pos & 7;
  const int mask = 1 << off;
  const gdb_byte byte = inst[idx];

  const bool old = (byte & mask) != 0;
  inst[idx] &= ~mask;

  return old;
}

bool
arch_info_gen9::set_breakpoint (gdb_byte inst[]) const
{
  return set_inst_bit (inst, breakpoint_bit_offset (inst));
}

bool
arch_info_gen9::clear_breakpoint (gdb_byte inst[]) const
{
  return clear_inst_bit (inst, breakpoint_bit_offset (inst));
}

bool
arch_info_gen9::has_breakpoint (const gdb_byte inst[]) const
{
  return get_inst_bit (inst, breakpoint_bit_offset (inst));
}

int
arch_info_gen9::breakpoint_bit_offset (const gdb_byte inst[]) const
{
  return (is_compacted_inst (inst) ? 7 : 30);
}

/* Architectural info for Gen 11.
   It is the same as Gen 9.  */

using arch_info_gen11 = arch_info_gen9;

/* Architectural info for Gen 12.
   It is the same as Gen 11.  */

using arch_info_gen12 = arch_info_gen11;

/* Static members of intelgt_arch_info.  */
std::map<version, arch_info *> arch_info::infos;

arch_info *
arch_info::get_or_create (version vers)
{
  if (infos.find (vers) == infos.end ())
    {
      switch (vers)
	{
	case version::Gen9:
	  infos[vers] = new arch_info_gen9 ();
	  break;
	case version::Gen11:
	  infos[vers] = new arch_info_gen11 ();
	  break;
	case version::Gen12:
	  infos[vers] = new arch_info_gen12 ();
	  break;
	}
    }

  return infos[vers];
}

} /* namespace intelgt */
