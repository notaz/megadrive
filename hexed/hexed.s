##################################################
#                                                #
# Assemble with gas                              #
#   --register-prefix-optional --bitwise-or      #
#                                                #
##################################################

.text
.globl main
.globl VBL

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

.equ VDP12_STE,		0x08
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

.equ MMODE_MAIN,	0
.equ MMODE_VAL_INPUT,	1
.equ MMODE_EDIT_VAL,	2
.equ MMODE_GOTO,	3


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


# convert tile coords in d0, d1 to nametable addr to a0
.macro XY2NT
	lsl.w		#6,d1
	add.w		d1,d0
	lsl.w		#1,d0
	movea.l		#0xe000,a0
	add.w		d0,a0
.endm

# check if some d-pad button (and modifier) is pressed
.macro do_dpad bit op val
	btst.l		#\bit,d0
	beq		0f
	\op.l		\val,a6
	bra		dpad_end
0:
.endm

# convert a6 to normal addr
#  destroys d0
.macro mk_a6_addr reg
	move.l		a6,\reg
	moveq.l		#0,d0
	move.b		\reg,d0
	lsr.l		#8,\reg
	add.l		d0,\reg
.endm

.macro change_mode mode_new mode_back
	and.w		#0xc0ff,d7
	or.w		#(\mode_back<<11)|(\mode_new<<8),d7
.endm

#################################################
#                                               #
#                    DATA                       #
#                                               #
#################################################

colors:
	dc.w 0x0000,0x0eee,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w 0x0000,0x02e2,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w 0x0000,0x0e44,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
colors_end:


sprite_data:
	/*         Y        size     link          attr        X */
	dc.w       0;  dc.b 0x05;  dc.b 0;  dc.w 0x6002;  dc.w 0
sprite_data_end:

txt_edit:
	.ascii	"- edit -\0"
txt_a_confirm:
	.ascii	"A-confirm\0"

##################################################
#                                                #
#               MAIN PROGRAM                     #
#                                                #
##################################################
 
.align 2

main:
	/* mask irqs during init */
	move.w		#0x2700,sr

	movea.l		#0,a6
	move.l		#0x8000,d7
	moveq.l		#0,d6

	/* Init pads */
	move.b		#0x40,(0xa10009).l
	move.b		#0x40,(0xa10003).l

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
	movea.l		#0xe000,a1
	move.l		#28-1,d4
lmaploop0:
	movea.l		a1,a0
	jsr		load_prepare

	move.l		#64/2-1,d3
0:	move.l		#0x00000000,(a0)
	dbra		d3,0b

	add.l		#64*2,a1
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

        move.w		#0x2000,sr

##################################################
#                                                #
#                 MAIN LOOP                      #
#                                                #
##################################################

# global regs:
# a6 = page_start[31:8]|cursor_offs[7:0]
# d7 = old_inputs[31:16]|edit_bytes[15:14]|g_mode_old[13:11]|g_mode[10:8]|irq_cnt[7:0]
# d6 = edit_word[31:8]|edit_done[7]|edit_pos[6:4]|autorep_cnt[3:0]

forever:


	jsr		wait_vsync
	bra 		forever
	


VBL:
	addq.b		#1,d7
	movem.l		d0-d5/a0-a5,-(a7)

	moveq.l		#0,d0
	move.w		d7,d0
	lsr.w		#6,d0
	and.w		#0x1c,d0
	move.l		(jumptab,pc,d0),a0
	jmp		(a0)
jumptab:
	dc.l		mode_main
	dc.l		mode_val_input
	dc.l		mode_edit_val	/* edit val in editor */
	dc.l		mode_goto
	dc.l		mode_main
	dc.l		mode_main
	dc.l		mode_main
	dc.l		mode_main

##################### main #######################

mode_main:
	clr.l		d1
	move.l		a6,d0
	move.b		d0,d1
	lsr.l		#8,d0
	move.l		d0,a1		/* current addr */
	lsr.b		#3,d1
	neg.b		d1
	add.b		#27-1,d1	/* line where the cursor sits */
	swap		d1

	movea.l		#0xe004,a2
	move.l		#27-1,d5	/* line counter for dbra */
	or.l		d1,d5

