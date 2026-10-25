/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

.section .text.start
.globl _start
_start:
   fsa.pri.reset
   csrr t0, mscratch
   csrr t1, mhartid
   lwu t2, 0x34(t0)
   bgeu t1, t2, 1f
   addi t1, t1, 1
   lwu t2, 0x50(t0)
   mul t1, t1, t2
   ld sp, 0x48(t0)
   add sp, sp, t1
   call main
1:
   ecall
   j 1b
