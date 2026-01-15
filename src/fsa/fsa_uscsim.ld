OUTPUT_ARCH(riscv)

MEMORY
{
  MEM0 (rwx): ORIGIN = 0x00000000, LENGTH = 128M
  MEM1 (rwx): ORIGIN = ORIGIN(MEM0) + LENGTH(MEM0), LENGTH = 128M
}

ENTRY(_start)

SECTIONS {

  .text : {
    _start = .;
    KEEP(*(.text.start))
    *(.text)
    *(.text.*)
    . = ALIGN(8);
  } > MEM0

  .rodata : {
    . = ALIGN(8);
    /* Small RO data before large RO data */
    *(.srodata)
    *(.srodata.*)
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(8);
  } > MEM0

  .data : {
    . = ALIGN(8);
    /* Small data before large data */
    *(.sdata)
    *(.sdata.*)
    *(.data);
    *(.data.*)
    . = ALIGN(8);
  } > MEM0

  .bss : {
    . = ALIGN(8);
    _bss_start = .;
    /* Small BSS before large BSS */
    *(.sbss)
    *(.sbss.*)
    *(.bss)
    *(.bss.*)
    *(COMMON)
    _bss_end = .;
    . = ALIGN(8);
  } > MEM0

  .stack (NOLOAD) : {
    . += 0x1000 * 64;
    _stack_end = .;
    /* 4K * threads_per_core */
    . += 0x1000 * 64;
    _stack_start = .;
  } > MEM0

}