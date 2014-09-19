/*
 * Tool for USB communication with Mega Everdrive
 * Copyright (c) 2013,2014 notaz
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

#define _BSD_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static int setup(int fd)
{
	struct termios tty;
	int ret;

	memset(&tty, 0, sizeof(tty));

	ret = tcgetattr(fd, &tty);
	if (ret != 0)
	{
		perror("tcgetattr");
		return 1;
	}

	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
			| INLCR | IGNCR | ICRNL | IXON);
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tty.c_cflag &= ~(CSIZE | PARENB);
	tty.c_cflag |= CS8;

	//tty.c_cc[VMIN] = 1;
	//tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

	ret = tcsetattr(fd, TCSANOW, &tty);
	if (ret != 0) {
		perror("tcsetattr");
		return ret;
	}

	return 0;
}

static int write_to_cart(int fd, const void *data, size_t size)
{
	int ret;

	ret = write(fd, data, size);
	if (ret != size) {
		perror("write");
		exit(1);
		//return -1;
	}

	return 0;
}

static int read_from_cart(int fd, void *data, size_t size)
{
	int ret;

	ret = read(fd, data, size);
	if (ret != (int)size) {
		perror("read");
		exit(1);
		//return -1;
	}

	return 0;
}

#define send_cmd(fd, cmd) \
	write_to_cart(fd, cmd, strlen(cmd))

static int read_check_byte(int fd, char expect)
{
	char r = '0';
	int ret;

	ret = read_from_cart(fd, &r, 1);
	if (ret != 0) {
		fprintf(stderr, "missing response, need '%c'\n", expect);
		return -1;
	}

	if (r != expect) {
		fprintf(stderr, "unexpected response: '%c', need '%c'\n",
			r, expect);
		return -1;
	}

	return 0;
}

static int write_with_check(int fd, const void *data, size_t size, char chk)
{
	int ret;

	ret = write_to_cart(fd, data, size);
	if (ret)
		return ret;

	ret = read_check_byte(fd, chk);
	if (ret != 0) {
		if (size < 16)
			fprintf(stderr, "data sent: '%16s'\n",
				(const char *)data);
		exit(1);
		//return -1;
	}

	return 0;
}

#define send_cmd_check_k(fd, cmd) \
	write_with_check(fd, cmd, strlen(cmd), 'k')

static int send_file(int fd, const char *fname, const char *cmd)
{
	char buf[0x10000];
	int retval = -1;
	FILE *f = NULL;
	size_t blocksz;
	size_t size;
	int blocks;
	int ret;
	int i;

	f = fopen(fname, "rb");
	if (f == NULL)
	{
		fprintf(stderr, "fopen %s: ", fname);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size > 0xf00000) {
		fprintf(stderr, "size too large: %zd\n", size);
		goto out;
	}

	if (cmd[1] == 'f')
		blocksz = 1024;
	else
		blocksz = sizeof(buf);

	blocks = (size + blocksz - 1) / blocksz;

	send_cmd(fd, cmd);
	ret = write_with_check(fd, &blocks, 1, 'k');
	if (ret)
		return ret;

	// at this point, 'd' will arrive even if we don't
	// write any more data, what's the point of that?
	// probably a timeout?

	for (i = 0; i < blocks; i++) {
		ret = fread(buf, 1, blocksz, f);
		if (i != blocks - 1 && ret != blocksz) {
			fprintf(stderr, "failed to read block %d/%d\n",
				i, blocks);
		}
		if (ret < blocksz)
			memset(buf + ret, 0, blocksz - ret);

		write_to_cart(fd, buf, blocksz);
	}

	ret = read_check_byte(fd, 'd');
	if (ret)
		goto out;

	retval = 0;
out:
	if (f != NULL)
		fclose(f);

	return retval;
}

static void usage(const char *argv0)
{
	printf("usage:\n"
		"%s [-d <ttydevice>] [-f <file>] command(s)\n"
		"known commands for ED OS:\n"
		"  *g - upload game\n"
		"  *w - upload and start OS\n"
		"  *o - upload and start OS\n"
		"  *f - upload (and start?) FPGA firmware\n"
		"  *s - start last ROM (same as pressing start in menu)\n"
		"  *r<x> - run SDRAM contents as mapper x:\n"
		"    s - sms, m - md, o - OS app, c - cd BIOS, M - md10m\n"
		"upload commands must be followed by -f <file>\n"
		, argv0);
	exit(1);
}

static void invarg(int argc, char *argv[], int arg)
{
	if (arg < argc)
		fprintf(stderr, "invalid arg %d: \"%s\"\n", arg, argv[arg]);
	else
		fprintf(stderr, "missing required argument %d\n", arg);
	exit(1);
}

int main(int argc, char *argv[])
{
	const char *portname = "/dev/ttyUSB0";
	const char *pending_cmd = NULL;
	int ret = -1;
	int arg = 1;
	int fd;

	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
		usage(argv[0]);

	if (!strcmp(argv[arg], "-d")) {
		arg++;
		if (argv[arg] != NULL)
			portname = argv[arg];
		else
			invarg(argc, argv, arg);
		arg++;
	}

	fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "open %s: ", portname);
		perror("");
		return 1;
	}

	setup(fd);

	send_cmd_check_k(fd, "    *T");

	for (; arg < argc; arg++) {
		if (!strcmp(argv[arg], "-f")) {
			arg++;
			if (argv[arg] == NULL)
				invarg(argc, argv, argc);
			if (pending_cmd == NULL)
				// assume game upload
				pending_cmd = "*g";

			ret = send_file(fd, argv[arg], pending_cmd);
			if (ret)
				return ret;

			pending_cmd = NULL;
			continue;
		}
		if (argv[arg + 1] && !strcmp(argv[arg + 1], "-f")) {
			/* we'll avoid sending command if there are
			 * problems with specified file */
			pending_cmd = argv[arg];
			continue;
		}

		ret = send_cmd_check_k(fd, argv[arg]);
	}

	return ret;
}
