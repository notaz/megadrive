#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "teensy3/core_pins.h"
#include "teensy3/usb_seremu.h"
#include "teensy3/usb_rawhid.h"
#include "pkts.h"

#define noinline __attribute__((noinline))

// use power of 2
#define STREAM_BUF_SIZE 512
#define STREAM_BUF_MASK (512 - 1)

/* ?0SA 00DU, ?1CB RLDU */
#define STREAM_EL_SZ 2

static struct {
	uint8_t stream_to[STREAM_BUF_SIZE][STREAM_EL_SZ];
	uint8_t stream_from[STREAM_BUF_SIZE][STREAM_EL_SZ];
	union {
		uint8_t fixed_state[4];
		uint32_t fixed_state32;
	};
	union {
		uint8_t pending_state[4];
		uint32_t pending_state32;
	};
	uint32_t stream_enable_to:1;
	uint32_t stream_enable_from:1;
	uint32_t stream_started:1;
	uint32_t stream_ended:1;
	uint32_t use_readinc:1;
	uint32_t use_pending:1;
	uint32_t frame_cnt;
	uint32_t edge_cnt;
	uint32_t t_i;
	uint32_t t_o;
	uint32_t f_i;
	uint32_t f_o;
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

/* portb handles TH */
static void portb_isr_fixed(void)
{
	uint32_t isfr, th;

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
	th = (GPIOB_PDIR >> CORE_PIN0_BIT) & 1;

	GPIOD_PDOR = g.fixed_state[th];
	g.edge_cnt++;
}

static void portb_isr_do_to_inc(void)
{
	uint32_t isfr, th;

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
	th = (GPIOB_PDIR >> CORE_PIN0_BIT) & 1;

	GPIOD_PDOR = g.stream_to[g.t_o][th];
	if (th) {
		g.t_o = (g.t_o + 1) & STREAM_BUF_MASK;
		if (g.t_o == g.t_i)
			// done
			attachInterruptVector(IRQ_PORTB, portb_isr_fixed);
		g.frame_cnt++;
	}
	g.edge_cnt++;
}

static void portb_isr_do_to(void)
{
	uint32_t isfr, th;

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
	th = (GPIOB_PDIR >> CORE_PIN0_BIT) & 1;

	GPIOD_PDOR = g.stream_to[g.t_o][th];
	g.edge_cnt++;
}

static void portc_isr_nop(void)
{
	uint32_t isfr;

	isfr = PORTC_ISFR;
	PORTC_ISFR = isfr;
}

// /vsync starts at line 235/259 (ntsc/pal), just as vcounter jumps back
// we care when it comes out (/vsync goes high) after 3 lines at 238/262
static void portc_isr_frameinc(void)
{
	uint32_t isfr;

	isfr = PORTC_ISFR;
	PORTC_ISFR = isfr;

	g.t_o = (g.t_o + 1) & STREAM_BUF_MASK;
	if (g.t_o == g.t_i) {
		attachInterruptVector(IRQ_PORTB, portb_isr_fixed);
		attachInterruptVector(IRQ_PORTC, portc_isr_nop);
	}
	g.frame_cnt++;
}

/* "recording" data */
static noinline void do_from_step(void)
{
	uint32_t s;

	// should hopefully give atomic fixed_state read..
	s = g.fixed_state32;
	g.fixed_state32 = g.pending_state32;
	g.stream_from[g.f_i][0] = s;
	g.stream_from[g.f_i][1] = s >> 8;
	g.f_i = (g.f_i + 1) & STREAM_BUF_MASK;
}

static void portb_isr_fixed_do_from(void)
{
	uint32_t isfr, th;

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
	th = (GPIOB_PDIR >> CORE_PIN0_BIT) & 1;

	GPIOD_PDOR = g.fixed_state[th];
	if (th)
		do_from_step();
	g.edge_cnt++;
}

static void portc_isr_frameinc_do_from(void)
{
	uint32_t isfr;

	isfr = PORTC_ISFR;
	PORTC_ISFR = isfr;

	do_from_step();
	g.frame_cnt++;
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
	g.fixed_state[0] =
	g.fixed_state[1] = 0x25;

	for (tout = 10000; tout > 0; tout--) {
		edge_cnt_last = edge_cnt;
		udelay(100);
		edge_cnt = g.edge_cnt;

		if (edge_cnt != edge_cnt_last)
			continue;
		if (!(GPIOB_PDIR & CORE_PIN0_BITMASK))
			break;
	}

	g.fixed_state[0] = 0x33;
	g.fixed_state[1] = 0x3f;
	GPIOD_PDOR = 0x33;

	t1 = micros();
	if (tout == 0) {
		printf("start_seq timeout1, t=%u\n", t1 - start);
		return;
	}

	for (tout = 100000; tout > 0; tout--) {
		udelay(1);

		if (GPIOB_PDIR & CORE_PIN0_BITMASK)
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

	if (g.stream_enable_to && g.t_i == g.t_o) {
		printf("got start_seq while stream_to is empty\n");
		return;
	}

	if (g.stream_enable_from && g.f_i != g.f_o) {
		printf("got start_seq while stream_from is not empty\n");
		return;
	}

	__disable_irq();
	g.stream_started = 1;
	if (g.stream_enable_to) {
		if (g.use_readinc) {
			attachInterruptVector(IRQ_PORTB, portb_isr_do_to_inc);
			attachInterruptVector(IRQ_PORTC, portc_isr_nop);
		}
		else {
			attachInterruptVector(IRQ_PORTB, portb_isr_do_to);
			attachInterruptVector(IRQ_PORTC, portc_isr_frameinc);
		}
	}
	else if (g.stream_enable_from) {
		g.use_pending = 1;
		if (g.use_readinc) {
			attachInterruptVector(IRQ_PORTB,
						portb_isr_fixed_do_from);
			attachInterruptVector(IRQ_PORTC, portc_isr_nop);
		}
		else {
			attachInterruptVector(IRQ_PORTB, portb_isr_fixed);
			attachInterruptVector(IRQ_PORTC,
					      portc_isr_frameinc_do_from);
		}
	}
	__enable_irq();
}

// callers must disable IRQs
static void clear_state(void)
{
	g.stream_enable_to = 0;
	g.stream_enable_from = 0;
	g.stream_started = 0;
	g.stream_ended = 0;
	g.use_readinc = 0;
	g.use_pending = 0;
	g.t_i = g.t_o = 0;
	g.f_i = g.f_o = 0;
	g.frame_cnt = 0;
	attachInterruptVector(IRQ_PORTB, portb_isr_fixed);
	attachInterruptVector(IRQ_PORTC, portc_isr_nop);
}

static int get_space_to(void)
{
	return STREAM_BUF_SIZE - ((g.t_i - g.t_o) & STREAM_BUF_MASK);
}

static int get_used_from(void)
{
	return (g.f_i - g.f_o) & STREAM_BUF_MASK;
}

static void do_usb(void *buf)
{
	struct tas_pkt *pkt = buf;
	uint32_t t_i, i;
	int space;

	switch (pkt->type) {
	case PKT_FIXED_STATE:
		memcpy(&i, pkt->data, sizeof(i));
		if (g.use_pending)
			g.pending_state32 = i;
		else
			g.fixed_state32 = i;
		break;
	case PKT_STREAM_ENABLE:
		__disable_irq();
		clear_state();
		/* wait for start from MD */
		g.stream_enable_to = pkt->enable.stream_to;
		g.stream_enable_from = pkt->enable.stream_from;
		g.use_readinc = pkt->enable.use_readinc;
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
	case PKT_STREAM_DATA_TO:
		t_i = g.t_i;
		space = get_space_to();
		if (space <= pkt->size / STREAM_EL_SZ) {
			printf("got data pkt while space=%d\n", space);
			return;
		}
		for (i = 0; i < pkt->size / STREAM_EL_SZ; i++) {
			memcpy(&g.stream_to[t_i++],
			       pkt->data + i * STREAM_EL_SZ,
			       STREAM_EL_SZ);
			t_i &= STREAM_BUF_MASK;
		}
		g.t_i = t_i;
		break;
	default:
		printf("got unknown pkt type: %04x\n", pkt->type);
		break;
	}
}

int main(void)
{
	uint32_t led_time = 0;
	uint32_t scheck_time = 0;
	uint32_t edge_cnt_last;
	uint32_t edge_cnt;
	uint8_t buf[64];
	int ret;

	delay(1000); // wait for usb..

	/* ?0SA 00DU, ?1CB RLDU */
	g.fixed_state[0] = 0x33;
	g.fixed_state[1] = 0x3f;

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	// md pin   th tr tl  r  l  d  u vsync
	// md bit*   6  5  4  3  2  1  0
	// t bit   b16 d5 d4 d3 d2 d1 d0    c6
	// t pin     0 20  6  8  7 14  2    11
	// * - note: tl/tr mixed in most docs
	pinMode(0, INPUT);
	attachInterrupt(0, portb_isr_fixed, CHANGE);
	attachInterruptVector(IRQ_PORTB, portb_isr_fixed);
	pinMode(11, INPUT);
	attachInterrupt(11, portc_isr_nop, RISING);
	attachInterruptVector(IRQ_PORTC, portc_isr_nop);

	NVIC_SET_PRIORITY(IRQ_PORTB, 0);
	NVIC_SET_PRIORITY(IRQ_PORTC, 16);

	pinMode( 2, OUTPUT);
	pinMode(14, OUTPUT);
	pinMode( 7, OUTPUT);
	pinMode( 8, OUTPUT);
	pinMode( 6, OUTPUT);
	pinMode(20, OUTPUT);

	// led
	pinMode(13, OUTPUT);

	// CORE_PIN0_PORTSET CORE_PIN0_BITMASK PORTB_PCR16
	printf("GPIOB PDDR, PDIR: %08x %08x\n", GPIOB_PDIR, GPIOB_PDDR);
	printf("GPIOC PDDR, PDIR: %08x %08x\n", GPIOC_PDIR, GPIOC_PDDR);
	printf("GPIOD PDDR, PDIR: %08x %08x\n", GPIOD_PDIR, GPIOD_PDDR);
	printf("PORTB_PCR16: %08x\n", PORTB_PCR16);
	printf("PORTC_PCR6:  %08x\n", PORTC_PCR6);

	asm("mrs %0, BASEPRI" : "=r"(ret));
	printf("BASEPRI: %d\n", ret);

	edge_cnt_last = g.edge_cnt;

	while (1) {
		struct tas_pkt pkt;
		uint32_t now;

		while (g.stream_enable_to && !g.stream_ended
		  && get_space_to() > sizeof(pkt.data) / STREAM_EL_SZ)
		{
			if (g.t_i == g.t_o && g.frame_cnt != 0) {
				printf("underflow detected\n");
				g.stream_enable_to = 0;
				break;
			}

			pkt.type = PKT_STREAM_REQ;
			pkt.req.frame = g.frame_cnt;

			ret = usb_rawhid_send(&pkt, 1000);
			if (ret != sizeof(pkt)) {
				printf("send STREAM_REQ: %d\n", ret);
				break;
			}

			ret = usb_rawhid_recv(buf, 1000);
			if (ret != 64)
				printf("usb_rawhid_recv/s: %d\n", ret);
			else
				do_usb(buf);
		}

		while (g.stream_enable_from && !g.stream_ended
		  && get_used_from() >= sizeof(pkt.data) / STREAM_EL_SZ)
		{
			uint32_t f_o;
			int i;

			f_o = g.f_o;
			for (i = 0; i < sizeof(pkt.data); i += STREAM_EL_SZ) {
				memcpy(pkt.data + i, &g.stream_from[f_o++],
					STREAM_EL_SZ);
				f_o &= STREAM_BUF_MASK;
			}
			g.f_o = f_o;

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
			//      (GPIOB_PDIR >> CORE_PIN0_BIT) & 1);
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
