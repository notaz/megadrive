/*
 * TeensyTP, Team Player/4-Player Adaptor implementation for Teensy3
 * using a host machine with USB hub
 * Copyright (c) 2015 notaz
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "teensy3/core_pins.h"
#include "teensy3/usb_seremu.h"
#include "teensy3/usb_rawhid.h"
#include "pkts.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define noinline __attribute__((noclone,noinline))

ssize_t _write(int fd, const void *buf, size_t nbyte)
{
	char tbuf[64];
	int ret;

	if (fd != 1 && fd != 2) {
		snprintf(tbuf, sizeof(tbuf), "write to fd %d\n", fd);
		usb_seremu_write(tbuf, strlen(tbuf));
	}

	ret = usb_seremu_write(buf, nbyte);
	return ret < 0 ? ret : nbyte;
}

void yield(void)
{
}

/*
 * 00157:231: w 70 @ 007ef2
 * 00157:231: r 7f @ 007f06
 * 00157:231: w 30 @ 007efa
 * 00157:231: r 33 @ 007f06
 * 00157:232: w 40 @ 007f6a
 * 00157:232: r 7f @ 007f70
 */

/* ?0SA 00DU, ?1CB RLDU */

static struct {
	union {
		uint16_t hw;
		uint8_t b[2];
	} btn3_state[2];
	//uint8_t btn6_state[2][4];
	uint8_t tp_state[16];

	uint32_t cnt;
	uint32_t mode;
	uint32_t pl1th:1;
} g;

/* player1 TH */
#define PL1_ISFR     PORTD_ISFR
#define PL1_TH()     ((CORE_PIN21_PINREG >> CORE_PIN21_BIT) & 1)
#define PL1_TR()     ((CORE_PIN20_PINREG >> CORE_PIN20_BIT) & 1)
#define PL1_TH_MASK  CORE_PIN21_BITMASK
#define PL1_TR_MASK  CORE_PIN20_BITMASK

static void pl1_isr_3btn(void)
{
	uint32_t isfr;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;

	GPIOD_PDOR = g.btn3_state[0].b[PL1_TH()];
}

static noinline void pl1_isr_tp_do_th(void)
{
	uint32_t cnt, th;

	th = PL1_TH();
	cnt = !th;

	GPIOD_PDOR = g.tp_state[cnt] | (PL1_TR() << 4);
	g.cnt = cnt;
	g.pl1th = th;
}

static void pl1_isr_tp(void)
{
	uint32_t isfr;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;

	if (isfr & PL1_TH_MASK)
		pl1_isr_tp_do_th();
	if (!(isfr & PL1_TR_MASK))
		return;
	if (g.pl1th)
		return;

	g.cnt++;
	GPIOD_PDOR = g.tp_state[g.cnt & 0x0f] | (PL1_TR() << 4);
}

/* player2 TH */
#define PL2_ISFR   PORTC_ISFR
#define PL2_TH()   ((CORE_PIN15_PINREG >> CORE_PIN15_BIT) & 1)
#define PL2_ADJ(x) ((x) | ((x) << 12))

static void pl2_isr_nop(void)
{
	uint32_t isfr;

	isfr = PL2_ISFR;
	PL2_ISFR = isfr;

	//GPIOB_PDOR = PL2_ADJ(v);
}

/* * */
static void clear_state()
{
	int p, i;

	// no hw connected
	for (p = 0; p < 2; p++) {
		for (i = 0; i < 2; i++)
			g.btn3_state[p].b[i] = 0x3f;
		//for (i = 0; i < 4; i++)
		//	g.btn6_state[p][i] = 0x3f;
	}

	// 4 pads, nothing pressed
	g.tp_state[0] = 0x03;
	g.tp_state[1] = 0x0f;
	g.tp_state[2] = 0x00;
	g.tp_state[3] = 0x00;
	for (i = 4; i < 8; i++)
		g.tp_state[i] = 0x00; // 3btn
	for (; i < 16; i++)
		g.tp_state[i] = 0x0f;

	g.cnt = 0;
	g.pl1th = PL1_TH();
}

static void switch_mode(int mode)
{
	void (*pl1_handler)(void) = pl1_isr_3btn;
	void (*pl2_handler)(void) = pl2_isr_nop;

	clear_state();

	switch (mode) {
	default:
	case OP_MODE_3BTN:
	case OP_MODE_6BTN:
		pinMode(20, OUTPUT);
		break;
	case OP_MODE_TEAMPLAYER:
		pinMode(20, INPUT);
		attachInterrupt(20, pl1_handler, CHANGE);
		pl1_handler = pl1_isr_tp;
		pl2_handler = pl2_isr_nop;
		GPIOB_PDOR = PL2_ADJ(0x3f);
		break;
	}

	attachInterruptVector(IRQ_PORTD, pl1_handler);
	attachInterruptVector(IRQ_PORTC, pl2_handler);
	g.mode = mode;
}

