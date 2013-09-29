###############################################################################
#
# Copyright (c) 2009,2011 Gražvydas Ignotas
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the organization nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Assemble with gas
#   --register-prefix-optional --bitwise-or
#

.equ USE_VINT,        0
.equ COPY_TO_EXP,     1
.equ RELOCATE_TO_RAM, 1

.text
.globl main
.globl INT
.globl VBL
.globl return_to_main

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
.equ VDP1_MODE5,	0x04

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
.equ MMODE_START_MENU,	4
.equ MMODE_GOTO_PREDEF,	5
.equ MMODE_JMP_ADDR,	6
.equ MMODE_PC,		7

.equ predef_addr_cnt,	((predef_addrs_end-predef_addrs)/4)

##################################################
#                                                #
#                   MACROS                       #
#                                                #
##################################################


# Write val to VDP register reg
.macro write_vdp_r_dst reg val dst
	move.w #((\reg << 8) + 0x8000 + \val),\dst
.endm

# Write val to VDP register reg, vdp addr in a3
.macro write_vdp_reg reg val
	write_vdp_r_dst \reg, \val, (a3)
.endm

# Set up address in VDP, control port in dst
.macro VRAM_ADDR adr dst
	move.l #(0x40000000 | ((\adr & 0x3fff) << 16) | (\adr >> 14)),\dst
.endm

.macro VSRAM_ADDR adr dst
	move.l #(0x40000010 | ((\adr & 0x3fff) << 16) | (\adr >> 14)),\dst
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

#  destroys a0,d0-d2
.macro menu_text str x y pal
	lea		(\str,pc),a0
	move.l		#\x,d0
	move.l		#\y,d1
	move.l		#0x8000|(\pal<<13),d2
	jsr		print
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
	dc.w 0x0000,0x044e,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
	dc.w	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
colors_end:


sprite_data:
	/*         Y        size     link          attr        X */
	dc.w       0;  dc.b 0x05;  dc.b 0;  dc.w 0x6002;  dc.w 0
sprite_data_end:

predef_addrs:
	dc.l 0x000000, 0x200000, 0x400000, 0xa00000, 0xa10000
	dc.l 0xa11100, 0xa12000, 0xa13000, 0xa14000, 0xa15100
	dc.l 0xc00000
predef_addrs_end:

safe_addrs:
	dc.l 0x000000, 0x7fffff
	dc.l 0xe00000, 0xffffff
	dc.l 0xa00000, 0xa100ff
	dc.l 0xa11000, 0xa113ff
	dc.l 0xa12000, 0xa120ff
	dc.l 0xa13000, 0xa130ff
safe_addrs_end:
	dc.l 0xa15100, 0xa1513f
safe_addrs_end_32x:
	dc.l 0xa15180, 0xa153ff
safe_addrs_end_32x_vdp:

sizeof_bin:
	dc.l _edata

txt_edit:
	.ascii	"- edit -\0"
txt_a_confirm:
	.ascii	"A-confirm\0"
txt_about:
	.ascii	"hexed r2\0"
txt_goto:
	.ascii	"Go to address\0"
txt_goto_predef:
	.ascii	"Go to (predef)\0"
txt_jmp_addr:
	.ascii	"Jump to address\0"
txt_dump:
	.ascii	"PC Transfer\0"
txt_dtack:
	.ascii	"DTACK safety\0"
txt_transfer_ready:
	.ascii	"Transfer Ready\0"
txt_working:
	.ascii	"PC mode       \0"
txt_dtack_err:
	.ascii	"DTACK err?\0"
txt_exc:
	.ascii	"Exception \0"

##################################################
#                                                #
#               MAIN PROGRAM                     #
#                                                #
##################################################

# global regs:
# a6 = page_start[31:8]|cursor_offs[7:0]
# d7 = old_inputs[31:16]|edit_bytes[15:14]|g_mode_old[13:11]|g_mode[10:8]|irq_cnt[7:0]
# d6 = edit_word_save[31:15]|edit_done[7]|no_dtack_detect[4]|autorep_cnt[3:0]
# d5 = main: tmp
#      edit: edit_word[31:8]|edit_pos[4:2]|byte_cnt[1:0]
#      menu: sel

.align 2

