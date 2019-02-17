EXTERN(__start_c__)
ENTRY(__start_c__)
/*
 * Sections names .text.* .rodata.* .data.* are produced by gcc with -ffunction-sections -fdata-sections
 * This works together with SORT_BY_ALIGNMENT directive to minimize alignment gaps
 * ld --gc-sections drops unused/unreferences sections
 */
 
SECTIONS {
    . = 0x200000; /* 2MB */
    .text : {
        *(SORT_BY_ALIGNMENT(.text*))
    }
    . = ALIGN(0x1000);
    .rodata : {
        *(SORT_BY_ALIGNMENT(.rodata*))
    }
    .eh_frame : {
        *(.eh_frame)
    }
    . = ALIGN(0x1000);
    .data : {
        *(SORT_BY_ALIGNMENT(.data*))
    }
    .bss : {
        *(SORT_BY_ALIGNMENT(.bss*)) *(COMMON)
    }
    __end_elf__ = .;
}
