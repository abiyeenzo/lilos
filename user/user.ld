ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    /* en-tete lu par le chargeur du noyau */
    .header : {
        LONG(0x474F5250)
        LONG(_start - 0x40000000)
        LONG(__text_end - 0x40000000)
        LONG(__data_end - 0x40000000)
        LONG(__bss_end - 0x40000000)
    }

    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __text_end = .;
    }

    .data : {
        *(.data .data.*)
        __data_end = .;
    }

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
