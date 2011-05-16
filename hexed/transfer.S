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
	bra		pcc_test_code


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


pcc_transfer_send:
	bsr		recv_ad
	move.l		d0,a0
	bsr		recv_ad
	move.l		d0,d3

0: /*Lwait_tl_low:*/
	move.b		(a1),d0
	btst.b		#4,d0
	bne		0b /*Lwait_tl_low*/

	move.b		#0x4f,(0xa1000b).l
	move.b		#0x40,(a1)

tr_send_loop:
	move.b		(a0)+,d0
	send_one_byte
	subq.l		#1,d3
	bgt		tr_send_loop
	bra		return


pcc_jump:
	bsr		recv_ad
	move.l		d0,a0
	jmp		(a0)


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
	bra		return_to_main


# vim:filetype=asmM68k
