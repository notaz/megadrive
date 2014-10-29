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
    move.w      #0x2700, sr
    ldarg       0, 0, d7
    ldarg       1, 0, d6
    movea.l     #0xa10000, a6
    movea.l     #0xc00000, a5
    movea.l     #0xc00005, a4
    movea.l     #0xc00004, a3
    moveq.l     #0x00, d0
    move.b      #0x40, d1     /* d2 is tmp */
    move.b      #0xff, d3     /* d4 is temp */
    moveq.l     #0x00, d5     /* progress cnt */
    movea.l     d0, a7
    move.b      d1, (0x09,a6) /* CtrlA */
    move.b      d0, (0x0b,a6) /* CtrlB */
    move.b      d0, (0x0d,a6) /* CtrlC */
    move.b      d0, (0x13,a6) /* S-CtrlA */
    move.b      d3, (0x0f,a6) /* TxDataA */
    move.b      d0, (0x19,a6) /* S-CtrlB */
    move.b      d3, (0x15,a6) /* TxDataB */
    move.b      d0, (0x1f,a6) /* S-CtrlC */
    move.b      d3, (0x1b,a6) /* TxDataC */

    move.w      #0xcbaf, (0xA13006) /* some scratch area */

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
    beq.s       sync_hvc

    movea.l     #0xa10003, a0
    bsr         sync_with_teensy  /* trashes d3 */
    move.l      d0, (-4,a7)

sync_hvc:
    addq.l      #1, d6        /* attempt counter */

    /* set up for progress vram write (x,y - tile #) */
    /* GFX_WRITE_VRAM_ADDR(0xc000 + (x + 64 * y) * 2) */
    /* d = d5 + '0' - 32 + 0xB000/32 - 128 = d5 + 0x510 */
    move.l      #(0x40000003 | ((36 + 64*1) << 17)), (a3)
    add.w       #0x510, d5
    move.w      d5, (a5)
    move.w      #('/'+0x4e0), (a5)
    move.w      #('4'+0x4e0), (a5)

    lea         hexchars, a1
    move.l      #(0x40000003 | ((31 + 64*2) << 17)), (a3)
    moveq.l     #8-1, d5
0:
    rol.l       #4, d3
    move.b      d3, d4
    and.l       #0x0f, d4
    move.b      (d4,a1), d4
    add.w       #0x4e0, d4
    move.w      d4, (a5)
    dbra        d5, 0b

    movea.l     #0xc00008, a0
    movea.l     #0x3ff000, a1
    movea.l     #0xffffe0, a2

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

    /* these seem stable for both 50Hz/60Hz */
    move.l      (a0), (a1)+      /* #0xff07ff09 */
    move.l      (a0), (a1)+      /* #0xff00ff11 */
    move.l      (a0), (a1)+      /* #0xff18ff1a */
    move.l      (a0), (a1)+      /* #0xff21ff23 */
    move.l      (a0), (a1)+      /* #0xff2aff28 */
    move.l      (a0), (a1)+      /* #0xff33ff34 */
    move.l      (a0), (a1)+      /* #0xff3cff3e */
    move.l      (a0), (a1)+      /* #0xff45ff47 */

    /* as long as exactly 8 or more RAM writes are performed here, */
    /* after multiple tries RAM refresh somehow eventually syncs */
    /* after cold boot, only 50Hz syncs to always same values though, */
    /* so values below are 50Hz */
    move.l      (a0), (a2)+      /* #0xff4eff4f */
    move.l      (a0), (a2)+      /* #0xff58ff59 */
    move.l      (a0), (a2)+      /* #0xff60ff62 */
    move.l      (a0), (a2)+      /* #0xff69ff6b */
    move.l      (a0), (a2)+      /* #0xff72ff74 */
    move.l      (a0), (a2)+      /* #0xff7bff7c */
    move.l      (a0), (a2)+      /* #0xff83ff85 */
    move.l      (a0), (a2)+      /* #0xff8eff8f */

    sub.l       #4*8, a1
    sub.l       #4*8, a2

    moveq.l     #1, d5
    move.l      (0x00,a1), d3
    cmp.l       #0xff07ff09, d3
    bne.w       sync_hvc

    moveq.l     #2, d5
    move.l      (0x04,a1), d3
    cmp.l       #0xff00ff11, d3  /* mystery value */
    bne.w       sync_hvc

    moveq.l     #3, d5
    move.l      (0x1c,a1), d3
    cmp.l       #0xff45ff47, d3
    bne.w       sync_hvc

    btst.b      #6, (0xa10001)
    bne.s       sync_hvc_50hz

sync_hvc_60hz:
    /* unable to get stable RAM between cold boots :( */
    moveq.l     #4, d5
    move.l      (0x1c,a2), d3
    cmp.l       #0xff8dff8f, d3  /* unstable */
    beq.s       sync_hvc_end
    cmp.l       #0xff8cff8e, d3  /* stable? */
    beq.s       sync_hvc_end
    cmp.l       #0xff8eff90, d3
    beq.s       sync_hvc_end
    bra.w       sync_hvc

sync_hvc_50hz:
    moveq.l     #4, d5
    move.l      (0x1c,a2), d3
    cmp.l       #0xff8eff8f, d3  /* RAM */
    bne.w       sync_hvc

sync_hvc_end:
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

hexchars:
    dc.b        '0','1','2','3','4','5','6','7'
    dc.b        '8','9','a','b','c','d','e','f'

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
