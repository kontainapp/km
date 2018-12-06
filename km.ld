ENTRY(main)

SECTIONS {
    . = 0x0;
    .text :
    {
        *(.text)
        *(.text.*)
    }
    . = ALIGN(0x1000);
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    }
    .eh_frame :
    {
        *(.eh_frame)
    }
    . = ALIGN(0x1000);
    .data :
    {
        *(.data)
        *(.data.*)
    }
    . = ALIGN(0x1000);
    .bss :
    {
        *(.bss)
        *(COMMON)
    }
    . = ALIGN(0x1000);
}
