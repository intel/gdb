/* Target interface for Intel GT based on level-zero for gdbserver.

   Copyright (C) 2020-2022 Free Software Foundation, Inc.

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

#include "server.h"
#include "ze-low.h"

#include "arch/intelgt.h"

#include <level_zero/zet_intel_gpu_debug.h>
#include <iomanip>
#include <sstream>


/* FIXME make into a target method?  */
int using_threads = 1;

/* Convenience macros.  */

#define dprintf(...)						\
  do								\
    {								\
      if (debug_threads)					\
	{							\
	  debug_printf ("%s: ", __FUNCTION__);			\
	  debug_printf (__VA_ARGS__);				\
	  debug_printf ("\n");					\
	}							\
    }								\
  while (0)


/* Determine the most appropriate unsigned integer container type for a
   register of BITSIZE bits.  */

static const char *
intelgt_uint_reg_type (tdesc_feature *feature, uint32_t bitsize)
{
  if (bitsize <= 8u)
    return "uint8";

  if (bitsize <= 16u)
    return "uint16";

  if (bitsize <= 32u)
    return "uint32";

  if (bitsize <= 64u)
    return "uint64";

  if (bitsize <= 128u)
    return "uint128";

  if (bitsize <= 256u)
    {
      tdesc_create_vector (feature, "vector256",
			   tdesc_named_type (feature, "uint32"), 8);
      return "vector256";
    }

  if (bitsize <= 512u)
    {
      tdesc_create_vector (feature, "vector512",
			   tdesc_named_type (feature, "uint32"), 16);
      return "vector512";
    }

  if (bitsize <= 1024u)
    {
      tdesc_create_vector (feature, "vector1024",
			   tdesc_named_type (feature, "uint32"), 32);
      return "vector1024";
    }

  if (bitsize <= 2048u)
    {
      tdesc_create_vector (feature, "vector2048",
			   tdesc_named_type (feature, "uint32"), 64);
      return "vector2048";
    }

  if (bitsize <= 4096u)
    {
      tdesc_create_vector (feature, "vector4096",
			   tdesc_named_type (feature, "uint32"), 128);
      return "vector4096";
    }

  if (bitsize <= 8192u)
    {
      tdesc_create_vector (feature, "vector8192",
			   tdesc_named_type (feature, "uint32"), 256);
      return "vector8192";
    }

  error (_("unsupported bitsize %" PRIu32), bitsize);
}

/* Add a (uniform) register set to FEATURE.  */

static void
intelgt_add_regset (tdesc_feature *feature, long &regnum,
		    const char *prefix, uint32_t count, const char *group,
		    uint32_t bitsize, const char *type, expedite_t &expedite)
{
  for (uint32_t reg = 0; reg < count; ++reg)
    {
      std::string name = std::string (prefix) + std::to_string (reg);

      bool is_expedited = false;
      for (const char *exp_reg : expedite)
	if (name == exp_reg)
	  is_expedited = true;

      tdesc_create_reg (feature, name.c_str (), regnum++, 1, group,
			bitsize, type, is_expedited);
    }
}

/* Control Register details.  */

enum
  {
    /* The position of the Breakpoint Suppress bit in CR0.0.  */
    intelgt_cr0_0_breakpoint_suppress = 15,

    /* The position of the Breakpoint Status and Control bit in CR0.1.  */
    intelgt_cr0_1_breakpoint_status = 31,

    /* The position of the External Halt Status and Control bit in CR0.1.  */
    intelgt_cr0_1_external_halt_status = 30,

    /* The position of the Illegal Opcode Exception Status bit in CR0.1.  */
    intelgt_cr0_1_illegal_opcode_status = 28,

    /* The position of the Force Exception Status and Control bit in CR0.1.  */
    intelgt_cr0_1_force_exception_status = 26,
};

/* Return CR0.SUBREG in REGCACHE.  */

