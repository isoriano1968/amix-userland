| The end of a C++ extension module's .init_array and .eh_frame: linked
| after all of its objects and libraries (files/amix-cc).  A zero word ends
| the frame list, as crtend.o's does.
	.section .init_array,"aw"
	.balign 4
	.globl __amix_so_init_array_end
	.hidden __amix_so_init_array_end
__amix_so_init_array_end:
	.section .eh_frame,"a"
	.balign 4
	.long 0
