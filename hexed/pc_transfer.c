/*
 * Copyright (c) 2011, Gražvydas Ignotas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the organization nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/io.h>
#include <signal.h>
#include <sys/time.h>
#include <zlib.h>

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

#define PBE2(p) ((*(p) << 8) | *(p+1))
#define PBE3(p) ((*(p) << 16) | (*(p + 1) << 8) | *(p + 2))
#define PBE4(p) ((*(p) << 24) | (*(p + 1) << 16) | (*(p + 2) << 8) | *(p + 3))

static void do_exit(const char *msg, const char *where)
{
	/* switch TL back to high */
	outb(0xe0, PORT_CONTROL);

	if (where)
		fprintf(stderr, "%s: ", where);
	if (msg)
		fprintf(stderr, "%s", msg);
	exit(1);
}

static void inthandler(int u)
{
	do_exit("\n", NULL);
}

static void wait_th_low(const char *where)
{
	struct timeval start, now;

	gettimeofday(&start, NULL);

	while (inb(PORT_STATUS) & 0x40) {
		gettimeofday(&now, NULL);
		if (timediff(now, start) > ACK_TIMEOUT)
			do_exit("timeout waiting TH low\n", where);
	}
}

static void wait_th_high(const char *where)
{
	struct timeval start, now;

	gettimeofday(&start, NULL);

	while (!(inb(PORT_STATUS) & 0x40)) {
		gettimeofday(&now, NULL);
		if (timediff(now, start) > ACK_TIMEOUT)
			do_exit("timeout waiting TH high\n", where);
	}
}

static void output_to_input(void)
{
	/* TL high, recv mode; also give time
	 * MD to see TL before we lower it in recv_byte */
	outb(0xe0, PORT_CONTROL);
	usleep(4*10);			/* must be at least 12+8+8 M68k cycles, 28/7.67M */
}

static void input_to_output(void)
{
	wait_th_low("input_to_output");
	outb(0xc0, PORT_CONTROL);	/* TL high, out mode */
}

static unsigned int recv_byte(void)
{
	unsigned int byte;

	outb(0xe2, PORT_CONTROL);	/* TL low */

	wait_th_low("recv_byte");

	byte = inb(PORT_DATA) & 0x0f;

	outb(0xe0, PORT_CONTROL);	/* TL high */

	wait_th_high("recv_byte");

	byte |= inb(PORT_DATA) << 4;

	return byte;
}

static void recv_bytes(unsigned char *b, size_t count)
{
	while (count-- > 0)
		*b++ = recv_byte();
}

static void send_byte(unsigned int byte)
{
	wait_th_low("recv_bytes");

	outb(byte & 0x0f, PORT_DATA);
	outb(0xc2, PORT_CONTROL);	/* TL low */

	wait_th_high("recv_bytes");

	outb((byte >> 4) & 0x0f, PORT_DATA);
	outb(0xc0, PORT_CONTROL);	/* TL high */
}

static void send_bytes(unsigned char *b, size_t count)
{
	while (count-- > 0)
		send_byte(*b++);
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
		"\trecv <file> <addr> <size>\n"
		"\tjump <addr>\n"
		"\tio {r{8,16,32} <addr>,w{8,16,32} <addr> <data>}*\n"
		"\tloadstate <picodrive_savestate>\n"
		"\trecvvram <file>\n", argv0);
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