static uint32_t
intelgt_read_cr0 (regcache *regcache, int subreg)
{
  int cr0regno = find_regno (regcache->tdesc, "cr0");
  int cr0size = register_size (regcache->tdesc, cr0regno);
  uint32_t cr0[16];
  gdb_assert (cr0size <= sizeof (cr0));
  gdb_assert (cr0size >= sizeof (cr0[0]) * (subreg + 1));
  collect_register (regcache, cr0regno, cr0);

  enum register_status cr0status = regcache->get_register_status (cr0regno);
  switch (cr0status)
    {
    case REG_VALID:
    case REG_DIRTY:
      return cr0[subreg];

    case REG_UNKNOWN:
      internal_error (_("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (_("unknown register status: %d."), cr0status);
}

/* Write VALUE into CR0.SUBREG in REGCACHE.  */

static void
intelgt_write_cr0 (regcache *regcache, int subreg, uint32_t value)
{
  int cr0regno = find_regno (regcache->tdesc, "cr0");
  int cr0size = register_size (regcache->tdesc, cr0regno);
  uint32_t cr0[16];
  gdb_assert (cr0size <= sizeof (cr0));
  gdb_assert (cr0size >= sizeof (cr0[0]) * (subreg + 1));
  collect_register (regcache, cr0regno, cr0);

  enum register_status cr0status = regcache->get_register_status (cr0regno);
  switch (cr0status)
    {
    case REG_VALID:
    case REG_DIRTY:
      cr0[subreg] = value;
      supply_register (regcache, cr0regno, cr0);
      return;

    case REG_UNKNOWN:
      internal_error (_("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (_("unknown register status: %d."), cr0status);
}

/* Return CR0.SUBREG for TP.  */

static uint32_t
intelgt_read_cr0 (thread_info *tp, int subreg)
{
  struct regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  return intelgt_read_cr0 (regcache, subreg);
}

/* Write VALUE into CR0.SUBREG for TP.  */

static void
intelgt_write_cr0 (thread_info *tp, int subreg, uint32_t value)
{
  struct regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  intelgt_write_cr0 (regcache, subreg, value);
}

static unsigned int
intelgt_decode_tagged_address (CORE_ADDR addr)
{
  /* Generic pointers are tagged in order to preserve the address space to
     which they are pointing.  Tags are encoded into bits [61:63] of an
     address:

     000/111 - global,
     001 - private,
     010 - local (SLM)

     We currently cannot decode this tag in GDB, as the information
     cannot be added to the (cached) type instance flags, as it changes at
     runtime.  */
  if ((addr >> 61) == 0x2ul)
    return (unsigned int) ZET_DEBUG_MEMORY_SPACE_TYPE_SLM;

  return (unsigned int) ZET_DEBUG_MEMORY_SPACE_TYPE_DEFAULT;
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

/* Target op definitions for Intel GT target based on level-zero.  */

class intelgt_ze_target : public ze_target
{
public:
  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size) override;

  bool supports_stopped_by_sw_breakpoint () override { return true; }
  bool stopped_by_sw_breakpoint () override;
  bool supports_run_command () override;

  CORE_ADDR read_pc (regcache *regcache) override;
  void write_pc (regcache *regcache, CORE_ADDR pc) override;

protected:
  bool is_device_supported
    (const ze_device_properties_t &,
     const std::vector<zet_debug_regset_properties_t> &) override;

  target_desc *create_tdesc
    (ze_device_info *dinfo,
     const std::vector<zet_debug_regset_properties_t> &) override;

  target_stop_reason get_stop_reason (thread_info *, gdb_signal &) override;

  void prepare_thread_resume (thread_info *tp) override;

  /* Read one instruction from memory at PC into BUFFER and return the
     number of bytes read on success or a negative errno error code.

     BUFFER must be intelgt::MAX_INST_LENGTH bytes long.  */
  int read_inst (thread_info *tp, CORE_ADDR pc, unsigned char *buffer);

  bool is_at_breakpoint (thread_info *tp) override;
  bool is_at_eot (thread_info *tp);

  bool erratum_18020355813 (thread_info *tp);

  /* Read the memory in the context of thread TP.  */
  int read_memory (thread_info *tp, CORE_ADDR memaddr,
		   unsigned char *myaddr, int len,
		   unsigned int addr_space = 0) override;

  /* Write the memory in the context of thread TP.  */
  int write_memory (thread_info *tp, CORE_ADDR memaddr,
		    const unsigned char *myaddr, int len,
		    unsigned int addr_space = 0) override;

private:
  /* Add a register set for REGPROP on DEVICE to REGSETS and increment REGNUM
     accordingly.

     May optionally add registers to EXPEDITE.  */
  void add_regset (target_desc *tdesc, const ze_device_properties_t &device,
		   const zet_debug_regset_properties_t &regprop,
		   long &regnum, ze_regset_info_t &regsets,
		   expedite_t &expedite);
};

bool
intelgt_ze_target::supports_run_command ()
{
  return false;
}

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

  ptid_t ptid = ptid_of (current_thread);

  if (zetp->exec_state != ze_thread_state_stopped)
    {
      dprintf ("not-stopped thread %s", ptid.to_string ().c_str ());
      return false;
    }

  return (zetp->stop_reason == TARGET_STOPPED_BY_SW_BREAKPOINT);
}

CORE_ADDR
intelgt_ze_target::read_pc (regcache *regcache)
{
  uint32_t ip = intelgt_read_cr0 (regcache, 2);
  uint64_t isabase;
  collect_register_by_name (regcache, "isabase", &isabase);

  if (UINT32_MAX < ip)
    warning (_("IP '0x%" PRIx32 "' outside of ISA range."), ip);

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

  intelgt_write_cr0 (regcache, 2, (uint32_t) pc);
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
	  warning (_("Ignoring empty regset %u in %s."), regprop.type,
		   properties.name);
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
	  have_cr = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_SR_INTEL_GPU:
	  have_sr = true;
	  break;

	case ZET_DEBUG_REGSET_TYPE_SBA_INTEL_GPU:
	  /* We need 'isabase', which is at position 5 in version 1.  */
	  if ((regprop.version == 0) && (regprop.count >= 5))
	    have_isabase = true;
	  else
	    warning (_("Ignoring unknown SBA regset version %u in %s."),
		     regprop.version, properties.name);
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
   const std::vector<zet_debug_regset_properties_t> &regset_properties)
{
  const ze_device_properties_t &properties = dinfo->properties;

  if (properties.vendorId != 0x8086)
    error (_("unknown vendor (%" PRIx32 ") of device (%" PRIx32 "): %s"),
	   properties.vendorId, properties.deviceId, properties.name);

  target_desc_up tdesc = allocate_target_description ();
  set_tdesc_architecture (tdesc.get (), "intelgt");
  set_tdesc_osabi (tdesc.get (), "GNU/Linux");

  tdesc_add_device_attribute (tdesc.get (), "vendor_id",
			      string_printf ("0x%04" PRIx32,
					     properties.vendorId));
  /* Within GDB the the device_id is called target_id.  Device ID is used in
     GDB to identify devices internally.  */
  tdesc_add_device_attribute (tdesc.get (), "target_id",
			      string_printf ("0x%04" PRIx32,
					     properties.deviceId));
  if (properties.flags & ZE_DEVICE_PROPERTY_FLAG_SUBDEVICE)
    tdesc_add_device_attribute (tdesc.get (), "subdevice_id",
				std::to_string (properties.subdeviceId));

  tdesc_add_device_attribute (tdesc.get (), "pci_slot", dinfo->pci_slot);

  std::string device_uuid = device_uuid_str (
    dinfo->properties.uuid.id, sizeof (dinfo->properties.uuid.id));
  tdesc_add_device_attribute (tdesc.get (), "device_uuid", device_uuid);

  const uint32_t total_cores = (properties.numSlices
				* properties.numSubslicesPerSlice
				* properties.numEUsPerSubslice);
  const uint32_t total_threads = (total_cores * properties.numThreadsPerEU);
  tdesc_add_device_attribute (tdesc.get (), "total_cores",
			      std::to_string (total_cores));
  tdesc_add_device_attribute (tdesc.get (), "total_threads",
			      std::to_string (total_threads));
  tdesc_add_device_attribute (tdesc.get (), "device_name",
			      properties.name);

  long regnum = 0;
  for (const zet_debug_regset_properties_t &regprop : regset_properties)
    add_regset (tdesc.get (), properties, regprop, regnum,
		dinfo->regsets, dinfo->expedite);

  /* Tdesc expects a nullptr-terminated array.  */
  dinfo->expedite.push_back (nullptr);

  init_target_desc (tdesc.get (), dinfo->expedite.data ());
  return tdesc.release ();
}

target_stop_reason
intelgt_ze_target::get_stop_reason (thread_info *tp, gdb_signal &signal)
{
  ze_device_thread_t thread = ze_thread_id (tp);
  uint32_t cr0[3] = {
    intelgt_read_cr0 (tp, 0),
    intelgt_read_cr0 (tp, 1),
    intelgt_read_cr0 (tp, 2)
  };

  dprintf ("thread %s (%s) stopped, cr0.0=%" PRIx32 ", .1=%" PRIx32
	   " [ %s%s%s%s], .2=%" PRIx32 ".", tp->id.to_string ().c_str (),
	   ze_thread_id_str (thread).c_str (), cr0[0], cr0[1],
	   (((cr0[1] & (1 << intelgt_cr0_1_breakpoint_status)) != 0)
	    ? "bp " : ""),
	   (((cr0[1] & (1 << intelgt_cr0_1_illegal_opcode_status)) != 0)
	    ? "ill " : ""),
	   (((cr0[1] & (1 << intelgt_cr0_1_force_exception_status)) != 0)
	    ? "fe " : ""),
	   (((cr0[1] & (1 << intelgt_cr0_1_external_halt_status)) != 0)
	    ? "eh " : ""),
	   cr0[2]);

  if ((cr0[1] & (1 << intelgt_cr0_1_breakpoint_status)) != 0)
    {
      cr0[1] &= ~(1 << intelgt_cr0_1_breakpoint_status);
      intelgt_write_cr0 (tp, 1, cr0[1]);

      /* We cannot distinguish a single step exception from a breakpoint
	 exception just by looking at CR0.

	 We could inspect the instruction to see if the breakpoint bit is
	 set.  Or we could check the resume type and assume that we set
	 things up correctly for single-stepping before we resumed.  */
      const ze_thread_info *zetp = ze_thread (tp);
      gdb_assert (zetp != nullptr);

      switch (zetp->resume_state)
	{
	case ze_thread_resume_step:
	  signal = GDB_SIGNAL_TRAP;
	  return TARGET_STOPPED_BY_SINGLE_STEP;

	case ze_thread_resume_run:
	case ze_thread_resume_none:
	  /* On some devices, we may get spurious breakpoint exceptions.  */
	  if (erratum_18020355813 (tp))
	    {
	      ze_device_thread_t zeid = ze_thread_id (tp);

	      dprintf ("applying #18020355813 workaround for thread "
		       "%s (%s)", tp->id.to_string ().c_str (),
		       ze_thread_id_str (zeid).c_str ());

	      signal = GDB_SIGNAL_0;
	      return TARGET_STOPPED_BY_NO_REASON;
	    }

	  /* Fall through.  */
	case ze_thread_resume_stop:
	  signal = GDB_SIGNAL_TRAP;
	  return TARGET_STOPPED_BY_SW_BREAKPOINT;
	}
    }

  if ((cr0[1] & (1 << intelgt_cr0_1_illegal_opcode_status)) != 0)
    {
      cr0[1] &= ~(1 << intelgt_cr0_1_illegal_opcode_status);
      intelgt_write_cr0 (tp, 1, cr0[1]);

      signal = GDB_SIGNAL_ILL;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  if ((cr0[1] & ((1 << intelgt_cr0_1_force_exception_status)
		 | (1 << intelgt_cr0_1_external_halt_status))) != 0)
    {
      cr0[1] &= ~(1 << intelgt_cr0_1_force_exception_status);
      cr0[1] &= ~(1 << intelgt_cr0_1_external_halt_status);
      intelgt_write_cr0 (tp, 1, cr0[1]);

      signal = GDB_SIGNAL_INT;
      return TARGET_STOPPED_BY_NO_REASON;
    }

  signal = GDB_SIGNAL_UNKNOWN;
  return TARGET_STOPPED_BY_NO_REASON;
}

int
intelgt_ze_target::read_memory (thread_info *tp, CORE_ADDR memaddr,
				unsigned char *myaddr, int len,
				unsigned int addr_space)
{
  if (addr_space == (unsigned int) ZET_DEBUG_MEMORY_SPACE_TYPE_DEFAULT)
    addr_space = intelgt_decode_tagged_address (memaddr);

  return ze_target::read_memory (tp, memaddr, myaddr, len, addr_space);
}

int
intelgt_ze_target::write_memory (thread_info *tp, CORE_ADDR memaddr,
				 const unsigned char *myaddr, int len,
				 unsigned int addr_space)
{
  if (addr_space == (unsigned int) ZET_DEBUG_MEMORY_SPACE_TYPE_DEFAULT)
    addr_space = intelgt_decode_tagged_address (memaddr);

  return ze_target::write_memory (tp, memaddr, myaddr, len, addr_space);
}

int
intelgt_ze_target::read_inst (thread_info *tp, CORE_ADDR pc,
			      unsigned char *buffer)
{
  int status = read_memory (tp, pc, buffer, intelgt::MAX_INST_LENGTH);
  if (status == 0)
    return intelgt::MAX_INST_LENGTH;

  status = read_memory (tp, pc, buffer, intelgt::COMPACT_INST_LENGTH);
  if (status > 0)
    return status;

  if (!intelgt::is_compacted_inst (buffer))
    return -EIO;

  memset (buffer + intelgt::COMPACT_INST_LENGTH, 0,
	  intelgt::MAX_INST_LENGTH - intelgt::COMPACT_INST_LENGTH);

  return intelgt::COMPACT_INST_LENGTH;
}

bool
intelgt_ze_target::is_at_breakpoint (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  CORE_ADDR pc = read_pc (regcache);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int status = read_inst (tp, pc, inst);
  if (status < 0)
    return false;

  return intelgt::has_breakpoint (inst);
}

bool
intelgt_ze_target::is_at_eot (thread_info *tp)
{
  regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  CORE_ADDR pc = read_pc (regcache);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int status = read_inst (tp, pc, inst);
  if (status < 0)
    {
      ze_device_thread_t zeid = ze_thread_id (tp);

      warning (_("error reading memory for thread %s (%s) at 0x%"
		 PRIx64), tp->id.to_string ().c_str (),
	       ze_thread_id_str (zeid).c_str (), pc);
      return false;
    }

  uint8_t opc = inst[0] & intelgt::opc_mask;
  switch (opc)
    {
    case intelgt::opc_send:
    case intelgt::opc_sendc:
      return intelgt::get_inst_bit (inst, intelgt::ctrl_eot);

    default:
      return false;
    }
}

/* Return whether erratum #18020355813 applies.  */

bool
intelgt_ze_target::erratum_18020355813 (thread_info *tp)
{
  const process_info *process = get_thread_process (tp);
  if (process == nullptr)
    {
      ze_device_thread_t zeid = ze_thread_id (tp);

      warning (_("error getting process for thread %s (%s)"),
	       tp->id.to_string ().c_str (),
	       ze_thread_id_str (zeid).c_str ());
      return false;
    }

  process_info_private *zeinfo = process->priv;
  gdb_assert (zeinfo != nullptr);

  /* We may not have a device if we got detached.  */
  ze_device_info *device = zeinfo->device;
  if (device == nullptr)
    return false;

  /* The erratum only applies to Intel devices.  */
  if (device->properties.vendorId != 0x8086)
    return false;

  /* The erratum only applies to a range of devices.  */
  switch (device->properties.deviceId)
    {
    case 0x4f80:
    case 0x4f81:
    case 0x4f82:
    case 0x4f83:
    case 0x4f84:
    case 0x4f85:
    case 0x4f86:
    case 0x4f87:
    case 0x4f88:
    case 0x56a0:
    case 0x56a1:
    case 0x56a2:
    case 0x5690:
    case 0x5691:
    case 0x5692:
    case 0x56c0:
    case 0x56c1:
    case 0x56c2:
    case 0x56a3:
    case 0x56a4:
    case 0x56a5:
    case 0x56a6:
    case 0x5693:
    case 0x5694:
    case 0x5695:
    case 0x5696:
    case 0x5697:
    case 0x56b0:
    case 0x56b1:
    case 0x56b2:
    case 0x56b3:
    case 0x56ba:
    case 0x56bb:
    case 0x56bc:
    case 0x56bd:

    case 0x0bd0:
    case 0x0bd4:
    case 0x0bd5:
    case 0x0bd6:
    case 0x0bd7:
    case 0x0bd8:
    case 0x0bd9:
    case 0x0bda:
    case 0x0bdb:
    case 0x0b69:
    case 0x0b6e:
      break;

    default:
      return false;
    }

  regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  CORE_ADDR pc = read_pc (regcache);

  gdb_byte inst[intelgt::MAX_INST_LENGTH];
  int status = read_inst (tp, pc, inst);
  if (status < 0)
    {
      ze_device_thread_t zeid = ze_thread_id (tp);

      warning (_("error reading memory for thread %s (%s) at 0x%"
		 PRIx64), tp->id.to_string ().c_str (),
	       ze_thread_id_str (zeid).c_str (), pc);
      return false;
    }

  /* The erratum applies to instructions without breakpoint control.  */
  return !intelgt::has_breakpoint (inst);
}

void
intelgt_ze_target::prepare_thread_resume (thread_info *tp)
{
  ze_thread_info *zetp = ze_thread (tp);
  gdb_assert (zetp != nullptr);

  regcache *regcache = get_thread_regcache (tp, /* fetch = */ false);
  uint32_t cr0[3] = {
    intelgt_read_cr0 (regcache, 0),
    intelgt_read_cr0 (regcache, 1),
    intelgt_read_cr0 (regcache, 2)
  };

  /* The thread is running.  We may need to overwrite this below.  */
  zetp->exec_state = ze_thread_state_running;

  /* Clear any potential interrupt indication.

     We leave other exception indications so the exception would be
     reported again and can be handled by GDB.  */
  cr0[1] &= ~(1 << intelgt_cr0_1_force_exception_status);
  cr0[1] &= ~(1 << intelgt_cr0_1_external_halt_status);

  /* Distinguish stepping and continuing.  */
  switch (zetp->resume_state)
    {
    case ze_thread_resume_step:
      /* We step by indicating a breakpoint exception, which will be
	 considered on the next instruction.

	 This does not work for EOT, though.  */
      if (!is_at_eot (tp))
	{
	  cr0[1] |= (1 << intelgt_cr0_1_breakpoint_status);
	  break;
	}

      /* At EOT, the thread dispatch ends and the thread becomes idle.

	 There's no point in requesting a single-step exception but we
	 need to inject an event to tell GDB that the step completed.  */
      zetp->exec_state = ze_thread_state_unavailable;
      zetp->waitstatus.set_unavailable ();

      /* Fall through.  */
    case ze_thread_resume_run:
      cr0[1] &= ~(1 << intelgt_cr0_1_breakpoint_status);
      break;

    default:
      internal_error (_("bad resume kind: %d."), zetp->resume_state);
    }

  /* When stepping over a breakpoint, we need to suppress the breakpoint
     exception we would otherwise get immediately.

     This requires breakpoints to be already inserted when this function
     is called.  It also handles permanent breakpoints.  */
  if (is_at_breakpoint (tp))
    cr0[0] |= (1 << intelgt_cr0_0_breakpoint_suppress);

  intelgt_write_cr0 (regcache, 0, cr0[0]);
  intelgt_write_cr0 (regcache, 1, cr0[1]);
  intelgt_write_cr0 (regcache, 2, cr0[2]);

  dprintf ("thread %s (%s) resumed, cr0.0=%" PRIx32 " .1=%" PRIx32
	   " .2=%" PRIx32 ".", tp->id.to_string ().c_str (),
	   ze_thread_id_str (zetp->id).c_str (), cr0[0], cr0[1], cr0[2]);
}

void
intelgt_ze_target::add_regset (target_desc *tdesc,
			       const ze_device_properties_t &device,
			       const zet_debug_regset_properties_t &regprop,
			       long &regnum, ze_regset_info_t &regsets,
			       expedite_t &expedite)
{
  tdesc_feature *feature = nullptr;

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

  if ((regprop.byteSize & (regprop.byteSize - 1)) != 0)
    {
      /* FIXME: DOQG-2381.  */
      warning (_("Ignoring regset %u with irregular size %u in %s."),
	       regprop.type, regprop.byteSize, device.name);
      return;
    }

  switch (regprop.type)
    {
    case ZET_DEBUG_REGSET_TYPE_GRF_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_grf);

      expedite.push_back ("r0");
      intelgt_add_regset (feature, regnum, "r", regprop.count, "grf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_ADDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_addr);

      intelgt_add_regset (feature, regnum, "a", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_FLAG_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_flag);

      intelgt_add_regset (feature, regnum, "f", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_CE_INTEL_GPU:
      /* We expect a single 'ce' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'ce' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::feature_ce);

      expedite.push_back ("ce");
      tdesc_create_reg (feature, "ce", regnum++, 1, "arf",
			regprop.bitSize,
			intelgt_uint_reg_type (feature, regprop.bitSize),
			true /* expedited */);
      break;

    case ZET_DEBUG_REGSET_TYPE_SR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_sr);

      expedite.push_back ("sr0");
      intelgt_add_regset (feature, regnum, "sr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_cr);

      expedite.push_back ("cr0");
      intelgt_add_regset (feature, regnum, "cr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_TDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_tdr);

      intelgt_add_regset (feature, regnum, "tdr", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_ACC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_acc);

      intelgt_add_regset (feature, regnum, "acc", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_MME_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_mme);

      intelgt_add_regset (feature, regnum, "mme", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_SP_INTEL_GPU:
      /* We expect a single 'sp' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'sp' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::feature_sp);

      tdesc_create_reg (feature, "sp", regnum++, 1, "arf",
			regprop.bitSize,
			intelgt_uint_reg_type (feature, regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_SBA_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_sba);

      switch (regprop.version)
	{
	case 0:
	  {
	    const char *regtype = intelgt_uint_reg_type (feature,
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
	      "scrbase",
	      "scrbase2",
	      nullptr
	    };
	    int reg = 0;
	    for (; (reg < regprop.count) && (sbaregs[reg] != nullptr); ++reg)
	      {
		bool is_expedited = false;
		if ((strcmp (sbaregs[reg], "genstbase") == 0)
		    || (strcmp (sbaregs[reg], "isabase") == 0))
		  {
		    is_expedited = true;
		    expedite.push_back (sbaregs[reg]);
		  }

		tdesc_create_reg (feature, sbaregs[reg], regnum++, 1,
				  "virtual", regprop.bitSize, regtype,
				  is_expedited);
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
      feature = tdesc_create_feature (tdesc, intelgt::feature_dbg);

      intelgt_add_regset (feature, regnum, "dbg", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
			  expedite);
      break;

    case ZET_DEBUG_REGSET_TYPE_FC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_fc);

      intelgt_add_regset (feature, regnum, "fc", regprop.count, "arf",
			  regprop.bitSize,
			  intelgt_uint_reg_type (feature, regprop.bitSize),
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
  /* Delayed initialization of level-zero targets.  See ze-low.h.  */
  the_intelgt_ze_target.init ();
  set_target_ops (&the_intelgt_ze_target);
}
