/* This testcase is part of GDB, the GNU debugger.

   Copyright 2018-2021 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


   This file has been generated using gcc version 8.1.1 20180502:
   gcc -S -dA -g ptwrite.c -o x86_64-ptwrite.S.  */

	.file	"ptwrite.c"
	.text
.Ltext0:
	.globl	ptwrite64
	.type	ptwrite64, @function
ptwrite64:
.LFB0:
	.file 1 "ptwrite.c"
	# ptwrite.c:22:1
	.loc 1 22 1
	.cfi_startproc
# BLOCK 2 seq:0
# PRED: ENTRY (FALLTHRU)
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	.cfi_offset 3, -24
	movq	%rdi, -16(%rbp)
	# ptwrite.c:23:3
	.loc 1 23 3
	movq	-16(%rbp), %rax
	movq	%rax, %rbx
#APP
# 23 "ptwrite.c" 1
	PTWRITE %rbx;
# 0 "" 2
	# ptwrite.c:24:1
	.loc 1 24 1
#NO_APP
	nop
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
# SUCC: EXIT [always] 
	ret
	.cfi_endproc
.LFE0:
	.size	ptwrite64, .-ptwrite64
	.globl	ptwrite32
	.type	ptwrite32, @function
ptwrite32:
.LFB1:
	# ptwrite.c:28:1
	.loc 1 28 1
	.cfi_startproc
# BLOCK 2 seq:0
# PRED: ENTRY (FALLTHRU)
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	.cfi_offset 3, -24
	movl	%edi, -12(%rbp)
	# ptwrite.c:29:3
	.loc 1 29 3
	movl	-12(%rbp), %eax
	movl	%eax, %ebx
#APP
# 29 "ptwrite.c" 1
	PTWRITE %ebx;
# 0 "" 2
	# ptwrite.c:30:1
	.loc 1 30 1
#NO_APP
	nop
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
# SUCC: EXIT [always] 
	ret
	.cfi_endproc
.LFE1:
	.size	ptwrite32, .-ptwrite32
	.globl	main
	.type	main, @function
main:
.LFB2:
	# ptwrite.c:34:1
	.loc 1 34 1
	.cfi_startproc
# BLOCK 2 seq:0
# PRED: ENTRY (FALLTHRU)
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	# ptwrite.c:36:3
	.loc 1 36 3
	movl	$66, %edi
	call	ptwrite64
	# ptwrite.c:37:3
	.loc 1 37 3
	movl	$67, %edi
	call	ptwrite32
	# ptwrite.c:39:10
	.loc 1 39 10
	movl	$0, %eax
	# ptwrite.c:40:1
	.loc 1 40 1
	popq	%rbp
	.cfi_def_cfa 7, 8
# SUCC: EXIT [always] 
	ret
	.cfi_endproc
.LFE2:
	.size	main, .-main
