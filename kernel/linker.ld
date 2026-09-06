ENTRY(kernel_main)

SECTIONS
{
    . = 0xffffffff80000000;

    __kernel_start = .;

    . = ALIGN(0x1000);
    .text :
    {
        __text_start = .;
        *(.text .text.*)
        __text_end = .;
    }

    . = ALIGN(0x1000);
    .rodata :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    . = ALIGN(0x1000);
    .data :
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }

    . = ALIGN(0x1000);
    .bss :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(0x1000);
    __kernel_end = .;
}