// btns: MXYZ SACB RLDU
static void update_btns_3btn(const struct tp_pkt *pkt, uint32_t player)
{
	// ?1CB RLDU ?0SA 00DU
	uint16_t s_3btn;

	s_3btn  = (~pkt->bnts[player] << 8) & 0x3f00;
	s_3btn |= (~pkt->bnts[player] >> 2) & 0x0030; // SA
	s_3btn |= (~pkt->bnts[player]     ) & 0x0003; // DU
	g.btn3_state[player].hw = s_3btn;
}

static void update_btns_tp(const struct tp_pkt *pkt, uint32_t player)
{
	uint8_t b0, b1;

	b0 =  ~pkt->bnts[player] & 0x0f;
	b1 = (~pkt->bnts[player] >> 4) & 0x0f;
	g.tp_state[8 + player * 2    ] = b0;
	g.tp_state[8 + player * 2 + 1] = b1;
}

static void do_usb(const void *buf)
{
	const struct tp_pkt *pkt = buf;
	uint32_t i;

	switch (pkt->type) {
	case PKT_UPD_MODE:
		__disable_irq();
		switch_mode(pkt->mode);
		__enable_irq();
		break;
	case PKT_UPD_BTNS:
		switch (g.mode) {
		case OP_MODE_3BTN:
			if (pkt->changed_players & 1)
				update_btns_3btn(pkt, 0);
			if (pkt->changed_players & 2)
				update_btns_3btn(pkt, 1);
			break;
		case OP_MODE_TEAMPLAYER:
			for (i = 0; i < 4; i++) {
				if (!(pkt->changed_players & (1 << i)))
					continue;
				update_btns_tp(pkt, i);
			}
		}
		break;
	default:
		printf("got unknown pkt type: %04x\n", pkt->type);
		break;
	}
}

int main(void)
{
	uint32_t led_time = 0;
	uint8_t buf[64];
	int ret;

	delay(1000); // wait for usb..

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	switch_mode(OP_MODE_3BTN);

	// md pin   th tr tl  r  l  d  u vsync   th  tr  tl  r  l  d  u
	//           7  9  6  4  3  2  1          7   9   6  4  3  2  1
	// md bit*   6  5  4  3  2  1  0          6   5   4  3  2  1  0
	// t bit    d6 d5 d4 d3 d2 d1 d0   a12   c0 b17 b16 b3 b2 b1 b0
	// t pin    21 20  6  8  7 14  2     3   15   1   0 18 19 17 16
	// * - note: tl/tr mixed in most docs

	// player1
	pinMode(21, INPUT);
	// note: func is not used, see attachInterruptVector()
	attachInterrupt(21, pl1_isr_3btn, CHANGE);
	NVIC_SET_PRIORITY(IRQ_PORTD, 0);

	pinMode( 2, OUTPUT);
	pinMode(14, OUTPUT);
	pinMode( 7, OUTPUT);
	pinMode( 8, OUTPUT);
	pinMode( 6, OUTPUT);
	pinMode(20, OUTPUT);

	// player2
	pinMode(15, INPUT);
	attachInterrupt(15, pl2_isr_nop, CHANGE);
	NVIC_SET_PRIORITY(IRQ_PORTC, 0);

	pinMode(16, OUTPUT);
	pinMode(17, OUTPUT);
	pinMode(19, OUTPUT);
	pinMode(18, OUTPUT);
	pinMode( 0, OUTPUT);
	pinMode( 1, OUTPUT);

	// led
	pinMode(13, OUTPUT);

	// lower other priorities
	SCB_SHPR1 = SCB_SHPR2 = SCB_SHPR3 = 0x10101010;

	// CORE_PIN0_PORTSET CORE_PIN0_BITMASK PORTB_PCR16
	printf("GPIOB PDDR, PDIR: %08x %08x\n", GPIOB_PDIR, GPIOB_PDDR);
	printf("GPIOC PDDR, PDIR: %08x %08x\n", GPIOC_PDIR, GPIOC_PDDR);
	printf("GPIOD PDDR, PDIR: %08x %08x\n", GPIOD_PDIR, GPIOD_PDDR);
	printf("PORTB_PCR16: %08x\n", PORTB_PCR16);
	printf("PORTC_PCR6:  %08x\n", PORTC_PCR6);
	printf("PORTD_PCR0:  %08x\n", PORTD_PCR0);

	asm("mrs %0, BASEPRI" : "=r"(ret));
	printf("BASEPRI: %d, SHPR: %08x %08x %08x\n",
		ret, SCB_SHPR1, SCB_SHPR2, SCB_SHPR3);

	while (1) {
		uint32_t now;

		now = millis();

		// led?
		if (CORE_PIN13_PORTREG & CORE_PIN13_BITMASK) {
			if ((int)(now - led_time) > 10)
				CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK;
		}

		// something on rawhid?
		if (usb_rawhid_available() > 0)
		{
			ret = usb_rawhid_recv(buf, 20);
			if (ret == 64) {
				led_time = millis();
				CORE_PIN13_PORTSET = CORE_PIN13_BITMASK;

				do_usb(buf);
			}
			else {
				printf("usb_rawhid_recv: %d\n", ret);
			}
		}
	}

	return 0;
}
