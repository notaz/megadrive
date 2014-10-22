# Assemble with gas
#   --register-prefix-optional --bitwise-or

.macro ldarg  arg, stacksz, reg
    move.l (4 + \arg * 4 + \stacksz)(%sp), \reg
.endm


.global read_joy_responses /* u8 *rbuf */
read_joy_responses:
    ldarg       0, 0, a1
    movem.l     d2-d7, -(sp)
    movea.l     #0xa10003, a0
    move.b      #0x40, (6,a0)
    move.b      #0x40, (a0)

.macro one_test val
    move.l      #100/12-1, d0
0:
    dbra        d0, 0b
    move.b      \val, d0
    move.b      d0, (a0)
    move.b      (a0), d0
    move.b      (a0), d1
    move.b      (a0), d2
    move.b      (a0), d3
    move.b      (a0), d4
    move.b      (a0), d5
    move.b      (a0), d6
    move.b      (a0), d7
    move.b      d0, (a1)+
    move.b      d1, (a1)+
    move.b      d2, (a1)+
    move.b      d3, (a1)+
    move.b      d4, (a1)+
    move.b      d5, (a1)+
    move.b      d6, (a1)+
    move.b      d7, (a1)+
.endm

	move.w		#0x2700, sr
    one_test    #0x00
    one_test    #0x40
    one_test    #0x00
    one_test    #0x40
    one_test    #0x00
	move.w		#0x2000, sr
    movem.l     (sp)+, d2-d7
    rts


.global test_joy_read_log /* u8 *dest, int size */
test_joy_read_log:
    ldarg       0, 0, a1
    ldarg       1, 0, d0
    movem.l     d2-d7, -(sp)
    movea.l     #0xa10003, a0
    move.l      d0, d7

.macro joy_read_log_prep
    move.l      #0x40, d1
    moveq.l     #0, d0
    move.b      d1, (6,a0)
    move.b      d1, (a0)

0:  /* wait for special code */
    move.b      d1, (a0)
    move.b      (a0), d2
    move.b      d0, (a0)
    move.b      (a0), d3
    and.b       #0x3f, d2
    cmp.b       d2, d3
    bne         0b
    cmp.b       #0x25, d2
    bne         0b

0:  /* wait for special code to end */
    cmp.b       (a0), d2
    beq         0b

    move.b      d1, (a0)
    move.l      #8000000/50/18, d2

0:  /* wait enough for teensy to setup it's stuff */
    subq.l      #1, d2   /* 8 */
    bgt.s       0b       /* 10 */
.endm
.macro nop_
    /*
     * when communicating with 3.3V teensy:
     * - no nops: see old value on multiple pins randomly
     * - 1 nop: only TR often shows old value
     * - 2 nops: ?
     */
    nop
    nop
.endm

    joy_read_log_prep

2:  /* save data */
    move.b      d0, (a0)
    nop_
    move.b      (a0), d2
    move.b      d1, (a0)
    nop_
    move.b      (a0), d3
    move.b      d0, (a0)
    nop_
    move.b      (a0), d4
    move.b      d1, (a0)
    nop_
    move.b      (a0), d5
.if 0
    /* broken on Mega-ED v9?? */
    move.b      d2, (a1)+
    move.b      d3, (a1)+
    move.b      d4, (a1)+
    move.b      d5, (a1)+
.else
    lsl.w       #8, d2
    move.b      d3, d2
    move.w      d2, (a1)+
    lsl.w       #8, d4
    move.b      d5, d4
    move.w      d4, (a1)+
.endif

    /* delay for teensy, 128 not enough.. */
    move.l      #256, d2
0:
    dbra        d2, 0b

    subq.l      #4, d7
    bgt.s       2b

    movem.l     (sp)+, d2-d7
    rts


.global test_joy_read_log_vsync /* u8 *dest, int size */
test_joy_read_log_vsync:
    ldarg       0, 0, a1
    ldarg       1, 0, d0
    movem.l     d2-d7/a2, -(sp)
    movea.l     #0xa10003, a0
    movea.l     #0xc00005, a2
    move.l      d0, d7

    /* syncs, sets d0=0, d1=0x40 */
    joy_read_log_prep

