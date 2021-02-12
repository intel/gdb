/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020-2021 Free Software Foundation, Inc.

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

/* This source file was generated using the following command line:

     gcc -S -dA -g -O0 dw2-inline-external.c

   and then manually adding the DW_AT_external flag to the "func"
   function's debug info.  Search for the phrase "MANUALLY-ADDED" below.  */

	.file	"dw2-inline-external.c"
	.text
.Ltext0:
	.globl	main
	.type	main, @function
main:
.LFB1:
	.file 1 "dw2-inline-external.c"
	# dw2-inline-external.c:35
	.loc 1 35 0
	.cfi_startproc
# BLOCK 2 seq:0
# PRED: ENTRY (FALLTHRU)
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
.LBB4:
.LBB5:
	# dw2-inline-external.c:30
	.loc 1 30 0
	movl	$42, %eax
.LBE5:
.LBE4:
	# dw2-inline-external.c:36
	.loc 1 36 0
	movl	%eax, -4(%rbp)
	# dw2-inline-external.c:38
	.loc 1 38 0
	movl	$0, %eax
	# dw2-inline-external.c:39
	.loc 1 39 0
	popq	%rbp
	.cfi_def_cfa 7, 8
# SUCC: EXIT [100.0%] 
	ret
	.cfi_endproc
.LFE1:
	.size	main, .-main
.Letext0:
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.long	0x82	# Length of Compilation Unit Info
	.value	0x4	# DWARF version number
	.long	.Ldebug_abbrev0	# Offset Into Abbrev. Section
	.byte	0x8	# Pointer Size (in bytes)
	.uleb128 0x1	# (DIE (0xb) DW_TAG_compile_unit)
	.long	.LASF0	# DW_AT_producer: "GNU C11 7.5.0 -mtune=generic -march=x86-64 -g -O0 -fstack-protector-strong"
	.byte	0xc	# DW_AT_language
	.long	.LASF1	# DW_AT_name: "dw2-inline-external.c"
	.long	.LASF2	# DW_AT_comp_dir: "gdb.dwarf2"
	.quad	.Ltext0	# DW_AT_low_pc
	.quad	.Letext0-.Ltext0	# DW_AT_high_pc
	.long	.Ldebug_line0	# DW_AT_stmt_list
	.uleb128 0x2	# (DIE (0x2d) DW_TAG_subprogram)
			# DW_AT_external
	.long	.LASF3	# DW_AT_name: "main"
	.byte	0x1	# DW_AT_decl_file (dw2-inline-external.c)
	.byte	0x22	# DW_AT_decl_line
	.long	0x72	# DW_AT_type
	.quad	.LFB1	# DW_AT_low_pc
	.quad	.LFE1-.LFB1	# DW_AT_high_pc
	.uleb128 0x1	# DW_AT_frame_base
	.byte	0x9c	# DW_OP_call_frame_cfa
			# DW_AT_GNU_all_call_sites
	.long	0x72	# DW_AT_sibling
	.uleb128 0x3	# (DIE (0x4e) DW_TAG_variable)
	.ascii "a\0"	# DW_AT_name
	.byte	0x1	# DW_AT_decl_file (dw2-inline-external.c)
	.byte	0x24	# DW_AT_decl_line
	.long	0x72	# DW_AT_type
	.uleb128 0x2	# DW_AT_location
	.byte	0x91	# DW_OP_fbreg
	.sleb128 -20
	.uleb128 0x4	# (DIE (0x5a) DW_TAG_inlined_subroutine)
	.long	0x79	# DW_AT_abstract_origin
	.quad	.LBB4	# DW_AT_low_pc
	.quad	.LBE4-.LBB4	# DW_AT_high_pc
	.byte	0x1	# DW_AT_call_file (dw2-inline-external.c)
	.byte	0x24	# DW_AT_call_line
	.byte	0	# end of children of DIE 0x2d
	.uleb128 0x5	# (DIE (0x72) DW_TAG_base_type)
	.byte	0x4	# DW_AT_byte_size
	.byte	0x5	# DW_AT_encoding
	.ascii "int\0"	# DW_AT_name
	.uleb128 0x6	# (DIE (0x79) DW_TAG_subprogram)
	.long	.LASF4	# DW_AT_name: "func"
	.byte	0x1	# DW_AT_decl_file (dw2-inline-external.c)
	.byte	0x1c	# DW_AT_decl_line
	.long	0x72	# DW_AT_type
	.byte	0x3	# DW_AT_inline
	.byte	0	# end of children of DIE 0xb
	.section	.debug_abbrev,"",@progbits
.Ldebug_abbrev0:
	.uleb128 0x1	# (abbrev code)
	.uleb128 0x11	# (TAG: DW_TAG_compile_unit)
	.byte	0x1	# DW_children_yes
	.uleb128 0x25	# (DW_AT_producer)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x13	# (DW_AT_language)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x1b	# (DW_AT_comp_dir)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x11	# (DW_AT_low_pc)
	.uleb128 0x1	# (DW_FORM_addr)
	.uleb128 0x12	# (DW_AT_high_pc)
	.uleb128 0x7	# (DW_FORM_data8)
	.uleb128 0x10	# (DW_AT_stmt_list)
	.uleb128 0x17	# (DW_FORM_sec_offset)
	.byte	0
	.byte	0
	.uleb128 0x2	# (abbrev code)
	.uleb128 0x2e	# (TAG: DW_TAG_subprogram)
	.byte	0x1	# DW_children_yes
	.uleb128 0x3f	# (DW_AT_external)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x11	# (DW_AT_low_pc)
	.uleb128 0x1	# (DW_FORM_addr)
	.uleb128 0x12	# (DW_AT_high_pc)
	.uleb128 0x7	# (DW_FORM_data8)
	.uleb128 0x40	# (DW_AT_frame_base)
	.uleb128 0x18	# (DW_FORM_exprloc)
	.uleb128 0x2117	# (DW_AT_GNU_all_call_sites)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.uleb128 0x1	# (DW_AT_sibling)
	.uleb128 0x13	# (DW_FORM_ref4)
	.byte	0
	.byte	0
	.uleb128 0x3	# (abbrev code)
	.uleb128 0x34	# (TAG: DW_TAG_variable)
	.byte	0	# DW_children_no
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0x8	# (DW_FORM_string)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x2	# (DW_AT_location)
	.uleb128 0x18	# (DW_FORM_exprloc)
	.byte	0
	.byte	0
	.uleb128 0x4	# (abbrev code)
	.uleb128 0x1d	# (TAG: DW_TAG_inlined_subroutine)
	.byte	0	# DW_children_no
	.uleb128 0x31	# (DW_AT_abstract_origin)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x11	# (DW_AT_low_pc)
	.uleb128 0x1	# (DW_FORM_addr)
	.uleb128 0x12	# (DW_AT_high_pc)
	.uleb128 0x7	# (DW_FORM_data8)
	.uleb128 0x58	# (DW_AT_call_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x59	# (DW_AT_call_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.byte	0
	.byte	0
	.uleb128 0x5	# (abbrev code)
	.uleb128 0x24	# (TAG: DW_TAG_base_type)
	.byte	0	# DW_children_no
	.uleb128 0xb	# (DW_AT_byte_size)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3e	# (DW_AT_encoding)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0x8	# (DW_FORM_string)
	.byte	0
	.byte	0
	.uleb128 0x6	# (abbrev code)
	.uleb128 0x2e	# (TAG: DW_TAG_subprogram)
	.byte	0	# DW_children_no
	# MANUALLY-ADDED ATTRIBUTE:
	.uleb128 0x3f	# (DW_AT_external)
	.uleb128 0x19	# (DW_FORM_flag_present)
	# END OF MANUALLY-ADDED ATTRIBUTE.
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x20	# (DW_AT_inline)
	.uleb128 0xb	# (DW_FORM_data1)
	.byte	0
	.byte	0
	.byte	0
	.section	.debug_aranges,"",@progbits
	.long	0x2c	# Length of Address Ranges Info
	.value	0x2	# DWARF Version
	.long	.Ldebug_info0	# Offset of Compilation Unit Info
	.byte	0x8	# Size of Address
	.byte	0	# Size of Segment Descriptor
	.value	0	# Pad to 16 byte boundary
	.value	0
	.quad	.Ltext0	# Address
	.quad	.Letext0-.Ltext0	# Length
	.quad	0
	.quad	0
	.section	.debug_line,"",@progbits
.Ldebug_line0:
	.section	.debug_str,"MS",@progbits,1
.LASF0:
	.string	"GNU C11 7.5.0 -mtune=generic -march=x86-64 -g -O0 -fstack-protector-strong"
.LASF2:
	.string	"gdb.dwarf2"
.LASF4:
	.string	"func"
.LASF1:
	.string	"dw2-inline-external.c"
.LASF3:
	.string	"main"
	.ident	"GCC: (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0"
	.section	.note.GNU-stack,"",@progbits