draw_column:
	move.l		a2,a0
	jsr		load_prepare

	/* addr */
	move.l		a1,d2
	moveq.l		#6,d3
	jsr		print_hex_preped

	/* 4 shorts */
	moveq.l		#4,d3
	moveq.l		#4-1,d4
draw_shorts:
	move.w		#' ',(a0)
	move.w		(a1)+,d2
	jsr		print_hex_preped
	dbra		d4,draw_shorts

	move.l		d5,d0
	swap		d0
	cmp.w		d5,d0
	beq		draw_cursor

draw_chars_pre:
	move.l		#(' '<<16)|' ',(a0)

	/* 8 chars */
	subq.l		#8,a1
	moveq.l		#8-1,d4
draw_chars:
	move.b		(a1)+,d0
	move.b		d0,d1
	sub.b		#0x20,d1
	cmp.b		#0x60,d1
	blo		0f
	move.w		#'.',d0
0:
	move.w		d0,(a0)
	dbra		d4,draw_chars

	add.w		#0x80,a2
	dbra		d5,draw_column

	/* status bar */
	movea.l		#0xe004+64*2*27,a0
	jsr		load_prepare
	mk_a6_addr	d2
	move.l		#0x4006,d3
	jsr		print_hex_preped

	/* handle input */
	jsr		get_input		/* x0cbrldu x1sa00du */

	btst.l		#16+4,d0		/* A - scroll modifier */
	beq		input_noa

	do_dpad		16+0,  sub, #0x0800
	do_dpad		16+1,  add, #0x0800
	do_dpad		16+10, sub, #0xd800
	do_dpad		16+11, add, #0xd800
input_noa:
	moveq.l		#0,d1
	move.w		d7,d1
	lsr.w		#7,d1
	lsr.w		#7,d1

	do_dpad		0,  subq, #0x0008
	do_dpad		1,  addq, #0x0008
	do_dpad		10, sub, d1
	do_dpad		11, add, d1

dpad_end:
	/* update addr */
	move.l		a6,d1
	cmp.b		#0xf0,d1
	blo		0f
	sub.l		#0xd800,a6
	add.w		#0x00d8,a6
	bra		1f
0:
	cmp.b		#0xd8,d1
	blo		1f
	add.l		#0xd800,a6
	sub.w		#0x00d8,a6
1:

	/* other btns */
	moveq.l		#0,d1
	btst.l		#12,d0			/* B - switch byte/word mode */
	beq		input_nob
	bclr.l		#15,d7
	add.w		#0x4000,d7		/* changes between 01 10 */
	move.l		a6,d1
	and.l		#1,d1
	sub.l		d1,a6			/* make even, just in case */

input_nob:
	btst.l		#13,d0			/* C - edit selected byte */
	beq		input_noc

	change_mode	MMODE_EDIT_VAL, MMODE_MAIN
	write_vdp_reg	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_STE)

input_noc:
	btst.l		#5,d0			/* Start - goto */
	beq		input_nos

	change_mode	MMODE_GOTO, MMODE_MAIN
	write_vdp_reg	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_STE)

input_nos:
vbl_end:
	movem.l		(a7)+,d0-d5/a0-a5
	rte


draw_cursor:
	move.l		a6,d0
	and.l		#7,d0		/* byte offs */
	move.l		d0,d1
	lsr.b		#1,d1		/* which word */
	move.b		d1,d2
	lsl.b		#2,d2
	add.b		d2,d1		/* num of chars to skip */
	lsl.b		#1,d1
	move.w		#0x2004,d3

	btst.l		#15,d7
	bne		draw_cursor_word

draw_cursor_byte:
	move.b		(-8,a1,d0),d2
	and.b		#1,d0
	lsl.b		#2,d0
	add.b		d0,d1
	subq.b		#2,d3
	bra		0f

draw_cursor_word:
	move.w		(-8,a1,d0),d2
0:
	lea		(7*2,a2,d1),a0
	jsr		load_prepare
	jsr		print_hex_preped

	move.l		a2,a0
	add.w		#26*2,a0
	jsr		load_prepare	/* restore a0 */

	jmp		draw_chars_pre


#################### hedit #######################

