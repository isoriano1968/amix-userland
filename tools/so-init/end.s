| The end of a shared library's .init_array: linked after its objects.
	.section .init_array,"aw"
	.balign 4
	.globl __amix_so_init_array_end
	.hidden __amix_so_init_array_end
__amix_so_init_array_end:
