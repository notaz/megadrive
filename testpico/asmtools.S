# Assemble with gas
#   --register-prefix-optional --bitwise-or
# reminder: d2-d7/a2-a6 are callee-save

.macro ldarg  arg, stacksz, reg
    move.l (4 + \arg * 4 + \stacksz)(%sp), \reg
.endm

# args are always 4 bytes in stack, caller restores sp
.macro ldargw arg, stacksz, reg
    move.w (4 + \arg * 4 + 2 + \stacksz)(%sp), \reg
.endm

.global burn10 /* u16 val */
burn10:
    ldargw      0, 0, d0
    subq.l      #1, d0
0:
    dbra        d0, 0b      /* 10|14 */
    rts                     /* 16 */

.global write16_x16 /* u32 a, u16 count, u16 d */
write16_x16:
    ldarg       0, 0, a0
    ldarg       2, 0, d0
    move.w      d0, d1
    swap        d0
    move.w      d1, d0
    ldarg       1, 0, d1
    subq.l      #1, d1
0:
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    move.l      d0, (a0)
    dbra        d1, 0b
    rts

# read single phase from controller
#  d0 - result
#  destroys d1
.global get_input
get_input:
.global get_input_s
get_input_s:
	move.b		#0x40,(0xa10003)
	moveq.l		#0,d0
	nop
	nop
	move.b		(0xa10003),d1
	move.b		#0x00,(0xa10003)
	andi.w		#0x3f,d1	/* 00CB RLDU */
	nop
	move.b		(0xa10003),d0
	lsl.b		#2,d0
	andi.w		#0xc0,d0	/* SA00 0000 */
	or.b		d1,d0
	eor.b		#0xff,d0
.if 0
	swap		d7
	move.w		d7,d1
	eor.w		d0,d1		/* changed btns */
	move.w		d0,d7		/* old val */
	swap		d7
	and.w		d0,d1		/* what changed now */
.endif
	rts
.global get_input_end
get_input_end:

.global get_line
get_line:
    movea.l     #0xc00008, a0
    moveq.l     #0, d0
0:
    move.b      (a0), d0    /*  8 d2 = vcnt */
    cmp.b       (a0), d0    /*  8 reread for super-rare corruption (torn read) */
    bne         0b
    rts

.global write_and_read1 /* u32 a, u16 d, void *dst */
write_and_read1:
    ldarg       0, 0, a0
    ldargw      1, 0, d0
#ifndef PICO
    move.w      d0, (a0)
#else
    /* different timing due to extra fetch of offset, */
    /* less troulesome to emulate */
    movea.l     a0, a1
    subq.l      #1, a1
    move.w      d0, 1(a1)
#endif
    move.l      (a0), d0
    move.l      (a0), d1

    ldarg       2, 0, a1
    move.l      d0, (a1)+
    move.l      d1, (a1)+
    rts

.global move_sr /* u16 sr */
move_sr:
    ldargw      0, 0, d0
    move.w      d0, sr
    rts

.global move_sr_and_read /* u16 sr, u32 a */
move_sr_and_read:
    ldargw      0, 0, d0
    ldarg       1, 0, a0
    move.w      d0, sr
    move.w      (a0), d0
    rts

.global read_sr
read_sr:
    move.w      sr, d0
    rts

.global memcpy_ /* void *dst, const void *src, u16 size */
memcpy_:
    ldarg       0, 0, a0
    ldarg       1, 0, a1
    ldargw      2, 0, d0
    subq.w      #1, d0
0:
    move.b      (a1)+, (a0)+      /* not in a hurry */
    dbra        d0, 0b
    rts

.global memset_ /* void *dst, int d, u16 size */
memset_:
    ldarg       0, 0, a0
    ldargw      1, 0, d1
    ldargw      2, 0, d0
    subq.w      #1, d0
0:
    move.b      d1, (a0)+         /* not in a hurry */
    dbra        d0, 0b
    rts

# tests

.global test_vcnt_vb
test_vcnt_vb:
    movem.l     d2-d7/a2, -(sp)
    movea.l     #0xc00007, a0
    movea.l     #0xc00008, a1
    movea.l     #0xff0000, a2
    moveq.l     #0, d4          /* d4 = count */
    moveq.l     #0, d5          /* d5 = vcnt_expect */
                                /* d6 = old */
    move.l      #1<<(3+16), d7  /* d7 = SR_VB */
0:
    btst        #3, (a0)
    beq         0b          /* not blanking */
0:
    btst        #3, (a0)
    bne         0b          /* blanking */

    addq.l      #1, a0
