# Assemble with gas
#   --register-prefix-optional --bitwise-or

.macro ldarg  arg, stacksz, reg
    move.l (4 + \arg * 4 + \stacksz)(%sp), \reg
.endm

# write with instructions that have bit0 clear
.global write_rreg_i0 /* u8 val */
write_rreg_i0:
    ldarg       0, 0, d0
    movea.l     #0xa130f1, a0
    move.b      d0, d0
    move.b      d0, d0
    move.b      d0, (a0)
    move.b      d0, d0
    move.b      d0, d0
    move.b      d0, d0
    move.b      d0, d0
    rts


.global write_rreg_i1 /* u8 val */
write_rreg_i1:
    ldarg       0, 0, d1
    movea.l     #0xa130f1, a0
    move.b      d1, d1
    move.b      d1, d1
    move.b      d1, (a0)
    move.b      d1, d1
    move.b      d1, d1
    move.b      d1, d1
    move.b      d1, d1
    rts


.global fillpx16 /* u8 *d, unsigned int blocks, u8 val */
fillpx16:
    ldarg       0, 0, a0
    ldarg       2, 0, d0
    move.b      d0, d1
    lsl.w       #8, d1
    or.w        d1, d0
    move.w      d0, d1
    swap        d0
    move.w      d1, d0
    ldarg       1, 0, d1
    subq.l      #1, d1
    movem.l     d2, -(sp)
    move.l      #32, d2
0:
    movep.l     d0, 0(a0)
    movep.l     d0, 8(a0)
    movep.l     d0, 16(a0)
    movep.l     d0, 24(a0)
    add.l       d2, a0
    dbra        d1, 0b

    movem.l     (sp)+, d2
    rts


.global checkpx4 /* u8 *d, unsigned int blocks, u8 val */
checkpx4:
    ldarg       0, 0, a0
    ldarg       2, 0, d0
    move.b      d0, d1
    lsl.w       #8, d1
    or.w        d1, d0
    move.w      d0, d1
    swap        d0
    move.w      d1, d0
    ldarg       1, 0, d1
    subq.l      #1, d1
    movem.l     d2-d4, -(sp)
    move.w      d1, d3
    move.w      d1, d4
    swap        d3
0:
    move.w      d4, d1
1:
    movep.l     0(a0), d2
    addq.l      #8, a0
    cmp.l       d0, d2
    dbne        d1, 1b
    dbne        d3, 0b

    movem.l     (sp)+, d2-d4
    move.l      d1, d0
    cmp.w       #-1, d1
    beq         0f
    moveq.l     #0, d0
0:
    rts


# vim:filetype=asmM68k:ts=4:sw=4:expandtab
