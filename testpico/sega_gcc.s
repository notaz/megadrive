exc_tab:
    dc.l     0, 0x200, exc02, exc03, exc04, exc05, exc06, exc07
    dc.l exc08, exc09, exc0a, exc0b, exc0c, exc0d, exc0e, exc0f
    dc.l exc10, exc11, exc12, exc13, exc14, exc15, exc16, exc17
    dc.l exc18, exc19, exc1a, exc1b, 0xff0100, exc1d, 0xff0140, exc1f
    dc.l exc20, exc21, exc22, exc23, exc24, exc25, exc26, exc27
    dc.l exc28, exc29, exc2a, exc2b, exc2c, exc2d, exc2e, exc2f
    dc.l exc30, exc31, exc32, exc33, exc34, exc35, exc3e, exc37
    dc.l exc38, exc39, exc3a, exc3b, exc3c, exc3d, exc3e, exc3f

    .ascii "SEGA 32X                        "
    .ascii "PD testsuite                                    "
    .ascii "PD testsuite                                    "
    .ascii "GM 00000000-00"
    .byte 0x00,0x00
    .ascii "J               "
    .long  0x000000,0x3fffff
    .long  0xff0000,0xffffff
    .ascii "RA"; .byte 0xf8,0x20                     /* 1b0 */
    .long  0x200001,0x20ffff                         /* 1b4 */
    .ascii "    "                                    /* 1bc */
    .ascii "                        "                /* 1c0 */
    .ascii "                        "
    .ascii "JUE             "                        /* 1f0 */

RST:
    move.w  #0x2700, %sr

    move.b (0xA10001), %d0
    andi.b #0x0F, %d0
    beq.s 0f
    move.l  #0x53454741, (0xA14000) /* 'SEGA' */
0:
	tst.w   (0xc00004).l

    moveq   #0, %d0
    movea.l %d0, %a7
    move    %a7, %usp

    /* clear .bss */
    lea     __bss_start, %a0
    lea     __end, %a1
0:
    move.l  %d0, (%a0)+
    cmp.l   %a1, %a0
    blt.s   0b

#    move.w  #0x2000, %sr
    jsr     main
0:
    bra     0b

#HBL:
#VBL:
#    rte

pre_exception:
    move.w  #0x2700, %sr
    movem.l %d0-%d7/%a0-%a7,-(%sp)
    move.l %sp, %d0
    move.l %d0,-(%sp)
    jsr exception
0:
    bra 0b

.macro exc_stub num
exc\num:
    move.w #0x\num, -(%sp)
    jmp pre_exception
.endm

exc_stub 02
exc_stub 03
exc_stub 04
exc_stub 05
exc_stub 06
exc_stub 07
exc_stub 08
exc_stub 09
exc_stub 0a
exc_stub 0b
exc_stub 0c
exc_stub 0d
exc_stub 0e
exc_stub 0f
exc_stub 10
exc_stub 11
exc_stub 12
exc_stub 13
exc_stub 14
exc_stub 15
exc_stub 16
exc_stub 17
exc_stub 18
exc_stub 19
exc_stub 1a
exc_stub 1b
HBL:
exc_stub 1c
exc_stub 1d
VBL:
exc_stub 1e
exc_stub 1f

    /* MARS data */
    .org 0x3c0
    .ascii "PD testsuite    "
    .long 0          /* version? not used */
    .long sh2_test   /* ROM src */
    .long 0          /* SDRAM dst */
    .long sh2_test_end-sh2_test /* length */
    .long 0x06000244 /* master entry */
    .long 0x06000248 /* slave entry */
    .long 0x06000000 /* master VBR */
    .long 0x06000124 /* slave VBR */
    /* Standard 32X startup code for MD side at 0x3F0 */
    .org 0x3f0
    .word 0x287C,0xFFFF,0xFFC0,0x23FC,0x0000,0x0000,0x00A1,0x5128
    .word 0x46FC,0x2700,0x4BF9,0x00A1,0x0000,0x7001,0x0CAD,0x4D41
    .word 0x5253,0x30EC,0x6600,0x03E6,0x082D,0x0007,0x5101,0x67F8
    .word 0x4AAD,0x0008,0x6710,0x4A6D,0x000C,0x670A,0x082D,0x0000
    .word 0x5101,0x6600,0x03B8,0x102D,0x0001,0x0200,0x000F,0x6706
    .word 0x2B78,0x055A,0x4000,0x7200,0x2C41,0x4E66,0x41F9,0x0000
    .word 0x04D4,0x6100,0x0152,0x6100,0x0176,0x47F9,0x0000,0x04E8
    .word 0x43F9,0x00A0,0x0000,0x45F9,0x00C0,0x0011,0x3E3C,0x0100
    .word 0x7000,0x3B47,0x1100,0x3B47,0x1200,0x012D,0x1100,0x66FA
    .word 0x7425,0x12DB,0x51CA,0xFFFC,0x3B40,0x1200,0x3B40,0x1100
    .word 0x3B47,0x1200,0x149B,0x149B,0x149B,0x149B,0x41F9,0x0000
    .word 0x04C0,0x43F9,0x00FF,0x0000,0x22D8,0x22D8,0x22D8,0x22D8
    .word 0x22D8,0x22D8,0x22D8,0x22D8,0x41F9,0x00FF,0x0000,0x4ED0
    .word 0x1B7C,0x0001,0x5101,0x41F9,0x0000,0x06BC,0xD1FC,0x0088
    .word 0x0000,0x4ED0,0x0404,0x303C,0x076C,0x0000,0x0000,0xFF00
    .word 0x8137,0x0002,0x0100,0x0000,0xAF01,0xD91F,0x1127,0x0021
    .word 0x2600,0xF977,0xEDB0,0xDDE1,0xFDE1,0xED47,0xED4F,0xD1E1
    .word 0xF108,0xD9C1,0xD1E1,0xF1F9,0xF3ED,0x5636,0xE9E9,0x9FBF
    .word 0xDFFF,0x4D41,0x5253,0x2049,0x6E69,0x7469,0x616C,0x2026
    .word 0x2053,0x6563,0x7572,0x6974,0x7920,0x5072,0x6F67,0x7261
    .word 0x6D20,0x2020,0x2020,0x2020,0x2020,0x2043,0x6172,0x7472
    .word 0x6964,0x6765,0x2056,0x6572,0x7369,0x6F6E,0x2020,0x2020
    .word 0x436F,0x7079,0x7269,0x6768,0x7420,0x5345,0x4741,0x2045
    .word 0x4E54,0x4552,0x5052,0x4953,0x4553,0x2C4C,0x5444,0x2E20
    .word 0x3139,0x3934,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020
    .word 0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020
    .word 0x2020,0x2020,0x2020,0x524F,0x4D20,0x5665,0x7273,0x696F
    .word 0x6E20,0x312E,0x3000,0x48E7,0xC040,0x43F9,0x00C0,0x0004
    .word 0x3011,0x303C,0x8000,0x323C,0x0100,0x3E3C,0x0012,0x1018
    .word 0x3280,0xD041,0x51CF,0xFFF8,0x4CDF,0x0203,0x4E75,0x48E7
    .word 0x81C0,0x41F9,0x0000,0x063E,0x43F9,0x00C0,0x0004,0x3298
    .word 0x3298,0x3298,0x3298,0x3298,0x3298,0x3298,0x2298,0x3341
    .word 0xFFFC,0x3011,0x0800,0x0001,0x66F8,0x3298,0x3298,0x7000
    .word 0x22BC,0xC000,0x0000,0x7E0F,0x3340,0xFFFC,0x3340,0xFFFC
    .word 0x3340,0xFFFC,0x3340,0xFFFC,0x51CF,0xFFEE,0x22BC,0x4000
    .word 0x0010,0x7E09,0x3340,0xFFFC,0x3340,0xFFFC,0x3340,0xFFFC
    .word 0x3340,0xFFFC,0x51CF,0xFFEE,0x4CDF,0x0381,0x4E75,0x8114
    .word 0x8F01,0x93FF,0x94FF,0x9500,0x9600,0x9780,0x4000,0x0080
    .word 0x8104,0x8F02,0x48E7,0xC140,0x43F9,0x00A1,0x5180,0x08A9
    .word 0x0007,0xFF80,0x66F8,0x3E3C,0x00FF,0x7000,0x7200,0x337C
    .word 0x00FF,0x0004,0x3341,0x0006,0x3340,0x0008,0x4E71,0x0829
    .word 0x0001,0x000B,0x66F8,0x0641,0x0100,0x51CF,0xFFE8,0x4CDF
    .word 0x0283,0x4E75,0x48E7,0x8180,0x41F9,0x00A1,0x5200,0x08A8
    .word 0x0007,0xFF00,0x66F8,0x3E3C,0x001F,0x20C0,0x20C0,0x20C0
    .word 0x20C0,0x51CF,0xFFF6,0x4CDF,0x0181,0x4E75,0x41F9,0x00FF
    .word 0x0000,0x3E3C,0x07FF,0x7000,0x20C0,0x20C0,0x20C0,0x20C0
    .word 0x20C0,0x20C0,0x20C0,0x20C0,0x51CF,0xFFEE,0x3B7C,0x0000
    .word 0x1200,0x7E0A,0x51CF,0xFFFE,0x43F9,0x00A1,0x5100,0x7000
    .word 0x2340,0x0020,0x2340,0x0024,0x1B7C,0x0003,0x5101,0x2E79
    .word 0x0088,0x0000,0x0891,0x0007,0x66FA,0x7000,0x3340,0x0002
    .word 0x3340,0x0004,0x3340,0x0006,0x2340,0x0008,0x2340,0x000C
    .word 0x3340,0x0010,0x3340,0x0030,0x3340,0x0032,0x3340,0x0038
    .word 0x3340,0x0080,0x3340,0x0082,0x08A9,0x0000,0x008B,0x66F8
    .word 0x6100,0xFF12,0x08E9,0x0000,0x008B,0x67F8,0x6100,0xFF06
    .word 0x08A9,0x0000,0x008B,0x6100,0xFF3C,0x303C,0x0040,0x2229
    .word 0x0020,0x0C81,0x5351,0x4552,0x6700,0x0092,0x303C,0x0080
    .word 0x2229,0x0020,0x0C81,0x5344,0x4552,0x6700,0x0080,0x21FC
    .word 0x0088,0x02A2,0x0070,0x303C,0x0002,0x7200,0x122D,0x0001
    .word 0x1429,0x0080,0xE14A,0x8242,0x0801,0x000F,0x660A,0x0801
    .word 0x0006,0x6700,0x0058,0x6008,0x0801,0x0006,0x6600,0x004E
    .word 0x7020,0x41F9,0x0088,0x0000,0x3C28,0x018E,0x4A46,0x6700
    .word 0x0010,0x3429,0x0028,0x0C42,0x0000,0x67F6,0xB446,0x662C
    .word 0x7000,0x2340,0x0028,0x2340,0x002C,0x3E14,0x2C7C,0xFFFF
    .word 0xFFC0,0x4CD6,0x7FF9,0x44FC,0x0000,0x6014,0x43F9,0x00A1
    .word 0x5100,0x3340,0x0006,0x303C,0x8000,0x6004,0x44FC,0x0001

exc_stub 20
exc_stub 21
exc_stub 22
exc_stub 23
exc_stub 24
exc_stub 25
exc_stub 26
exc_stub 27
exc_stub 28
exc_stub 29
exc_stub 2a
exc_stub 2b
exc_stub 2c
exc_stub 2d
exc_stub 2e
exc_stub 2f
exc_stub 30
exc_stub 31
exc_stub 32
exc_stub 33
exc_stub 34
exc_stub 35
exc_stub 36
exc_stub 37
exc_stub 38
exc_stub 39
exc_stub 3a
exc_stub 3b
exc_stub 3c
exc_stub 3d
exc_stub 3e
exc_stub 3f

.section .rodata
.align 2

.global sh2_test
.global sh2_test_end
sh2_test:
.incbin "sh2_test.binsh"
sh2_test_end:

# vim:filetype=asmM68k:ts=4:sw=4:expandtab
