.section .rodata
.align 2

.global font_base
font_base:
.incbin "font.bin"

.global z80_test
.global z80_test_end
z80_test:
.incbin "z80_test.bin80"
z80_test_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
