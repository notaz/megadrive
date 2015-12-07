/*
 * TeensyTAS, TAS input player for MegaDrive
 * Copyright (c) 2014 notaz
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

#define noinline __attribute__((noinline))

// use power of 2
#define STREAM_BUF_SIZE 512
#define STREAM_BUF_MASK (512 - 1)

/* ?0SA 00DU, ?1CB RLDU */
#define STREAM_EL_SZ 2

static struct {
	uint8_t stream_to[2][STREAM_BUF_SIZE][STREAM_EL_SZ];
	uint8_t stream_from[STREAM_BUF_SIZE][STREAM_EL_SZ];
	struct {
		union {
			uint8_t fixed_state[4];
			uint32_t fixed_state32;
		};
		union {
			uint8_t pending_state[4];
			uint32_t pending_state32;
		};
	} pl[2];
	uint32_t stream_enable_to:1;
	uint32_t stream_enable_from:1;
	uint32_t stream_started:1;
	uint32_t stream_ended:1;
	uint32_t inc_mode:2;
	uint32_t use_pending:1;
	uint32_t frame_cnt;
	uint32_t edge_cnt;
	struct {
		uint32_t i;
		uint32_t o;
	} pos_to_p[2], pos_from;
} g;

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

static noinline void choose_isrs_idle(void);

/* player1 TH */
#define PL1_ISFR   PORTD_ISFR
#define PL1_TH()   ((CORE_PIN21_PINREG >> CORE_PIN21_BIT) & 1)

static void pl1th_isr_fixed(void)
{
	uint32_t isfr, th;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;
	th = PL1_TH();

	GPIOD_PDOR = g.pl[0].fixed_state[th];
	g.edge_cnt++;
}

static noinline void do_to_step_pl1(void)
{
	g.frame_cnt++;

	g.pos_to_p[0].o = (g.pos_to_p[0].o + 1) & STREAM_BUF_MASK;
	if (g.pos_to_p[0].o == g.pos_to_p[0].i)
		// done
		choose_isrs_idle();
}

static void pl1th_isr_do_to_inc(void)
{
	uint32_t isfr, th;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;
	th = PL1_TH();

	GPIOD_PDOR = g.stream_to[0][g.pos_to_p[0].o][th];
	if (th)
		do_to_step_pl1();
}

static void pl1th_isr_do_to(void)
{
	uint32_t isfr, th;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;
	th = PL1_TH();

	GPIOD_PDOR = g.stream_to[0][g.pos_to_p[0].o][th];
	g.edge_cnt++;
}

/* player2 TH */
#define PL2_ISFR   PORTC_ISFR
#define PL2_TH()   ((CORE_PIN15_PINREG >> CORE_PIN15_BIT) & 1)
#define PL2_ADJ(x) ((x) | ((x) << 12))

static void pl2th_isr_fixed(void)
{
	uint32_t isfr, th, v;

	isfr = PL2_ISFR;
	PL2_ISFR = isfr;
	th = PL2_TH();

	v = g.pl[1].fixed_state[th];
	GPIOB_PDOR = PL2_ADJ(v);
}

static noinline void do_to_step_pl2(void)
{
	g.pos_to_p[1].o = (g.pos_to_p[1].o + 1) & STREAM_BUF_MASK;
	if (g.pos_to_p[1].o == g.pos_to_p[1].i)
		// done
		choose_isrs_idle();
}

static void pl2th_isr_do_to_inc(void)
{
	uint32_t isfr, th, v;

	isfr = PL2_ISFR;
	PL2_ISFR = isfr;
	th = PL2_TH();

	v = g.stream_to[1][g.pos_to_p[1].o][th];
	GPIOB_PDOR = PL2_ADJ(v);
	if (th)
		do_to_step_pl2();
}

static void pl2th_isr_do_to_p1d(void)
{
	uint32_t isfr, th, v;

	isfr = PL2_ISFR;
	PL2_ISFR = isfr;
	th = PL2_TH();

	v = g.stream_to[1][g.pos_to_p[0].o][th];
	GPIOB_PDOR = PL2_ADJ(v);

	g.pos_to_p[1].o = g.pos_to_p[0].o;
}

static void pl2th_isr_do_to_inc_pl1(void)
{
	uint32_t isfr, th, v;

	isfr = PL2_ISFR;
	PL2_ISFR = isfr;
	th = PL2_TH();

	v = g.stream_to[1][g.pos_to_p[1].o][th];
	GPIOB_PDOR = PL2_ADJ(v);
	if (th) {
		do_to_step_pl1();
		g.pos_to_p[1].o = g.pos_to_p[0].o;
	}
}

