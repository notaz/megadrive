    dc.l     0, RST,   exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, HBL,   exc__, VBL,   exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__

    .ascii "SEGA                            "
    .ascii "MEGA-ED SDRAM vs STOP                           "
    .ascii "MEGA-ED SDRAM vs STOP                           "
    .ascii "GM 00000000-00"
    .byte 0x00,0x00
    .ascii "JD              "
    .byte 0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00
    .byte 0x00,0xff,0x00,0x00,0xff,0xff,0xff,0xff
    .ascii "               "
    .ascii "                        "
    .ascii "                         "
    .ascii "JUE             "

RST:
    move.w #0x2700, %sr

    move.b (0xA10001), %d0
    andi.b #0x0F, %d0
    beq.s 0f
    move.l  #0x53454741, (0xA14000) /* 'SEGA' */
0:
    moveq.l #0, %d0
    movea.l %d0, %a7
    move.l  %a7, %usp
    bra     main

HBL:
VBL:
exc__:
    rte

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
