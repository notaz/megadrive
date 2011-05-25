###############################################################################
#
# Copyright (c) 2011, Gražvydas Ignotas
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

#include "transfer.h"

.text
.globl do_transfer


# receive 1 byte to d0
#  in: a1 - data port
#  trash: d1
.macro recv_one_byte is_last=0
	move.b		#0,(a1)		/* clear TH */

0: /*L_wait_tl_low*/
	move.b		(a1),d1
	btst.b		#4,d1
	bne		0b /*L_wait_tl_low*/

	move.b		#0x40,(a1)	/* set TH */
	and.b		#0x0f,d1

0: /*L_wait_tl_hi*/
	move.b		(a1),d0
	btst.b		#4,d0
	beq		0b /*L_wait_tl_hi*/

.if !\is_last
	move.b		#0,(a1)		/* clear TH - ready for next */
.endif
	lsl.b		#4,d0
	or.b		d1,d0
.endm

# send 1 byte in d0
#  in: a1 - data port
#  trash: d1,d2
.macro send_one_byte
	move.b		d0,d2
	and.b		#0x0f,d2

0: /*Lwait_tl_low:*/
	move.b		(a1),d1
	btst.b		#4,d1
	bne		0b /*Lwait_tl_low*/

	move.b		d2,(a1)		/* clears TH and writes data */

	move.b		d0,d2
	lsr.b		#4,d2
	bset.b		#6,d2		/* prepare TH */

0: /*wait_tl_hi*/
	move.b		(a1),d1
	btst.b		#4,d1
	beq		0b /*wait_tl_hi1*/

	move.b		d2,(a1)
.endm

recv_byte:
	moveq.l		#0,d0
	recv_one_byte 1
	rts

# receive 1 16bit word to d0
#  in: a1 - data port
#  trash: d1,d2
recv_word:
	recv_one_byte
	move.b		d0,d2
	recv_one_byte
	lsl.w		#8,d2
	move.b		d0,d2
	move.w		d2,d0
	rts

# receive address/size to d0 (3 bytes BE)
#  in: a1 - data port
#  trash: d1,d2
recv_ad:
	moveq.l		#0,d2
	bsr		recv_byte
	move.b		d0,d2
	bsr		recv_byte
	lsl.l		#8,d2
	move.b		d0,d2
	bsr		recv_byte
	lsl.l		#8,d2
	move.b		d0,d2
	move.l		d2,d0
	rts

# send 1 byte in d0
#  in: a1 - data port
#  trash: d1,d2
send_byte:
	send_one_byte
	rts

.macro switch_to_output
0: /*Lwait_tl_low: PC should switch to rx mode before lowering tl */
	move.b		(a1),d0
	btst.b		#4,d0
	bne		0b /*Lwait_tl_low*/

	move.b		#0x4f,(0xa1000b).l
	move.b		#0x40,(a1)
.endm

.equ sat_maxsize, (80*8+0x200) /* sprites+max_align */

# make sure cache is invalidated
# note: VRAM copy doesn't seem to help here
# note2: cache is updated as data is written
#  in: d0 - vdp reg5, a0 = 0xc00000
#  trash: d1,d2,a2
invalidate_sprite_cache:
	move.w		#0x8f02,4(a0)		/* auto increment 2 */
	lsl.b		#1,d0			/* upper byte of sat address */
	move.b		d0,d1
	lsr.b		#6,d1			/* 15:14 dst addr */
	and.b		#0x3f,d0		/* assemble cmd */
	lsl.w		#8,d0
	swap		d0
	move.b		d1,d0
	move.l		d0,4(a0)

	move.l		#0xffe000,a2
	move.l		#sat_maxsize/2-1,d2
0:
	move.w		(a0),(a2)+
	dbra		d2,0b

	bset		#30,d0			/* VRAM write */
	move.l		d0,4(a0)

	move.l		#0xffe000,a2
	move.l		#sat_maxsize/2-1,d2
0:
	move.w		(a2)+,(a0)
	dbra		d2,0b
	rts



do_transfer:
	lea		0xa10005,a1
	move.b		#0x40,(0xa1000b).l	/* ctrl - all inputs except TH */
	move.b		#0x00,(a1)

	bsr		recv_byte
	cmp.b		#CMD_PREFIX,d0
	bne		return

	bsr		recv_byte
	cmp.b		#CMD_FIRST,d0
	bcs		return
	cmp.b		#CMD_LAST+1,d0
	bcc		return
	sub.b		#CMD_FIRST,d0

	lsl.w		#2,d0
	lea		(jumptab,pc,d0),a0
	jmp		(a0)
jumptab:
	bra		pcc_transfer_recv	/* sent to us */
	bra		pcc_transfer_send	/* recv from us */
	bra		pcc_jump
	bra		pcc_io
	bra		pcc_loadstate
	bra		pcc_vram_send
	bra		pcc_test_code


/* receive data from PC */
pcc_transfer_recv:
	bsr		recv_ad
	move.l		d0,a0
	bsr		recv_ad
	move.l		d0,d3

tr_recv_loop:
	recv_one_byte
	move.b		d0,(a0)+
	subq.l		#1,d3
	bgt		tr_recv_loop
	bra		return


/* send data to PC */
pcc_transfer_send:
	bsr		recv_ad
	move.l		d0,a0
	bsr		recv_ad
	move.l		d0,d3

	switch_to_output

tr_send_loop:
	move.b		(a0)+,d0
	send_one_byte
	subq.l		#1,d3
	bgt		tr_send_loop
	bra		return