/* vsync handler */
#define VSYNC_ISFR PORTC_ISFR

static void vsync_isr_nop(void)
{
	uint32_t isfr;

	isfr = VSYNC_ISFR;
	VSYNC_ISFR = isfr;
}

// /vsync starts at line 235/259 (ntsc/pal), just as vcounter jumps back
// we care when it comes out (/vsync goes high) after 3 lines at 238/262
static void vsync_isr_frameinc(void)
{
	uint32_t isfr;

	isfr = VSYNC_ISFR;
	VSYNC_ISFR = isfr;

	g.pos_to_p[0].o = (g.pos_to_p[0].o + 1) & STREAM_BUF_MASK;
	g.pos_to_p[1].o = g.pos_to_p[0].o;

	if (g.pos_to_p[0].o == g.pos_to_p[0].i)
		choose_isrs_idle();
	g.frame_cnt++;
}

/* "recording" data */
static noinline void do_from_step(void)
{
	uint32_t s;

	// should hopefully give atomic fixed_state read..
	s = g.pl[0].fixed_state32;
	g.pl[0].fixed_state32 = g.pl[0].pending_state32;
	g.stream_from[g.pos_from.i][0] = s;
	g.stream_from[g.pos_from.i][1] = s >> 8;
	g.pos_from.i = (g.pos_from.i + 1) & STREAM_BUF_MASK;
}

static void pl1th_isr_fixed_do_from(void)
{
	uint32_t isfr, th;

	isfr = PL1_ISFR;
	PL1_ISFR = isfr;
	th = PL1_TH();

	GPIOD_PDOR = g.pl[0].fixed_state[th];
	if (th)
		do_from_step();
	g.edge_cnt++;
}

static void vsync_isr_frameinc_do_from(void)
{
	uint32_t isfr;

	isfr = VSYNC_ISFR;
	VSYNC_ISFR = isfr;

	do_from_step();
	g.frame_cnt++;
}

/* * */
static void choose_isrs(void)
{
	void (*pl1th_handler)(void) = pl1th_isr_fixed;
	void (*pl2th_handler)(void) = pl2th_isr_fixed;
	void (*vsync_handler)(void) = vsync_isr_nop;

	if (g.stream_enable_to) {
		switch (g.inc_mode) {
		case INC_MODE_VSYNC:
			pl1th_handler = pl1th_isr_do_to;
			pl2th_handler = pl2th_isr_do_to_p1d;
			vsync_handler = vsync_isr_frameinc;
			break;
		case INC_MODE_SHARED_PL1:
			pl1th_handler = pl1th_isr_do_to_inc;
			pl2th_handler = pl2th_isr_do_to_p1d;
			break;
		case INC_MODE_SHARED_PL2:
			pl1th_handler = pl1th_isr_do_to;
			pl2th_handler = pl2th_isr_do_to_inc_pl1;
			break;
		case INC_MODE_SEPARATE:
			pl1th_handler = pl1th_isr_do_to_inc;
			pl2th_handler = pl2th_isr_do_to_inc;
			break;
		}
	}
	else if (g.stream_enable_from) {
		g.use_pending = 1;
		switch (g.inc_mode) {
		case INC_MODE_VSYNC:
			vsync_handler = vsync_isr_frameinc_do_from;
			break;
		case INC_MODE_SHARED_PL1:
			pl1th_handler = pl1th_isr_fixed_do_from;
			break;
		case INC_MODE_SHARED_PL2:
		case INC_MODE_SEPARATE:
			/* TODO */
			break;
		}
	}

	attachInterruptVector(IRQ_PORTD, pl1th_handler);
	attachInterruptVector(IRQ_PORTC, pl2th_handler);
	attachInterruptVector(IRQ_PORTA, vsync_handler);
}

static noinline void choose_isrs_idle(void)
{
	attachInterruptVector(IRQ_PORTD, pl1th_isr_fixed);
	attachInterruptVector(IRQ_PORTC, pl2th_isr_fixed);
	attachInterruptVector(IRQ_PORTA, vsync_isr_nop);
}

static void udelay(uint32_t us)
{
	uint32_t start = micros();

	while ((micros() - start) < us) {
		asm volatile("nop; nop; nop; nop");
		yield();
	}
}

