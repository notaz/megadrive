#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/io.h>
#include <signal.h>
#include <sys/time.h>

#include "transfer.h"

/*
 * PC:
 * BASE+0 ~ data
 * BASE+1 ~ status:  BAOSEI??
 *          /BUSY, ACK, PE, SLCT, ERROR, IRQ
 * BASE+2 ~ control: ??MISIAS
 *          bidirrectMODE, IRQ_EN, /SLCT_IN, INIT, /AUTO_FD_XT, /STROBE
 *
 * SEGA
 * ?HRL3210
 * TH, TR, TL, D3, D2, D1, D0
 *
 * SEGA      PC
 * 1 D0  <->  2 D0
 * 2 D1  <->  3 D1
 * 3 D2  <->  4 D2
 * 4 D3  <->  5 D3
 * 5 +5V
 * 6 TL  <-- 14 /AUTO_FD_XT
 * 7 TH  --> 10 ACK
 * 8 GND --- 21 GND
 * 9 TR  <-- 17 /SLCT_IN
 *
 * start: TH low/high, TL high
 *
 * TH low  - lower nibble: MD ready to recv | MD sent to PC
 * TL low  - lower niblle: sent to MD       | ready to recv from MD
 * TH high - upper nibble: MD ready to recv | MD sent to PC
 * TL high - upper nibble: sent             | ready to recv from MD
 */

#define ACK_TIMEOUT	2000000

#define PORT_DATA	888
#define PORT_STATUS	889
#define PORT_CONTROL	890

#define timediff(now, start) \
	((now.tv_sec - start.tv_sec) * 1000000 + now.tv_usec - start.tv_usec)

static void do_exit(const char *msg)
{
	/* switch TL back to high */
	outb(0xe0, PORT_CONTROL);

	if (msg)
		printf("%s", msg);
	exit(1);
}

static void inthandler(int u)
{
	do_exit("\n");
}

static void wait_th_low(void)
{
	struct timeval start, now;

	gettimeofday(&start, NULL);

	while (inb(PORT_STATUS) & 0x40) {
		gettimeofday(&now, NULL);
		if (timediff(now, start) > ACK_TIMEOUT)
			do_exit("timeout waiting TH low\n");
	}
}

static void wait_th_high(void)
{
	struct timeval start, now;

	gettimeofday(&start, NULL);

	while (!(inb(PORT_STATUS) & 0x40)) {
		gettimeofday(&now, NULL);
		if (timediff(now, start) > ACK_TIMEOUT)
			do_exit("timeout waiting TH high\n");
	}
}

static unsigned int recv_byte(void)
{
	unsigned int byte;

	outb(0xe2, PORT_CONTROL);	/* TL low */

	wait_th_low();

	byte = inb(PORT_DATA) & 0x0f;

	outb(0xe0, PORT_CONTROL);	/* TL high */

	wait_th_high();

	byte |= inb(PORT_DATA) << 4;

	return byte;
}

static void send_byte(unsigned int byte)
{
	wait_th_low();

	outb(byte & 0x0f, PORT_DATA);
	outb(0xc2, PORT_CONTROL);	/* TL low */

	wait_th_high();

	outb((byte >> 4) & 0x0f, PORT_DATA);
	outb(0xc0, PORT_CONTROL);	/* TL high */
}

static void send_cmd(unsigned int cmd)
{
	send_byte(CMD_PREFIX);
	send_byte(cmd);
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage:\n%s <cmd> [args]\n"
		"\tsend <file> <addr> [size]\n"
		"\trecv <file> <addr> <size>\n", argv0);
	exit(1);
}

static unsigned int atoi_or_die(const char *a)
{
	char *p = NULL;
	unsigned int i;

	i = strtoul(a, &p, 0);
	if (p == NULL || *p != 0) {
		fprintf(stderr, "atoi: can't convert: %s\n", a);
		exit(1);
	}

	return i;
}

int main(int argc, char *argv[])
{
	unsigned int addr = 0, size = 0, i = 0;
	int ret;
	unsigned char *data;
	FILE *file = NULL;

	if (argc < 2)
		usage(argv[0]);

	data = malloc(0x1000000);
	if (data == NULL) {
		fprintf(stderr, "can't alloc %d bytes\n", 0x1000000);
		return 1;
	}

	/* parse args, read files.. */
	if (strcmp(argv[1], "send") == 0)
	{
		if (argc != 4 && argc != 5)
			usage(argv[0]);

		file = fopen(argv[2], "rb");
		if (file == NULL) {
			fprintf(stderr, "can't open file: %s\n", argv[2]);
			return 1;
		}

		addr = atoi_or_die(argv[3]);
		if (argv[4] == NULL) {
			fseek(file, 0, SEEK_END);
			size = ftell(file);
			fseek(file, 0, SEEK_SET);
		}
		else
			size = atoi_or_die(argv[4]);

		ret = fread(data, 1, size, file);
		if (ret != size) {
			fprintf(stderr, "fread returned %d/%d\n", ret, size);
			perror(NULL);
			return 1;
		}
	}
	else if (strcmp(argv[1], "recv") == 0)
	{
		if (argc != 5)
			usage(argv[0]);

		file = fopen(argv[2], "wb");
		if (file == NULL) {
			fprintf(stderr, "can't open file: %s\n", argv[2]);
			return 1;
		}

		addr = atoi_or_die(argv[3]);
		size = atoi_or_die(argv[4]);

		memset(data, 0, size);
	}
	else
		usage(argv[0]);

	ret = ioperm(PORT_DATA, 3, 1);
	if (ret != 0) {
		perror("ioperm");
		return 1;
	}

	signal(SIGINT, inthandler);

	printf("regs: %02x %02x %02x\n",
		inb(PORT_DATA), inb(PORT_STATUS), inb(PORT_CONTROL));
	outb(0xe8, PORT_CONTROL);	/* TR low - request for transfer */

	if (inb(PORT_STATUS) & 0x40)
		printf("waiting for TH low..\n");
	while (inb(PORT_STATUS) & 0x40)
		sleep(1);

	outb(0xe0, PORT_CONTROL);

	if (strcmp(argv[1], "send") == 0)
	{
		printf("send %06x %06x\n", addr, size);
		send_cmd(CMD_MD_SEND);
		send_byte((addr >> 16) & 0xff);
		send_byte((addr >>  8) & 0xff);
		send_byte((addr >>  0) & 0xff);
		send_byte((size >> 16) & 0xff);
		send_byte((size >>  8) & 0xff);
		send_byte((size >>  0) & 0xff);

		for (i = 0; i < size; i++)
		{
			if ((i & 0xff) == 0) {
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("%06x/%06x", i, size);
				fflush(stdout);
			}

			send_byte(data[i]);
		}
	}
	else if (strcmp(argv[1], "recv") == 0)
	{
		send_cmd(CMD_MD_RECV);
		send_byte((addr >> 16) & 0xff);
		send_byte((addr >>  8) & 0xff);
		send_byte((addr >>  0) & 0xff);
		send_byte((size >> 16) & 0xff);
		send_byte((size >>  8) & 0xff);
		send_byte((size >>  0) & 0xff);
		outb(0xe0, PORT_CONTROL);	/* TL high, recv mode */

		for (i = 0; i < size; i++)
		{
			if ((i & 0xff) == 0) {
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("%06x/%06x", i, size);
				fflush(stdout);
			}

			data[i] = recv_byte();
		}

		fwrite(data, 1, size, file);
	}
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("%06x/%06x\n", i, size);
	fclose(file);

	/* switch TL back to high, disable outputs */
	outb(0xe0, PORT_CONTROL);

	return 0;
}

