| crt0.S -- c1bench's start at $70000: a stack below it, bss cleared,
| CoreMark, then the "done" mark and the CRC for the host.
    .text
    .globl  _start
_start:
    lea     0x6fff0,%sp
    lea     __bss_start,%a0
    lea     _end,%a1
1:  cmp.l   %a1,%a0
    bcc.s   2f
    clr.b   (%a0)+
    bra.s   1b
2:  jsr     main
    move.l  #0x43314244,0x7ff00     | "C1BD": done, the report is complete
3:  bra.s   3b
