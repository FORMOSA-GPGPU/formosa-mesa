  .section        .text.start
  .globl          _start

_start:
  li              x1, 0
  li              x2, 0
  li              x3, 0
  li              x4, 0
  li              x5, 0
  li              x6, 0
  li              x7, 0
  li              x8, 0
  li              x9, 0
  li              x10, 0
  li              x11, 0
  li              x12, 0
  li              x13, 0
  li              x14, 0
  li              x15, 0
  li              x16, 0
  li              x17, 0
  li              x18, 0
  li              x19, 0
  li              x20, 0
  li              x21, 0
  li              x22, 0
  li              x23, 0
  li              x24, 0
  li              x25, 0
  li              x26, 0
  li              x27, 0
  li              x28, 0
  li              x29, 0
  li              x30, 0
  li              x31, 0

  csrr            t0, mhartid
# Init stack
  la              t1, _stack_start
  slli            sp, t0, 12       # stack size = 0x1000 = shift 12
  sub             sp, t1, sp
  fsa.pri.raise.n 1
  bnez            t0, 2f
# Init bss
  la              t0, _bss_start
  la              t1, _bss_end
  bge             t0, t1, 2f
1:
  sd              zero, 0(t0)
  addi            t0, t0, 8
  blt             t0, t1, 1b
2:
  fsa.pri.lower.n 1
  fsa.bar         0, 0

# Enter shader main
  call            main

# End
  fsa.bar         0, 0
  ecall
1:
  j               1b