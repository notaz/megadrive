*-------------------------------------------------------
*
*       Sega startup code for the GNU Assembler
*       Translated from:
*       Sega startup code for the Sozobon C compiler
*       Written by Paul W. Lee
*       Modified from Charles Coty's code
*
*-------------------------------------------------------

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
        .ascii "hex editor (c) notaz                            "
        .ascii "HEX EDITOR (C) NOTAZ                            "
        .ascii "GM 00000000-00"
        .byte 0xa5,0xfb
        .ascii "JD              "
        .byte 0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00
        .byte 0x00,0xff,0x00,0x00,0xff,0xff,0xff,0xff
        .ascii "               "
        .ascii "                        "
        .ascii "                         "
        .ascii "JUE             "
*debugee:
*        bra     debugee
        tst.l   0xa10008
	bne     SkipJoyDetect                               
        tst.w   0xa1000c
SkipJoyDetect:
	bne     SkipSetup
        lea     Table,%a5                       
        movem.w (%a5)+,%d5-%d7
        movem.l (%a5)+,%a0-%a4                       
* Check Version Number                      
        move.b  -0x10ff(%a1),%d0
        andi.b  #0x0f,%d0                             
	beq     WrongVersion                                   
* Sega Security Code (SEGA)   
        move.l  #0x53454741,0x2f00(%a1)
WrongVersion:
        move.w  (%a4),%d0
        moveq   #0x00,%d0                                
        movea.l %d0,%a6                                  
        move    %a6,%usp
* Set VDP registers
        moveq   #0x17,%d1
FillLoop:                           
        move.b  (%a5)+,%d5
        move.w  %d5,(%a4)                              
        add.w   %d7,%d5                                 
        dbra    %d1,FillLoop                           
        move.l  (%a5)+,(%a4)                            
        move.w  %d0,(%a3)                                 
        move.w  %d7,(%a1)                                 
        move.w  %d7,(%a2)                                 
L0250:
        btst    %d0,(%a1)
	bne     L0250                                   
* Put initial values into a00000                
        moveq   #0x25,%d2
Filla:                                 
        move.b  (%a5)+,(%a0)+
        dbra    %d2,Filla
        move.w  %d0,(%a2)                                 
        move.w  %d0,(%a1)                                 
        move.w  %d7,(%a2)                                 
L0262:
        move.l  %d0,-(%a6)
        dbra    %d6,L0262                            
        move.l  (%a5)+,(%a4)                              
        move.l  (%a5)+,(%a4)                              
* Put initial values into c00000                  
        moveq   #0x1f,%d3
Filc0:                             
        move.l  %d0,(%a3)
        dbra    %d3,Filc0
        move.l  (%a5)+,(%a4)                              
* Put initial values into c00000                 
        moveq   #0x13,%d4
Fillc1:                            
        move.l  %d0,(%a3)
        dbra    %d4,Fillc1
* Put initial values into c00011                 
        moveq   #0x03,%d5
Fillc2:                            
        move.b  (%a5)+,0x0011(%a3)        
        dbra    %d5,Fillc2                            
        move.w  %d0,(%a2)                                 
        movem.l (%a6),%d0-%d7/%a0-%a6                    
        move    #0x2700,%sr                           
SkipSetup:
	bra     Continue
Table:
        dc.w    0x8000, 0x3fff, 0x0100, 0x00a0, 0x0000, 0x00a1, 0x1100, 0x00a1
        dc.w    0x1200, 0x00c0, 0x0000, 0x00c0, 0x0004, 0x0414, 0x302c, 0x0754
        dc.w    0x0000, 0x0000, 0x0000, 0x812b, 0x0001, 0x0100, 0x00ff, 0xff00                                   
        dc.w    0x0080, 0x4000, 0x0080, 0xaf01, 0xd91f, 0x1127, 0x0021, 0x2600
        dc.w    0xf977, 0xedb0, 0xdde1, 0xfde1, 0xed47, 0xed4f, 0xd1e1, 0xf108                                   
        dc.w    0xd9c1, 0xd1e1, 0xf1f9, 0xf3ed, 0x5636, 0xe9e9, 0x8104, 0x8f01                
        dc.w    0xc000, 0x0000, 0x4000, 0x0010, 0x9fbf, 0xdfff                                

