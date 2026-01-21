/* Target interface for Intel GT based on Level-Zero for gdbserver.

   Copyright (C) 2020-2026 Free Software Foundation, Inc.

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

#include "ze-low.h"
#include "arch/intelgt.h"
#include "gdbsupport/osabi.h"

#include <level_zero/zet_intel_gpu_debug.h>
#include <iomanip>
#include <sstream>
#include <string>


/* FIXME make into a target method?  */
int using_threads = 1;

/* Convenience macros.  */

#define dprintf(fmt, ...)					    \
  debug_prefixed_printf_cond (debug_threads, "intelgt-ze-low", fmt, \
			      ##__VA_ARGS__)


/* Determine the most suitable type to be used for a register with bit size
   BITSIZE and element size ELEMSIZE.  */

static const char *
intelgt_uint_reg_type (tdesc_feature *feature, uint32_t bitsize,
		       uint32_t elemsize)
{
  if (0 != (bitsize % elemsize))
    error (_("unsupported combination of bitsize %" PRIu32 " and elemsize %"
	     PRIu32), bitsize, elemsize);
  if ((elemsize < 8) || (elemsize > 128) || ((elemsize & (elemsize - 1)) != 0))
    error (_("unsupported elemsize %" PRIu32), elemsize);

  std::string type_name = "uint" + std::to_string (elemsize);
  tdesc_type *type = tdesc_named_type (feature, type_name.c_str ());

  if (elemsize == bitsize)
    return type->name.c_str ();

  uint32_t elements = bitsize / elemsize;
  std::string vector_name = "vector" + std::to_string (elements) + "x"
    + std::to_string (elemsize);
  tdesc_type *vector
    = tdesc_create_vector (feature, vector_name.c_str (), type, elements);

  return vector->name.c_str ();
}

/* Add a (uniform) register set to FEATURE.  */

static void
intelgt_add_regset (tdesc_feature *feature, int &regnum, const char *prefix,
		    uint32_t count, const char *group, uint32_t bitsize,
		    const char *type, expedite_t &expedite)
{
  for (uint32_t reg = 0; reg < count; ++reg)
    {
      std::string name = std::string (prefix) + std::to_string (reg);

      tdesc_create_reg (feature, name.c_str (), regnum++, 1, group,
			bitsize, type);
    }
}

/* Control Register details.  */

enum
{
    /* The position of the Breakpoint Suppress bit in CR0.0.  */
    INTELGT_CR0_0_BREAKPOINT_SUPPRESS = 15,

    /* The position of the Breakpoint Status and Control bit in CR0.1.  */
    INTELGT_CR0_1_BREAKPOINT_STATUS = 31,

    /* The position of the External Halt Status and Control bit in CR0.1.  */
    INTELGT_CR0_1_EXTERNAL_HALT_STATUS = 30,

    /* The position of the Software Exception Control bit in CR0.1.  */
    INTELGT_CR0_1_SOFTWARE_EXCEPTION_CONTROL = 29,

    /* The position of the Illegal Opcode Exception Status bit in CR0.1.  */
    INTELGT_CR0_1_ILLEGAL_OPCODE_STATUS = 28,

    /* The position of the Force Exception Status and Control bit in CR0.1.  */
    INTELGT_CR0_1_FORCE_EXCEPTION_STATUS = 26,

    /* The position of the Page Fault Status bit in CR0.1.
       This is a software convention using a reserved bit to indicate
       page faults by the user mode driver.  */
    INTELGT_CR0_1_PAGEFAULT_STATUS = 16,
};

/* Find a regset by type.  */
static ze_regset_info
ze_regset_by_type (const ze_device_info &device, uint32_t type)
{
  for (const ze_regset_info &regset : device.regsets)
    {
      if (regset.type == type)
	return regset;
    }

  internal_error ("Cannot find regset of type %" PRIu32
		  " on device %lu (%s).", type, device.ordinal,
		  device.properties.name);
}

/* Return CR0 in REGCACHE.  */

static std::vector<uint32_t>
intelgt_read_cr0 (regcache *regcache)
{
  int cr0regno = find_regno (regcache->tdesc, "cr0");
  int cr0size = register_size (regcache->tdesc, cr0regno);

  gdb_assert ((cr0size % sizeof (uint32_t)) == 0);
  std::vector<uint32_t> cr0 (cr0size / sizeof (uint32_t));

  collect_register (regcache, cr0regno, cr0.data ());

  enum register_status cr0status = regcache->get_register_status (cr0regno);
  switch (cr0status)
    {
    case REG_VALID:
      return cr0;

    case REG_UNKNOWN:
      internal_error (_("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (_("unknown register status: %d."), cr0status);
}

/* Write VALUE into CR0 in REGCACHE.  */

static void
intelgt_write_cr0 (regcache *regcache, std::vector<uint32_t> value)
{
  int cr0regno = find_regno (regcache->tdesc, "cr0");
  int cr0size = register_size (regcache->tdesc, cr0regno);
  gdb_assert (cr0size == (value.size () * sizeof (uint32_t)));

  enum register_status cr0status = regcache->get_register_status (cr0regno);
  switch (cr0status)
    {
    case REG_VALID:
      supply_register (regcache, cr0regno, value.data ());
      return;

    case REG_UNKNOWN:
      internal_error (_("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (_("unknown register status: %d."), cr0status);
}

/* Return CR0 for TP.  */

static std::vector<uint32_t>
intelgt_read_cr0 (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ true);
  return intelgt_read_cr0 (regcache);
}

/* Return CR0 for TID on DEVICE.  */

static std::vector<uint32_t>
intelgt_read_cr0 (const ze_device_info &device,
		  const ze_device_thread_t &tid)
{
  thread_info *tp = ze_find_thread (device, tid);
  if (tp != nullptr)
    return intelgt_read_cr0 (tp);

  /* If we do not have a thread for TID, yet, read the register directly.

     We use this in get_stop_reason () and, depending on what we read and
     if we're able to read registers at all, we may create a thread.  */
  uint32_t type = ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU;
  ze_regset_info regset = ze_regset_by_type (device, type);

  gdb_assert ((regset.size % sizeof (uint32_t)) == 0);
  std::vector<uint32_t> cr0 (regset.size / sizeof (uint32_t));

  ze_result_t status
    = zetDebugReadRegisters (device.session, tid, type, 0, 1, cr0.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      return cr0;

    default:
      error (_("Error %x reading CR0 for %s on device %lu (%s)."), status,
	     ze_thread_id_str (tid).c_str (), device.ordinal,
	     device.properties.name);
    }
}

/* Write VALUE into CR0 for TP.  */

static void
intelgt_write_cr0 (thread_info *tp, std::vector<uint32_t> value)
{
  struct regcache *regcache = get_thread_regcache (tp, /* fetch = */ true);
  intelgt_write_cr0 (regcache, value);
}

/* Write VALUE into CR0 for TID on DEVICE.  */

static void
intelgt_write_cr0 (const ze_device_info &device,
		   const ze_device_thread_t &tid,
		   std::vector<uint32_t> value)
{
  thread_info *tp = ze_find_thread (device, tid);
  if (tp != nullptr)
    {
      intelgt_write_cr0 (tp, value);
      return;
    }

  /* If we do not have a thread for TID, yet, write the register directly.

     We use this in get_stop_reason () and, depending on what we read, we
     may create a thread later.  Or, we may want to clear the exception
     status and not create a thread, at all.  */
  uint32_t type = ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU;
  ze_regset_info regset = ze_regset_by_type (device, type);
  gdb_assert (regset.size == (value.size () * sizeof (uint32_t)));

  ze_result_t status
    = zetDebugWriteRegisters (device.session, tid, type, 0, 1,
			      value.data ());
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      return;

    default:
      error (_("Error %x writing CR0 for %s on device %lu (%s)."), status,
	     ze_thread_id_str (tid).c_str (), device.ordinal,
	     device.properties.name);
    }
}

/* Return a human-readable device UUID string.  */

static std::string
device_uuid_str (const uint8_t uuid[], size_t size)
{
  std::stringstream sstream;
  for (int i = size - 1; i >= 0; --i)
    sstream << std::hex << std::setfill ('0') << std::setw (2)
	    << static_cast<int> (uuid[i]);

  return sstream.str ();
}

/* Target op definitions for Intel GT target based on Level-Zero.  */

class intelgt_ze_target : public ze_target
{
public:
  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size) override;

  bool supports_stopped_by_sw_breakpoint () override { return true; }
  bool stopped_by_sw_breakpoint () override;

  CORE_ADDR read_pc (regcache *regcache) override;
  void write_pc (regcache *regcache, CORE_ADDR pc) override;

protected:
  bool is_device_supported
    (const ze_device_properties_t &,
     const std::vector<zet_debug_regset_properties_t> &) override;

  target_desc *create_tdesc
    (ze_device_info *dinfo,
     const std::vector<zet_debug_regset_properties_t> &,
     const ze_pci_ext_properties_t &) override;

  target_stop_reason get_stop_reason (const ze_device_info &device,
				      const ze_device_thread_t &tid,
				      gdb_signal &) override;

  void prepare_thread_resume (thread_info *tp) override;

  /* Read one instruction from memory at PC into BUFFER and return the
     number of bytes read on success or a negative errno error code.

     BUFFER must be intelgt::MAX_INST_LENGTH bytes long.  */
  int read_inst (const ze_device_info &device, CORE_ADDR pc,
		 gdb::array_view<gdb_byte> buffer);

  bool is_at_breakpoint (thread_info *tp) override;

  /* Return whether TP is at an end-of-thread instruction.  */
  bool is_at_eot (thread_info *tp);

private:
  /* Add a register set for REGPROP on DEVICE to REGSETS and increment REGNUM
     accordingly.

     May optionally add registers to EXPEDITE.  */
  void add_regset (target_desc *tdesc, const ze_device_info &dinfo,
		   const zet_debug_regset_properties_t &regprop,
		   int &regnum, ze_regset_info_t &regsets,
		   expedite_t &expedite);
};

const gdb_byte *
intelgt_ze_target::sw_breakpoint_from_kind (int kind, int *size)
{
  /* We do not support breakpoint instructions.

     Use gdbarch methods that use read/write memory target operations for
     setting s/w breakopints.  */
  *size = 0;
  return nullptr;
}

bool
intelgt_ze_target::stopped_by_sw_breakpoint ()
{
  const ze_thread_info *zetp = ze_thread (current_thread);
  if (zetp == nullptr)
    return false;

  ptid_t ptid = current_thread->id;

  if (zetp->exec_state != ZE_THREAD_STATE_STOPPED)
    {
      dprintf ("not-stopped thread %s", ptid.to_string ().c_str ());
      return false;
    }

  return (zetp->stop_reason == TARGET_STOPPED_BY_SW_BREAKPOINT);
}

CORE_ADDR
intelgt_ze_target::read_pc (regcache *regcache)
{
  std::vector<uint32_t> cr0 = intelgt_read_cr0 (regcache);
  gdb_assert (cr0.size () >= 2);
  uint32_t ip = cr0[2];

  uint64_t isabase;
  collect_register_by_name (regcache, "isabase", &isabase);

  CORE_ADDR pc = (CORE_ADDR) isabase + (CORE_ADDR) ip;
  if (pc < isabase)
    warning (_("PC '%s' outside of ISA range."),
	     core_addr_to_string_nz (pc));

  return pc;
}

void
intelgt_ze_target::write_pc (regcache *regcache, CORE_ADDR pc)
{
  uint64_t isabase;
  collect_register_by_name (regcache, "isabase", &isabase);

  if (pc < isabase)
    error (_("PC '%s' outside of ISA range."), core_addr_to_string_nz (pc));

  pc -= isabase;
  if (UINT32_MAX < pc)
    error (_("PC '%s' outside of ISA range."), core_addr_to_string_nz (pc));

  std::vector<uint32_t> cr0 = intelgt_read_cr0 (regcache);
  gdb_assert (cr0.size () >= 2);
  cr0[2] = pc;
  intelgt_write_cr0 (regcache, cr0);
}

bool
intelgt_ze_target::is_device_supported
  (const ze_device_properties_t &properties,
   const std::vector<zet_debug_regset_properties_t> &regset_properties)
{
  if (properties.type != ZE_DEVICE_TYPE_GPU)
    {
      dprintf ("non-gpu (%x) device (%" PRIx32 "): %s", properties.type,
	       properties.deviceId, properties.name);
      return false;
    }

  if (properties.vendorId != 0x8086)
    {
      dprintf ("unknown vendor (%" PRIx32 ") of device (%" PRIx32 "): %s",
	       properties.vendorId, properties.deviceId, properties.name);
      return false;
    }

  /* We need a few registers to support an Intel GT device.

     Those are registers that GDB itself uses.  Without those, we might run into
     internal errors at some point.  We need others, too, that may be referenced
     in debug information.  */
  bool have_grf = false;
  bool have_isabase = false;
  bool have_cr = false;
  bool have_sr = false;
  bool have_ce = false;
  for (const zet_debug_regset_properties_t &regprop : regset_properties)
    {
      if (regprop.count < 1)
	{
	  warning (_("Ignoring empty regset %u on device %s."),
		   regprop.type, properties.name);
	  continue;
	}

      switch (regprop.type)
	{
	case ZET_DEBUG_REGSET_TYPE_GRF_INTEL_GPU:
	  have_grf = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_CE_INTEL_GPU:
	  have_ce = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU:
	  if (regprop.byteSize >= (3 * sizeof (uint32_t)))
	    have_cr = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_SR_INTEL_GPU:
	  have_sr = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_SBA_INTEL_GPU:
	  /* We need 'isabase', which is at position 5 in version 0.  */
	  if ((regprop.version == 0) && (regprop.count >= 5)
	      && (regprop.byteSize <= sizeof (uint64_t)))
	    have_isabase = true;
	  else
	    warning (_("Ignoring unknown SBA regset version %u on device "
		       "%s."), regprop.version, properties.name);
	  break;
	}
    }

  if (have_grf && have_isabase && have_cr && have_sr && have_ce)
    return true;

  dprintf ("unsupported device (%" PRIx32 "): %s", properties.deviceId,
	   properties.name);
  return false;
}

target_desc *
intelgt_ze_target::create_tdesc
  (ze_device_info *dinfo,
   const std::vector<zet_debug_regset_properties_t> &regset_properties,
   const ze_pci_ext_properties_t &pci_properties)
{
  const ze_device_properties_t &properties = dinfo->properties;

  if (properties.vendorId != 0x8086)
    error (_("unknown vendor (%" PRIx32 ") of device (%" PRIx32 "): %s"),
	   properties.vendorId, properties.deviceId, properties.name);

  target_desc_up tdesc = allocate_target_description ();
  set_tdesc_architecture (tdesc.get (), "intelgt");
  set_tdesc_osabi (tdesc.get (), GDB_OSABI_LINUX);

  std::string device_uuid = device_uuid_str (
    dinfo->properties.uuid.id, sizeof (dinfo->properties.uuid.id));
  const uint32_t total_cores = (properties.numSlices
				* properties.numSubslicesPerSlice
				* properties.numEUsPerSubslice);
  const uint32_t total_threads = (total_cores * properties.numThreadsPerEU);

  tdesc_device *device_info = new tdesc_device ();
  device_info->vendor_id = properties.vendorId;
  device_info->target_id = properties.deviceId;
  device_info->name = properties.name;
  device_info->pci_slot = string_printf ("%02" PRIx32 ":%02" PRIx32
					 ".%" PRId32,
					 pci_properties.address.bus,
					 pci_properties.address.device,
					 pci_properties.address.function);
  device_info->uuid = device_uuid;
  device_info->total_cores = total_cores;
  device_info->total_threads = total_threads;

  if (properties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE)
    device_info->subdevice_id = properties.subdeviceId;

  set_tdesc_device_info (tdesc.get (), device_info);

  int regnum = 0;
  for (const zet_debug_regset_properties_t &regprop : regset_properties)
    add_regset (tdesc.get (), *dinfo, regprop, regnum,
		dinfo->regsets, dinfo->expedite);

  /* Tdesc expects a nullptr-terminated array.  */
  dinfo->expedite.push_back (nullptr);

  init_target_desc (tdesc.get (), dinfo->expedite.data (), GDB_OSABI_LINUX);
  return tdesc.release ();
}

target_stop_reason
intelgt_ze_target::get_stop_reason (const ze_device_info &device,
				    const ze_device_thread_t &tid,
				    gdb_signal &signal)
{
  std::vector<uint32_t> cr0 = intelgt_read_cr0 (device, tid);
  gdb_assert (cr0.size () >= 2);

  dprintf ("thread %s stopped, cr0.0=%" PRIx32 ", .1=%" PRIx32
	   " [ %s%s%s%s%s%s], .2=%" PRIx32 ".",
	   ze_thread_id_str (tid).c_str (), cr0[0], cr0[1],
	   (((cr0[1] & (1 << INTELGT_CR0_1_BREAKPOINT_STATUS)) != 0)
	    ? "bp " : ""),
	   (((cr0[1] & (1 << INTELGT_CR0_1_ILLEGAL_OPCODE_STATUS)) != 0)
	    ? "ill " : ""),
	   (((cr0[1] & (1 << INTELGT_CR0_1_FORCE_EXCEPTION_STATUS)) != 0)
	    ? "fe " : ""),
	   (((cr0[1] & (1 << INTELGT_CR0_1_SOFTWARE_EXCEPTION_CONTROL)) != 0)
	    ? "sw " : ""),
	   (((cr0[1] & (1 << INTELGT_CR0_1_EXTERNAL_HALT_STATUS)) != 0)
	    ? "eh " : ""),
	   (((cr0[1] & (1 << INTELGT_CR0_1_PAGEFAULT_STATUS)) != 0)
	    ? "pf " : ""),
	   cr0[2]);

  /* We will overwrite this signal if we recognize an exception.  */
  signal = GDB_SIGNAL_UNKNOWN;

  if ((cr0[1] & (1 << INTELGT_CR0_1_PAGEFAULT_STATUS)) != 0)
    {
      cr0[1] &= ~(1 << INTELGT_CR0_1_PAGEFAULT_STATUS);
      intelgt_write_cr0 (device, tid, cr0);

      signal = GDB_SIGNAL_SEGV;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  if ((cr0[1] & (1 << INTELGT_CR0_1_BREAKPOINT_STATUS)) != 0)
    {
      cr0[1] &= ~(1 << INTELGT_CR0_1_BREAKPOINT_STATUS);
      intelgt_write_cr0 (device, tid, cr0);

      signal = GDB_SIGNAL_TRAP;

      /* We cannot distinguish a single step exception from a breakpoint
	 exception just by looking at CR0.

	 We could inspect the instruction to see if the breakpoint bit is
	 set.  Or we could check the resume type and assume that we set
	 things up correctly for single-stepping before we resumed.  */
      thread_info *tp = ze_find_thread (device, tid);
      if (tp != nullptr)
	{
	  const ze_thread_info *zetp = ze_thread (tp);
	  gdb_assert (zetp != nullptr);

	  if (zetp->resume_state == ZE_THREAD_RESUME_STEP)
	    return TARGET_STOPPED_BY_SINGLE_STEP;
	}

      return TARGET_STOPPED_BY_SW_BREAKPOINT;
    }

  if ((cr0[1] & (1 << INTELGT_CR0_1_ILLEGAL_OPCODE_STATUS)) != 0)
    {
      cr0[1] &= ~(1 << INTELGT_CR0_1_ILLEGAL_OPCODE_STATUS);
      intelgt_write_cr0 (device, tid, cr0);

      signal = GDB_SIGNAL_ILL;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  if ((cr0[1] & (1 << INTELGT_CR0_1_SOFTWARE_EXCEPTION_CONTROL)) != 0)
    {
      cr0[1] &= ~(1 << INTELGT_CR0_1_SOFTWARE_EXCEPTION_CONTROL);
      intelgt_write_cr0 (device, tid, cr0);

      signal = GDB_EXC_SOFTWARE;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  if ((cr0[1] & ((1 << INTELGT_CR0_1_FORCE_EXCEPTION_STATUS)
		 | (1 << INTELGT_CR0_1_EXTERNAL_HALT_STATUS))) != 0)
    {
      cr0[1] &= ~(1 << INTELGT_CR0_1_FORCE_EXCEPTION_STATUS);
      cr0[1] &= ~(1 << INTELGT_CR0_1_EXTERNAL_HALT_STATUS);
      intelgt_write_cr0 (device, tid, cr0);

      signal = GDB_SIGNAL_0;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  return TARGET_STOPPED_BY_NO_REASON;
}

int
intelgt_ze_target::read_inst (const ze_device_info &device, CORE_ADDR pc,
			      gdb::array_view<gdb_byte> buffer)
{
  gdb_assert (buffer.size () >= intelgt::MAX_INST_LENGTH);

  ze_device_thread_t all = ze_thread_id_all ();
  int status = read_memory (device, all, pc, buffer.data (),
			    intelgt::MAX_INST_LENGTH);
  if (status == 0)
    return intelgt::MAX_INST_LENGTH;

  status = read_memory (device, all, pc, buffer.data (),
			intelgt::COMPACT_INST_LENGTH);
  if (status > 0)
    return status;

  if (intelgt::inst_length (buffer, device.properties.deviceId)
      == intelgt::MAX_INST_LENGTH)
    return -EIO;

  memset (buffer.begin () + intelgt::COMPACT_INST_LENGTH, 0,
	  intelgt::MAX_INST_LENGTH - intelgt::COMPACT_INST_LENGTH);

  return intelgt::COMPACT_INST_LENGTH;
}

bool
intelgt_ze_target::is_at_breakpoint (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ true);
  CORE_ADDR pc = read_pc (regcache);

  const ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int status = read_inst (*device, pc, inst);
  if (status < 0)
    return false;

  return intelgt::has_breakpoint (inst, device->properties.deviceId);
}

bool
intelgt_ze_target::is_at_eot (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ true);
  CORE_ADDR pc = read_pc (regcache);

  const ze_device_info *device = ze_thread_device (tp);
  gdb_assert (device != nullptr);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int status = read_inst (*device, pc, inst);
  if (status < 0)
    {
      ze_device_thread_t zeid = ze_thread_id (tp);

      warning (_("error reading memory for thread %s (%s) at 0x%"
		 PRIx64), tp->id.to_string ().c_str (),
	       ze_thread_id_str (zeid).c_str (), pc);
      return false;
    }

  intelgt::xe_version device_version
    = intelgt::get_xe_version (device->properties.deviceId);
  switch (device_version)
    {
    case intelgt::xe_version::XE_HP:
    case intelgt::xe_version::XE_HPG:
    case intelgt::xe_version::XE_HPC:
    case intelgt::xe_version::XE2:
    case intelgt::xe_version::XE3:
      {
	/* The opcode mask for bits 6:0.  */
	constexpr uint8_t OPC_MASK = 0x7f;
	switch (inst[0] & OPC_MASK)
	  {
	  case 0x31: /* send */
	  case 0x32: /* sendc */
	    {
	      /* The End Of Thread control.  Only used for SEND and
		 SENDC.  */
	      constexpr uint8_t CTRL_EOT_SEND = 34;
	      return intelgt::get_inst_bit (inst, CTRL_EOT_SEND);
	    }

	  default:
	    return false;
	  }
      }

    default:
      error (_("Unsupported device id 0x%" PRIx32 "."),
	     device->properties.deviceId);
    }
}

void
intelgt_ze_target::prepare_thread_resume (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  regcache *regcache = get_thread_regcache (tp, /* fetch = */ true);
  std::vector<uint32_t> cr0 = intelgt_read_cr0 (regcache);
  gdb_assert (cr0.size () >= 2);

  /* The thread is running.  We may need to overwrite this below.  */
  zetp->exec_state = ZE_THREAD_STATE_RUNNING;

  /* Clear any potential interrupt indication.

     We leave other exception indications so the exception would be
     reported again and can be handled by GDB.  */
  cr0[1] &= ~(1 << INTELGT_CR0_1_FORCE_EXCEPTION_STATUS);
  cr0[1] &= ~(1 << INTELGT_CR0_1_EXTERNAL_HALT_STATUS);

  /* Distinguish stepping and continuing.  */
  switch (zetp->resume_state)
    {
    case ZE_THREAD_RESUME_STEP:
      /* We step by indicating a breakpoint exception, which will be
	 considered on the next instruction.

	 This does not work for EOT, though.  */
      if (!is_at_eot (tp))
	{
	  cr0[0] |= (1 << INTELGT_CR0_0_BREAKPOINT_SUPPRESS);
	  cr0[1] |= (1 << INTELGT_CR0_1_BREAKPOINT_STATUS);
	  break;
	}

      /* At EOT, the thread dispatch ends and the h/w thread becomes idle.

	 There's no point in requesting a single-step exception, since
	 that will never come, but we need to inject an event to tell GDB
	 that the step completed.

	 An exception when executing the EOT instruction will overwrite
	 the thread exit as long as we have not reported the event to GDB.
	 If we have already reported the exit to GDB, this will re-create
	 the thread in order to report the exception.  */
      zetp->exec_state = ZE_THREAD_STATE_EXITED;
      zetp->waitstatus.set_thread_exited (0);

      [[fallthrough]];
    case ZE_THREAD_RESUME_RUN:
      cr0[1] &= ~(1 << INTELGT_CR0_1_BREAKPOINT_STATUS);
      break;

    default:
      internal_error (_("bad resume kind: %d."), zetp->resume_state);
    }

  /* When stepping over a breakpoint, we need to suppress the breakpoint
     exception we would otherwise get immediately.

     This requires breakpoints to be already inserted when this function
     is called.  It also handles permanent breakpoints.  */
  if (is_at_breakpoint (tp))
    cr0[0] |= (1 << INTELGT_CR0_0_BREAKPOINT_SUPPRESS);

  intelgt_write_cr0 (regcache, cr0);

  dprintf ("thread %s (%s) resumed, cr0.0=%" PRIx32 " .1=%" PRIx32
	   " .2=%" PRIx32 ".", tp->id.to_string ().c_str (),
	   ze_thread_id_str (zetp->id).c_str (), cr0[0], cr0[1], cr0[2]);
}

void
intelgt_ze_target::add_regset (target_desc *tdesc, const ze_device_info &dinfo,
			       const zet_debug_regset_properties_t &regprop,
			       int &regnum, ze_regset_info_t &regsets,
			       expedite_t &expedite)
{
  tdesc_feature *feature = nullptr;
  const ze_device_properties_t &device = dinfo.properties;

  ze_regset_info regset = {};
  regset.type = (uint32_t) regprop.type;
  regset.size = regprop.byteSize;
  regset.begin = regnum;
  regset.is_writeable
    = ((regprop.generalFlags & ZET_DEBUG_REGSET_FLAG_WRITEABLE) != 0);

  if (regprop.count < 1)
    {
      warning (_("Ignoring empty regset %u in %s."), regprop.type,
	       device.name);
      return;
    }

  if ((regprop.generalFlags & ZET_DEBUG_REGSET_FLAG_READABLE) == 0)
    {
      warning (_("Ignoring non-readable regset %u in %s."), regprop.type,
	       device.name);
      return;
    }

  switch (regprop.type)
    {
    case ZET_DEBUG_REGSET_TYPE_GRF_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_GRF);

      intelgt_add_regset (feature, regnum, "r", regprop.count, "grf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_ADDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_ADDR);

      intelgt_add_regset (feature, regnum, "a", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 16u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_FLAG_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_FLAG);

      intelgt_add_regset (feature, regnum, "f", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 16u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_CE_INTEL_GPU:
      /* We expect a single 'ce' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'ce' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_CE);

      tdesc_create_reg (feature, "ce", regnum++, 1, "arf",
			regprop.bitSize,
			intelgt_uint_reg_type (feature, regprop.bitSize,
					       32u));

      expedite.push_back ("ce");
      break;

    case ZET_DEBUG_REGSET_TYPE_SR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_SR);

      intelgt_add_regset (feature, regnum, "sr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_CR);

      expedite.push_back ("cr0");
      intelgt_add_regset (feature, regnum, "cr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_TDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_TDR);

      intelgt_add_regset (feature, regnum, "tdr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 16u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_ACC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_ACC);

      intelgt_add_regset (feature, regnum, "acc", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_MME_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_MME);

      intelgt_add_regset (feature, regnum, "mme", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_SP_INTEL_GPU:
      /* We expect a single 'sp' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'sp' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_SP);

      tdesc_create_reg (feature, "sp", regnum++, 1, "arf",
			regprop.bitSize,
			intelgt_uint_reg_type (feature, regprop.bitSize,
					       regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_SBA_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_SBA);

      switch (regprop.version)
	{
	case 0:
	  {
	    const char *regtype = intelgt_uint_reg_type (feature,
							 regprop.bitSize,
							 regprop.bitSize);
	    const char *sbaregs[] = {
	      "genstbase",
	      "sustbase",
	      "dynbase",
	      "iobase",
	      "isabase",
	      "blsustbase",
	      "blsastbase",
	      "btbase",
	      "scrbase0",
	      "scrbase1",
	      nullptr
	    };
	    int reg = 0;
	    for (; (reg < regprop.count) && (sbaregs[reg] != nullptr); ++reg)
	      {
		if (strcmp (sbaregs[reg], "isabase") == 0)
		  expedite.push_back (sbaregs[reg]);

		tdesc_create_reg (feature, sbaregs[reg], regnum++, 1,
				  "virtual", regprop.bitSize, regtype);
	      }
	  }
	  break;

	default:
	  warning (_("Ignoring unknown SBA regset version %u in %s"),
		   regprop.version, device.name);
	  break;
	}
      break;

    case ZET_DEBUG_REGSET_TYPE_DBG_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_DBG);

      intelgt_add_regset (feature, regnum, "dbg", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_FC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::FEATURE_FC);

      intelgt_add_regset (feature, regnum, "fc", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize,
						 32u),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_INVALID_INTEL_GPU:
    case ZET_DEBUG_REGSET_TYPE_FORCE_UINT32:
      break;
    }

  if (feature == nullptr)
    {
      warning (_("Ignoring unknown regset %u in %s."), regprop.type,
	       device.name);

      return;
    }

  regset.end = regnum;
  regsets.push_back (regset);
}


/* The Intel GT target ops object.  */

static intelgt_ze_target the_intelgt_ze_target;

extern void initialize_low ();
void
initialize_low ()
{
  /* Delayed initialization of Level-Zero targets.  See ze-low.h.  */
  the_intelgt_ze_target.init ();
  set_target_ops (&the_intelgt_ze_target);
}
