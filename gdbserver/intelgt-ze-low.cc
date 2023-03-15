/* Target interface for Intel GT based on level-zero for gdbserver.

   Copyright (C) 2020-2023 Free Software Foundation, Inc.

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
intelgt_uint_reg_type (uint32_t bitsize)
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
    return "uint256";

  if (bitsize <= 512u)
    return "uint512";

  if (bitsize <= 1024u)
    return "uint1024";

  if (bitsize <= 2048u)
    return "uint2048";

  if (bitsize <= 4096u)
    return "uint4096";

  if (bitsize <= 8192u)
    return "uint8192";

  error (_("unsupported bitsize %" PRIu32), bitsize);
}

/* Add a (uniform) register set to FEATURE.  */

static void
intelgt_add_regset (tdesc_feature *feature, long &regnum,
		    const char *prefix, uint32_t count, const char *group,
		    uint32_t bitsize, const char *type)
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
    intelgt_cr0_0_breakpoint_suppress = 15,

    /* The position of the Breakpoint Status and Control bit in CR0.1.  */
    intelgt_cr0_1_breakpoint_status = 31,

    /* The position of the External Halt Status and Control bit in CR0.1.  */
    intelgt_cr0_1_external_halt_status = 30,

    /* The position of the Illegal Opcode Exception Status bit in CR0.1.  */
    intelgt_cr0_1_illegal_opcode_status = 28,

    /* The position of the Force Exception Status and Control bit in CR0.1.  */
    intelgt_cr0_1_force_exception_status = 26
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
      internal_error (__FILE__, __LINE__, _("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (__FILE__, __LINE__, _("unknown register status: %d."),
		  cr0status);
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
      internal_error (__FILE__, __LINE__, _("unknown register 'cr0'."));

    case REG_UNAVAILABLE:
      error (_("cr0 is not available"));
    }

  internal_error (__FILE__, __LINE__, _("unknown register status: %d."),
		  cr0status);
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

/* Instruction details.  */

enum
  {
    /* The opcode mask for bits 6:0.  */
    intelgt_opc_mask = 0x3f,

    /* Selected instruction opcodes.  */
    intelgt_opc_send = 0x31,
    intelgt_opc_sendc = 0x32,
  };

/* Selected instruction control bit positions.  */
enum
  {
    /* The End Of Thread control.  Only used for SEND and SENDC.  */
    intelgt_ctrl_eot = 34,
  };

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
    (const ze_device_properties_t &,
     const std::vector<zet_debug_regset_properties_t> &,
     const ze_pci_ext_properties_t &,
     ze_regset_info_t &, expedite_t &) override;

  target_stop_reason get_stop_reason (thread_info *, gdb_signal &) override;

  void prepare_thread_resume (thread_info *tp,
			      enum resume_kind rkind) override;

  /* Read one instruction from memory at PC into BUFFER and return the
     number of bytes read on success or a negative errno error code.

     BUFFER must be intelgt::MAX_INST_LENGTH bytes long.  */
  int read_inst (thread_info *tp, CORE_ADDR pc, unsigned char *buffer);

  bool is_at_breakpoint (thread_info *tp) override;
  bool is_at_eot (thread_info *tp);

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
      dprintf ("not-stopped thread %d.%ld", ptid.pid (), ptid.tid ());
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
  (const ze_device_properties_t &properties,
   const std::vector<zet_debug_regset_properties_t> &regset_properties,
   const ze_pci_ext_properties_t &pci_properties,
   ze_regset_info_t &regsets, expedite_t &expedite)
{
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

  tdesc_add_device_attribute (tdesc.get (), "pci_slot",
			      string_printf ("%02" PRIx32 ":%02" PRIx32
					     ".%" PRId32,
					     pci_properties.address.bus,
					     pci_properties.address.device,
					     pci_properties.address.function));

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
    add_regset (tdesc.get (), properties, regprop, regnum, regsets, expedite);

  /* Tdesc expects a nullptr-terminated array.  */
  expedite.push_back (nullptr);

  init_target_desc (tdesc.get (), expedite.data ());
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

  dprintf ("thread %d.%ld (%s) stopped, cr0.0=%" PRIx32 ", .1=%" PRIx32
	   " [ %s%s%s%s], .2=%" PRIx32 ".", tp->id.pid (), tp->id.lwp (),
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

      signal = GDB_SIGNAL_TRAP;
      return ((zetp->resume_state == ze_thread_resume_step)
	      ? TARGET_STOPPED_BY_SINGLE_STEP
	      : TARGET_STOPPED_BY_SW_BREAKPOINT);
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

  return TARGET_STOPPED_BY_NO_REASON;
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

      warning (_("error reading memory for thread %d.%ld (%s) at 0x%"
		 PRIx64), tp->id.pid (), tp->id.lwp (),
	       ze_thread_id_str (zeid).c_str (), pc);
      return false;
    }

  uint8_t opc = inst[0] & intelgt_opc_mask;
  switch (opc)
    {
    case intelgt_opc_send:
    case intelgt_opc_sendc:
      return intelgt::get_inst_bit (inst, intelgt_ctrl_eot);

    default:
      return false;
    }
}