static void do_start_seq(void)
{
	uint32_t edge_cnt_last;
	uint32_t edge_cnt;
	uint32_t start, t1, t2;
	int tout;

	start = micros();
	edge_cnt = g.edge_cnt;

	/* magic value */
	g.pl[0].fixed_state[0] =
	g.pl[0].fixed_state[1] = 0x25;

	for (tout = 10000; tout > 0; tout--) {
		edge_cnt_last = edge_cnt;
		udelay(100);
		edge_cnt = g.edge_cnt;

		if (edge_cnt != edge_cnt_last)
			continue;
		if (!PL1_TH())
			break;
	}

	g.pl[0].fixed_state[0] = 0x33;
	g.pl[0].fixed_state[1] = 0x3f;
	GPIOD_PDOR = 0x33;

	t1 = micros();
	if (tout == 0) {
		printf("start_seq timeout1, t=%u\n", t1 - start);
		return;
	}

	for (tout = 100000; tout > 0; tout--) {
		udelay(1);

		if (PL1_TH())
			break;
	}

	t2 = micros();
	if (tout == 0) {
		printf("start_seq timeout2, t1=%u, t2=%u\n",
			t1 - start, t2 - t1);
		return;
	}

	//printf(" t1=%u, t2=%u\n", t1 - start, t2 - t1);

	if (g.stream_started) {
		printf("got start_seq when already started\n");
		return;
	}

	if (!g.stream_enable_to && !g.stream_enable_from) {
		printf("got start_seq, without enable from USB\n");
		return;
	}

	if (g.stream_enable_to && g.pos_to_p[0].i == g.pos_to_p[0].o) {
		printf("got start_seq while stream_to is empty\n");
		return;
	}

	if (g.stream_enable_from && g.pos_from.i != g.pos_from.o) {
		printf("got start_seq while stream_from is not empty\n");
		return;
	}

	__disable_irq();
	choose_isrs();
	g.stream_started = 1;
	__enable_irq();
}

// callers must disable IRQs
static void clear_state(void)
{
	int i;

	g.stream_enable_to = 0;
	g.stream_enable_from = 0;
	g.stream_started = 0;
	g.stream_ended = 0;
	g.inc_mode = INC_MODE_VSYNC;
	g.use_pending = 0;
	for (i = 0; i < ARRAY_SIZE(g.pos_to_p); i++)
		g.pos_to_p[i].i = g.pos_to_p[i].o = 0;
	g.pos_from.i = g.pos_from.o = 0;
	g.frame_cnt = 0;
	memset(g.stream_to[1], 0x3f, sizeof(g.stream_to[1]));
	choose_isrs_idle();
}

static int get_space_to(int p)
{
	return STREAM_BUF_SIZE - ((g.pos_to_p[p].i - g.pos_to_p[p].o)
		& STREAM_BUF_MASK);
}

static int get_used_from(void)
{
	return (g.pos_from.i - g.pos_from.o) & STREAM_BUF_MASK;
}

static void do_usb(void *buf)
{
	struct tas_pkt *pkt = buf;
	uint32_t pos_to_i, i, p;
	int space;

	switch (pkt->type) {
	case PKT_FIXED_STATE:
		memcpy(&i, pkt->data, sizeof(i));
		if (g.use_pending)
			g.pl[0].pending_state32 = i;
		else
			g.pl[0].fixed_state32 = i;
		break;
	case PKT_STREAM_ENABLE:
		__disable_irq();
		clear_state();
		/* wait for start from MD */
		g.stream_enable_to = pkt->enable.stream_to;
		g.stream_enable_from = pkt->enable.stream_from;
		g.inc_mode = pkt->enable.inc_mode;
		if (pkt->enable.no_start_seq) {
			GPIOD_PDOR = 0x3f;
			choose_isrs();
			g.stream_started = 1;
		}
		__enable_irq();
		break;
	case PKT_STREAM_ABORT:
		__disable_irq();
		clear_state();
		__enable_irq();
		break;
	case PKT_STREAM_END:
		g.stream_ended = 1;
		printf("end of stream\n");
		break;
	case PKT_STREAM_DATA_TO_P1:
	case PKT_STREAM_DATA_TO_P2:
		p = pkt->type == PKT_STREAM_DATA_TO_P1 ? 0 : 1;
		pos_to_i = g.pos_to_p[p].i;
		space = get_space_to(p);
		if (space <= pkt->size / STREAM_EL_SZ) {
			printf("got data pkt while space=%d\n", space);
			return;
		}
		for (i = 0; i < pkt->size / STREAM_EL_SZ; i++) {
			memcpy(&g.stream_to[p][pos_to_i++],
			       pkt->data + i * STREAM_EL_SZ,
			       STREAM_EL_SZ);
			pos_to_i &= STREAM_BUF_MASK;
		}
		g.pos_to_p[p].i = pos_to_i;
		break;
	default:
		printf("got unknown pkt type: %04x\n", pkt->type);
		break;
	}
}

