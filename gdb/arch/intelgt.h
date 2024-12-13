/* Copyright (C) 2019-2026 Free Software Foundation, Inc.

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

#ifndef GDB_ARCH_INTELGT_H
#define GDB_ARCH_INTELGT_H

namespace intelgt {

/* Various arch constants.  */

enum breakpoint_kind : int
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
constexpr const char *FEATURE_GRF = "org.gnu.gdb.intelgt.grf";
constexpr const char *FEATURE_ADDR = "org.gnu.gdb.intelgt.addr";
constexpr const char *FEATURE_FLAG = "org.gnu.gdb.intelgt.flag";
constexpr const char *FEATURE_CE = "org.gnu.gdb.intelgt.ce";
constexpr const char *FEATURE_SR = "org.gnu.gdb.intelgt.sr";
constexpr const char *FEATURE_CR = "org.gnu.gdb.intelgt.cr";
constexpr const char *FEATURE_TDR = "org.gnu.gdb.intelgt.tdr";
constexpr const char *FEATURE_ACC = "org.gnu.gdb.intelgt.acc";
constexpr const char *FEATURE_MME = "org.gnu.gdb.intelgt.mme";
constexpr const char *FEATURE_SP = "org.gnu.gdb.intelgt.sp";
constexpr const char *FEATURE_SBA = "org.gnu.gdb.intelgt.sba";
constexpr const char *FEATURE_DBG = "org.gnu.gdb.intelgt.dbg";
constexpr const char *FEATURE_FC = "org.gnu.gdb.intelgt.fc";
constexpr const char *FEATURE_DEBUGGER = "org.gnu.gdb.intelgt.debugger";

/* Register sets/groups needed for DWARF mapping.  Used for
   declaring static arrays for various mapping tables.  */

enum dwarf_regsets : int
{
  REGSET_SBA = 0,
  REGSET_GRF,
  REGSET_ADDR,
  REGSET_FLAG,
  REGSET_ACC,
  REGSET_MME,
  REGSET_COUNT
};

/* Map of dwarf_regset values to the target description
   feature names.  */

constexpr const char *DWARF_REGSET_FEATURES[REGSET_COUNT] = {
  FEATURE_SBA,
  FEATURE_GRF,
  FEATURE_ADDR,
  FEATURE_FLAG,
  FEATURE_ACC,
  FEATURE_MME
};

/* The encoding for XE version enumerators follows this pattern, which is
   aligned with the Intel Graphics (dis)Assembler (IGA) encoding.  */

constexpr int XE_VERSION (int major, int minor)
{
  return ((major << 24) | minor);
}

/* Supported GDB XE platforms.  */

enum class xe_version
{
  INVALID = 0,
  XE_HP = XE_VERSION (1, 1),
  XE_HPG = XE_VERSION (1, 2),
  XE_HPC = XE_VERSION (1, 4),
  XE2 = XE_VERSION (2, 0),
  XE3 = XE_VERSION (3, 0),
};

/* Helper function to translate the device id to a device version.  */

xe_version get_xe_version (uint32_t device_id);

/* Get the bit at POS in INST.  */

bool get_inst_bit (gdb::array_view<const gdb_byte> inst, int pos);

/* Set the bit at POS in INST.  */

bool set_inst_bit (gdb::array_view<gdb_byte> inst, int pos);

/* Clear the bit at POS in INST.  */

bool clear_inst_bit (gdb::array_view<gdb_byte> inst, int pos);

static inline int
breakpoint_bit_offset (gdb::array_view<const gdb_byte> inst,
		       uint32_t device_id)
{
  xe_version device_version = get_xe_version (device_id);
  switch (device_version)
    {
    case intelgt::xe_version::XE_HP:
    case intelgt::xe_version::XE_HPG:
    case intelgt::xe_version::XE_HPC:
    case intelgt::xe_version::XE2:
    case intelgt::xe_version::XE3:
      /* Check the CmptCtrl flag (bit 29).  */
      return (((inst[3] & 0x20) != 0) ? 7 : 30);

    case intelgt::xe_version::INVALID:
      break;
    }
  error (_("Unsupported device id 0x%" PRIx32), device_id);
}

static inline bool
set_breakpoint (gdb::array_view<gdb_byte> inst, uint32_t device_id)
{
  return set_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
}

static inline bool
clear_breakpoint (gdb::array_view<gdb_byte> inst, uint32_t device_id)
{
  return clear_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
}

static inline bool
has_breakpoint (gdb::array_view<const gdb_byte> inst, uint32_t device_id)
{
  return get_inst_bit (inst, breakpoint_bit_offset (inst, device_id));
}

static inline unsigned int
inst_length (gdb::array_view<const gdb_byte> inst, uint32_t device_id)
{
  xe_version device_version = get_xe_version (device_id);
  switch (device_version)
    {
    case intelgt::xe_version::XE_HP:
    case intelgt::xe_version::XE_HPG:
    case intelgt::xe_version::XE_HPC:
    case intelgt::xe_version::XE2:
    case intelgt::xe_version::XE3:
      /* Check the CmptCtrl flag (bit 29).  */
      return (((inst[3] & 0x20) != 0)
	      ? COMPACT_INST_LENGTH
	      : MAX_INST_LENGTH);

    case intelgt::xe_version::INVALID:
      break;
    }
  error (_("Unsupported device id 0x%" PRIx32), device_id);
}

} /* namespace intelgt */

#endif /* GDB_ARCH_INTELGT_H */
