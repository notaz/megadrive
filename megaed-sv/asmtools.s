# Assemble with gas
#   --register-prefix-optional --bitwise-or

.macro ldarg  arg, stacksz, reg
    move.l (4 + \arg * 4 + \stacksz)(%sp), \reg
.endm


.globl read_joy_responses /* u8 *rbuf */
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


# vim:filetype=asmM68k:ts=4:sw=4:expandtab
