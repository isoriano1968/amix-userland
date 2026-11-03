| The start of a shared library's .init_array: linked before its objects.
	.section .init_array,"aw"
	.balign 4
	.globl __amix_so_init_array
	.hidden __amix_so_init_array
__amix_so_init_array:
