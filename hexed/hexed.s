##################################################
#                                                #
# Assemble with gas                              #
#   --register-prefix-optional --bitwise-or      #
#                                                #
##################################################

.text
.globl main

##################################################
#                                                #
#        Register and bitmask definitions        #
#                                                #
##################################################

.equ GFXDATA, 		0xc00000
.equ GFXCNTL, 		0xc00004

.equ VDP0_E_HBI, 	0x10
.equ VDP0_E_DISPLAY, 	0x02 
.equ VDP0_PLTT_FULL, 	0x04 

.equ VDP1_SMS_MODE,	0x80
.equ VDP1_E_DISPLAY,	0x40
.equ VDP1_E_VBI,	0x20
.equ VDP1_E_DMA,	0x10
.equ VDP1_NTSC,		0x00
.equ VDP1_PAL,		0x08
.equ VDP1_RESERVED,	0x04

.equ VDP12_SPR_SHADOWS,	0x08
.equ VDP12_SCREEN_V224,	0x00
.equ VDP12_SCREEN_V448,	0x04
.equ VDP12_PROGRESSIVE,	0x00
.equ VDP12_INTERLACED,	0x02
.equ VDP12_SCREEN_H256,	0x00
.equ VDP12_SCREEN_H320,	0x81

.equ VDP16_MAP_V32,	0x00
.equ VDP16_MAP_V64,	0x10
.equ VDP16_MAP_V128,	0x30
.equ VDP16_MAP_H32,	0x00
.equ VDP16_MAP_H64,	0x01
.equ VDP16_MAP_H128,	0x03



##################################################
#                                                #
#                   MACROS                       #
#                                                #
##################################################


/* Write val to VDP register reg */
.macro write_vdp_reg reg val
	move.w #((\reg << 8) + 0x8000 + \val),(a3)
.endm


/* For immediate addresses */
.macro VRAM_ADDR reg adr
	move.l #(((0x4000 + (\adr & 0x3fff)) << 16) + (\adr >> 14)),\reg
.endm


.macro CRAM_ADDR reg adr
	move.l	#(((0xc000 + (\adr & 0x3fff)) << 16) + (\adr >> 14)),\reg
.endm


# make VDP word from address adr and store in d0
.macro XRAM_ADDR_var adr
	move.l \adr,d0
	lsl.l #8,d0
	lsl.l #8,d0
	rol.l #2,d0
	lsl.b #2,d0
	lsr.l #2,d0
.endm


.macro VRAM_ADDR_var adr
	XRAM_ADDR_var \adr
	or.l #0x40000000,d0
.endm


.macro CRAM_ADDR_var adr
	XRAM_ADDR_var \adr
	or.l #0xc0000000,d0
.endm


.macro VSCROLL_ADDR reg adr
	move.l	#(((0x4000 + (\adr & 0x3fff)) << 16) + ((\adr >> 14) | 0x10)),\reg
.endm


.macro HSCROLL_ADDR reg adr
	move.l #(((0x4000 + (\adr & 0x3fff)) << 16) + (\adr >> 14)),\reg
.endm


#################################################
#                                               #
#                    DATA                       #
#                                               #
#################################################

colors:
	dc.w 0x0000,0x0eee
colors_end:

# pattern:


sprite_data:
	/*         Y        size     link          attr        X */
	dc.w       0;  dc.b 0x05;  dc.b 0;  dc.w 0x6002;  dc.w 0
sprite_data_end:

hello:
	.ascii	"hello world"

##################################################
#                                                #
#               MAIN PROGRAM                     #
#                                                #
##################################################
 
.align 2

main:
	/* Initialize VDP */
	jsr 		init_gfx

	/* Load color data */
	movea.l		#0,a0
	movea.l		#colors,a1
	moveq.l		#(colors_end-colors)/2,d0
	jsr		load_colors

	/* load patterns */
	movea.l		#0,a0
	movea.l		#font,a1
	move.l		#128,d0
	jsr		load_tiles

	/* generate A layer map */
	movea.l		#0xe000,a6
	move.l		#28-1,d4
lmaploop0:
	movea.l		a6,a0
	jsr		load_prepare

	move.l		#64/2-1,d3
0:	move.l		#0x00000000,(a0)
	dbra		d3,0b

	add.l		#64*2,a6
	dbra 		d4,lmaploop0

	/* generate B layer map */
	movea.l		#0xc000,a0
	jsr		load_prepare

	move.l		#64*28/2-1,d3
0:	move.l		#0x00000000,(a0)
	dbra		d3,0b

	/* upload sprite data */
	movea.l		#0xfc00,a0
	jsr		load_prepare
	movea.l		#sprite_data,a1

	move.l		#(sprite_data_end-sprite_data)/2-1,d3
0:	move.l		(a1)+,(a0)
	dbra		d3,0b

	jsr		wait_vsync

	movea.l		#hello,a0
	moveq.l		#1,d0
	moveq.l		#1,d1
	jsr		print

##################################################
#                                                #
#                 MAIN LOOP                      #
#                                                #
##################################################

forever:


	jsr		wait_vsync
	bra 		forever
	


#################################################
#                                               #
#         Initialize VDP registers              #
#                                               #
#################################################

init_gfx:
	move.l 		#GFXCNTL,a3
	write_vdp_reg 	0,(VDP0_E_DISPLAY | VDP0_PLTT_FULL)
	write_vdp_reg 	1,(VDP1_E_VBI | VDP1_E_DISPLAY | VDP1_E_DMA | VDP1_RESERVED)
	write_vdp_reg 	2,(0xe000 >> 10)	/* Screen map a adress */
	write_vdp_reg 	3,(0xe000 >> 10)	/* Window address */
	write_vdp_reg 	4,(0xc000 >> 13)	/* Screen map b address */
	write_vdp_reg 	5,(0xfc00 >>  9)	/* Sprite address */
	write_vdp_reg 	6,0	
	write_vdp_reg	7,0			/* Backdrop color */
	write_vdp_reg	10,1			/* Lines per hblank interrupt */
	write_vdp_reg	11,0			/* 2-cell vertical scrolling */
	write_vdp_reg	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320)
	write_vdp_reg	13,(0x8000 >> 10)	/* Horizontal scroll address */
	write_vdp_reg	15,2
	write_vdp_reg	16,(VDP16_MAP_V32 | VDP16_MAP_H64)	/* layer size */
	write_vdp_reg	17,0
	write_vdp_reg	18,0xff
	rts


# Load tile data from ROM
#  a0: VRAM base
#  a1: pattern address
#  d0: number of tiles to load
#  destroys d1

load_tiles:
	move.l		d0,d1
	VRAM_ADDR_var 	a0
	move.l 		d0,(GFXCNTL).l
	
	move.l 		#GFXDATA,a0
	lsl.w		#3,d1
	subq.l 		#1,d1
0:
	move.l 		(a1)+,(a0)
	dbra 		d1,0b

	rts


# Prepare to write to VDP RAM @a3
# sets a0 to VDP data port for convenience
#  a0: VRAM base
#  destroys d0

load_prepare:
	VRAM_ADDR_var 	a0
	move.l 		d0,(GFXCNTL).l
	move.l 		#GFXDATA,a0
	rts


# Load color data from ROM
#  a0: CRAM base
#  a1: color list address
#  d0: number of colors to load
#  destroys d1

load_colors:
	move.l		d0,d1
	CRAM_ADDR_var 	a0
	move.l 		d0,(GFXCNTL).l

	move.l 		#GFXDATA,a0
	subq.w		#1,d1
0:
	move.w		(a1)+,(a0)
	dbra		d1,0b

	rts


# print
#  a0 - string
#  d0 - x
#  d1 - y 
#  destroys a1

print:
	move.l		a0,a1
	lsl.w		#6,d1
	add.w		d1,d0
	lsl.w		#1,d0
	movea.l		#0xe000,a0
	add.w		d0,a0
	jsr		load_prepare
	moveq.l		#0,d0

_print_loop:
	move.b		(a1)+,d0
	beq		_print_end

	move.w		d0,(a0)
	jmp		_print_loop

_print_end:
	rts


#################################################
#                                               #
#       Wait for next VBlank interrupt          #
#                                               #
#################################################

wait_vsync:
	movea.l		#vtimer,a0
	move.l		(a0),a1
_wait_change:
	stop		#0x2000
	cmp.l		(a0),a1
	beq		_wait_change
	rts


#################################################
#                                               #
#                 RAM DATA                      #
#                                               #
#################################################

.bss

# used by sega_gcc.s
.globl htimer
.globl vtimer
.globl rand_num
htimer:		.long 0
vtimer:		.long 0
rand_num:	.long 0

#

.end

# vim:filetype=asmM68k
