/* c1bench payload at $70000: below the marks ($7E000-$7FFFF) */
SECTIONS
{
    . = 0x70000;
    .text : { *(.text) *(.text.*) *(.rodata) *(.rodata.*) }
    .data : { *(.data) *(.data.*) }
    __bss_start = .;
    .bss : { *(.bss) *(.bss.*) *(COMMON) }
    _end = .;
    ASSERT(_end <= 0x7E000, "c1bench payload too big")
}