main:
	/* make sure io port 2 is doing inputs */
	move.b		#0,(0xa1000b).l
	/* make sure irqs are masked */
	move.w		#0x2700,sr
	/* take care of TMSS */
	move.b		(0xa10000).l,d0
	andi.b		#0x0f,d0
	beq		no_tmss
	move.l		#0x53454741,(0xa14000).l
	/* want cart, not OS rom if cart pops in */
	move.w		#1,(0xa14100).l
	/* touch VDP after TMSS setup? */
	tst.w		(0xc00004).l
no_tmss:

	/* want to do early PC transfer (with RAM/VRAM intact and such)?
	 * also give time PC to see start condition */
	move.l		#0x2000,d0
0:	dbra		d0,0b

	move.l		#0xa10005,a0
	btst.b		#5,(a0)
	bne		no_early_transfer
move.b #1,(0)
	move.b		#0x40,(0xa1000b).l	/* port 2 ctrl */
	move.b		#0x00,(a0)		/* port 2 data - start with TH low */
	move.l		#0x2000,d0
0:
	btst.b		#4,(a0)
	beq		do_early_transfer
	dbra		d0,0b

move.b #2,(0)
	move.b		#0,(0xa1000b).l
	bra		no_early_transfer	/* timeout */

do_early_transfer:
move.b #9,(0)
	bsr		do_transfer

no_early_transfer:

.if COPY_TO_EXP
	/* copy to expansion device if magic number is set */
	move.l		#0x400000,a1
	cmp.w		#0x1234,(a1)
	bne		0f

	move.l		#0,a0
	move.l		(sizeof_bin,pc),d0
	lsr.l		#3,d0
1:
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	dbra		d0,1b
0:
.endif

.if RELOCATE_TO_RAM
	/* we could be relocated by 32x or something else, adjust start addr */
	lea		(pc),a0
	move.l		a0,d0
	and.l		#0xff0000,d0
	move.l		d0,a0

	/* copy, assume 8K size */
	move.l		#0xFF0100,a1
	move.l		(sizeof_bin,pc),d0
	lsr.l		#3,d0
1:
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	dbra		d0,1b

	/* copy test code */
	lea             (test_code,pc),a0
	move.l		#0xffc000,a1
	move.w		#(test_code_end - test_code)/2-1,d0
1:
	move.w		(a0)+,(a1)+
	dbra		d0,1b

	lea		(0f,pc),a0
	move.l		a0,d0
	and.l		#0x00ffff,d0
	add.l		#0xFF0100,d0
	move.l		d0,a0

	/* patch test code */
	move.l		#0xffc000,a1
	add.w		#(test_code_ret_op-test_code+2),a1
	move.l		a0,(a1)

	jmp		(a0)
0:
.endif

	movea.l		#0,a6
	move.l		#0x8000,d7
	moveq.l		#0,d6

	/* Init pads */
	move.b		#0x40,(0xa10009).l
	move.b		#0x40,(0xa10003).l

	/* Initialize VDP */
	jsr 		init_gfx

	/* Clear h/v scroll */
	movea.l		#GFXDATA,a0
	VRAM_ADDR	0x8000,(GFXCNTL)
	move.l		#0,(a0)
	VSRAM_ADDR	0,(GFXCNTL)
	move.l		#0,(a0)

	/* Load color data */
	movea.l		#0,a0
	lea		(colors,pc),a1
	moveq.l		#(colors_end-colors)/2,d0
	jsr		load_colors

	/* load font patterns */
	movea.l		#GFXDATA,a0
	lea		(font,pc),a1
	VRAM_ADDR	0,(GFXCNTL)
	move.w		#128*8,d3
font_loop:
	moveq.l		#8-1,d2
	moveq.l		#0,d1
	move.b		(a1)+,d0
0:
	lsr.b		#1,d0
	roxl.l		#1,d1
	ror.l		#5,d1
	dbra		d2,0b

	rol.l		#1,d1		/* fixup */
	move.l		d1,(a0)
	dbra		d3,font_loop

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
	lea		(sprite_data,pc),a1

	move.l		#(sprite_data_end-sprite_data)/2-1,d3
0:	move.l		(a1)+,(a0)
	dbra		d3,0b

.if USE_VINT
	/* wait for vsync before unmask */
	jsr		wait_vsync_poll

	/* wait a bit to avoid nested vint */
	move.w		#20,d0
0:
	dbra		d0,0b		/* 10 cycles to go back */

	/* enable and unmask vint */
	write_vdp_r_dst 1,(VDP1_E_VBI | VDP1_E_DISPLAY | VDP1_MODE5),(GFXCNTL)
	move.w		#0x2000,sr
