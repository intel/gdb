/* Copyright (C) 2019-2023 Free Software Foundation, Inc.

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
#include <string>
#include <vector>

namespace intelgt {

/* Various arch constants.  */

enum breakpoint_kind
  {
    BP_INSTRUCTION = 1,
  };

/* The length of a full and compact IntelGT instruction in bytes.  */

constexpr int MAX_INST_LENGTH = 16;
constexpr int COMPACT_INST_LENGTH = 8;

/* Feature names.

   They correspond to register sets defined in zet_intel_gpu_debug.h.  We
   declare feature names in the order used in that header.

   The SBA register set consists of a set of base registers in the order
   defined in that header file.

   Not all registers have DWARF numbers.  See DWARF_REGSETS below for a
   list of features that do.  */
constexpr const char* feature_grf = "org.gnu.gdb.intelgt.grf";
constexpr const char* feature_addr = "org.gnu.gdb.intelgt.addr";
constexpr const char* feature_flag = "org.gnu.gdb.intelgt.flag";
constexpr const char *feature_ce = "org.gnu.gdb.intelgt.ce";
constexpr const char *feature_sr = "org.gnu.gdb.intelgt.sr";
constexpr const char *feature_cr = "org.gnu.gdb.intelgt.cr";
constexpr const char *feature_tdr = "org.gnu.gdb.intelgt.tdr";
constexpr const char* feature_acc = "org.gnu.gdb.intelgt.acc";
constexpr const char* feature_mme = "org.gnu.gdb.intelgt.mme";
constexpr const char *feature_sp = "org.gnu.gdb.intelgt.sp";
constexpr const char* feature_sba = "org.gnu.gdb.intelgt.sba";
constexpr const char *feature_dbg = "org.gnu.gdb.intelgt.dbg";
constexpr const char *feature_fc = "org.gnu.gdb.intelgt.fc";

/* Register sets/groups needed for DWARF mapping.  Used for
   declaring static arrays for various mapping tables.  */

enum dwarf_regsets : int
{
  regset_sba = 0,
  regset_grf,
  regset_addr,
  regset_flag,
  regset_acc,
  regset_mme,
  regset_count
};

/* Map of dwarf_regset values to the target description
   feature names.  */

constexpr const char *dwarf_regset_features[regset_count] = {
  feature_sba,
  feature_grf,
  feature_addr,
  feature_flag,
  feature_acc,
  feature_mme
};

/* Get the bit at POS in INST.  */

bool get_inst_bit (const gdb_byte inst[], int pos);

/* Set the bit at POS in INST.  */

bool set_inst_bit (gdb_byte inst[], int pos);

/* Clear the bit at POS in INST.  */

bool clear_inst_bit (gdb_byte inst[], int pos);

static inline bool
is_compacted_inst (const gdb_byte inst[])
{
  /* Check the CmptCtrl flag (bit 29).  */
  return inst[3] & 0x20;
}

static inline int
breakpoint_bit_offset (const gdb_byte inst[])
{
  return (is_compacted_inst (inst) ? 7 : 30);
}

static inline bool
set_breakpoint (gdb_byte inst[])
{
  return set_inst_bit (inst, breakpoint_bit_offset (inst));
}

static inline bool
clear_breakpoint (gdb_byte inst[])
{
  return clear_inst_bit (inst, breakpoint_bit_offset (inst));
}

static inline bool
has_breakpoint (const gdb_byte inst[])
{
  return get_inst_bit (inst, breakpoint_bit_offset (inst));
}

static inline unsigned int
inst_length_compacted ()
{
  return COMPACT_INST_LENGTH;
}

static inline unsigned int
inst_length_full ()
{
  return MAX_INST_LENGTH;
}

static inline unsigned int
inst_length (const gdb_byte inst[])
{
  return (is_compacted_inst (inst)
	  ? inst_length_compacted ()
	  : inst_length_full ());
}

} /* namespace intelgt */

#endif
