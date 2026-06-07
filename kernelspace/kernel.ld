OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80200000;

  .text : {
    *(.text.entry)
    *(.text .text.*)
    . = ALIGN(0x1000);
    PROVIDE(etext = .);
  }

  .fs_section : {
    . = ALIGN(0x1000);
    PROVIDE(fs_start = .);
    *(.fs_code)
    *(.fs_data)
    . = ALIGN(0x1000);
    PROVIDE(fs_end = .);
  }

  .rodata : {
    . = ALIGN(0x1000);
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
    . = ALIGN(0x1000);
    PROVIDE(erodata = .);
  }

  .data : {
    . = ALIGN(0x1000);
    *(.sdata .sdata.*)
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(0x1000);
    *(.sbss .sbss.*)
    *(.bss .bss.*)
  }

  . = ALIGN(0x1000);
  PROVIDE(end = .);
}
