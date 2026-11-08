/* module.ld -- one game module (cgame, qagame, ui) as one relocatable
   object for the core 1 image (ld -r -T): MOD is replaced by its name.
   Its globals lie together, between MOD_data_start/_end and
   MOD_bss_start/_end, so sys_c1.c can give them back their first values at
   every load, as a fresh dlopen would.  The image's lscript.ld places the
   sections by their prefixes (.text.*, .rodata.*, .data.*, .bss.*). */
SECTIONS
{
    .text.MOD : { *(.text .text.*) }
    .rodata.MOD : { *(.rodata .rodata.*) }
    .data.MOD : { MOD_data_start = .; *(.data .data.*) MOD_data_end = .; }
    .bss.MOD : { MOD_bss_start = .; *(.bss .bss.* COMMON) MOD_bss_end = .; }
}
