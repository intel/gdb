/* THIS FILE IS GENERATED.  -*- buffer-read-only: t -*- vi:set ro:
  Original: 64bit-amx.xml */

#include "gdbsupport/tdesc.h"

static int
create_feature_i386_64bit_amx (struct target_desc *result, long regnum)
{
  struct tdesc_feature *feature;

  feature = tdesc_create_feature (result, "org.gnu.gdb.i386.amx");
  tdesc_type *element_type;
  element_type = tdesc_named_type (feature, "int8");
  tdesc_create_vector (feature, "v_i8", element_type, 4);

  element_type = tdesc_named_type (feature, "v_i8");
  tdesc_create_vector (feature, "matrix_i8", element_type, 64);

  element_type = tdesc_named_type (feature, "int32");
  tdesc_create_vector (feature, "v_i32", element_type, 16);

  element_type = tdesc_named_type (feature, "v_i32");
  tdesc_create_vector (feature, "matrix_i32", element_type, 16);

  element_type = tdesc_named_type (feature, "bfloat16");
  tdesc_create_vector (feature, "v_bf16", element_type, 2);

  element_type = tdesc_named_type (feature, "v_bf16");
  tdesc_create_vector (feature, "matrix_bf16", element_type, 32);

  tdesc_type_with_fields *type_with_fields;
  type_with_fields = tdesc_create_union (feature, "tile");
  tdesc_type *field_type;
  field_type = tdesc_named_type (feature, "matrix_i8");
  tdesc_add_field (type_with_fields, "m_int8", field_type);
  field_type = tdesc_named_type (feature, "matrix_i32");
  tdesc_add_field (type_with_fields, "m_int32", field_type);
  field_type = tdesc_named_type (feature, "matrix_bf16");
  tdesc_add_field (type_with_fields, "m_bf16", field_type);

  tdesc_create_reg (feature, "tilecfg", regnum++, 1, NULL, 512, "uint512");
  tdesc_create_reg (feature, "tmm0", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm1", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm2", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm3", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm4", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm5", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm6", regnum++, 1, NULL, 8192, "tile");
  tdesc_create_reg (feature, "tmm7", regnum++, 1, NULL, 8192, "tile");
  return regnum;
}
