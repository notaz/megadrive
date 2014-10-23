    dc.l     0, 0x200, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, HBL,   exc__, VBL,   exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__
    dc.l exc__, exc__, exc__, exc__, exc__, exc__, exc__, exc__

    .ascii "SEGA EVERDRIVE                  "
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
/* magic ED app init */
	move.w #0x0000, (0xA13006)
	jmp init_ed.l
init_ed:
	move.w #0x210f, (0xA13006)
    move.l #HBL, (0x70)
    move.l #VBL, (0x78)

	moveq   #0, %d0
	movea.l %d0, %a7
	move    %a7, %usp
	bra     main

HBL:
VBL:
exc__:
	rte

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
