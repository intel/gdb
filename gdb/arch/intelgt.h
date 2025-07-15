/* Copyright (C) 2019-2025 Free Software Foundation, Inc.
   Copyright (C) 2019-2025 Intel Corporation

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
constexpr const char *feature_mf = "org.gnu.gdb.intelgt.mf";
constexpr const char *feature_debugger = "org.gnu.gdb.intelgt.debugger";
constexpr const char *feature_scratch = "org.gnu.gdb.intelgt.scratch";
constexpr const char *feature_scalar = "org.gnu.gdb.intelgt.scalar";

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

/* The encoding for XE version enumerates follows this pattern, which is
   aligned with the IGA encoding.  */

#define XE_VERSION(MAJ, MIN) (((MAJ) << 24) | (MIN))

/* Supported GDB XE platforms.  */

enum xe_version
{
  XE_INVALID = 0,
  XE_HP = XE_VERSION (1, 1),
  XE_HPG = XE_VERSION (1, 2),
  XE_HPC = XE_VERSION (1, 4),
  XE2 = XE_VERSION (2, 0),
  XE3 = XE_VERSION (3, 0),
};

/* Helper function to translate the device id to a device version.  */

extern xe_version get_xe_version (uint32_t device_id);

/* Instruction details.  */

enum
{
  /* The opcode mask for bits 6:0.  */
  opc_mask = 0x7f,

  /* Branch instruction opcodes.  */
  opc_branchd = 0x21,
  opc_branchc = 0x23,
  opc_goto = 0x2e,

  /* Send instruction opcodes.  */
  opc_send = 0x31,
  opc_sendc = 0x32,

  /* DPAS instruction opcodes.  */
  opc_dpas = 0x59,
  opc_dpasw = 0x5a,
};

/* Selected instruction control bit positions.  */

enum
{
  /* AtomicCtrl bit for non-compact instructions.  */
  ctrl_atomic = 32,

  /* FwdCtrl bit for non-compact instructions.  Only used for atomic
     instructions.  */
  ctrl_fwd = 33,

  /* The End Of Thread control.  Only used for SEND and SENDC.  */
  ctrl_eot_send = 34,
};

/* Get the bit at POS in INST.  */

bool get_inst_bit (const gdb_byte inst[], int pos);

/* Set the bit at POS in INST.  */

bool set_inst_bit (gdb_byte inst[], int pos);

/* Clear the bit at POS in INST.  */

bool clear_inst_bit (gdb_byte inst[], int pos);

static inline int
breakpoint_bit_offset (const gdb_byte inst[], uint32_t device_id)
{
  xe_version device_version = get_xe_version (device_id);
  switch (device_version)
    {
    case intelgt::XE_HP:
    case intelgt::XE_HPG:
    case intelgt::XE_HPC:
    case intelgt::XE2:
    case intelgt::XE3:
      /* Check the CmptCtrl flag (bit 29).  */
      return (((inst[3] & 0x20) != 0) ? 7 : 30);

    case intelgt::XE_INVALID:
      break;
    }
  error (_("Unsupported device id 0x%" PRIx32), device_id);
}

static inline bool
set_breakpoint (gdb_byte inst[], uint32_t device_id)
{
  return set_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
}

static inline bool
clear_breakpoint (gdb_byte inst[], uint32_t device_id)
{
  return clear_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
}

static inline bool
has_breakpoint (const gdb_byte inst[], uint32_t device_id)
{
  return get_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
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
inst_length (const gdb_byte inst[], uint32_t device_id)
{
  xe_version device_version = get_xe_version (device_id);
  switch (device_version)
    {
    case intelgt::XE_HP:
    case intelgt::XE_HPG:
    case intelgt::XE_HPC:
    case intelgt::XE2:
    case intelgt::XE3:
      /* Check the CmptCtrl flag (bit 29).  */
      return (((inst[3] & 0x20) != 0)
	      ? inst_length_compacted ()
	      : inst_length_full ());

    case intelgt::XE_INVALID:
      break;
    }
  error (_("Unsupported device id 0x%" PRIx32), device_id);
}

} /* namespace intelgt */

#endif
