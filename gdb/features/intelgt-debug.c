/* THIS FILE IS GENERATED.  -*- buffer-read-only: t -*- vi:set ro:
  Original: intelgt-debug.xml.  */

#include "gdbsupport/tdesc.h"

static int
create_feature_intelgt_debug (struct target_desc *result, long regnum)
{
  struct tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.intelgt.debug");
  tdesc_create_reg (feature, "btbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "scrbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "genstbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "sustbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "blsustbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "blsastbase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "isabase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "iobase", regnum++, 1, NULL, 64, "uint64");
  tdesc_create_reg (feature, "dynbase", regnum++, 1, NULL, 64, "uint64");
  return regnum;
}
