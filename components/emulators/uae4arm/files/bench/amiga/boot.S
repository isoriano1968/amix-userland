| boot.S -- c1bench.adf's boot block: read the payload (sectors 2 on) to
| $70000, motor off, interrupts off, run it.  Entered with A1 = the boot
| IOStdReq, A6 = SysBase.  The checksum is filled in by mkbenchadf.py.
    .text
    .ascii  "DOS"
    .byte   0
    .long   0                   | checksum
    .long   880                 | root block
    move.l  %a1,%a5
    move.l  #LOADSZ,%d0
    lea     0x70000,%a1
    jsr     -204(%a6)           | AllocAbs (if it is free; we never go back anyway)
    move.l  %a5,%a1
    move.w  #2,28(%a1)          | CMD_READ
    move.l  #LOADSZ,36(%a1)
    move.l  #0x70000,40(%a1)
    move.l  #1024,44(%a1)
    jsr     -456(%a6)           | DoIO
    move.l  %a5,%a1
    move.w  #9,28(%a1)          | TD_MOTOR off
    clr.l   36(%a1)
    jsr     -456(%a6)
    jsr     -120(%a6)           | Disable
    jmp     0x70000
