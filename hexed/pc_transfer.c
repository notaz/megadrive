#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h>
#include <signal.h>

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
 */

static void inthandler(int u)
{
	/* switch TL back to high */
	outb(0xe0, 890);
	printf("\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int size, byte, ret, i;
	unsigned char *data;
	char *p = NULL;
	FILE *file;

	if (argc != 4 || argv[1][0] != '-' || (argv[1][1] != 'r' && argv[1][1] != 'w')) {
		printf("usage:\n%s {-r,-w} <file> <size_hex>\n", argv[0]);
		return 1;
	}

	file = fopen(argv[2], argv[1][1] == 'r' ? "wb" : "rb");
	if (file == NULL) {
		fprintf(stderr, "can't open file: %s\n", argv[2]);
		return 1;
	}

	size = (int)strtoul(argv[3], &p, 16);
	if (p == NULL || *p != 0) {
		fprintf(stderr, "can't convert size %s\n", argv[3]);
		return 1;
	}

	data = malloc(size);
	if (data == NULL) {
		fprintf(stderr, "can't alloc %d bytes\n", size);
		return 1;
	}

	ret = ioperm(888, 3, 1);
	if (ret != 0) {
		perror("ioperm");
		return 1;
	}

	signal(SIGINT, inthandler);

	printf("regs: %02x %02x %02x\n", inb(888), inb(889), inb(890));
	outb(0xe0, 890);

	while (!(inb(889) & 0x40)) {
		printf("waiting for TH..\n");
		sleep(5);
	}

	if (argv[1][1] == 'r')
	{
		for (i = 0; i < size; i++)
		{
			if ((i & 0xff) == 0) {
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("%06x/%06x", i, size);
				fflush(stdout);
			}

			outb(0xe2, 890);	/* TL low */

			/* wait for TH low */
			while (inb(889) & 0x40) ;

			byte = inb(888) & 0x0f;

			outb(0xe0, 890);	/* TL high */

			/* wait for TH high */
			while (!(inb(889) & 0x40)) ;

			byte |= inb(888) << 4;
			data[i] = byte;
		}

		fwrite(data, 1, size, file);
	}
	else
	{
		ret = fread(data, 1, size, file);
		if (ret < size)
			printf("warning: read only %d/%d\n", ret, size);

		outb(0xc0, 890);	/* out mode, TL hi */
		outb(data[0] & 0x0f, 888);
		outb(0xc2, 890);	/* out mode, TL low (start condition) */

		for (i = 0; i < size; i++)
		{
			if ((i & 0xff) == 0) {
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
				printf("%06x/%06x", i, size);
				fflush(stdout);
			}

			/* wait for TH low */
			while (inb(889) & 0x40) ;

			byte = data[i];

			outb(byte & 0x0f, 888);
			outb(0xc2, 890);	/* TL low */

			/* wait for TH high */
			while (!(inb(889) & 0x40)) ;

			outb(byte >> 4, 888);
			outb(0xc0, 890);	/* TL high */
		}
	}
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("%06x/%06x\n", i, size);
	fclose(file);

	return 0;
}