mode_edit_val:
	btst.l		#7,d6
	bne		mode_hedit_finish

	/* read val to edit */
	mk_a6_addr	d1
	move.l		d1,a0
	moveq.l		#0,d0
	btst.l		#15,d7
	bne		0f
	move.b		(a0),d0
	bra		1f
0:
	move.w		(a0),d0
1:
	lsl.l		#8,d0
	and.l		#0xff,d6
	or.l		d0,d6

	and.b		#0x0f,d6	/* not done, reset pos */
	change_mode	MMODE_VAL_INPUT, MMODE_EDIT_VAL
	jmp		vbl_end

mode_hedit_finish:
	/* write the val */
	mk_a6_addr	d1
	move.l		d1,a0
	move.l		d6,d0
	lsr.l		#8,d0

	btst.l		#15,d7
	bne		0f
	move.b		d0,(a0)
	bra		1f
0:
	move.w		d0,(a0)
1:

	and.l		#0xf,d6		/* forget val and pos */
	bra		return_to_main

##################### goto #######################

mode_goto:
	btst.l		#7,d6
	bne		mode_goto_finish

	or.w		#0xc000,d7	/* 3 bytes */
	bclr.l		#7,d6
	change_mode	MMODE_VAL_INPUT, MMODE_GOTO
	jmp		vbl_end

mode_goto_finish:
	move.l		d6,d0
	lsr.l		#8,d0
	move.l		d0,d1
	and.l		#7,d1
	and.b		#0xf8,d0
	lsl.l		#8,d0
	or.l		d1,d0
	move.l		d0,a6

	and.w		#0x3fff,d7
	or.w		#0x8000,d7	/* back to 2 bytes */
	bra		return_to_main

################### val edit #####################

mode_val_input:
	/* frame */
	movea.l		#0xe000+14*2+11*64*2,a1
	moveq.l		#6-1,d1
0:
	move.w		a1,a0
	jsr		load_prepare
	moveq.l		#11-1,d0
1:
	move.w		#0,(a0)
	dbra		d0,1b

	add.w		#64*2,a1
	dbra		d1,0b

	/* text */
	lea		txt_edit,a0
	move.l		#15,d0
	move.l		#11,d1
	move.l		#0xc000,d2
	jsr		print

	lea		txt_a_confirm,a0
	move.l		#15,d0
	move.l		#15,d1
	move.l		#0xc000,d2
	jsr		print

	/* edit field */
	moveq.l		#0,d0
	moveq.l		#0,d1
	moveq.l		#0,d3
	move.w		d7,d3
	lsr.w		#7,d3
	lsr.w		#7,d3

	move.b		#19,d0
	sub.b		d3,d0
	move.b		#13,d1
	move.l		d6,d2
	lsr.l		#8,d2
	add.b		d3,d3
	or.w		#0x8000,d3
	jsr		print_hex

	/* current char */
	moveq.l		#0,d0
	moveq.l		#0,d1

	and.w		#6,d3
	move.b		#19,d0
	move.b		d3,d1
	lsr.b		#1,d1		/* length in bytes */
	sub.b		d1,d0
	move.b		d6,d1
	lsr.b		#4,d1
	and.b		#7,d1		/* nibble to edit */
	add.b		d1,d0

	sub.b		d1,d3
	sub.b		#1,d3		/* chars to shift out */
	lsl.b		#2,d3
	add.b		#8,d3
	move.l		d6,d2
	lsr.l		d3,d2

	move.b		#13,d1
	move.w		#0xa001,d3
	jsr		print_hex

	/* handle input */
	jsr		get_input	/* x0cbrldu x1sa00du */

	move.w		d0,d1
	and.w		#0x0f00,d1
	beq		ai_no_dpad
	move.w		d7,d1
	lsr.w		#7,d1
	lsr.w		#7,d1
	add.b		d1,d1		/* nibble count */
	sub.b		#1,d1		/* max n.t.e. val */
	move.b		d6,d2
	lsr.b		#4,d2
	and.b		#7,d2		/* nibble to edit */

	move.b		d0,d3
	and.b		#3,d3
	beq		ai_no_ud
	moveq.l		#0,d3
	moveq.l		#0,d4
	move.b		#0xf,d3
	move.b		#0x1,d4
	sub.b		d2,d1
	lsl.b		#2,d1
	add.b		#8,d1
	lsl.l		d1,d3		/* mask */
	lsl.l		d1,d4		/* what to add/sub */
	move.l		d6,d1
	and.l		d3,d1
	btst.l		#8,d0
	beq		0f
	add.l		d4,d1
	bra		1f