2:  /* save data */
    move.b      d0, (a0)
    move.b      (a0), d2
    move.b      d1, (a0)
    move.b      (a0), d3
    move.b      d2, (a1)+
    move.b      d3, (a1)+

    /* wait for next vsync */
    moveq.l     #3, d2
0:
    btst        d2, (a2)
    bne.s       0b
0:
    btst        d2, (a2)
    beq.s       0b

    subq.l      #2, d7
    bgt.s       2b

    movem.l     (sp)+, d2-d7/a2
    rts


.global run_game /* u16 mapper, int tas_sync */
run_game:
	move.w		#0x2700, sr
    ldarg       0, 0, d7
    ldarg       1, 0, d6
    movea.l     #0xa10000, a6
    movea.l     #0xc00000, a5
    movea.l     #0xc00005, a4
    movea.l     #0xc00004, a3
    moveq.l     #0x00, d2
    move.b      #0xff, d3
    move.b      #0x40, d4
    move.b      d4, (0x09,a6) /* CtrlA */
    move.b      d2, (0x0b,a6) /* CtrlB */
    move.b      d2, (0x0d,a6) /* CtrlC */
    move.b      d2, (0x13,a6) /* S-CtrlA */
    move.b      d3, (0x0f,a6) /* TxDataA */
    move.b      d2, (0x19,a6) /* S-CtrlB */
    move.b      d3, (0x15,a6) /* TxDataB */
    move.b      d2, (0x1f,a6) /* S-CtrlC */
    move.b      d3, (0x1b,a6) /* TxDataC */

    /* set up for vram write */
    move.l      #0x40000000, (a3)

    move.l      #0xff0000, a1
    move.l      #0x10000/4/4-1, d0
0:
    move.l      d2, (a1)+
    move.l      d2, (a1)+
    move.l      d2, (a1)+
    move.l      d2, (a1)+
    dbra        d0, 0b

    move.l      #0xfffe00, a1
    tst.l       d6
    bne         use_tas_code

    lea         (run_game_r,pc), a0
    move.l      #(run_game_r_end - run_game_r)/2-1, d0
    bra         0f
use_tas_code:
    lea         (run_game_r_tas,pc), a0
    move.l      #(run_game_r_tas_end - run_game_r_tas)/2-1, d0

0:
    move.w      (a0)+, (a1)+
    dbra        d0, 0b
    jmp         0xfffe00

run_game_r:
    move.w      #0x3210, (0xA13006)

    move.w      d7, (0xA13010)
    move.w      #0, (0xA13000)
    
    move.l      (0x00), a7
    move.l      (0x04), a0
    jmp         (a0)
run_game_r_end:

run_game_r_tas:
    move.w      #0x3210, (0xA13006)
    move.w      d7, (0xA13010)
    move.w      #0, (0xA13000)
    
    move.l      (0x00), a7
    move.l      (0x04), a0

0:  /* wait for special code */
    move.b      d4, (0x03,a6)
    move.b      (0x03,a6), d0
    move.b      d2, (0x03,a6)
    move.b      (0x03,a6), d1
    and.b       #0x3f, d0
    cmp.b       d0, d1
    bne         0b
    cmp.b       #0x25, d0
    bne         0b

0:  /* wait for special code to end */
    cmp.b       (0x03,a6), d0
    beq         0b

    /* wait for active display */
    moveq.l     #3, d0
0:
    btst        d0, (a4)      /* 8 */
    beq.s       0b            /* 10 */
0:
    btst        d0, (a4)
    bne.s       0b

    /* flood the VDP FIFO */
.rept 5
    move.w      d2, (a5)
.endr

    /* doesn't help.. */
.if 0
.rept 94
    nop
.endr
    move.l      #0x93049400, (a3) /* DMALEN LO/HI = 0x0008 */
    move.l      #0x95009601, (a3) /* DMA SRC LO/MID */
    move.l      #0x977f8114, (a3) /* DMA SRC HI/MODE, Turn off Display */
    move.l      #0xc0000080, (a3) /* start DMA */
.endif

    move.b      d2, (0x09,a6) /* CtrlA */
    move.b      d4, (0x03,a6)
    jmp         (a0)
run_game_r_tas_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
