# Spike htif required
  .section .htif, "aw", @progbits
  .globl   tohost
  .globl   fromhost
  .align   6
tohost:
  .dword   0
fromhost:
  .dword   0

  .section .text
  .globl   _start
_start:
  li       x1, 0
  li       x2, 0
  li       x3, 0
  li       x4, 0
  li       x5, 0
  li       x6, 0
  li       x7, 0
  li       x8, 0
  li       x9, 0
  li       x10, 0
  li       x11, 0
  li       x12, 0
  li       x13, 0
  li       x14, 0
  li       x15, 0
  li       x16, 0
  li       x17, 0
  li       x18, 0
  li       x19, 0
  li       x20, 0
  li       x21, 0
  li       x22, 0
  li       x23, 0
  li       x24, 0
  li       x25, 0
  li       x26, 0
  li       x27, 0
  li       x28, 0
  li       x29, 0
  li       x30, 0
  li       x31, 0
# Prepare C env
  la       sp, _stack_top
# Turn on FPU
  li       t0, 0x6000
  csrs     mstatus, t0
# Jump to test entry
  call     main
  la       t0, tohost
  li       t1, 1
  sd       t1, 0(t0)
hang:
  j        hang