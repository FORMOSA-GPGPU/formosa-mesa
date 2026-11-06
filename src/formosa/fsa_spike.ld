OUTPUT_ARCH("riscv")
MEMORY
{
  MEM (rwx) : ORIGIN = 0x1000, LENGTH = 4M
}

ENTRY(_start)
SECTIONS
{
  .text :
  {
    *(.text*)
    *(.rodata*)
  } > MEM

  .htif : { *(.htif) } > MEM

  .data :
  {
    . = ALIGN(8);
    _data_start = .;
    *(.data*)
    _data_end = .;
  } > MEM

  .bss :
  {
    . = ALIGN(8);
    _bss_start = .;
    *(.bss*)
    *(COMMON)
    _bss_end = .;
  } > MEM

  _stack_top = ORIGIN(MEM) + LENGTH(MEM);
}