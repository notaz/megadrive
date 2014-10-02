#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "teensy3/core_pins.h"
#include "teensy3/usb_seremu.h"
#include "teensy3/usb_rawhid.h"
#include "pkts.h"

// use power of 2
#define STREAM_BUF_SIZE 512
#define STREAM_BUF_MASK (512 - 1)

/* ?0SA 00DU, ?1CB RLDU */
static struct {
	uint8_t stream[STREAM_BUF_SIZE][2];
	uint8_t fixed_state[4];
	uint32_t stream_enable:1;
	uint32_t stream_started:1;
	uint32_t stream_received:1;
	uint32_t edge_cnt;
	uint32_t i;
	uint32_t o;
} g;

#define STREAM_EL_SZ sizeof(g.stream[0])

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

static void my_portb_isr(void)
{
	uint32_t isfr, th;

	//printf("irq, GPIOB_PDIR: %08x\n", GPIOB_PDIR);

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
	th = (GPIOB_PDIR >> CORE_PIN0_BIT) & 1;

	if (g.stream_started) {
		GPIOD_PDOR = g.stream[g.o][th];
		if (th) {
			g.o = (g.o + 1) & STREAM_BUF_MASK;
			if (g.o == g.i) {
				g.stream_started = 0;
				g.stream_enable = 0;
			}
		}
	}
	else {
		GPIOD_PDOR = g.fixed_state[th];
	}
	g.edge_cnt++;
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

	if (!g.stream_enable) {
		printf("got start_seq, without enable from USB\n");
		return;
	}

	if (g.i == g.o) {
		printf("got start_seq while buffer is empty\n");
		return;
	}

	g.stream_started = 1;
}

static int get_space(void)
{
	return STREAM_BUF_SIZE - ((g.i - g.o) & STREAM_BUF_MASK);
}

static void do_usb(void *buf)
{
	struct tas_pkt *pkt = buf;
	uint32_t i, g_i;
	int space;

	switch (pkt->type) {
	case PKT_FIXED_STATE:
		memcpy(g.fixed_state, pkt->data, sizeof(g.fixed_state));
		break;
	case PKT_STREAM_ENABLE:
		g.stream_enable = 1;
		g.stream_started = 0;
		g.stream_received = 0;
		g.i = g.o = 0;
		break;
	case PKT_STREAM_END:
		g.stream_received = 1;
		printf("end of data\n");
		break;
	case PKT_STREAM_DATA:
		g_i = g.i;
		space = get_space();
		if (space <= sizeof(pkt->data) / STREAM_EL_SZ) {
			printf("got data pkt while space=%d\n", space);
			return;
		}
		for (i = 0; i < sizeof(pkt->data) / STREAM_EL_SZ; i++) {
			memcpy(&g.stream[g_i++],
			       pkt->data + i * STREAM_EL_SZ,
			       STREAM_EL_SZ);
			g_i &= STREAM_BUF_MASK;
		}
		g.i = g_i;
		break;
	default:
		printf("got unknown pkt type: %04x\n", pkt->type);
		break;
	}
}

int main(void)
{
	uint32_t edge_cnt_last;
	uint32_t edge_cnt;
	uint8_t buf[64];
	int timeout;
	int ret;

	delay(1000); // wait for usb..

	/* ?0SA 00DU, ?1CB RLDU */
	g.fixed_state[0] = 0x33;
	g.fixed_state[1] = 0x3f;

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	// md pin   th tr tl  r  l  d  u
	// md bit*   6  5  4  3  2  1  0
	// t bit   b16 d5 d4 d3 d2 d1 d0
	// t pin     0 20  6  8  7 14  2
	// * - note: tl/tr mixed in most docs
	pinMode(0, INPUT);
	attachInterrupt(0, my_portb_isr, CHANGE);
	attachInterruptVector(IRQ_PORTB, my_portb_isr);

	// NVIC_SET_PRIORITY(104, 0); // portb
	NVIC_SET_PRIORITY(IRQ_PORTB, 0); // portb

	pinMode( 2, OUTPUT);
	pinMode(14, OUTPUT);
	pinMode( 7, OUTPUT);
	pinMode( 8, OUTPUT);
	pinMode( 6, OUTPUT);
	pinMode(20, OUTPUT);

	// led
	pinMode(13, OUTPUT);

	// CORE_PIN0_PORTSET CORE_PIN0_BITMASK PORTB_PCR16
	printf("GPIOC PDDR, PDIR: %08x %08x\n", GPIOC_PDIR, GPIOC_PDDR);
	printf("GPIOD PDDR, PDIR: %08x %08x\n", GPIOD_PDIR, GPIOD_PDDR);
	printf("PORTB_PCR16: %08x\n", PORTB_PCR16);

	asm("mrs %0, BASEPRI" : "=r"(ret));
	printf("BASEPRI: %d\n", ret);

	timeout = 1000;
	edge_cnt_last = g.edge_cnt;

	while (1) {
		struct tas_pkt pkt;
		while (g.stream_enable && !g.stream_received
		       && get_space() > sizeof(pkt.data) / STREAM_EL_SZ)
		{
			pkt.type = PKT_STREAM_REQ;
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

		if (timeout == 1000) {
			edge_cnt = g.edge_cnt;
			//printf("e: %d th: %d\n", edge_cnt - edge_cnt_last,
			//      (GPIOB_PDIR >> CORE_PIN0_BIT) & 1);
			if (edge_cnt - edge_cnt_last > 10000) {
				do_start_seq();
				edge_cnt = g.edge_cnt;
			}
			edge_cnt_last = edge_cnt;
		}

		ret = usb_rawhid_recv(buf, timeout);
		if (ret == 64) {
			CORE_PIN13_PORTSET = CORE_PIN13_BITMASK;

			do_usb(buf);
			timeout = 20;
		}
		else if (ret == 0) {
			CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK;
			timeout = 1000;
		}
		else {
			printf("usb_rawhid_recv: %d\n", ret);
			timeout = 1000;
		}
	}

	return 0;
}
