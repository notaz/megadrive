#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "teensy3/core_pins.h"
#include "teensy3/usb_seremu.h"
#include "teensy3/usb_rawhid.h"

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

int main(void)
{
	int ret;

	delay(1000); // wait for usb..

	printf("starting, rawhid: %d\n", usb_rawhid_available());

	// ret = usb_rawhid_recv(buf, 2000);
	// ret = usb_rawhid_send(buf, 2000);

	pinMode(13, OUTPUT);
	pinMode(14, OUTPUT);
	while (1) {
		CORE_PIN13_PORTSET = CORE_PIN13_BITMASK;
		CORE_PIN14_PORTSET = CORE_PIN14_BITMASK;
		delay(500*4);
		CORE_PIN13_PORTCLEAR = CORE_PIN13_BITMASK;
		CORE_PIN14_PORTCLEAR = CORE_PIN14_BITMASK;
		delay(500*4);
	}
}
