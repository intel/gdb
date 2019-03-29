/* THIS FILE IS GENERATED.  -*- buffer-read-only: t -*- vi:set ro:
  Original: 32bit-cet.xml */

#include "gdbsupport/tdesc.h"

static int
create_feature_i386_32bit_cet (struct target_desc *result, long regnum)
{
  struct tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.i386.cet");
  tdesc_type_with_fields *type_with_fields;
  type_with_fields = tdesc_create_struct (feature, "cet_u_setting");
  tdesc_set_struct_size (type_with_fields, 8);
  tdesc_add_bitfield (type_with_fields, "SH_STK_EN", 0, 0);
  tdesc_add_bitfield (type_with_fields, "WR_SHSTK_EN", 1, 1);
  tdesc_add_bitfield (type_with_fields, "ENDBR_EN", 2, 2);
  tdesc_add_bitfield (type_with_fields, "LEG_IW_EN", 3, 3);
  tdesc_add_bitfield (type_with_fields, "NO_TRACK_EN", 4, 4);
  tdesc_add_bitfield (type_with_fields, "SUPPRESS_DIS", 5, 5);
  tdesc_add_bitfield (type_with_fields, "RSVD", 6, 9);
  tdesc_add_bitfield (type_with_fields, "SUPPRESS", 10, 10);
  tdesc_add_bitfield (type_with_fields, "TRACKER", 11, 11);
  tdesc_type *field_type;
  field_type = tdesc_named_type (feature, "data_ptr");
  tdesc_add_typed_bitfield (type_with_fields, "EB_LEG_BITMAP_BASE", 12, 63, field_type);

  tdesc_create_reg (feature, "cet_u", regnum++, 1, NULL, 64, "cet_u_setting");
  tdesc_create_reg (feature, "pl3_ssp", regnum++, 1, NULL, 64, "code_ptr");
  return regnum;
}