.endif

##################################################

forever:
.if USE_VINT
	jsr		wait_vsync
.else
	jsr		wait_vsync_poll
	jsr		VBL
.endif
	bra 		forever


INT:
	/* let's hope VRAM is already set up.. */
	lea		(txt_exc,pc),a0
	move.l		#9,d0
	move.l		#27,d1
	move.l		#0xe000,d2
	jsr		print
	bra		forever

##################################################

VBL:
	addq.b		#1,d7
#	movem.l		d0-d4/a0-a5,-(a7)

	btst.b		#5,(0xa10005).l
	bne		no_auto_transfer
	change_mode	MMODE_PC, MMODE_MAIN
	write_vdp_r_dst	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_STE),(GFXCNTL)

no_auto_transfer:
	moveq.l		#0,d0
	move.w		d7,d0
	lsr.w		#6,d0
	and.w		#0x1c,d0
	lea		(jumptab,pc,d0),a0
	jmp		(a0)
jumptab:
	/* branch insns here because we want to be position independent */
	bra		mode_main
	bra		mode_val_input
	bra		mode_edit_val	/* edit val in editor */
	bra		mode_goto
	bra		mode_start_menu
	bra		mode_goto_predef
	bra		mode_jmp_addr
	bra		mode_transfer

##################### main #######################

mode_main:
	/* assume we will hang */
	lea		(txt_dtack_err,pc),a0
	move.l		#9,d0
	move.l		#27,d1
	move.l		#0xe000,d2
	jsr		print

	moveq.l		#0,d1
	move.l		a6,d0
	move.b		d0,d1
	lsr.l		#8,d0
	move.l		d0,a1		/* current addr */
	lsr.b		#3,d1
	neg.b		d1
	add.b		#27-1,d1	/* line where the cursor sits */
	swap		d1

	movea.l		#0xe002,a2
	move.l		#27-1,d5	/* line counter for dbra */
	or.l		d1,d5

draw_row:
	move.l		a2,a0
	jsr		load_prepare

	btst.l		#15,d7
	beq		0f
	move.w		#' ',(a0)
0:
	/* addr */
	move.l		a1,d2
	moveq.l		#6,d3
	jsr		print_hex_preped

	btst.l		#4,d6
	bne		draw_row_safe

	bsr		get_safety_mask
	cmp.b		#0xf,d0
	beq		draw_row_safe

# unsafe or partially safe
draw_row_hsafe:
	move.l		d0,d4
	swap		d4		/* mask in upper word */
	btst.l		#15,d7
	bne		draw_row_hsafe_words_pre

draw_row_hsafe_bytes_pre:
	/* 8 bytes */
	moveq.l		#2,d3
	move.w		#3,d4

draw_row_hsafe_bytes:
	move.w		#' ',(a0)
	move.b		d4,d0
	add.b		#16,d0
	btst.l		d0,d4
	bne		0f
	move.l		#'?'|('?'<<16),(a0)
	move.w		#' ',(a0)
	move.l		#'?'|('?'<<16),(a0)
	bra		1f
0:
	move.b		(0,a1),d2
	jsr		print_hex_preped
	move.w		#' ',(a0)
	move.b		(1,a1),d2
	jsr		print_hex_preped
1:
	addq.l		#2,a1
	dbra		d4,draw_row_hsafe_bytes

	move.w		#' ',(a0)

	move.l		d5,d0
	swap		d0
	cmp.w		d5,d0
	beq		draw_cursor_unsafe_byte
	bra		draw_chars_hsafe_pre

draw_row_hsafe_words_pre:
	/* 4 shorts */
	moveq.l		#4,d3
	move.w		#3,d4

draw_row_hsafe_words:
	move.w		#' ',(a0)
	move.b		d4,d0
	add.b		#16,d0
	btst.l		d0,d4
	bne		0f
	move.l		#'?'|('?'<<16),(a0)
	move.l		#'?'|('?'<<16),(a0)
	bra		1f
0:
	move.w		(a1),d2
	jsr		print_hex_preped
1:
	addq.l		#2,a1
	dbra		d4,draw_row_hsafe_words

	move.l		#(' '<<16)|' ',(a0)

	move.l		d5,d0
	swap		d0
	cmp.w		d5,d0
	beq		draw_cursor_unsafe_word

