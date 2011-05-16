        dc.l 0x0,0x200
        dc.l INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,HBL,INT,VBL,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT,INT
        dc.l INT,INT,INT,INT,INT,INT,INT
        .ascii "SEGA GENESIS                    "
        .ascii "hexed (c) notaz, 2009                           "
        .ascii "HEXED (C) NOTAZ, 2009                           "
        .ascii "GM 00000000-00"
        .byte 0x00,0x00
        .ascii "JD              "
        .byte 0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00
        .byte 0x00,0xff,0x00,0x00,0xff,0xff,0xff,0xff
        .ascii "               "
        .ascii "                        "
        .ascii "                         "
        .ascii "JUE             "

* Check Version Number
	move.b  (0xa10000),%d0
	andi.b  #0x0f,%d0
	beq     WrongVersion
* Sega Security Code (SEGA)
	move.l  #0x53454741,(0xa14000)
WrongVersion:
	moveq   #0,%d0
	movea.l %d0,%a7
	move    %a7,%usp

	tst.w   0xC00004
	bra     main

* INT:
*	rte

HBL:
	rte

* VBL:
*	addq.l #1,(vtimer).l
*	rte

