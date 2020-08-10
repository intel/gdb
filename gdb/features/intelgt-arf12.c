/* THIS FILE IS GENERATED.  -*- buffer-read-only: t -*- vi:set ro:
  Original: intelgt-arf12.xml */

#include "gdbsupport/tdesc.h"

static int
create_feature_intelgt_arf12 (struct target_desc *result, long regnum)
{
  struct tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.intelgt.arf12");
  tdesc_create_reg (feature, "a0", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc0", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc1", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc2", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc3", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc4", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc5", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc6", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc7", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc8", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "acc9", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "f0", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "f1", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "ce", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "sp", regnum++, 1, NULL, 128, "uint128");
  tdesc_create_reg (feature, "sr0", regnum++, 1, NULL, 128, "uint128");
  tdesc_create_reg (feature, "cr0", regnum++, 1, NULL, 128, "uint128");
  tdesc_create_reg (feature, "ip", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "tdr", regnum++, 1, NULL, 128, "uint128");
  tdesc_create_reg (feature, "tm0", regnum++, 1, NULL, 128, "uint128");
  tdesc_create_reg (feature, "emask", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "iemask", regnum++, 1, NULL, 32, "uint32");
  tdesc_create_reg (feature, "mme0", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme1", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme2", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme3", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme4", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme5", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme6", regnum++, 1, NULL, 256, "uint256");
  tdesc_create_reg (feature, "mme7", regnum++, 1, NULL, 256, "uint256");
  return regnum;
}