draw_chars_hsafe_pre:
	subq.l		#8,a1
	move.w		#3,d4
	moveq.l		#0,d0

draw_chars_hsafe:
	move.b		d4,d0
	add.b		#16,d0
	btst.l		d0,d4
	bne		0f
	move.l		#'?'|('?'<<16),(a0)
	bra		draw_chars_hsafe_next
0:
	btst.l		#15,d7		/* must perform correct read type */
	bne		0f		/* doing byte reads on security reg hangs */
	move.b		(0,a1),d0
	lsl.l		#8,d0
	move.b		(1,a1),d0
	bra		1f
0:
	move.w		(a1),d0
1:
	ror.l		#8,d0
	move.b		d0,d1
	sub.b		#0x20,d1
	cmp.b		#0x60,d1
	blo		0f
	move.b		#'.',d0
0:
	move.w		d0,(a0)

	move.b		#0,d0
	rol.l		#8,d0
	move.b		d0,d1
	sub.b		#0x20,d1
	cmp.b		#0x60,d1
	blo		0f
	move.b		#'.',d0
0:
	move.w		d0,(a0)

draw_chars_hsafe_next:
	addq.l		#2,a1
	dbra		d4,draw_chars_hsafe

	move.l		#(' '<<16)|' ',(a0)
	add.w		#0x80,a2
	dbra		d5,draw_row
	bra		draw_status_bar


# normal draw
draw_row_safe:
	btst.l		#15,d7
	bne		draw_row_words

draw_row_bytes:
	/* 8 bytes */
	moveq.l		#2,d3
	moveq.l		#8-1,d4
draw_bytes:
	move.w		#' ',(a0)
	move.b		(a1)+,d2
	jsr		print_hex_preped
	dbra		d4,draw_bytes

	move.w		#' ',(a0)

	move.l		d5,d0
	swap		d0
	cmp.w		d5,d0
	beq		draw_cursor_byte
	bra		draw_chars_pre

draw_row_words:
	/* 4 shorts */
	moveq.l		#4,d3
	moveq.l		#4-1,d4
draw_words:
	move.w		#' ',(a0)
	move.w		(a1)+,d2
	jsr		print_hex_preped
	dbra		d4,draw_words

	move.l		#(' '<<16)|' ',(a0)

	move.l		d5,d0
	swap		d0
	cmp.w		d5,d0
	beq		draw_cursor_word

draw_chars_pre:
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

	move.l		#(' '<<16)|' ',(a0)

	add.w		#0x80,a2
	dbra		d5,draw_row


draw_status_bar:
	/* status bar */
	move.l		a2,a0
	jsr		load_prepare

	btst.l		#15,d7
	beq		0f
	move.w		#' ',(a0)
0:
	mk_a6_addr	d2
	move.l		#0x4006,d3
	jsr		print_hex_preped

	/* clear error */
	moveq.l		#5-1,d0
0:
	move.l		#' '|(' '<<16),(a0)
	move.l		#' '|(' '<<16),(a0)
	dbra		d0,0b


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
	write_vdp_r_dst	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_STE),(GFXCNTL)

input_noc:
	btst.l		#5,d0			/* Start - menu */
	beq		input_nos

	moveq.l		#0,d5
	change_mode	MMODE_START_MENU, MMODE_MAIN
	write_vdp_r_dst	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_STE),(GFXCNTL)

input_nos:
vbl_end:
#	movem.l		(a7)+,d0-d4/a0-a5
.if USE_VINT
	rte
.else
	rts
.endif


draw_cursor_unsafe_byte:
	move.l		a6,d0
	and.l		#7,d0		/* byte offs */
	move.b		d0,d1
	add.b		d0,d0
	add.b		d1,d0		/* d0 *= 3 (chars) */
	add.b		d0,d0
	lea		(7*2,a2,d0),a0
	jsr		load_prepare
	move.l		#(0x2000|'?'|((0x2000|'?')<<16)),(a0)

	move.l		a2,a0
	add.w		#31*2,a0
	jsr		load_prepare	/* restore a0 */
	bra		draw_chars_hsafe_pre

