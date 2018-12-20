ENTRY(main)

SECTIONS {
    . = 0x0;
    .text : {
        SORT_BY_ALIGNMENT*(.text) SORT_BY_ALIGNMENT*(.text.*)
    }
    . = ALIGN(0x1000);
    .rodata : {
        SORT_BY_ALIGNMENT*(.rodata) SORT_BY_ALIGNMENT*(.rodata.*)
    }
    .eh_frame : {
        SORT_BY_ALIGNMENT*(.eh_frame)
    }
    . = ALIGN(0x1000);
    .data : {
        SORT_BY_ALIGNMENT*(.data) SORT_BY_ALIGNMENT*(.data.*)
    }
    . = ALIGN(0x1000);
    .bss : {
        SORT_BY_ALIGNMENT*(.bss) SORT_BY_ALIGNMENT*(COMMON)
    }
    . = ALIGN(0x1000);
}
