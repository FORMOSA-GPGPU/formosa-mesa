OUTPUT_ARCH(riscv)

MEMORY
{
  global_mem (rwx): ORIGIN = 0x00000000, LENGTH = 512M
}

ENTRY(_start)

SECTIONS {

.text : {
  . = ALIGN(8);
  _start = .;
  KEEP(*(.text.start))
  *(.text)
  *(.text.*)
  . = ALIGN(8);
} > global_mem

.rodata : {
  . = ALIGN(8);
  /* Small RO data before large RO data */
  *(.srodata)
  *(.srodata.*)
  *(.rodata)
  *(.rodata.*)
  . = ALIGN(8);
} > global_mem

.data : {
  . = ALIGN(8);
  /* Small data before large data */
  *(.sdata)
  *(.sdata.*)
  *(.data);
  *(.data.*)
  . = ALIGN(8);
} > global_mem

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
} > global_mem

_stack_top = 0x20000000;
_thread_num = 64;
_stack_size = 0x100 * _thread_num;

.stack (_stack_top - _stack_size) (NOLOAD) : {
  . = ALIGN(8);
  _stack_end = .;
  . += _stack_size;
  _stack_start = .;
} > global_mem

}