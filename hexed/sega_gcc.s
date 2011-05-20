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
        .ascii "hexed (c) notaz, 2009-2011                      "
        .ascii "HEXED (C) NOTAZ, 2009-2011                      "
        .ascii "GM 00000000-00"
        .byte 0x00,0x00
        .ascii "JD              "
        .byte 0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00
        .byte 0x00,0xff,0x00,0x00,0xff,0xff,0xff,0xff
        .ascii "               "
        .ascii "                        "
        .ascii "                         "
        .ascii "JUE             "

	moveq   #0,%d0
	movea.l %d0,%a7
	move    %a7,%usp
	bra     main

* INT:
*	rte

HBL:
	rte

* VBL:
*	addq.l #1,(vtimer).l
*	rte