static void check_get_data(int p)
{
	struct tas_pkt pkt;
	uint8_t buf[64];
	int ret;

	if (get_space_to(p) <= sizeof(pkt.data) / STREAM_EL_SZ)
		return;

	if (g.pos_to_p[p].i == g.pos_to_p[p].o && g.frame_cnt != 0) {
		printf("underflow detected\n");
		g.stream_enable_to = 0;
		return;
	}

	pkt.type = PKT_STREAM_REQ;
	pkt.req.frame = g.frame_cnt;
	pkt.req.is_p2 = p;

	ret = usb_rawhid_send(&pkt, 1000);
	if (ret != sizeof(pkt)) {
		printf("send STREAM_REQ/%d: %d\n", p, ret);
		return;
	}

	ret = usb_rawhid_recv(buf, 1000);
	if (ret != 64)
		printf("usb_rawhid_recv/s: %d\n", ret);
	else
		do_usb(buf);
}

int main(void)
{
	uint32_t led_time = 0;
	uint32_t scheck_time = 0;
	uint32_t edge_cnt_last;
	uint32_t edge_cnt;
	uint8_t buf[64];
	int i, ret;

	delay(1000); // wait for usb..

	/* ?0SA 00DU, ?1CB RLDU */
	for (i = 0; i < 2; i++) {
		g.pl[i].fixed_state[0] = 0x33;
		g.pl[i].fixed_state[1] = 0x3f;
	}

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	choose_isrs_idle();

	// md pin   th tr tl  r  l  d  u vsync   th  tr  tl  r  l  d  u
	//           7  9  6  4  3  2  1          7   9   6  4  3  2  1
	// md bit*   6  5  4  3  2  1  0          6   5   4  3  2  1  0
	// t bit    d6 d5 d4 d3 d2 d1 d0   a12   c0 b17 b16 b3 b2 b1 b0
	// t pin    21 20  6  8  7 14  2     3   15   1   0 18 19 17 16
	// * - note: tl/tr mixed in most docs

	// player1
	pinMode(21, INPUT);
	attachInterrupt(21, pl1th_isr_fixed, CHANGE);
	NVIC_SET_PRIORITY(IRQ_PORTD, 0);

	pinMode( 2, OUTPUT);
	pinMode(14, OUTPUT);
	pinMode( 7, OUTPUT);
	pinMode( 8, OUTPUT);
	pinMode( 6, OUTPUT);
	pinMode(20, OUTPUT);

	// player2
	pinMode(15, INPUT);
	attachInterrupt(15, pl1th_isr_fixed, CHANGE);
	NVIC_SET_PRIORITY(IRQ_PORTC, 0);

	pinMode(16, OUTPUT);
	pinMode(17, OUTPUT);
	pinMode(19, OUTPUT);
	pinMode(18, OUTPUT);
	pinMode( 0, OUTPUT);
	pinMode( 1, OUTPUT);

	// vsync line
	pinMode(3, INPUT);
	attachInterrupt(3, vsync_isr_nop, RISING);
	NVIC_SET_PRIORITY(IRQ_PORTA, 16);

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

	edge_cnt_last = g.edge_cnt;

	while (1) {
		struct tas_pkt pkt;
		uint32_t now;

		if (g.stream_enable_to && !g.stream_ended) {
			check_get_data(0);
			if (g.inc_mode == INC_MODE_SHARED_PL2
			    || g.inc_mode == INC_MODE_SEPARATE)
				check_get_data(1);
		}

		while (g.stream_enable_from && !g.stream_ended
		  && get_used_from() >= sizeof(pkt.data) / STREAM_EL_SZ)
		{
			uint32_t o;
			int i;

			o = g.pos_from.o;
			for (i = 0; i < sizeof(pkt.data); i += STREAM_EL_SZ) {
				memcpy(pkt.data + i, &g.stream_from[o++],
					STREAM_EL_SZ);
				o &= STREAM_BUF_MASK;
			}
			g.pos_from.o = o;

			pkt.type = PKT_STREAM_DATA_FROM;
			pkt.size = i;

			ret = usb_rawhid_send(&pkt, 1000);
			if (ret != sizeof(pkt)) {
				printf("send DATA_FROM: %d\n", ret);
				break;
			}
		}

		now = millis();

		// start condition check
		if (now - scheck_time > 1000) {
			edge_cnt = g.edge_cnt;
			//printf("e: %d th: %d\n", edge_cnt - edge_cnt_last,
			//      PL1_TH());
			if ((g.stream_enable_to || g.stream_enable_from)
			    && !g.stream_started
			    && edge_cnt - edge_cnt_last > 10000)
			{
				do_start_seq();
				edge_cnt = g.edge_cnt;
			}
			edge_cnt_last = edge_cnt;
			scheck_time = now;
		}

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
