.section .rodata

# I don't know why multiple different .align don't work..
.align 0x8000

.global font_base
font_base:
.incbin "font.bin"

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