.Letext0:
	.file 2 "/usr/include/bits/types.h"
	.file 3 "/usr/include/bits/stdint-uintn.h"
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.long	0x10f	# Length of Compilation Unit Info
	.value	0x4	# DWARF version number
	.long	.Ldebug_abbrev0	# Offset Into Abbrev. Section
	.byte	0x8	# Pointer Size (in bytes)
	.uleb128 0x1	# (DIE (0xb) DW_TAG_compile_unit)
	.long	.LASF13	# DW_AT_producer: "GNU C17 8.1.1 20180502 (Red Hat 8.1.1-1) -mtune=generic -march=x86-64 -g"
	.byte	0xc	# DW_AT_language
	.long	.LASF14	# DW_AT_name: "ptwrite.c"
	.long	.LASF15	# DW_AT_comp_dir: "gdb/gdb/testsuite/gdb.btrace"
	.quad	.Ltext0	# DW_AT_low_pc
	.quad	.Letext0-.Ltext0	# DW_AT_high_pc
	.long	.Ldebug_line0	# DW_AT_stmt_list
	.uleb128 0x2	# (DIE (0x2d) DW_TAG_base_type)
	.byte	0x1	# DW_AT_byte_size
	.byte	0x8	# DW_AT_encoding
	.long	.LASF0	# DW_AT_name: "unsigned char"
	.uleb128 0x2	# (DIE (0x34) DW_TAG_base_type)
	.byte	0x2	# DW_AT_byte_size
	.byte	0x7	# DW_AT_encoding
	.long	.LASF1	# DW_AT_name: "short unsigned int"
	.uleb128 0x2	# (DIE (0x3b) DW_TAG_base_type)
	.byte	0x4	# DW_AT_byte_size
	.byte	0x7	# DW_AT_encoding
	.long	.LASF2	# DW_AT_name: "unsigned int"
	.uleb128 0x2	# (DIE (0x42) DW_TAG_base_type)
	.byte	0x8	# DW_AT_byte_size
	.byte	0x7	# DW_AT_encoding
	.long	.LASF3	# DW_AT_name: "long unsigned int"
	.uleb128 0x2	# (DIE (0x49) DW_TAG_base_type)
	.byte	0x1	# DW_AT_byte_size
	.byte	0x6	# DW_AT_encoding
	.long	.LASF4	# DW_AT_name: "signed char"
	.uleb128 0x2	# (DIE (0x50) DW_TAG_base_type)
	.byte	0x2	# DW_AT_byte_size
	.byte	0x5	# DW_AT_encoding
	.long	.LASF5	# DW_AT_name: "short int"
	.uleb128 0x3	# (DIE (0x57) DW_TAG_base_type)
	.byte	0x4	# DW_AT_byte_size
	.byte	0x5	# DW_AT_encoding
	.ascii "int\0"	# DW_AT_name
	.uleb128 0x4	# (DIE (0x5e) DW_TAG_typedef)
	.long	.LASF7	# DW_AT_name: "__uint32_t"
	.byte	0x2	# DW_AT_decl_file (/usr/include/bits/types.h)
	.byte	0x29	# DW_AT_decl_line
	.byte	0x16	# DW_AT_decl_column
	.long	0x3b	# DW_AT_type
	.uleb128 0x2	# (DIE (0x6a) DW_TAG_base_type)
	.byte	0x8	# DW_AT_byte_size
	.byte	0x5	# DW_AT_encoding
	.long	.LASF6	# DW_AT_name: "long int"
	.uleb128 0x4	# (DIE (0x71) DW_TAG_typedef)
	.long	.LASF8	# DW_AT_name: "__uint64_t"
	.byte	0x2	# DW_AT_decl_file (/usr/include/bits/types.h)
	.byte	0x2c	# DW_AT_decl_line
	.byte	0x1b	# DW_AT_decl_column
	.long	0x42	# DW_AT_type
	.uleb128 0x2	# (DIE (0x7d) DW_TAG_base_type)
	.byte	0x1	# DW_AT_byte_size
	.byte	0x6	# DW_AT_encoding
	.long	.LASF9	# DW_AT_name: "char"
	.uleb128 0x4	# (DIE (0x84) DW_TAG_typedef)
	.long	.LASF10	# DW_AT_name: "uint32_t"
	.byte	0x3	# DW_AT_decl_file (/usr/include/bits/stdint-uintn.h)
	.byte	0x1a	# DW_AT_decl_line
	.byte	0x14	# DW_AT_decl_column
	.long	0x5e	# DW_AT_type
	.uleb128 0x4	# (DIE (0x90) DW_TAG_typedef)
	.long	.LASF11	# DW_AT_name: "uint64_t"
	.byte	0x3	# DW_AT_decl_file (/usr/include/bits/stdint-uintn.h)
	.byte	0x1b	# DW_AT_decl_line
	.byte	0x14	# DW_AT_decl_column
	.long	0x71	# DW_AT_type
	.uleb128 0x5	# (DIE (0x9c) DW_TAG_subprogram)
			# DW_AT_external
	.long	.LASF16	# DW_AT_name: "main"
	.byte	0x1	# DW_AT_decl_file (ptwrite.c)
	.byte	0x21	# DW_AT_decl_line
	.byte	0x1	# DW_AT_decl_column
			# DW_AT_prototyped
	.long	0x57	# DW_AT_type
	.quad	.LFB2	# DW_AT_low_pc
	.quad	.LFE2-.LFB2	# DW_AT_high_pc
	.uleb128 0x1	# DW_AT_frame_base
	.byte	0x9c	# DW_OP_call_frame_cfa
			# DW_AT_GNU_all_tail_call_sites
	.uleb128 0x6	# (DIE (0xba) DW_TAG_subprogram)
			# DW_AT_external
	.long	.LASF17	# DW_AT_name: "ptwrite32"
	.byte	0x1	# DW_AT_decl_file (ptwrite.c)
	.byte	0x1b	# DW_AT_decl_line
	.byte	0x1	# DW_AT_decl_column
			# DW_AT_prototyped
	.quad	.LFB1	# DW_AT_low_pc
	.quad	.LFE1-.LFB1	# DW_AT_high_pc
	.uleb128 0x1	# DW_AT_frame_base
	.byte	0x9c	# DW_OP_call_frame_cfa
			# DW_AT_GNU_all_call_sites
	.long	0xe8	# DW_AT_sibling
	.uleb128 0x7	# (DIE (0xd8) DW_TAG_formal_parameter)
	.long	.LASF12	# DW_AT_name: "value"
	.byte	0x1	# DW_AT_decl_file (ptwrite.c)
	.byte	0x1b	# DW_AT_decl_line
	.byte	0x15	# DW_AT_decl_column
	.long	0x84	# DW_AT_type
	.uleb128 0x2	# DW_AT_location
	.byte	0x91	# DW_OP_fbreg
	.sleb128 -28
	.byte	0	# end of children of DIE 0xba
	.uleb128 0x8	# (DIE (0xe8) DW_TAG_subprogram)
			# DW_AT_external
	.long	.LASF18	# DW_AT_name: "ptwrite64"
	.byte	0x1	# DW_AT_decl_file (ptwrite.c)
	.byte	0x15	# DW_AT_decl_line
	.byte	0x1	# DW_AT_decl_column
			# DW_AT_prototyped
	.quad	.LFB0	# DW_AT_low_pc
	.quad	.LFE0-.LFB0	# DW_AT_high_pc
	.uleb128 0x1	# DW_AT_frame_base
	.byte	0x9c	# DW_OP_call_frame_cfa
			# DW_AT_GNU_all_call_sites
	.uleb128 0x7	# (DIE (0x102) DW_TAG_formal_parameter)
	.long	.LASF12	# DW_AT_name: "value"
	.byte	0x1	# DW_AT_decl_file (ptwrite.c)
	.byte	0x15	# DW_AT_decl_line
	.byte	0x15	# DW_AT_decl_column
	.long	0x90	# DW_AT_type
	.uleb128 0x2	# DW_AT_location
	.byte	0x91	# DW_OP_fbreg
	.sleb128 -32
	.byte	0	# end of children of DIE 0xe8
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
	.uleb128 0x24	# (TAG: DW_TAG_base_type)
	.byte	0	# DW_children_no
	.uleb128 0xb	# (DW_AT_byte_size)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3e	# (DW_AT_encoding)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.byte	0
	.byte	0
	.uleb128 0x3	# (abbrev code)
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
	.uleb128 0x4	# (abbrev code)
	.uleb128 0x16	# (TAG: DW_TAG_typedef)
	.byte	0	# DW_children_no
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x39	# (DW_AT_decl_column)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.byte	0
	.byte	0
	.uleb128 0x5	# (abbrev code)
	.uleb128 0x2e	# (TAG: DW_TAG_subprogram)
	.byte	0	# DW_children_no
	.uleb128 0x3f	# (DW_AT_external)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x39	# (DW_AT_decl_column)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x27	# (DW_AT_prototyped)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x11	# (DW_AT_low_pc)
	.uleb128 0x1	# (DW_FORM_addr)
	.uleb128 0x12	# (DW_AT_high_pc)
	.uleb128 0x7	# (DW_FORM_data8)
	.uleb128 0x40	# (DW_AT_frame_base)
	.uleb128 0x18	# (DW_FORM_exprloc)
	.uleb128 0x2116	# (DW_AT_GNU_all_tail_call_sites)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.byte	0
	.byte	0
	.uleb128 0x6	# (abbrev code)
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
	.uleb128 0x39	# (DW_AT_decl_column)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x27	# (DW_AT_prototyped)
	.uleb128 0x19	# (DW_FORM_flag_present)
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
	.uleb128 0x7	# (abbrev code)
	.uleb128 0x5	# (TAG: DW_TAG_formal_parameter)
	.byte	0	# DW_children_no
	.uleb128 0x3	# (DW_AT_name)
	.uleb128 0xe	# (DW_FORM_strp)
	.uleb128 0x3a	# (DW_AT_decl_file)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x3b	# (DW_AT_decl_line)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x39	# (DW_AT_decl_column)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x49	# (DW_AT_type)
	.uleb128 0x13	# (DW_FORM_ref4)
	.uleb128 0x2	# (DW_AT_location)
	.uleb128 0x18	# (DW_FORM_exprloc)
	.byte	0
	.byte	0
	.uleb128 0x8	# (abbrev code)
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
	.uleb128 0x39	# (DW_AT_decl_column)
	.uleb128 0xb	# (DW_FORM_data1)
	.uleb128 0x27	# (DW_AT_prototyped)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.uleb128 0x11	# (DW_AT_low_pc)
	.uleb128 0x1	# (DW_FORM_addr)
	.uleb128 0x12	# (DW_AT_high_pc)
	.uleb128 0x7	# (DW_FORM_data8)
	.uleb128 0x40	# (DW_AT_frame_base)
	.uleb128 0x18	# (DW_FORM_exprloc)
	.uleb128 0x2117	# (DW_AT_GNU_all_call_sites)
	.uleb128 0x19	# (DW_FORM_flag_present)
	.byte	0
	.byte	0
	.byte	0
	.section	.debug_aranges,"",@progbits
	.long	0x2c	# Length of Address Ranges Info
	.value	0x2	# DWARF aranges version
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
.LASF2:
	.string	"unsigned int"
.LASF7:
	.string	"__uint32_t"
.LASF15:
	.string	"gdb/gdb/testsuite/gdb.btrace"
.LASF14:
	.string	"ptwrite.c"
.LASF18:
	.string	"ptwrite64"
.LASF3:
	.string	"long unsigned int"
.LASF11:
	.string	"uint64_t"
.LASF0:
	.string	"unsigned char"
.LASF16:
	.string	"main"
.LASF10:
	.string	"uint32_t"
.LASF6:
	.string	"long int"
.LASF8:
	.string	"__uint64_t"
.LASF13:
	.string	"GNU C17 8.1.1 20180502 (Red Hat 8.1.1-1) -mtune=generic -march=x86-64 -g"
.LASF1:
	.string	"short unsigned int"
.LASF4:
	.string	"signed char"
.LASF12:
	.string	"value"
.LASF5:
	.string	"short int"
.LASF17:
	.string	"ptwrite32"
.LASF9:
	.string	"char"
	.ident	"GCC: (GNU) 8.1.1 20180502 (Red Hat 8.1.1-1)"
	.section	.note.GNU-stack,"",@progbits