draw_cursor_unsafe_word:
	move.l		a6,d0
	and.l		#7,d0		/* byte offs */
	move.l		d0,d1
	lsr.b		#1,d1		/* which word */
	move.b		d1,d2
	lsl.b		#2,d2
	add.b		d2,d1		/* num of chars to skip */
	add.b		d1,d1

	lea		(8*2,a2,d1),a0
	jsr		load_prepare
	move.l		#(0x2000|'?'|((0x2000|'?')<<16)),d0
	move.l		d0,(a0)
	move.l		d0,(a0)

	move.l		a2,a0
	add.w		#29*2,a0
	jsr		load_prepare	/* restore a0 */
	bra		draw_chars_hsafe_pre


draw_cursor_byte:
	move.l		a6,d0
	and.l		#7,d0		/* byte offs */
	move.w		#0x2002,d3

	move.b		(-8,a1,d0),d2
	move.b		d0,d1
	add.b		d0,d0
	add.b		d1,d0		/* d0 *= 3 (chars) */
	add.b		d0,d0
	lea		(7*2,a2,d0),a0
	jsr		load_prepare
	jsr		print_hex_preped

	move.l		a2,a0
	add.w		#31*2,a0
	jsr		load_prepare	/* restore a0 */

	bra		draw_chars_pre

draw_cursor_word:
	move.l		a6,d0
	and.l		#7,d0		/* byte offs */
	move.l		d0,d1
	lsr.b		#1,d1		/* which word */
	move.b		d1,d2
	lsl.b		#2,d2
	add.b		d2,d1		/* num of chars to skip */
	add.b		d1,d1
	move.w		#0x2004,d3

	move.w		(-8,a1,d0),d2
	lea		(8*2,a2,d1),a0
	jsr		load_prepare
	jsr		print_hex_preped

	move.l		a2,a0
	add.w		#29*2,a0
	jsr		load_prepare	/* restore a0 */

	bra		draw_chars_pre


#################### hedit #######################

mode_edit_val:
	btst.l		#7,d6
	bne		mode_hedit_finish

	/* read val to edit */
	moveq.l		#0,d5
	mk_a6_addr	d1
	move.l		d1,a0
	btst.l		#15,d7
	bne		0f
	move.b		(a0),d5
	lsl.l		#8,d5
	or.b		#1,d5
	bra		1f
0:
	move.w		(a0),d5
	lsl.l		#8,d5
	or.b		#2,d5
1:

	change_mode	MMODE_VAL_INPUT, MMODE_EDIT_VAL
	bra		vbl_end

mode_hedit_finish:
	/* write the val */
	mk_a6_addr	d1
	move.l		d1,a0
	lsr.l		#8,d5

	btst.l		#15,d7
	bne		0f
	move.b		d5,(a0)
	bra		1f
0:
	move.w		d5,(a0)
1:

	bra		return_to_main

##################### goto #######################

mode_goto:
	btst.l		#7,d6
	bne		mode_goto_finish

	moveq.l		#0,d5
	swap		d6
	move.w		d6,d5
	swap		d6
	swap		d5
	or.b		#3,d5		/* 3 bytes */
	bclr.l		#7,d6
	change_mode	MMODE_VAL_INPUT, MMODE_GOTO
	bra		vbl_end

mode_goto_finish:
	lsr.l		#8,d5
	move.l		d5,d0
	move.l		d0,d1
	and.l		#7,d1
	and.b		#0xf8,d0
	lsl.l		#8,d0
	or.l		d1,d0
	move.l		d0,a6

	lsr.l		#8,d5
	swap		d6
	move.w		d5,d6
	swap		d6

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
	lea		(txt_edit,pc),a0
	move.l		#15,d0
	move.l		#11,d1
	move.l		#0xc000,d2
	jsr		print

	lea		(txt_a_confirm,pc),a0
	move.l		#15,d0
	move.l		#15,d1
	move.l		#0xc000,d2
	jsr		print

	/* edit field */
	moveq.l		#0,d0
	moveq.l		#0,d1
	moveq.l		#0,d3
	move.b		d5,d3
	and.b		#3,d3		/* edit field bytes */

	move.b		#19,d0
	sub.b		d3,d0
	move.b		#13,d1
	move.l		d5,d2
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
	move.b		d5,d1
	lsr.b		#2,d1
	and.b		#7,d1		/* nibble to edit */
	add.b		d1,d0

	sub.b		d1,d3
	sub.b		#1,d3		/* chars to shift out */
	lsl.b		#2,d3
	add.b		#8,d3
	move.l		d5,d2
	lsr.l		d3,d2

	move.b		#13,d1
	move.w		#0xa001,d3
	jsr		print_hex

	/* handle input */
	jsr		get_input	/* x0cbrldu x1sa00du */

	move.w		d0,d1
	and.w		#0x0f00,d1
	beq		ai_no_dpad
	move.b		d5,d1
	and.b		#3,d1
	add.b		d1,d1		/* nibble count */
	sub.b		#1,d1		/* max n.t.e. val */
	move.b		d5,d2
	lsr.b		#2,d2
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
	move.l		d5,d1
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
	and.l		d3,d5
	or.l		d1,d5
	bra		vbl_end

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
	and.b		#0xe3,d5
	lsl.b		#2,d2
	or.b		d2,d5
	bra		vbl_end

