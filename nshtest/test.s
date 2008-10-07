##################################################
#                                                #
#                  KeroDemo                      #
#                                                #
#                 mic, 2005                      #
#                                                #
#                                                #
#          Assemble with m68k-coff-as            #
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


/* For indirect (variable) addresses.
   Destroys d6-d7. */
.macro VRAM_ADDR_var reg adr
	move.l \adr,d6
	move.l \adr,d7
	and.w #0x3fff,d6
	lsr.w #7,d7
	lsr.w #7,d7
	add.w #0x4000,d6
	lsl.l #7,d6
	lsl.l #7,d6
	lsl.l #2,d6
	or.l d7,d6
	move.l d6,\reg
.endm


.macro CRAM_ADDR reg adr
	move.l	#(((0xc000 + (\adr & 0x3fff)) << 16) + (\adr >> 14)),\reg
.endm


/* For indirect (variable) addresses */
.macro CRAM_ADDR_var reg adr
	move.l \adr,d6
	move.l \adr,d7
	and.w #0x3fff,d6
	lsr.w #7,d7
	lsr.w #7,d7
	add.w #0xc000,d6
	lsl.l #7,d6
	lsl.l #7,d6
	lsl.l #2,d6
	or.l d7,d6
	move.l d6,\reg
.endm


.macro VSCROLL_ADDR reg adr
	move.l	#(((0x4000 + (\adr & 0x3fff)) << 16) + ((\adr >> 14) | 0x10)),\reg
.endm


.macro HSCROLL_ADDR reg adr
	move.l #(((0x4000 + (\adr & 0x3fff)) << 16) + (\adr >> 14)),\reg
.endm


colors:
	dc.w 0x0040,0x0080,0x000e,0x00e0,0x0e00,0x00ee
pattern:
	dc.l 0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000
	dc.l 0x22334455,0x22334455,0x22334455,0x22334455,0x22334455,0x22334455,0x22334455,0x22334455
	/* shadow sprite */
	dc.l 0x00000fff
	dc.l 0x0000ffff
	dc.l 0x00ffffff
	dc.l 0x00ffffff
	dc.l 0x0fffffff
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0xffffffff
	/* */
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0x0fffffff
	dc.l 0x00ffffff
	dc.l 0x00ffffff
	dc.l 0x0000ffff
	dc.l 0x00000fff
	/* */
	dc.l 0xfff00000
	dc.l 0xffff0000
	dc.l 0xffffff00
	dc.l 0xffffff00
	dc.l 0xfffffff0
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0xffffffff
	/* */
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0xffffffff
	dc.l 0xfffffff0
	dc.l 0xffffff00
	dc.l 0xffffff00
	dc.l 0xffff0000
	dc.l 0xfff00000
	/* hilight sprite */
	dc.l 0x00000eee
	dc.l 0x0000eeee
	dc.l 0x00eeeeee
	dc.l 0x00eeeeee
	dc.l 0x0eeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	/* */
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0x0eeeeeee
	dc.l 0x00eeeeee
	dc.l 0x00eeeeee
	dc.l 0x0000eeee
	dc.l 0x00000eee
	/* */
	dc.l 0xeee00000
	dc.l 0xeeee0000
	dc.l 0xeeeeee00
	dc.l 0xeeeeee00
	dc.l 0xeeeeeee0
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	/* */
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeeee
	dc.l 0xeeeeeee0
	dc.l 0xeeeeee00
	dc.l 0xeeeeee00
	dc.l 0xeeee0000
	dc.l 0xeee00000


sprite_data:
	/*         Y        size     link          attr        X */
	dc.w  10+128;  dc.b 0x05;  dc.b 1;  dc.w 0x6002;  dc.w 0
	dc.w  30+128;  dc.b 0x05;  dc.b 2;  dc.w 0x6006;  dc.w 0
	dc.w  60+128;  dc.b 0x05;  dc.b 3;  dc.w 0xe002;  dc.w 0
	dc.w  80+128;  dc.b 0x05;  dc.b 4;  dc.w 0xe006;  dc.w 0
	dc.w 120+128;  dc.b 0x05;  dc.b 5;  dc.w 0x6002;  dc.w 0
	dc.w 140+128;  dc.b 0x05;  dc.b 6;  dc.w 0x6006;  dc.w 0
	dc.w 170+128;  dc.b 0x05;  dc.b 7;  dc.w 0xe002;  dc.w 0
	dc.w 190+128;  dc.b 0x05;  dc.b 0;  dc.w 0xe006;  dc.w 0
sprite_data_end:


##################################################
#                                                #
#               MAIN PROGRAM                     #
#                                                #
##################################################
 
main:
	/* Initialize VDP */
	jsr 		init_gfx

	/* Load color data */
	movea.l		#0,a3
	move.l 		#colors,a4
	moveq.l		#6,d4
	jsr		load_colors

	/* load patterns */
	movea.l		#0,a3
	movea.l		#pattern,a4
	move.l		#10,d4
	jsr		load_tiles

	/* generate A layer map */
	movea.l		#0xe000+10*2,a6
	move.l		#28-1,d4
