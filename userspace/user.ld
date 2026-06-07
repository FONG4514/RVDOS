SECTIONS
{
    . = 0x88001000;
    .text : {
        *(.text.entry)
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss : {
        *(.bss .bss.*)
    }
}
