#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "teensy3/core_pins.h"
#include "teensy3/usb_seremu.h"
#include "teensy3/usb_rawhid.h"

/* ?0SA 00DU, ?1CB RLDU */
static uint8_t fixed_state[4] = { 0x33, 0x3f };

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

static void pin0_irq(void)
{
}

void portb_isr(void)
{
	uint32_t isfr;

	//printf("irq, GPIOB_PDIR: %08x\n", GPIOB_PDIR);

	GPIOD_PDOR = fixed_state[(GPIOB_PDIR >> CORE_PIN0_BIT) & 1];

	isfr = PORTB_ISFR;
	PORTB_ISFR = isfr;
}

int main(void)
{
	//int ret;

	delay(1000); // wait for usb..

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	// md pin   th tr tl  r  l  d  u
	// md bit*   6  5  4  3  2  1  0
	// t bit   b16 d5 d4 d3 d2 d1 d0
	// t pin     0 20  6  8  7 14  2
	// * - note: tl/tr mixed in most docs
	pinMode(0, INPUT);
	attachInterrupt(0, pin0_irq, CHANGE);

	pinMode( 2, OUTPUT);
	pinMode(14, OUTPUT);
	pinMode( 7, OUTPUT);
	pinMode( 8, OUTPUT);
	pinMode( 6, OUTPUT);
	pinMode(20, OUTPUT);

	// led
	pinMode(13, OUTPUT);
	// CORE_PIN13_PORTSET = CORE_PIN13_BITMASK;
	// CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK;

	// CORE_PIN0_PORTSET CORE_PIN0_BITMASK PORTB_PCR16
	printf("GPIOC PDDR, PDIR: %08x %08x\n", GPIOC_PDIR, GPIOC_PDDR);
	printf("GPIOD PDDR, PDIR: %08x %08x\n", GPIOD_PDIR, GPIOD_PDDR);
	printf("PORTB_PCR16: %08x\n", PORTB_PCR16);

	// ret = usb_rawhid_recv(buf, 2000);
	// ret = usb_rawhid_send(buf, 2000);

	while (1) {
		delay(4000);
		fixed_state[1] &= ~0x20;
		CORE_PIN13_PORTSET = CORE_PIN13_BITMASK;

		delay(700);
		fixed_state[1] |= 0x20;
		CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK;
	}
}
