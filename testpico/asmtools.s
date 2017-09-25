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

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