void
intelgt_ze_target::prepare_thread_resume (thread_info *tp,
					  enum resume_kind rkind)
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
  switch (rkind)
    {
    case resume_step:
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
    case resume_continue:
      cr0[1] &= ~(1 << intelgt_cr0_1_breakpoint_status);
      break;

    default:
      internal_error (__FILE__, __LINE__, _("bad resume kind: %d."), rkind);
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

  dprintf ("thread %d.%ld (%s) resumed, cr0.0=%" PRIx32 " .1=%" PRIx32
	   " .2=%" PRIx32 ".", tp->id.pid (), tp->id.lwp (),
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

      intelgt_add_regset (feature, regnum, "r", regprop.count, "GRF",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_ADDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_addr);

      intelgt_add_regset (feature, regnum, "a", regprop.count, "ADDR",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_FLAG_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_flag);

      intelgt_add_regset (feature, regnum, "f", regprop.count, "FLAG",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_CE_INTEL_GPU:
      /* We expect a single 'ce' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'ce' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::feature_ce);

      tdesc_create_reg (feature, "ce", regnum++, 1, "CE",
			regprop.bitSize,
			intelgt_uint_reg_type (regprop.bitSize));

      expedite.push_back ("ce");
      break;

    case ZET_DEBUG_REGSET_TYPE_SR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_sr);

      intelgt_add_regset (feature, regnum, "sr", regprop.count, "SR",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));

      expedite.push_back ("sr0");
      break;

    case ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_cr);

      intelgt_add_regset (feature, regnum, "cr", regprop.count, "CR",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));

      expedite.push_back ("cr0");
      break;

    case ZET_DEBUG_REGSET_TYPE_TDR_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_tdr);

      intelgt_add_regset (feature, regnum, "tdr", regprop.count, "TDR",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_ACC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_acc);

      intelgt_add_regset (feature, regnum, "acc", regprop.count, "ACC",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_MME_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_mme);

      intelgt_add_regset (feature, regnum, "mme", regprop.count, "MME",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_SP_INTEL_GPU:
      /* We expect a single 'sp' register.  */
      if (regprop.count != 1)
	warning (_("Ignoring %u unexpected 'sp' registers in %s."),
		 regprop.count - 1, device.name);

      feature = tdesc_create_feature (tdesc, intelgt::feature_sp);

      tdesc_create_reg (feature, "sp", regnum++, 1, "SP",
			regprop.bitSize,
			intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_SBA_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_sba);

      switch (regprop.version)
	{
	case 0:
	  {
	    const char *regtype = intelgt_uint_reg_type (regprop.bitSize);
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
	      tdesc_create_reg (feature, sbaregs[reg], regnum++, 1, "SBA",
				regprop.bitSize, regtype);

	    if (regprop.count >= 4)
	      expedite.push_back ("isabase");
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

      intelgt_add_regset (feature, regnum, "dbg", regprop.count, "DBG",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
      break;

    case ZET_DEBUG_REGSET_TYPE_FC_INTEL_GPU:
      feature = tdesc_create_feature (tdesc, intelgt::feature_fc);

      intelgt_add_regset (feature, regnum, "fc", regprop.count, "FC",
			  regprop.bitSize,
			  intelgt_uint_reg_type (regprop.bitSize));
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