/* call specified location */
pcc_jump:
	bsr		recv_ad
	move.l		d0,a0
	jsr		(a0)
	bra		return


/* do simple i/o commands */
pcc_io:
	moveq.l		#0,d4
	bsr		recv_byte
	move.b		d0,d4
	bsr		recv_byte
	lsl.l		#8,d4
	move.b		d0,d4

pcc_io_loop:
	move.b		#0x40,(0xa1000b).l	/* input mode */

	sub.w		#1,d4
	bmi		return

	bsr		recv_byte
	move.b		d0,d3			/* cmd */

	bsr		recv_ad
	move.l		d0,a2			/* addr */

	cmp.b		#IOSEQ_W32, d3
	beq		pcc_io_w32
	cmp.b		#IOSEQ_W16, d3
	beq		pcc_io_w16
	cmp.b		#IOSEQ_W8, d3
	bne		pcc_io_rx

pcc_io_w8:
	bsr		recv_byte
	move.b		d0,(a2)
	bra		pcc_io_loop

pcc_io_w16:
	bsr		recv_byte
	move.b		d0,d3
	bsr		recv_byte
	lsl.w		#8,d3
	move.b		d0,d3
	move.w		d3,(a2)
	bra		pcc_io_loop

pcc_io_w32:
	bsr		recv_byte
	move.b		d0,d3
	bsr		recv_byte
	lsl.w		#8,d3
	move.b		d0,d3
	bsr		recv_byte
	lsl.l		#8,d3
	move.b		d0,d3
	bsr		recv_byte
	lsl.l		#8,d3
	move.b		d0,d3
	move.l		d3,(a2)
	bra		pcc_io_loop

pcc_io_rx:
	switch_to_output

	cmp.b		#IOSEQ_R32, d3
	beq		pcc_io_r32
	cmp.b		#IOSEQ_R16, d3
	beq		pcc_io_r16
	cmp.b		#IOSEQ_R8, d3
	bne		return

pcc_io_r8:
	move.b		(a2),d0
	bsr		send_byte
	bra		pcc_io_loop

pcc_io_r16:
	move.w		(a2),d3
	move.w		d3,d0
	lsr.w		#8,d0
	bsr		send_byte
	move.b		d3,d0
	bsr		send_byte
	bra		pcc_io_loop

pcc_io_r32:
	move.l		(a2),d3
	move.l		d3,d0
	swap		d0
	lsr.l		#8,d0
	bsr		send_byte
	move.l		d3,d0
	swap		d0
	bsr		send_byte
	move.w		d3,d0
	lsr.w		#8,d0
	bsr		send_byte
	move.b		d3,d0
	bsr		send_byte
	bra		pcc_io_loop


/* PicoDrive savestate load */
pcc_loadstate:
	/* write VRAM */
	move.l		#0xc00000,a0
	move.w		#0x8f02,4(a0)		/* auto increment 2 */

	move.l		#0x40000000,4(a0)
	move.l		#0x10000/2-1,d3
tr_do_vram_loop:
	bsr		recv_word
	move.w		d0,(a0)
	dbra		d3, tr_do_vram_loop

	/* write cram */
	move.l		#0xc0000000,4(a0)
	move.l		#0x80/2-1,d3
tr_do_cram_loop:
	bsr		recv_word
	move.w		d0,(a0)
	dbra		d3, tr_do_cram_loop

	/* write vsram */
	move.l		#0x40000010,4(a0)
	move.l		#0x80/2-1,d3
tr_do_vsram_loop:
	bsr		recv_word
	move.w		d0,(a0)
	dbra		d3, tr_do_vsram_loop

	/* recv and write regs */
	lea		0xffe000,a3
	move.l		a3,a2
	moveq.l		#0x20-1,d3
tr_do_vdpreg_recv_loop:
	bsr		recv_byte
	move.b		d0,(a2)+
	dbra		d3, tr_do_vdpreg_recv_loop

	move.l		a3,a2
	moveq.l		#0,d3
tr_do_vdpreg_loop:
	move.b		d3,d1
	or.b		#0x80,d1
	lsl.w		#8,d1
	move.b		(d3,a2),d1
	move.w		d1,4(a0)
	addq.l		#1,d3
	cmp.b		#0x17,d3	/* FIXME: r23 might cause DMA or.. */
	bne		0f		/* ..something and hang VDP.. */
	add.b		#1,d3		/* ..so we skip it */
0:
	cmp.b		#0x20,d3
	blt		tr_do_vdpreg_loop

	moveq.l		#0,d0
	move.b		5(a3),d0
	bsr		invalidate_sprite_cache


0:	bra		0b

	bra		return


pcc_vram_send:
	/* write VRAM */
	move.l		#0xc00000,a0
	move.w		#0x8f02,4(a0)		/* auto increment 2 */
	move.l		#0,4(a0)		/* VRAM read, addr 0 */
	move.l		#0x10000/2-1,d4

	switch_to_output

tr_vram_send_loop:
	move.w		(a0),d3
	move.w		d3,d0
	lsr.w		#8,d0
	bsr		send_byte
	move.b		d3,d0
	bsr		send_byte
	dbra		d4,tr_vram_send_loop

	bra		return



/* some random code */
pcc_test_code:
	bra		return



return:
	move.b		#0,(0xa1000b).l	/* all inputs */
	move.l		#0xffe000,a1
	move.l		d0,(a1)+	/* last state for debug */
	move.l		d1,(a1)+
	move.l		d2,(a1)+
	move.l		d3,(a1)+
	move.l		a0,(a1)+
	rts
#	bra		return_to_main


# vim:filetype=asmM68k
