/* This testcase is part of GDB, the GNU debugger.

   Copyright 2025 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See theg
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#define SYMBOL(sym) #sym

void __attribute__ ((noinline))
prologue_standard_fp (void);

void __attribute__ ((noinline))
prologue_no_locals_fp (void);

void __attribute__ ((noinline))
prologue_no_callee_regs_push_fp (void);

void __attribute__ ((noinline))
prologue_standard_sp (void);

void __attribute__ ((noinline))
prologue_no_locals_sp (void);

void __attribute__ ((noinline))
prologue_no_callee_regs_push_sp (void);

int
main (void)
{
  prologue_standard_fp ();
  prologue_no_locals_fp ();
  prologue_no_callee_regs_push_fp ();

  prologue_standard_sp ();
  prologue_no_locals_sp ();
  prologue_no_callee_regs_push_sp ();

  return 0;
}

/* BP based frame functions.  */
asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_standard_fp) ":\n"
     "	push %rbp\n"
     "	mov %rsp, %rbp\n"
     "	push %r12\n"
     "	sub $16, %rsp\n"
     "	nop\n"
     "	add $16, %rsp\n"
     "	pop %r12\n"
     "	leave\n"
     "	ret\n");

asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_no_locals_fp) ":\n"
     "	push %rbp\n"
     "	mov %rsp, %rbp\n"
     "	push %r12\n"
     "	nop\n"
     "	pop %r12\n"
     "	leave\n"
     "	ret\n");

asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_no_callee_regs_push_fp) ":\n"
     "	push %rbp\n"
     "	mov %rsp, %rbp\n"
     "	sub $8, %rsp\n"
     "	nop\n"
     "	add $8, %rsp\n"
     "	leave\n"
     "	ret\n");

/* SP based frame functions.  */
asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_standard_sp) ":\n"
     "	.cfi_startproc\n"
     "	push %r12\n"
     "	.cfi_def_cfa_offset 16\n"
     "	.cfi_offset 12, -16\n"
     "	sub $8, %rsp\n"
     "	.cfi_def_cfa_offset 24\n"
     "	nop\n"
     "	add $8, %rsp\n"
     "	.cfi_def_cfa_offset 16\n"
     "	pop %r12\n"
     "	.cfi_def_cfa_offset 8\n"
     "	ret\n"
     "	.cfi_endproc\n");

asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_no_locals_sp) ":\n"
     "	.cfi_startproc\n"
     "	push %r12\n"
     "	.cfi_def_cfa_offset 16\n"
     "	.cfi_offset 12, -16\n"
     "	nop\n"
     "	pop %r12\n"
     "	.cfi_def_cfa_offset 8\n"
     "	ret\n"
     "	.cfi_endproc\n");

asm (".text\n"
     "	.align 8\n"
     SYMBOL (prologue_no_callee_regs_push_sp) ":\n"
     "	.cfi_startproc\n"
     "	sub $8, %rsp\n"
     "	.cfi_def_cfa_offset 16\n"
     "	nop\n"
     "	add $8, %rsp\n"
     "	.cfi_def_cfa_offset 8\n"
     "	ret\n"
     "	.cfi_endproc\n");