ai_no_dpad:
	move.w		d0,d1
	and.w		#0x1020,d1
	beq		ai_no_sb

	bra		return_to_main

ai_no_sb:
	btst.l		#4,d0		/* A - confirm */
	beq		ai_no_input
	bset.l		#7,d6
	move.w		d7,d1		/* back to prev mode */
	and.w		#0x3800,d1
	lsr.w		#3,d1
	and.w		#0xc0ff,d7
	or.w		d1,d7

ai_no_input:
	bra		vbl_end


################### start menu ###################

mode_start_menu:
	/* frame */
	bsr		start_menu_box

	/* text */
	menu_text	txt_about,       13,  9, 1
	menu_text	txt_goto,        13, 11, 0
	menu_text	txt_goto_predef, 13, 12, 0
	menu_text	txt_jmp_addr,    13, 13, 0
	menu_text	txt_dump,        13, 14, 0
	menu_text	txt_dtack,       13, 15, 0
	menu_text	txt_a_confirm,   13, 17, 2

	/* dtack safety on/off */
	movea.l		#0xe000+26*2+15*64*2,a0
	jsr		load_prepare
	move.w		#0x8000|'O',(a0)
	btst.l		#4,d6
	bne		0f
	move.w		#0x8000|'N',(a0)
	bra		1f
0:
	move.w		#0x8000|'F',(a0)
	move.w		#0x8000|'F',(a0)
1:

	/* cursor */
	movea.l		#0xe000+11*2+11*64*2,a0
	moveq.l		#0,d0
	move.b		d5,d0
	and.b		#7,d0
	lsl.w		#7,d0
	add.w		d0,a0
	jsr		load_prepare
	move.w		#'>',(a0)

	/* input */
	jsr		get_input	/* x0cbrldu x1sa00du */

	move.w		d0,d1
	and.w		#3,d1
	beq		msm_no_ud
	move.b		d5,d1
	and.b		#7,d1
	btst.l		#0,d0
	sne		d2
	or.b		#1,d2		/* up -1, down 1 */
	add.b		d2,d1
	cmp.b		#0,d1
	bge		0f
	move.b		#4,d1
0:
	cmp.b		#4,d1
	ble		0f
	move.b		#0,d1
0:
	and.b		#0xf8,d5
	or.b		d1,d5
	bra		vbl_end

msm_no_ud:
	btst.l		#4,d0		/* A - confirm */
	beq		msm_no_a
	move.b		d5,d1
	and.b		#7,d1
	bne		0f
	change_mode	MMODE_GOTO, MMODE_MAIN
	bsr		start_menu_box
	bra		vbl_end
0:
	cmp.b		#1,d1
	bne		0f
	moveq.l		#0,d5
	change_mode	MMODE_GOTO_PREDEF, MMODE_MAIN
	bsr		start_menu_box
	bra		vbl_end
0:
	cmp.b		#2,d1
	bne		0f
	change_mode	MMODE_JMP_ADDR, MMODE_MAIN
	bsr		start_menu_box
	bra		vbl_end
0:
	cmp.b		#3,d1
	bne		0f
	change_mode	MMODE_PC, MMODE_MAIN
	bsr		start_menu_box
	bra		vbl_end
0:
	cmp.b		#4,d1
	bne		0f
	bchg.l		#4,d6
	bra		vbl_end
0:

msm_no_a:
	move.w		d0,d1
	and.w		#0x3000,d1
	beq		msm_no_bc
	bra		return_to_main

msm_no_bc:
	bra		vbl_end

start_menu_box:
	movea.l		#0xe000+10*2+8*64*2,a1
	move.w		#11-1,d1
0:
	move.w		a1,a0
	jsr		load_prepare
	move.w		#20-1,d0