lmaploop0:
	movea.l		a6,a3
	jsr		load_prepare

	moveq.l		#6-1,d3
0:	move.l		#0x00010001,(a3)
	dbra		d3,0b

	moveq.l		#9-1,d3
0:	move.l		#0x80018001,(a3)
	dbra		d3,0b

	add.l		#64*2,a6
	dbra 		d4,lmaploop0

	/* generate B layer map */
	movea.l		#0xc000+64*14*2,a3
	jsr		load_prepare

	move.l		#64*14/2-1,d3
0:	move.l		#0x80008000,(a3)
	dbra		d3,0b

	/* upload sprite data */
	movea.l		#0xfc00,a3
	jsr		load_prepare
	movea.l		#sprite_data,a0

	move.l		#(sprite_data_end-sprite_data)/2-1,d3
0:	move.l		(a0)+,(a3)
	dbra		d3,0b

	jsr		wait_vsync

##################################################
#                                                #
#                 MAIN LOOP                      #
#                                                #
##################################################

forever:
	movea.l		#vtimer,a0
	move.l		(a0),d4
	and.w           #0x1ff,d4
	movea.l		#0xfc06,a6
	moveq.l		#8-1,d5

0:
	movea.l		a6,a3
	jsr		load_prepare
	move.w          d4,(a3)
	addq.w		#8,a6
	dbra		d5,0b

	jsr		wait_vsync
	bra 		forever
	


#################################################
#                                               #
#         Initialize VDP registers              #
#                                               #
#################################################

init_gfx:
	move.l 		#GFXCNTL,a3
	write_vdp_reg 	0,(VDP0_E_DISPLAY + VDP0_PLTT_FULL)
	write_vdp_reg 	1,(VDP1_E_VBI + VDP1_E_DISPLAY + VDP1_E_DMA + VDP1_RESERVED)
	write_vdp_reg 	2,(0xe000 >> 10)	/* Screen map a adress */
	write_vdp_reg 	3,(0xe000 >> 10)	/* Window address */
	write_vdp_reg 	4,(0xc000 >> 13)	/* Screen map b address */
	write_vdp_reg 	5,(0xfc00 >>  9)	/* Sprite address */
	write_vdp_reg 	6,0	
	write_vdp_reg	7,1			/* Border color */
	write_vdp_reg	10,1			/* Lines per hblank interrupt */
	write_vdp_reg	11,0			/* 2-cell vertical scrolling */
	write_vdp_reg	12,(VDP12_SCREEN_V224 | VDP12_SCREEN_H320 | VDP12_SPR_SHADOWS)
	write_vdp_reg	13,(0x6000 >> 10)	/* Horizontal scroll address */
	write_vdp_reg	15,2
	write_vdp_reg	16,(VDP16_MAP_V32 + VDP16_MAP_H64)
	write_vdp_reg	17,0
	write_vdp_reg	18,0xff
	rts



#################################################
#                                               #
#        Load tile data from ROM                #
#                                               #
# Parameters:                                   #
#  a3: VRAM base                                # 
#  a4: pattern address                          #
#  d4: number of tiles to load                  #
#  Destroys a2,d0,d6-d7...                      #
#                                               #
#################################################

load_tiles:
	move.l 		#GFXCNTL,a2
	VRAM_ADDR_var 	d0,a3
	move.l 		d0,(a2)
	lsl		#3,d4
	
	move.l 		#GFXDATA,a3
	subq.l 		#1,d4
_copy_tile_data:
	move.l 		(a4)+,(a3)
	dbra 		d4,_copy_tile_data

	rts


load_prepare:
	move.l 		#GFXCNTL,a2
	VRAM_ADDR_var 	d0,a3
	move.l 		d0,(a2)
	move.l 		#GFXDATA,a3

	rts


#################################################
#                                               #
#        Clear one of the screen maps           #
#                                               #
# Parameters:                                   #
#  a0: Map address                              # 
#  d0: Data to write to each map entry          #
#                                               #
#################################################

clear_map:
	move.l 		#GFXCNTL,a4
	VRAM_ADDR_var	d1,a0
	move.l 		d1,(a4)
	move.l 		#GFXDATA,a3
	move.w		#1023,d1	/* Loop counter */
_clear_map_loop:
	move.w		d0,(a3)
	move.w		d0,(a3)
	dbra		d1,_clear_map_loop
	rts
	

#################################################
#                                               #
#        Load color data from ROM               #
#                                               #
# Parameters:                                   #
#  a3: CRAM base                                # 
#  a4: color list address                       #
#  d4: number of colors to load                 #
#                                               #
#################################################

load_colors:
	move.l 		#GFXCNTL,a2
	CRAM_ADDR_var 	d0,a3
	move.l 		d0,(a2)

	move.l 		#GFXDATA,a3
	subq.w		#1,d4
_copy_color_data:
	move.w		(a4)+,(a3)
	dbra		d4,_copy_color_data

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
.globl htimer
.globl vtimer
.globl rand_num
htimer:		.long 0
vtimer:		.long 0
rand_num:	.long 0
scrollx:	.long 0

.end

