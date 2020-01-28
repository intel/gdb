/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020 Free Software Foundation, Inc.

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

	.file	"amd64-simd-sse.c"
	.text
    .globl Ltext
Ltext:
	.p2align 4
	.globl	test
	.type	test, @function
test:
.LFB0:
	.cfi_startproc
    .globl l28_1
l28_1:
	testl	%esi, %esi
	jle	.L2
	leal	-1(%rsi), %eax
	cmpl	$2, %eax
	jbe	.L7
	movl	%esi, %edx
	movdqa	.LC0(%rip), %xmm3
	movq	%rdi, %rax
	shrl	$2, %edx
	salq	$5, %rdx
	addq	%rdi, %rdx
	.p2align 4,,10
	.p2align 3
.L4:
    .globl l30_1
l30_1:
	movdqu	(%rax), %xmm2
	movdqu	16(%rax), %xmm4
	addq	$32, %rax
    .globl l30_2
l30_2:
	movdqa	%xmm2, %xmm1
	shufps	$221, %xmm4, %xmm2
	shufps	$136, %xmm4, %xmm1
    .globl l30_3
l30_3:
	pslld	$2, %xmm1
    .globl l31_1
l31_1:
	movdqa	%xmm1, %xmm0
	psrlq	$32, %xmm1
    .globl l31_2
l31_2:
	pmuludq	%xmm2, %xmm0
	psrlq	$32, %xmm2
	pmuludq	%xmm2, %xmm1
	pshufd	$8, %xmm0, %xmm0
	pshufd	$8, %xmm1, %xmm1
	punpckldq	%xmm1, %xmm0
    .globl l32_1
l32_1:
	paddd	%xmm3, %xmm0
	pshufd	$85, %xmm0, %xmm1
	movd	%xmm0, -32(%rax)
	movd	%xmm1, -24(%rax)
	movdqa	%xmm0, %xmm1
	punpckhdq	%xmm0, %xmm1
    .globl l32_2
l32_2:
	pshufd	$255, %xmm0, %xmm0
	movd	%xmm1, -16(%rax)
	movd	%xmm0, -8(%rax)
    .globl l28_2
l28_2:
	cmpq	%rdx, %rax
	jne	.L4
    .globl l28_3
l28_3:
	movl	%esi, %edx
	andl	$-4, %edx
	testb	$3, %sil
	je	.L2
.L3:
    .globl l28_4
l28_4:
	movslq	%edx, %rdx
	.p2align 4,,10
	.p2align 3
.L6:
    .globl l30_4
l30_4:
	movl	(%rdi,%rdx,8), %eax
    .globl l30_5
l30_5:
	sall	$2, %eax
    .globl l31_3
l31_3:
	imull	4(%rdi,%rdx,8), %eax
    .globl l32_3
l32_3:
	subl	$42, %eax
	movl	%eax, (%rdi,%rdx,8)
    .globl l28_5
l28_5:
	addq	$1, %rdx
	cmpl	%edx, %esi
	jg	.L6
.L2:
    .globl l35
l35:
	xorl	%eax, %eax
	ret
.L7:
    .globl l28_6
l28_6:
	xorl	%edx, %edx
	jmp	.L3
	.cfi_endproc
    .globl LFE0
LFE0:
	.size	test, .-test
	.section	.rodata.cst16,"aM",@progbits,16
	.align 16
.LC0:
	.long	-42
	.long	-42
	.long	-42
	.long	-42
	.ident	"GCC: (GNU) 9.2.1 20190827 (Red Hat 9.2.1-1)"
    .globl Letext
Letext:
	.section	.note.GNU-stack,"",@progbits