1:
	move.w		#0,(a0)
	dbra		d0,1b

	add.w		#64*2,a1
	dbra		d1,0b
	rts

################### goto predef ##################

mode_goto_predef:
	/* frame */
	movea.l		#0xe000+14*2+8*64*2,a1
	move.l		#predef_addr_cnt+2-1,d1
0:
	move.w		a1,a0
	jsr		load_prepare
	moveq.l		#10-1,d0
1:
	move.w		#0,(a0)
	dbra		d0,1b

	add.w		#64*2,a1
	dbra		d1,0b

	/* draw addresses */
	movea.l		#0xe000+17*2+9*64*2,a1
	lea		(predef_addrs,pc),a2
	move.w		#predef_addr_cnt-1,d4
	move.l		#0x8006,d3
mgp_da_loop:
	move.w		a1,a0
	jsr		load_prepare
	move.l		(a2)+,d2
	jsr		print_hex_preped
	add.w		#64*2,a1
	dbra		d4,mgp_da_loop

	/* cursor */
	movea.l		#0xe000+15*2+9*64*2,a0
	moveq.l		#0,d0
	move.b		d5,d0
	lsl.w		#7,d0
	add.w		d0,a0
	jsr		load_prepare
	move.w		#'>',(a0)

	/* input */
	jsr		get_input	/* x0cbrldu x1sa00du */

	move.w		d0,d1
	and.w		#3,d1
	beq		mgp_no_ud
	btst.l		#0,d0
	sne		d2
	or.b		#1,d2		/* up -1, down 1 */
	add.b		d2,d5
	cmp.b		#0,d5
	bge		0f
	move.b		#predef_addr_cnt-1,d5
0:
	cmp.b		#predef_addr_cnt-1,d5
	ble		0f
	move.b		#0,d5
0:
	bra		vbl_end

mgp_no_ud:
	btst.l		#4,d0		/* A - confirm */
	beq		mgp_no_a
	moveq.l		#0,d0
	move.b		d5,d0
	lsl.w		#2,d0
	lea		(predef_addrs,pc),a0
	move.l		(a0,d0),d5
	lsl.l		#8,d5
	bra		mode_goto_finish

mgp_no_a:
	move.w		d0,d1
	and.w		#0x3000,d1
	beq		mgp_no_bc
	bra		return_to_main

mgp_no_bc:
	bra		vbl_end

##################### jmp ########################

mode_jmp_addr:
	btst.l		#7,d6
	bne		mode_jmp_finish

	moveq.l		#0,d5
	or.b		#3,d5		/* 3 bytes */
	bclr.l		#7,d6
	change_mode	MMODE_VAL_INPUT, MMODE_JMP_ADDR
	bra		vbl_end

mode_jmp_finish:
	lsr.l		#8,d5
	write_vdp_r_dst	1,(VDP1_E_DISPLAY | VDP1_MODE5),(GFXCNTL)	/* disable vint */
	move.l		d5,a0
	jmp		(a0)

mode_transfer:
	move.b		#0x40,(0xa1000b).l	/* port 2 ctrl */
	move.b		#0x00,(0xa10005).l	/* port 2 data - start with TH low */

	lea		(txt_transfer_ready,pc),a0
	move.l		#13,d0
	move.l		#13,d1
	move.l		#0x8000,d2
	jsr		print

wait_tl_low0:
	move.b		(0xa10005),d0
	btst.b		#4,d0
	bne		wait_tl_low0

	menu_text	txt_working, 13, 13, 0
	bsr		do_transfer
	bra		return_to_main

# go back to main mode
return_to_main:
	bclr.l		#7,d6		/* not edited */
	change_mode	MMODE_MAIN, MMODE_MAIN
	write_vdp_r_dst	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320),(GFXCNTL)
	bra		vbl_end


#################################################
#                                               #
#         Initialize VDP registers              #
#                                               #
#################################################

