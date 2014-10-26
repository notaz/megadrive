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


/* expects:
 * a0 = #0xa10003
 * d0 = #0
 * d1 = #0x40
 * trashes d2, d3
 */
sync_with_teensy:
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

    rts


.macro t_nop
    /*
     * when communicating with 3.3V teensy:
     * - no nops: see old value on multiple pins randomly
     * - 1 nop: only TR often shows old value
     * - 2 nops: ?
     */
    nop
    nop
.endm


.global test_joy_read_log /* u8 *dest, int size, int do_sync */
test_joy_read_log:
    ldarg       0, 0, a1
    ldarg       1, 0, d0
    ldarg       2, 0, d1
    movem.l     d2-d7, -(sp)
    movea.l     #0xa10003, a0
    move.l      d0, d7
    move.l      d1, d6

    moveq.l     #0, d0
    move.l      #0x40, d1
    move.b      d1, (6,a0)
    move.b      d1, (a0)

    tst.l       d6
    beq.s       2f
    bsr         sync_with_teensy

2:  /* save data */
    move.b      d0, (a0)
    t_nop
    move.b      (a0), d2
    move.b      d1, (a0)
    t_nop
    move.b      (a0), d3
    move.b      d0, (a0)
    t_nop
    move.b      (a0), d4
    move.b      d1, (a0)
    t_nop
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

    move.l      #0x40, d1
    moveq.l     #0, d0
    move.b      d1, (6,a0)
    move.b      d1, (a0)

    bsr         sync_with_teensy

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


.global test_byte_write /* u8 *dest, int size, int seed */
test_byte_write:
    ldarg       0, 0, a0
    ldarg       1, 0, d0
    ldarg       2, 0, d1
    movem.l     d2-d7, -(sp)

    move.l      a0, a1
    add.l       d0, a1
    move.l      d1, d7
0:
    move.b      d7, d0
    addq.b      #1, d7
    move.b      d7, d1
    addq.b      #1, d7
    move.b      d7, d2
    addq.b      #1, d7
    move.b      d7, d3
    addq.b      #1, d7
    move.b      d7, d4
    addq.b      #1, d7
    move.b      d7, d5
    addq.b      #1, d7
    move.b      d7, d6
    addq.b      #1, d7

    move.b      d0, (a0)+
    move.b      d1, (a0)+
    move.b      d2, (a0)+
    move.b      d3, (a0)+
    move.b      d4, (a0)+
    move.b      d5, (a0)+
    move.b      d6, (a0)+
    move.b      d7, (a0)+
    addq.b      #1, d7
    cmp.l       a1, a0
    blt.s       0b

    movem.l     (sp)+, d2-d7
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
    moveq.l     #0x00, d0
    move.b      #0x40, d1     /* d2 is tmp */
    move.b      #0xff, d3
    move.b      d1, (0x09,a6) /* CtrlA */
    move.b      d0, (0x0b,a6) /* CtrlB */
    move.b      d0, (0x0d,a6) /* CtrlC */
    move.b      d0, (0x13,a6) /* S-CtrlA */
    move.b      d3, (0x0f,a6) /* TxDataA */
    move.b      d0, (0x19,a6) /* S-CtrlB */
    move.b      d3, (0x15,a6) /* TxDataB */
    move.b      d0, (0x1f,a6) /* S-CtrlC */
    move.b      d3, (0x1b,a6) /* TxDataC */

    /* set up for vram write */
    move.l      #0x40000000, (a3)

    move.l      #0xff0000, a1
    move.l      #0x10000/4/4-1, d2
0:
    move.l      d0, (a1)+
    move.l      d0, (a1)+
    move.l      d0, (a1)+
    move.l      d0, (a1)+
    dbra        d2, 0b

    lea         (run_game_r,pc), a0
    move.l      #0xffff80, a1
    move.l      #(run_game_r_end - run_game_r)/2-1, d2
0:
    move.w      (a0)+, (a1)+
    dbra        d2, 0b

    tst.l       d6
    beq.s       0f

    movea.l     #0xa10003, a0
    movea.l     d0, a7
    bsr         sync_with_teensy  /* trashes d3 */
    move.l      d0, (-4,a7)

0:
    /* wait for active display */
    moveq.l     #3, d2
0:
    btst        d2, (a4)      /* 8 */
    beq.s       0b            /* 10 */
0:
    btst        d2, (a4)
    bne.s       0b

    /* flood the VDP FIFO */
.rept 5
    move.w      d0, (a5)
.endr

    movea.l     d0, a0
    movea.l     #0xA13000, a1

    move.b      d0, (0x09,a6) /* CtrlA */
    move.b      d1, (0x03,a6)

    jmp         0xffff80

run_game_r:
    move.w      #0x3210, (0x06,a1) /* 0xA13006 */
    move.w      d7, (0x10,a1)      /* 0xA13010 */
    move.w      d0, (a1)           /* 0xA13000 */
    
    move.l      (a0)+, a7
    move.l      (a0),  a0

    jmp         (a0)
run_game_r_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