0:
	sub.l		d4,d1
1:
	and.l		d3,d1
	eor.l		#0xffffffff,d3
	and.l		d3,d6
	or.l		d1,d6
	jmp		vbl_end

ai_no_ud:
	btst.l		#10,d0
	bne		0f
	add.b		#1,d2
	bra		1f
0:
	sub.b		#1,d2
1:
	cmp.b		#0,d2
	bge		0f
	move.b		d1,d2
0:
	cmp.b		d1,d2
	ble		0f
	move.b		#0,d2
0:
	and.b		#0x8f,d6
	lsl.b		#4,d2
	or.b		d2,d6
	jmp		vbl_end

ai_no_dpad:
	move.w		d0,d1
	and.w		#0x1020,d1
	beq		ai_no_sb

	and.l		#0xf,d6		/* forget val and pos */
	bra		return_to_main

ai_no_sb:
	btst.l		#4,d0
	beq		ai_no_input
	bset.l		#7,d6
	move.w		d7,d1
	and.w		#0x3800,d1
	lsr.w		#3,d1
	and.w		#0xc0ff,d7
	or.w		d1,d7

ai_no_input:
	jmp		vbl_end


# go back to main mode
return_to_main:
	bclr.l		#7,d6
	change_mode	MMODE_MAIN, MMODE_MAIN
	write_vdp_reg	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320)
	jmp		vbl_end


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


# read single phase from controller
#  #a0 - addr
#  d0 - result
#  destroys d1,d2
get_input:
	move.b		#0x40,(0xa10003)
	nop
	nop
	nop
	move.b		(0xa10003),d0
	move.b		#0x00,(0xa10003)
	lsl.w		#8,d0
	nop
	move.b		(0xa10003),d0
	eor.w		#0xffff,d0

	swap		d7
	move.w		d7,d1
	eor.w		d0,d1		/* changed btns */
	move.w		d0,d7		/* old val */
	swap		d7
	and.w		d0,d1		/* what changed now */
	bne		0f

	addq.b		#1,d6
	move.b		d6,d2
	and.b		#7,d2		/* do autorepeat every 8 frames */
	cmp.b		#7,d2
	bne		1f
	move.w		d0,d1
0:
	and.b		#0xf8,d6
1:
	swap		d0
	move.w		d1,d0
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


# Prepare to write to VDP RAM @a0
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
#  d2 - tile_bits[15:11]
#  destroys a1

print:
	move.l		a0,a1
	XY2NT
	jsr		load_prepare
	move.l		d2,d0
	and.w		#0xf800,d0

_print_loop:
	move.b		(a1)+,d0
	beq		_print_end

	move.w		d0,(a0)
	jmp		_print_loop

_print_end:
	rts


# print_hex
#  d0 - x
#  d1 - y 
#  d2 - value
#  d3 - digit_cnt[0:7]|tile_bits[11:15]
#  destroys a0, preserves d3

print_hex:
	XY2NT
	jsr		load_prepare

print_hex_preped:
	moveq.l		#0,d0
	move.b		d3,d0
	move.l		d0,d1
	lsl.b		#2,d0
	ror.l		d0,d2		/* prep value */
	subq.l		#1,d1		/* count */
	move.w		d3,d0
	and.w		#0xf800,d0	/* keep upper bits in d0 */

_print_hex_loop:
	rol.l		#4,d2
	move.b		d2,d0
	and.b		#0xf,d0

	add.b		#'0',d0
	cmp.b		#'9',d0
	ble		0f
	addq.b		#7,d0
0:
	move.w		d0,(a0)
	dbra		d1,_print_hex_loop

	rts


#################################################
#                                               #
#       Wait for next VBlank interrupt          #
#                                               #
#################################################

wait_vsync:
	move.b		d7,d0
_wait_change:
	stop		#0x2000
	cmp.b		d7,d0
	beq		_wait_change
	rts


#################################################
#                                               #
#                 RAM DATA                      #
#                                               #
#################################################

.bss

# nothing :)

.end

# vim:filetype=asmM68k