static void checked_gzread(gzFile f, void *data, size_t size)
{
	unsigned int ret;
	ret = gzread(f, data, size);
	if (ret != size) {
		fprintf(stderr, "gzread returned %d/%zu\n", ret, size);
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	unsigned int addr = 0, size = 0;
	unsigned int count = 0, i = 0;
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
	else if (strcmp(argv[1], "jump") == 0)
	{
		if (argc != 3)
			usage(argv[0]);

		addr = atoi_or_die(argv[2]);
	}
	else if (strcmp(argv[1], "io") == 0)
	{
		unsigned int cmd = 0, value, iosize;
		unsigned char *p = data;

		for (i = 2; i < argc; ) {
			if (argv[i][0] == 'r')
				cmd = IOSEQ_R8;
			else if (argv[i][0] == 'w')
				cmd = IOSEQ_W8;
			else
				usage(argv[0]);

			iosize = atoi_or_die(&argv[i][1]);
			if (iosize == 32)
				cmd += 2;
			else if (iosize == 16)
				cmd += 1;
			else if (iosize != 8)
				usage(argv[0]);
			*p++ = cmd;
			i++;

			addr = atoi_or_die(argv[i]);
			*p++ = addr >> 16;
			*p++ = addr >> 8;
			*p++ = addr >> 0;
			i++;

			if (cmd == IOSEQ_W8 || cmd == IOSEQ_W16 || cmd == IOSEQ_W32) {
				value = atoi_or_die(argv[i]);
				switch (iosize) {
				case 32:
					*p++ = value >> 24;
					*p++ = value >> 16;
				case 16:
					*p++ = value >> 8;
				case 8:
					*p++ = value >> 0;
				}
				i++;
			}

			count++;
		}
	}
	else if (strcmp(argv[1], "loadstate") == 0)
	{
		unsigned char chunk;
		char header[12];
		gzFile f;
		int len;

		if (argc != 3)
			usage(argv[0]);

		f = gzopen(argv[2], "rb");
		if (f == NULL) {
			perror("gzopen");
			return 1;
		}

		checked_gzread(f, header, sizeof(header));
		if (strncmp(header, "PicoSEXT", 8) != 0) {
			fprintf(stderr, "bad header\n");
			return 1;
		}

		while (!gzeof(file))
		{
			ret = gzread(f, &chunk, 1);
			if (ret == 0)
				break;
			checked_gzread(f, &len, 4);
			//printf("%2d %x\n", chunk, len);
			switch (chunk) {
			case 3: // VRAM
				checked_gzread(f, data, len);
				size += len;
				break;
			case 5: // CRAM
				checked_gzread(f, data + 0x10000, len);
				size += len;
				break;
			case 6: // VSRAM
				checked_gzread(f, data + 0x10080, len);
				size += len;
				break;
			case 8: // video
				checked_gzread(f, data + 0x10100, len);
				data[size+0] &= ~1;   // no display disable
				data[size+1] |= 0x40; // no blanking
				size += 0x20;
				break;
			default:
				if (chunk > 64+8) {
					fprintf(stderr, "bad chunk: %d\n", chunk);
					return 1;
				}
				gzseek(f, len, SEEK_CUR);
				break;
			}
		}
		gzclose(f);
		if (size != 0x10120) {
			fprintf(stderr, "bad final size: %x\n", size);
			return 1;
		}
		// unbyteswap *RAMs (stored byteswapped)
		for (i = 0; i < 0x10100; i += 2) {
			int tmp = data[i];
			data[i] = data[i + 1];
			data[i + 1] = tmp;
		}
	}
	else if (strcmp(argv[1], "recvvram") == 0)
	{
		if (argc != 3)
			usage(argv[0]);

		file = fopen(argv[2], "wb");
		if (file == NULL) {
			fprintf(stderr, "can't open file: %s\n", argv[2]);
			return 1;
		}

		size = 0x10000;
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

	/* wait for start condition */
	if (!(inb(PORT_STATUS) & 0x40))
		printf("waiting for TH high..\n");
	while (!(inb(PORT_STATUS) & 0x40))
		usleep(10000);

	outb(0xe8, PORT_CONTROL);	/* TR low - request for transfer */

	/* wait for request ack */
	if (inb(PORT_STATUS) & 0x40)
		printf("waiting for TH low..\n");
	for (i = 10000; inb(PORT_STATUS) & 0x40; i += 100) {
		if (i > 100000)
			i = 100000;
		usleep(i);
	}

	outb(0xe0, PORT_CONTROL);

	if (strcmp(argv[1], "send") == 0)
	{
		send_cmd(CMD_PC_SEND);
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
		send_cmd(CMD_PC_RECV);
		send_byte((addr >> 16) & 0xff);
		send_byte((addr >>  8) & 0xff);
		send_byte((addr >>  0) & 0xff);
		send_byte((size >> 16) & 0xff);
		send_byte((size >>  8) & 0xff);
		send_byte((size >>  0) & 0xff);
		output_to_input();

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
	else if (strcmp(argv[1], "jump") == 0)
	{
		send_cmd(CMD_JUMP);
		send_byte((addr >> 16) & 0xff);
		send_byte((addr >>  8) & 0xff);
		send_byte((addr >>  0) & 0xff);
	}
	else if (strcmp(argv[1], "io") == 0)
	{
		unsigned char *p = data;
		unsigned char rdata[4];
		send_cmd(CMD_IOSEQ);
		send_byte((count >> 8) & 0xff);
		send_byte((count >> 0) & 0xff);

		for (; count > 0; count--) {
			input_to_output();
			send_bytes(p, 4);	/* cmd + addr */

			switch (p[0]) {
			case IOSEQ_R8:
				output_to_input();
				recv_bytes(rdata, 1);
				printf("r8  %06x       %02x\n", PBE3(p + 1), rdata[0]);
				p += 4;
				break;
			case IOSEQ_R16:
				output_to_input();
				recv_bytes(rdata, 2);
				printf("r16 %06x     %04x\n", PBE3(p + 1), PBE2(rdata));
				p += 4;
				break;
			case IOSEQ_R32:
				output_to_input();
				recv_bytes(rdata, 4);
				printf("r32 %06x %08x\n", PBE3(p + 1), PBE4(rdata));
				p += 4;
				break;
			case IOSEQ_W8:
				send_bytes(&p[4], 1);
				printf("w8  %06x       %02x\n", PBE3(p + 1), p[4]);
				p += 5;
				break;
			case IOSEQ_W16:
				send_bytes(&p[4], 2);
				printf("w16 %06x     %04x\n", PBE3(p + 1), PBE2(p + 4));
				p += 6;
				break;
			case IOSEQ_W32:
				send_bytes(&p[4], 4);
				printf("w32 %06x %08x\n", PBE3(p + 1), PBE4(p + 4));
				p += 8;
				break;
			default:
				do_exit("error in ioseq data\n", NULL);
				break;
			}
		}
	}
	else if (strcmp(argv[1], "loadstate") == 0)
	{
		send_cmd(CMD_LOADSTATE);

		for (i = 0; i < size; i++)
		{
			if ((i & 0x1f) == 0) {
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("%06x/%06x", i, size);
				fflush(stdout);
			}

			send_byte(data[i]);
		}
	}
	else if (strcmp(argv[1], "recvvram") == 0)
	{
		send_cmd(CMD_VRAM_RECV);
		output_to_input();

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

	if (size != 0) {
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
		printf("%06x/%06x\n", i, size);
	}
	if (file != NULL)
		fclose(file);

	/* switch TL back to high, disable outputs */
	outb(0xe0, PORT_CONTROL);

	return 0;
}

