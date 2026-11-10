ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    /* Les sections sont alignees sur une page : le paging leur applique
     * des droits distincts (code RX, rodata R, donnees RW non executables). */
    .text : {
        __text_start = .;
        *(.text.entry)
        *(.text .text.*)
        . = ALIGN(4096);
        __text_end = .;
    }

    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __rodata_end = .;
    }

    .data : ALIGN(16) {
        *(.data .data.*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4);
        __bss_end = .;
    }

    _end = .;
    ASSERT(_end <= 0x7F000, "kernel trop gros : il recouvre la pile")

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
