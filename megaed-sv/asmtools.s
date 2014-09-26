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


.global run_game /* u16 mapper */
run_game:
	move.w		#0x2700, sr
    ldarg       0, 0, d7
    move.l      #0xa10000, a7
    moveq.l     #0x00, d1
    move.b      #0xff, d2
    move.b      d1, (0x0b,a7) /* CtrlB */
    move.b      d1, (0x0d,a7) /* CtrlC */
    move.b      d2, (0x0f,a7) /* TxDataA */
    move.b      d1, (0x13,a7) /* S-CtrlA */
    move.b      d2, (0x15,a7) /* TxDataB */
    move.b      d1, (0x19,a7) /* S-CtrlB */
    move.b      d2, (0x1b,a7) /* TxDataC */
    move.b      d1, (0x1f,a7) /* S-CtrlC */

    move.l      #0xff0000, a1
    move.l      #0x10000/4/4-1, %d0
0:
    move.l      d1, (%a1)+
    move.l      d1, (%a1)+
    move.l      d1, (%a1)+
    move.l      d1, (%a1)+
    dbra        d0, 0b

    lea         (run_game_code,pc), a0
    move.l      #0xfff000, a1
    move.l      #(run_game_code_end - run_game_code)/2-1, d0
0:
    move.w      (%a0)+, (%a1)+
    dbra        d0, 0b
    jmp         0xfff000

run_game_code:
    move.w      #0x3210, (0xA13006)

    move.w      d7, (0xA13010)
    move.w      #0, (0xA13000)
    
    move.l      (0x00), a7
    move.l      (0x04), a0
    jmp         (a0)
run_game_code_end:


# vim:filetype=asmM68k:ts=4:sw=4:expandtab