init_gfx:
	move.l 		#GFXCNTL,a3
	write_vdp_reg 	0,(VDP0_E_DISPLAY | VDP0_PLTT_FULL)
	write_vdp_reg 	1,(VDP1_E_DISPLAY | VDP1_MODE5)
	write_vdp_reg 	2,(0xe000 >> 10)	/* Screen map a adress */
	write_vdp_reg 	3,(0xe000 >> 10)	/* Window address */
	write_vdp_reg 	4,(0xc000 >> 13)	/* Screen map b address */
	write_vdp_reg 	5,(0xfc00 >>  9)	/* Sprite address */
	write_vdp_reg 	6,0	
	write_vdp_reg	7,0			/* Backdrop color */
	write_vdp_reg	0x0a,1			/* Lines per hblank interrupt */
	write_vdp_reg	0x0b,0			/* 2-cell vertical scrolling */
	write_vdp_reg	0x0c,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320)
	write_vdp_reg	0x0d,(0x8000 >> 10)	/* Horizontal scroll address */
	write_vdp_reg	0x0e,0
	write_vdp_reg	0x0f,2
	write_vdp_reg	0x10,(VDP16_MAP_V32 | VDP16_MAP_H64)	/* layer size */
	write_vdp_reg	0x11,0
	write_vdp_reg	0x12,0
	rts


# get mask of bits representing safe words
#  a1 - address to check
#  destroys d0-d2, strips upper bits from a1
get_safety_mask:
	move.l		a1,d1
	lsl.l		#8,d1
	lsr.l		#8,d1
	lea		(safe_addrs,pc),a1
	move.w		#(safe_addrs_end - safe_addrs)/8-1,d2
	cmp.l		#0x4D415253,(0xa130ec)	/* 'MARS' */
	bne		no_32x
	move.w		#(safe_addrs_end_32x - safe_addrs)/8-1,d2
	move.w		(0xa15100),d0
	and.w		#3,d0
	cmp.w		#3,d0			/* ADEN and nRES */
	bne		no_32x_vdp
	btst.b		#7,d0			/* FM */
	bne		no_32x_vdp
	move.w		#(safe_addrs_end_32x_vdp - safe_addrs)/8-1,d2
no_32x_vdp:
no_32x:

0:
	move.l		(a1)+,d0
	cmp.l		d0,d1
	blt		1f
	move.l		(a1),d0
	cmp.l		d0,d1
	ble		addr_safe
1:
	addq.l		#4,a1
	dbra		d2,0b

	move.l		d1,a1

	moveq.l		#0x0c,d0
	cmp.l		#0xa14000,d1
	beq		gsm_rts

	moveq.l		#0x08,d0
	cmp.l		#0xa14100,d1
	beq		gsm_rts

	/* check for VDP address */
	move.l		d1,d0
	swap		d0
	and.b		#0xe0,d0
	cmp.b		#0xc0,d0
	bne		addr_unsafe	/* not vdp */

	move.l		d1,d0
	and.l		#0x0700e0,d0
	bne		addr_unsafe

	move.l		d1,d0
	and.b		#0x1f,d0
	cmp.b		#0x04,d0
	blt		addr_hsafe_3	/* data port */
	cmp.b		#0x10,d0
	blt		addr_safe	/* below PSG */
	cmp.b		#0x18,d0
	bge		addr_safe	/* above PSG */

addr_unsafe:
	moveq.l		#0,d0		/* skip line */
	rts

addr_hsafe_3:
	moveq.l		#3,d0		/* skip 2 words */
	rts

addr_safe:
	move.l		d1,a1
	moveq.l		#0xf,d0
gsm_rts:
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
	and.b		#0x0f,d2	/* do autorepeat */
	cmp.b		#9,d2
	bne		1f
	move.w		d0,d1
0:
	and.b		#0xf0,d6
1:
	swap		d0
	move.w		d1,d0
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
	bra		_print_loop

_print_end:
	rts


# print_hex
#  d0 - x
#  d1 - y 
#  d2 - value
#  d3 - tile_bits[15:11]|digit_cnt[7:0]
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


# wait vertical sync interrupt

wait_vsync:
	move.b		d7,d0
_wait_change:
	stop		#0x2000
	cmp.b		d7,d0
	beq		_wait_change
	rts


# wait vsync start (polling)
#  destroys a0,d0

wait_vsync_poll:
	move.l 		#GFXCNTL,a0
0:
	move.w		(a0),d0
	and.b		#8,d0
	nop
	nop
	bne		0b
0:
	move.w		(a0),d0
	and.b		#8,d0
	nop
	nop
	beq		0b
	rts


test_code:
	nop

test_code_ret_op:
	jmp	0x123456        /* will be patched */
test_code_end:

#################################################
#                                               #
#                 RAM DATA                      #
#                                               #
#################################################

.bss

# nothing :)

.end

# vim:filetype=asmM68k
