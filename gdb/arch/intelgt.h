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

#ifndef ARCH_INTELGT_H
#define ARCH_INTELGT_H

#include "gdbsupport/tdesc.h"
#include <map>
#include <string>
#include <vector>

namespace intelgt {

/* Supported Intel(R) Graphics Technology versions.  */

enum class version : unsigned char
  {
    Gen9 = 9,
    Gen11 = 11,
    Gen12 = 12,
  };

/* Register information.  */

enum class reg_group : unsigned short
  {
    Address,
    Accumulator,
    Flag,
    ChannelEnable,
    StackPointer,
    State,
    Control,
    NotificationCount,
    InstructionPointer,
    ThreadDependency,
    Timestamp,
    FlowControl,
    Grf,
    ExecMaskPseudo,
    Mme,
    Debug,
  };

struct gt_register
{
  /* The name of the register.  */
  std::string name;

  /* The group that the register belongs to.  */
  reg_group group;

  /* The index of the register within its group.  */
  unsigned short local_index;

  /* The size of the register in terms of bytes.  */
  unsigned short size_in_bytes;

  explicit gt_register (std::string name, reg_group group,
			unsigned short local_index,
			unsigned short size_in_bytes);
};

enum breakpoint_kind
  {
    BP_INSTRUCTION = 1,
  };

enum
  {
    /* The maximal length of an IntelGT instruction in bytes.  */
    MAX_INST_LENGTH = 16
  };

/* Architectural information for an Intel(R) Graphics Technology
   version.  One instance per Gen version is created.  Instances can
   be accessed through the factory method 'get_or_create'.  */

class arch_info
{
public:
  arch_info (unsigned int num_grfs, unsigned int num_addresses,
	     unsigned int num_accumulators, unsigned int num_flags,
	     unsigned int num_mmes, unsigned int num_debug);

  /* Return the total number of registers.  */
  unsigned int num_registers ();

  /* The number of GRF registers.  */
  unsigned int grf_reg_count () const;

  /* The number of address registers.  */
  unsigned int address_reg_count () const;

  /* The number of accumulator registers.  */
  unsigned int acc_reg_count () const;

  /* The number of flag registers.  */
  unsigned int flag_reg_count () const;

  /* The number of mme registers.  */
  unsigned int mme_reg_count () const;

  /* The number of the virtual debug registers.  */
  unsigned int debug_reg_count () const;

  /* The base index of address registers.  */
  virtual unsigned int address_reg_base () const = 0;

  /* The base index of accumulator registers.  */
  virtual unsigned int acc_reg_base () const = 0;

  /* The base index of flag registers.  */
  virtual unsigned int flag_reg_base () const = 0;

  /* The base index of mme registers.  */
  virtual unsigned int mme_reg_base () const = 0;

  /* The base index of virtual debug registers.  */
  virtual unsigned int debug_reg_base () const = 0;

  /* Return the register at INDEX.  */
  const gt_register &get_register (int index);

  /* Return the name of the register at INDEX.  */
  const char *get_register_name (int index);

  /* The length of an instruction in bytes.  */
  virtual unsigned int inst_length_compacted () const = 0;

  virtual unsigned int inst_length_full () const = 0;

  /* The length of INST in bytes.  */
  virtual unsigned int inst_length (const gdb_byte inst[]) const = 0;

  /* The maximum size of a register.  */
  virtual unsigned int max_reg_size () = 0;

  /* Return true if the given INST is compacted; false otherwise.  */
  virtual bool is_compacted_inst (const gdb_byte inst[]) const = 0;

  /* The index of the PC register.  */
  virtual int pc_regnum () const = 0;

  /* The index of the SP register.  */
  virtual int sp_regnum () const = 0;

  /* The index of the 'emask' register.  */
  virtual int emask_regnum () const = 0;

  /* Set the breakpoint bit in INST.

     Returns the state of the bit prior to setting it:

       false: clear
       true : set.  */
  virtual bool set_breakpoint (gdb_byte inst[]) const = 0;

  /* Clear the breakpoint bit in INST.

     Returns the state of the bit prior to clearing it:

       false: clear
       true : set.  */
  virtual bool clear_breakpoint (gdb_byte inst[]) const = 0;

  /* Get the state of the breakpoint bit in INST.

       false: clear
       true : set.  */
  virtual bool has_breakpoint (const gdb_byte inst[]) const = 0;

  /* Factory method to ensure one instance per version.  */
  static arch_info *get_or_create (version vers);

protected:

  /* The collection of registers (GRF + ARF).  */
  std::vector<gt_register> regs;

  /* The offset in bits of the breakpoint bit in INST.  */
  virtual int breakpoint_bit_offset (const gdb_byte inst[]) const = 0;

private:

  /* The arch info instances created per version.  */
  static std::map<version, arch_info *> infos;

  /* Number of registers of various categories.  */
  const unsigned int num_grfs;

  const unsigned int num_addresses;

  const unsigned int num_accumulators;

  const unsigned int num_flags;

  const unsigned int num_mmes;

  const unsigned int num_debug;
};

} /* namespace intelgt */

#endif