0:
    tst.b       (a0)
    bne         0b          /* not line 0 */

    subq.l      #2, a0
    move.l      (a0), d6
    move.l      d6, (a2)+   /* d0 = old */
###
0:
    move.b      (a1), d2    /*  8 d2 = vcnt */
    cmp.b       (a1), d2    /*  8 reread for corruption */
    bne 0b                  /* 10 on changing vcounter? */
    cmp.b       d2, d5      /*  4 vcnt == vcnt_expect? */
    beq         0b          /* 10 */
    move.l      (a0), d0    /* 12 */
    tst.b       d2          /*  4 */
    beq         3f
1:
    addq.l      #1, d4      /* count++ */
    addq.l      #1, d5
    cmp.b       d2, d5
    bne         2f          /* vcnt == vcnt_expect + 1 */
    move.l      d0, d1
    eor.l       d6, d1
    and.l       d7, d1      /* (old ^ val) & vb */
    bne         2f
    move.l      d0, d6      /* old = val */
    bra         0b

2: /* vcnt jump or vb change */
    move.l      d6, (a2)+   /* *ram++ = old */
    move.l      d0, (a2)+   /* *ram++ = val */
    move.b      d2, d5      /* vcnt_expect = vcnt */
    move.l      d0, d6      /* old = val */
    bra         0b

3: /* vcnt == 0 */
    move.l      d0, d1
    and.l       d7, d1
    bne         1b          /* still in VB */

    move.l      d0, (a2)+   /* *ram++ = val */
    move.l      d4, (a2)+   /* *ram++ = count */

    movem.l     (sp)+, d2-d7/a2
    rts

.global test_vcnt_loops
test_vcnt_loops:
    movem.l     d2, -(sp)
    movea.l     #0xc00007, a0
    movea.l     #0xfff000, a1
    move.b      #0xff, d0       /* d0 = current_vcnt */
    moveq.l     #0, d1          /* d1 = loop counter */
    move.w      #315-1, d2      /* d2 = line limit */
0:
    btst        #3, (a0)
    beq         0b          /* not blanking */
0:
    btst        #3, (a0)
    bne         0b          /* blanking */

    addq.w      #1, a0
0:
    addq.w      #1, d1      /*  4 */
    cmp.b       (a0), d0    /*  8 vcnt changed? */
    beq         0b          /* 10 */

    move.w      d0, (a1)+   /*  8 save */
    move.w      d1, (a1)+
    move.b      (a0), d0    /*  8 new vcnt */
    moveq.l     #0, d1
    dbra        d2, 0b

    movem.l     (sp)+, d2
    rts

.global test_hint
test_hint:
    move.w      d0, -(sp)         /*  8 */
    move.w      (0xc00008).l, d0  /* 16 */
    addq.w      #1, (0xf000).w    /* 16 */
    tst.w       (0xf002).w        /* 12 */
    bne         0f                /* 10 */
    move.w      d0, (0xf002).w    /* 12 */
0:
    move.w      d0, (0xf004).w    /* 12 */
    move.w      (sp)+, d0         /*  8 */
    rte                           /* 20 114+44 */
.global test_hint_end
test_hint_end:

.global test_vint
test_vint:
    move.w      d0, -(sp)         /*  8 */
    move.w      (0xc00008).l, d0  /* 16 */
    addq.w      #1, (0xf008).w    /* 16 */
    tst.w       (0xf00a).w        /* 12 */
    bne         0f                /* 10 */
    move.w      d0, (0xf00a).w    /* 12 */
0:
    move.w      d0, (0xf00c).w    /* 12 */
    move.w      (sp)+, d0         /*  8 */
    rte                           /* 20 114 */
.global test_vint_end
test_vint_end:

.global x32x_enable
x32x_enable:
    movea.l     #0xa15100, a0
    movea.l     #0xa15122, a1
    move.w      #1, (a0)          /* ADEN */
# wait for min(20_sh2_cycles, pll_setup_time)
# pll time is unclear, icd_mars.prg mentions 10ms which sounds
# way too much. Hope 40 68k cycles is enough
    move.w      #40/10, d0
0:
    dbra        d0, 0b
    move.w      #3, (a0)          /* ADEN, nRES */
0:
    move.w      #0xffff, d0       /* waste some cycles */
    tst.w       (a1)
    beq         0b                /* master BIOS busy */

0:                                /* for slave, use a limit, as it */
    tst.w       4(a1)             /* won't respond on master error. */
    dbne        d0, 0b            /* slave BIOS busy */

    or.w        #1, 6(a0)         /* RV */
    rts
.global x32x_enable_end
x32x_enable_end:

.global x32x_disable
x32x_disable:
    movea.l     #0xa15100, a0
    move.w      #0, 6(a0)         /* RV=0 */
    move.w      #1, (a0)          /* ADEN (reset sh2) */
    move.w      #0, (a0)          /* adapter disable, reset sh2 */
    move.w      #1, d0
0:
    dbra        d0, 0b
    move.w      #2, (a0)          /* nRES - sh2s should see no ADEN and sleep */
    rts
.global x32x_disable_end
x32x_disable_end:

.global test_32x_b_c0
test_32x_b_c0:
    ldarg       0, 0, a1
    ldargw      1, 0, d0
    jsr         (0xc0).l          /* move.b d0, (a1); RV=0 */
    bset        #0, (0xa15107).l  /* RV=1 */
    rts
.global test_32x_b_c0_end
test_32x_b_c0_end:

# some nastyness from Fatal Rewind
.global test_h_v_2
test_h_v_2:
    move.w      #0x2000, sr
    move.w      #0x8014, (0xFFC00004).l
    move.w      #0x8164, (0xFFC00004).l
    move.w      #1, d0
0:
    dbra        d0, 0b
    move.w      #0x2700, sr
    rts

.global test_v_h_2
test_v_h_2:
    move.w      #0x2000, sr
    movea.l     #0xc00004, a0
    move.w      #0x8164, (a0)
    move.w      #0x8144, (a0)
    move.w      #480/2/10-1, d0
0:
    dbra        d0, 0b
    move.w      #0x8164, (0xFFC00004).l
    move.w      #0x8014, (0xFFC00004).l
    move.w      #0x2700, sr
    rts

.global test_f_vint
test_f_vint:
    move.w      (a1), d0
    rte
.global test_f_vint_end
test_f_vint_end:

.global test_f
test_f:
    movea.l     #0xc00005, a0
    movea.l     #0xc00004, a1
    move.w      #0x2000, sr
0:
    btst        #3, (a0)
    bne         0b
0:
    btst        #3, (a0)
    beq         0b
    movem.l     d2-d7/a2, -(sp)
    move.l      (a1), d1
    move.l      (a1), d2
    move.l      (a1), d3
    move.l      (a1), d4
    move.l      (a1), d5
    move.l      (a1), d6
    move.w      #0x2700, sr
    movea.l     #0xff0000, a0
    move.b      d0, (a0)+
    move.b      #0, (a0)+
.macro test_lb_s sr, dr
    swap        \sr
    move.b      \sr, (\dr)+
    swap        \sr
    move.b      \sr, (\dr)+
.endm
    test_lb_s   d1, a0
    test_lb_s   d2, a0
    test_lb_s   d3, a0
    test_lb_s   d4, a0
    test_lb_s   d5, a0
    movem.l     (sp)+, d2-d7/a2
    rts

.global test_hb
test_hb:
    movem.l     d2-d7, -(sp)
    movea.l     #0xc00004, a0
    movea.l     #0xc00008, a1
    moveq.l     #1, d0
0:
    cmp.b       (a1), d0
    beq         0b
0:
    cmp.b       (a1), d0
    bne         0b
    move.l      (a0), d0
    move.l      (a0), d1
    move.l      (a0), d2
    movea.l     #0xff0000, a1
    movea.l     #0xff0000, a1
    nop
    nop
    move.l      (a0), d3
    move.l      (a0), d4
    move.l      (a0), d5
    move.l      (a0), d6
    move.l      (a0), d7
    test_lb_s   d0, a1
    test_lb_s   d1, a1
    test_lb_s   d2, a1
    test_lb_s   d3, a1
    test_lb_s   d4, a1
    test_lb_s   d5, a1
    test_lb_s   d6, a1
    test_lb_s   d7, a1
    movem.l     (sp)+, d2-d7
    rts

.macro ymwrite  areg, dreg addr dat
    move.b      \addr, (\areg) /* 12 addr */
    nbcd        d0             /*  6 delay to reach 17 ym cycles (M/7) */
    move.b      \dat, (\dreg)  /* 12 data */
.endm

.global test_ym_stopped_tick
test_ym_stopped_tick:
    movem.l     a2-a3, -(sp)
    movea.l     #0xa04000, a0
    movea.l     #0xa04001, a1
    movea.l     #0xc00007, a2
    movea.l     #0xfff000, a3

    ymwrite     a0, a1, #0x27, #0x30  /* 30 disable, clear */
    ymwrite     a0, a1, #0x26, #0xff  /* 30 timer b shortest interval */
    move.b      #0x27, (a0)           /* 12 addr prep */
0:
    btst        #3, (a2)
    beq         0b                    /* not blanking */
0:
    btst        #3, (a2)
    bne         0b                    /* blanking */

    addq.l      #1, a2
0:
    tst.b       (a2)
    bne         0b                    /* not line 0 - waiting for sequential vcnt */

    move.b      #0x0a, (a1)           /* 12 start timer b  */
    moveq.l     #0, d0
    moveq.l     #2, d1
0:
    move.b      (a0), d0
    and.b       d1, d0
    beq         0b
0:
#    move.w      (a2), (a3)+           /* 12 save hvcnt */
    move.b      (a2), d0
    cmp.b       (a2), d0
    bne         0b
    move.w      d0, (a3)+
    move.b      #0x30, (a1)           /* 12 stop b, clear  */

    move.w      #(1900/10-1), d0      /* waste cycles */
0:
    dbra        d0, 0b
    moveq.l     #0, d0

    move.w      (a0), (a3)+           /* 12 save status */
    move.b      #0x0a, (a1)           /* 12 start b  */
0:
    move.b      (a0), d0
    and.b       d1, d0
    beq         0b

0:
#    move.w      (a2), (a3)+           /* 12 save hvcnt */
    move.b      (a2), d1
    cmp.b       (a2), d1
    bne         0b
    move.w      d1, (a3)+
    move.w      d0, (a3)+             /* 12 save status */

    movem.l     (sp)+, a2-a3
    rts

.global test_ym_ab_sync
test_ym_ab_sync:
    movea.l     #0xa04000, a0
    movea.l     #0xa04001, a1

    ymwrite     a0, a1, #0x27, #0x30  /* 30 disable, clear */
    ymwrite     a0, a1, #0x24, #0xfc  /* 30 timer a */
    ymwrite     a0, a1, #0x25, #0x01  /* 30  =15 */
    ymwrite     a0, a1, #0x26, #0xff  /* 30 timer b shortest interval */
    move.b      #0x27, (a0)           /* 12 addr prep */
    nop
    nop

    move.b      #0x0a, (a1)           /* 12 start timer b  */
    moveq.l     #0, d0
    moveq.l     #2, d1
0:
    move.b      (a0), d0              /*  8 */
    and.b       d1, d0                /*  4 */
    beq         0b                    /* 10|8 */

    move.b      #0x3f, (a1)           /* 12 start a, clear  */

    move.w      #(488/10), d0         /* waste cycles */
0:  dbra        d0, 0b

    ymwrite     a0, a1, #0x24, #0xf0  /* 30 show that rewriting count */
    ymwrite     a0, a1, #0x25, #0x00  /* 30 does nothing until timer expires */

    move.w      #(488*2/10), d0       /* waste cycles */
0:  dbra        d0, 0b

    ymwrite     a0, a1, #0x26, #0xfc  /* 30 same for timer b */
    ymwrite     a0, a1, #0x27, #0x0f  /* 30 setting already set bits too  */
    moveq.l     #0, d0
    moveq.l     #3, d1
0:
    move.b      (a0), d0
    and.b       d1, d0
    beq         0b
    move.b      (a0), d0              /* re-read, else very occasionally get 1 */
    rts

.global test_ym_ab_sync2
test_ym_ab_sync2:
    movea.l     #0xa04000, a0
    movea.l     #0xa04001, a1

    move.b      #0x0f, (a1)           /* 12 enable  */
    moveq.l     #0, d0
    moveq.l     #3, d1
    nop                               /*  4 need ~12c to clear */
0:
    move.b      (a0), d0              /*  8 */
    and.b       d1, d0                /*  4 */
    beq         0b                    /* 10|8 */
    move.b      (a0), d0              /* re-read */
    move.b      #0x3c, (a1)           /* 12 clear, disable  */
    rts

.global x32x_switch_rv
x32x_switch_rv:
    ldargw      0, 0, d0
    move.l      (sp)+, d1
    movea.l     #0xa15106, a0
    btst        #0, d0
    bne         0f
    move.w      #0, (a0)
    or.l        #0x880000, d1
    bra         1f
0:
    move.w      #1, (a0)
    and.l       #0x07ffff, d1
1:
    movea.l     d1, a1
    jmp         (a1)
.global x32x_switch_rv_end
x32x_switch_rv_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
