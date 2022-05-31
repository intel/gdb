/* Copyright 2022 Free Software Foundation, Inc.

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

/* This test relies on inlined_trampoline being inlined into main and the other
   functions not.  All functions except target will be marked via
   DW_AT_trampoline in the debug info and we'll check whether one can step
   through the trampolines towards target.  */
volatile int global_var;

int __attribute__ ((noinline))
target ()					/* target decl line */
{						/* target prologue */
  asm ("target_label: .globl target_label");
  ++global_var;					/* target add */
  asm ("target_label2: .globl target_label2");
  return 9 + 10;				/* target return */
}						/* target end */

int __attribute__ ((noinline))
trampoline ()
{						/* trampoline prologue */
  asm ("trampoline_label: .globl trampoline_label");
  ++global_var;
  return target ();				/* trampoline target call */
}						/* trampoline end */

static inline int __attribute__ ((always_inline))
inlined_trampoline ()
{						/* inlined_trampoline prologue */
  asm ("inlined_trampoline_label: .globl inlined_trampoline_label");
  ++global_var;					/* inlined_trampoline add */
  asm ("inlined_trampoline_label2: .globl inlined_trampoline_label2");
  return target ();				/* inlined_trampoline target call */
}						/* inlined_trampoline end */

int __attribute__ ((noinline))
chained_trampoline ()
{						/* chained_trampoline prologue */
  asm ("chained_trampoline_label: .globl chained_trampoline_label");
  ++global_var;
  return trampoline ();				/* chained_trampoline trampoline call */
}						/* chained_trampoline end */

int __attribute__ ((noinline))
doubly_chained_trampoline ()
{						/* doubly_chained_trampoline prologue */
  asm ("doubly_chained_trampoline_label: .globl doubly_chained_trampoline_label");
  ++global_var;
  return chained_trampoline ();			/* doubly_chained_trampoline chained_trampoline call */
}						/* doubly_chained_trampoline end */

int
main ()						/* main decl line */
{						/* main prologue */
  int ans;
  asm ("main_label: .globl main_label");
  global_var = 0;				/* main set global_var */
  asm ("main_label2: .globl main_label2");
  ans = inlined_trampoline ();			/* main call inlined_trampoline */
  asm ("main_label3: .globl main_label3");
  ans = trampoline ();				/* main call trampoline */
  asm ("main_label4: .globl main_label4");
  ans = chained_trampoline ();			/* main call chained_trampoline */
  asm ("main_label5: .globl main_label5");
  ans = doubly_chained_trampoline ();		/* main call doubly_chained_trampoline */
  asm ("main_label6: .globl main_label6");
  return ans;					/* main call return */
}						/* main end */
