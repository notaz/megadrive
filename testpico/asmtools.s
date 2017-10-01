# Assemble with gas
#   --register-prefix-optional --bitwise-or

.macro ldarg  arg, stacksz, reg
    move.l (4 + \arg * 4 + \stacksz)(%sp), \reg
.endm

.global burn10 /* u16 val */
burn10:
    ldarg       0, 0, d0
    subq.l      #1, d0
0:
    dbra        d0, 0b
    rts

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
#  destroys d1,d2
.global get_input
get_input:
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

.global do_vcnt_vb
do_vcnt_vb:
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

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