Continue:
        tst.w    0x00C00004

* set stack pointer
*        clr.l   %a7
        move.w   #0,%a7

* user mode
        move.w  #0x2300,%sr

* clear Genesis RAM
        lea     0xff0000,%a0
        moveq   #0,%d0
clrram: move.w  #0,(%a0)+
        subq.w  #2,%d0
	bne     clrram

*----------------------------------------------------------        
*
*       Load driver into the Z80 memory
*
*----------------------------------------------------------        

* halt the Z80
        move.w  #0x100,0xa11100
* reset it
        move.w  #0x100,0xa11200

        lea     Z80Driver,%a0
        lea     0xa00000,%a1
        move.l  #Z80DriverEnd,%d0
        move.l  #Z80Driver,%d1
        sub.l   %d1,%d0
Z80loop:
        move.b  (%a0)+,(%a1)+
        subq.w  #1,%d0
	bne     Z80loop

* enable the Z80
        move.w  #0x0,0xa11100

*----------------------------------------------------------        
        jmp      main

INT:    
	rte

HBL:
        /* addq.l   #1,htimer */
	rte

* VBL:
*	addq.l #1,(vtimer).l
*	rte

*----------------------------------------------------------        
*
*       Z80 Sound Driver
*
*----------------------------------------------------------        
Z80Driver:
          dc.b  0xc3,0x46,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
          dc.b  0x00,0x00,0x00,0x00,0x00,0x00,0xf3,0xed
          dc.b  0x56,0x31,0x00,0x20,0x3a,0x39,0x00,0xb7
          dc.b  0xca,0x4c,0x00,0x21,0x3a,0x00,0x11,0x40
          dc.b  0x00,0x01,0x06,0x00,0xed,0xb0,0x3e,0x00
          dc.b  0x32,0x39,0x00,0x3e,0xb4,0x32,0x02,0x40
          dc.b  0x3e,0xc0,0x32,0x03,0x40,0x3e,0x2b,0x32
          dc.b  0x00,0x40,0x3e,0x80,0x32,0x01,0x40,0x3a
          dc.b  0x43,0x00,0x4f,0x3a,0x44,0x00,0x47,0x3e
          dc.b  0x06,0x3d,0xc2,0x81,0x00,0x21,0x00,0x60
          dc.b  0x3a,0x41,0x00,0x07,0x77,0x3a,0x42,0x00
          dc.b  0x77,0x0f,0x77,0x0f,0x77,0x0f,0x77,0x0f
          dc.b  0x77,0x0f,0x77,0x0f,0x77,0x0f,0x77,0x3a
          dc.b  0x40,0x00,0x6f,0x3a,0x41,0x00,0xf6,0x80
          dc.b  0x67,0x3e,0x2a,0x32,0x00,0x40,0x7e,0x32
          dc.b  0x01,0x40,0x21,0x40,0x00,0x7e,0xc6,0x01
          dc.b  0x77,0x23,0x7e,0xce,0x00,0x77,0x23,0x7e
          dc.b  0xce,0x00,0x77,0x3a,0x39,0x00,0xb7,0xc2
          dc.b  0x4c,0x00,0x0b,0x78,0xb1,0xc2,0x7f,0x00
          dc.b  0x3a,0x45,0x00,0xb7,0xca,0x4c,0x00,0x3d
          dc.b  0x3a,0x45,0x00,0x06,0xff,0x0e,0xff,0xc3
          dc.b  0x7f,0x00
Z80DriverEnd:


