| The start of a C++ extension module's .init_array and .eh_frame: linked
| before all of its objects (files/amix-cc).
	.section .init_array,"aw"
	.balign 4
	.globl __amix_so_init_array
	.hidden __amix_so_init_array
__amix_so_init_array:
	.section .eh_frame,"a"
	.balign 4
	.globl __amix_so_eh_frame
	.hidden __amix_so_eh_frame
__amix_so_eh_frame:
