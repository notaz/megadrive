exc_tab:
    dc.l     0, 0x200, exc02, exc03, exc04, exc05, exc06, exc07
    dc.l exc08, exc09, exc0a, exc0b, exc0c, exc0d, exc0e, exc0f
    dc.l exc10, exc11, exc12, exc13, exc14, exc15, exc16, exc17
    dc.l exc18, exc19, exc1a, exc1b, HBL,   exc1d, VBL,   exc1f
    dc.l exc20, exc21, exc22, exc23, exc24, exc25, exc26, exc27
    dc.l exc28, exc29, exc2a, exc2b, exc2c, exc2d, exc2e, exc2f
    dc.l exc30, exc31, exc32, exc33, exc34, exc35, exc3e, exc37
    dc.l exc38, exc39, exc3a, exc3b, exc3c, exc3d, exc3e, exc3f

    .ascii "SEGA GENESIS                    "
    .ascii "SRAM test                                       "
    .ascii "SRAM test                                       "
    .ascii "GM 00000000-00"
    .byte 0x00,0x00
    .ascii "J               "
    .byte 0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00
    .byte 0x00,0xff,0x00,0x00,0xff,0xff,0xff,0xff
    .ascii "RA"; .byte 0xf8,0x20                     /* 1b0 */
    .byte 0x00,0x02,0x00,0x00,0x00,0x3f,0xff,0xff    /* 1b4 */
    .ascii "    "                                    /* 1bc */
    .ascii "                        "                /* 1c0 */
    .ascii "                        "
    .ascii "JUE             "                        /* 1f0 */

RST:
    move.w  #0x2700, %sr

    move.b (0xA10001), %d0
    andi.b #0x0F, %d0
    beq.s 0f
    move.l  #0x53454741, (0xA14000) /* 'SEGA' */
0:
	tst.w   (0xc00004).l

    moveq   #0, %d0
    movea.l %d0, %a7
    move    %a7, %usp
#    move.w  #0x2000, %sr
    jsr     main
0:
    bra     0b

#HBL:
#VBL:
#    rte

pre_exception:
    move.w  #0x2700, %sr
    movem.l %d0-%d7/%a0-%a7,-(%sp)
    move.l %sp, %d0
    move.l %d0,-(%sp)
    jsr exception
0:
    bra 0b

.macro exc_stub num
exc\num:
    move.w #0x\num, -(%sp)
    jmp pre_exception
.endm

exc_stub 02
exc_stub 03
exc_stub 04
exc_stub 05
exc_stub 06
exc_stub 07
exc_stub 08
exc_stub 09
exc_stub 0a
exc_stub 0b
exc_stub 0c
exc_stub 0d
exc_stub 0e
exc_stub 0f
exc_stub 10
exc_stub 11
exc_stub 12
exc_stub 13
exc_stub 14
exc_stub 15
exc_stub 16
exc_stub 17
exc_stub 18
exc_stub 19
exc_stub 1a
exc_stub 1b
HBL:
exc_stub 1c
exc_stub 1d
VBL:
exc_stub 1e
exc_stub 1f
exc_stub 20
exc_stub 21
exc_stub 22
exc_stub 23
exc_stub 24
exc_stub 25
exc_stub 26
exc_stub 27
exc_stub 28
exc_stub 29
exc_stub 2a
exc_stub 2b
exc_stub 2c
exc_stub 2d
exc_stub 2e
exc_stub 2f
exc_stub 30
exc_stub 31
exc_stub 32
exc_stub 33
exc_stub 34
exc_stub 35
exc_stub 36
exc_stub 37
exc_stub 38
exc_stub 39
exc_stub 3a
exc_stub 3b
exc_stub 3c
exc_stub 3d
exc_stub 3e
exc_stub 3f

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
