.section .rodata

# I don't know why multiple different .align don't work..
.align 0x8000

.global font_base
font_base:
.incbin "font.bin"

.align 0x8000
.global test_data
test_data:
.incbin "test.bin"
.global test_data_end
test_data_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
