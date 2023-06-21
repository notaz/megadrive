/*
 * Tool for USB communication with Mega Everdrive Pro
 * Copyright (c) 2023 notaz
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> // hton
#include <termios.h>
#include <poll.h>
#include <unistd.h>

/* from krikzz/MEGA-PRO/megalink/megalink/Edio.cs */
enum megapro_cmd {
        CMD_STATUS = 0x10,
        CMD_GET_MODE = 0x11,
        CMD_IO_RST = 0x12,
        CMD_GET_VDC = 0x13,
        CMD_RTC_GET = 0x14,
        CMD_RTC_SET = 0x15,
        CMD_FLA_RD = 0x16,
        CMD_FLA_WR = 0x17,
        CMD_FLA_WR_SDC = 0x18,
        CMD_MEM_RD = 0x19,
        CMD_MEM_WR = 0x1A,
        CMD_MEM_SET = 0x1B,
        CMD_MEM_TST = 0x1C,
        CMD_MEM_CRC = 0x1D,
        CMD_FPG_USB = 0x1E,
        CMD_FPG_SDC = 0x1F,
        CMD_FPG_FLA = 0x20,
        CMD_FPG_CFG = 0x21,
        CMD_USB_WR = 0x22,
        CMD_FIFO_WR = 0x23,
        CMD_UART_WR = 0x24,
        CMD_REINIT = 0x25,
        CMD_SYS_INF = 0x26,
        CMD_GAME_CTR = 0x27,
        CMD_UPD_EXEC = 0x28,
        CMD_HOST_RST = 0x29,

        CMD_DISK_INIT = 0xC0,
        CMD_DISK_RD = 0xC1,
        CMD_DISK_WR = 0xC2,
        CMD_F_DIR_OPN = 0xC3,
        CMD_F_DIR_RD = 0xC4,
        CMD_F_DIR_LD = 0xC5,
        CMD_F_DIR_SIZE = 0xC6,
        CMD_F_DIR_PATH = 0xC7,
        CMD_F_DIR_GET = 0xC8,
        CMD_F_FOPN = 0xC9,
        CMD_F_FRD = 0xCA,
        CMD_F_FRD_MEM = 0xCB,
        CMD_F_FWR = 0xCC,
        CMD_F_FWR_MEM = 0xCD,
        CMD_F_FCLOSE = 0xCE,
        CMD_F_FPTR = 0xCF,
        CMD_F_FINFO = 0xD0,
        CMD_F_FCRC = 0xD1,
        CMD_F_DIR_MK = 0xD2,
        CMD_F_DEL = 0xD3,

        CMD_USB_RECOV = 0xF0,
        CMD_RUN_APP = 0xF1,
};

#define MAX_ROM_SIZE 0xF80000

#define ADDR_ROM    0x0000000
#define ADDR_SRAM   0x1000000
#define ADDR_BRAM   0x1080000
#define ADDR_CFG    0x1800000
#define ADDR_SSR    0x1802000
#define ADDR_FIFO   0x1810000

#define SIZE_ROMX   0x1000000
#define SIZE_SRAM     0x80000
#define SIZE_BRAM     0x80000

#define HOST_RST_OFF        0
#define HOST_RST_SOFT       1
#define HOST_RST_HARD       2

#define fatal(fmt, ...) do { \
	fprintf(stderr, "\n:%d " fmt, __LINE__, ##__VA_ARGS__); \
	exit(1); \
} while (0)

static int serial_setup(int fd)
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
	//tty.c_cc[VTIME] = 50;            // seconds*10 read timeout?

	ret = tcsetattr(fd, TCSANOW, &tty);
	if (ret != 0) {
		perror("tcsetattr");
		return ret;
	}

	return 0;
}

static void flush_input(int fd)
{
	struct pollfd pfd = { fd, POLLIN, 0 };
	uint8_t buf[0x10000];
	int b = 0, total = 0;

	while (poll(&pfd, 1, 10) > 0) {
		b = read(fd, buf, sizeof(buf));
		if (b > 0)
			total += b;
	}

	if (total != 0) {
		fprintf(stderr, "flushed %d leftover bytes", total);
		if (b > 0)
			fprintf(stderr, ", last '%c' %02x\n", buf[b-1], buf[b-1]);
		puts("");
	}
}

static void serial_write(int fd, const void *data, size_t size)
{
	int ret;

	ret = write(fd, data, size);
	if (ret != size) {
		fprintf(stderr, "write %d/%zd: ", ret, size);
		perror("");
		exit(1);
	}
}

static void serial_read(int fd, void *data, size_t size)
{
	size_t got = 0;
	int ret;

	while (got < size) {
		ret = read(fd, (char *)data + got, size - got);
		if (ret <= 0) {
			fprintf(stderr, "read %d %zd/%zd: ",
				ret, got, size);
			perror("");
			exit(1);
		}
		got += ret;
	}
}

#define serial_read_expect_byte(fd, b) \
	serial_read_expect_byte_(fd, b, __LINE__)
static void serial_read_expect_byte_(int fd, char expected, int line)
{
	struct pollfd pfd = { fd, POLLIN, 0 };
	char byte = 0;
	int ret;

	ret = poll(&pfd, 1, 5000); // 32x reset is > 3s
	if (ret <= 0)
		fatal(":%d poll %d,%d\n", line, ret, errno);

	serial_read(fd, &byte, sizeof(byte));
	if (byte != expected) {
		fatal(":%d wrong response: '%c' %02x, expected '%c'\n",
		      line, byte, byte, expected);
	}
}

static void send_cmd_a(int fd, enum megapro_cmd cmd, const void *arg, size_t alen)
{
	uint8_t buf[4] = { '+', '+' ^ 0xff, cmd, cmd ^ 0xff };
	serial_write(fd, buf, sizeof(buf));
	if (alen)
		serial_write(fd, arg, alen);
}

#if 0
static void send_cmd(int fd, enum megapro_cmd cmd)
{
	send_cmd_a(fd, cmd, NULL, 0);
}
#endif

static void send_cmd_addr_len(int fd, enum megapro_cmd cmd, uint32_t addr, uint32_t len)
{
	struct {
		uint32_t addr;
		uint32_t len;
		uint8_t exec; // ?
	} __attribute__((packed)) args;
	assert(sizeof(args) == 9);
	args.addr = htonl(addr);
	args.len = htonl(len);
	args.exec = 0;
	send_cmd_a(fd, cmd, &args, sizeof(args));
}

static void send_reset_cmd(int fd, uint8_t type)
{
	static int prev_type = -2;

	if (type == prev_type)
		return;

	send_cmd_a(fd, CMD_HOST_RST, &type, sizeof(type));

	//if (type == HOST_RST_OFF && prev_type != HOST_RST_OFF)
	if (!(type & 1) && (prev_type & 1))
		serial_read_expect_byte(fd, 'r');
	prev_type = type;
}

static void mem_write(int fd, uint32_t addr, const void *buf, size_t buf_size)
{
	if (addr < ADDR_CFG)
		send_reset_cmd(fd, HOST_RST_SOFT);
	send_cmd_addr_len(fd, CMD_MEM_WR, addr, buf_size);
	serial_write(fd, buf, buf_size);
}

static void mem_read(int fd, uint32_t addr, void *buf, size_t buf_size)
{
	if (addr < ADDR_CFG)
		send_reset_cmd(fd, HOST_RST_SOFT);
	send_cmd_addr_len(fd, CMD_MEM_RD, addr, buf_size);
	serial_read(fd, buf, buf_size);
}

// supposedly fifo communicates with whatever is running on MD side?
// (as there is no response to fifo stuff while commercial game runs)
static void fifo_write(int fd, const void *buf, size_t buf_size)
{
	mem_write(fd, ADDR_FIFO, buf, buf_size);
}

static void fifo_write_u32(int fd, uint32_t u32)
{
	u32 = htonl(u32);
	fifo_write(fd, &u32, sizeof(u32));
}

static void fifo_write_str(int fd, const char *s)
{
	fifo_write(fd, s, strlen(s));
}

static void fifo_write_len_n_str(int fd, const char *s)
{
	size_t len = strlen(s);
	uint16_t len16 = len;
	assert(len16 == len);
	len16 = htons(len16);
	fifo_write(fd, &len16, sizeof(len16));
	fifo_write(fd, s, len);
}

#define run_t_check(fd) run_t_check_(fd, __LINE__)
static void run_t_check_(int fd, int line)
{
	fifo_write_str(fd, "*t");
	serial_read_expect_byte_(fd, 'k', line);
}

// try to recover from messed up states
static void run_t_check_initial(int fd)
{
	struct pollfd pfd = { fd, POLLIN, 0 };
	char byte = 0;
	int ret;

	fifo_write_str(fd, "*t");
	ret = poll(&pfd, 1, 1000);
	if (ret == 1) {
		serial_read(fd, &byte, sizeof(byte));
		if (byte == 'k')
			return;
		fprintf(stderr, "bad response to '*t': '%c' %02x\n", byte, byte);
	}
	else if (ret == 0)
		fprintf(stderr, "timeout on initial '*t'\n");
	else
		fatal("poll %d,%d\n", ret, errno);

	/*fprintf(stderr, "attempting reset...\n");
	send_reset_cmd(fd, HOST_RST_OFF);
	send_reset_cmd(fd, HOST_RST_SOFT);*/
}

static uint32_t send_from_file(int fd, uint32_t addr, uint32_t size_ovr,
	const char *fname)
{
	uint8_t *buf = NULL;
	FILE *f = NULL;
	size_t size;
	int ret;

	f = fopen(fname, "rb");
	if (f == NULL)
		fatal("fopen %s: %s\n", fname, strerror(errno));

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size_ovr) {
		if (size_ovr > size)
			fatal("size override %u > %zd\n", size_ovr, size);
		size = size_ovr;
	}
	if (size > MAX_ROM_SIZE)
		fatal("size too large: %zd\n", size);
	buf = malloc(size);
	if (!buf)
		fatal("oom\n");
	ret = fread(buf, 1, size, f);
	if (ret != size)
		fatal("fread %s: %d/%zd %s\n", fname, ret, size, strerror(errno));

	mem_write(fd, addr, buf, size);

	free(buf);
	fclose(f);
	return size;
}

static uint32_t recv_to_file(int fd, uint32_t addr, uint32_t size,
	const char *fname)
{
	uint8_t *buf = NULL;
	FILE *f = NULL;
	int ret;

	if (strcmp(fname, "-") == 0)
		f = stdout;
	else
		f = fopen(fname, "wb");
	if (f == NULL)
		fatal("fopen %s: %s", fname, strerror(errno));

	buf = malloc(size);
	if (!buf)
		fatal("oom\n");

	mem_read(fd, addr, buf, size);

	ret = fwrite(buf, 1, size, f);
	if (ret != size)
		fatal("fwrite %s: %d/%d %s\n", fname, ret, size, strerror(errno));

	free(buf);
	if (f == stdout)
		fflush(stdout);
	else
		fclose(f);
	return size;
}

// reference: megalink/Usbio::loadGame
static void send_run_game(int fd, uint32_t size_ovr, const char *fname)
{
	const char *fname_out;
	char name_out[256];
	uint32_t size;

	fname_out = strrchr(fname, '/');
	fname_out = fname_out ? fname_out + 1 : fname;
	snprintf(name_out, sizeof(name_out), "USB:%s", fname_out);

	size = send_from_file(fd, 0, size_ovr, fname);
	send_reset_cmd(fd, HOST_RST_OFF);

	run_t_check(fd);

	fifo_write_str(fd, "*g");
	fifo_write_u32(fd, size);
	fifo_write_len_n_str(fd, name_out);

	serial_read_expect_byte(fd, 0); // not done by reference
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage:\n"
		"%s [-d <ttydevice>] [opts]\n"
		"  -rst [type]             reset\n"
		"  -l <file> [len]         load and run\n"
		"  -w <file> <addr> [len]  write to cart\n"
		"  -r <file> <addr> <len>  read from cart\n"
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

static int goodarg(const char *arg)
{
	return arg && arg[0] && (arg[0] != '-' || arg[1] == 0);
}

static void *getarg(int argc, char *argv[], int arg)
{
	if (!goodarg(argv[arg]))
		invarg(argc, argv, arg);
	return argv[arg];
}

int main(int argc, char *argv[])
{
	const char *portname = "/dev/ttyACM0";
	const char *fname;
	int no_exit_t_check = 0;
	uint8_t byte;
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

	serial_setup(fd);
	flush_input(fd);
	run_t_check_initial(fd);

	for (; arg < argc; arg++)
	{
		uint32_t addr, size = 0;
		if (!strcmp(argv[arg], "-rst")) {
			byte = HOST_RST_SOFT;
			if (goodarg(argv[arg + 1]))
				byte = strtol(argv[++arg], NULL, 0); 
			send_reset_cmd(fd, byte);
		}
		else if (!strcmp(argv[arg], "-l")) {
			fname = getarg(argc, argv, ++arg);
			if (goodarg(argv[arg + 1]))
				size = strtol(argv[++arg], NULL, 0); 
			send_run_game(fd, size, fname);
			no_exit_t_check = 1; // disturbs game startup
		}
		else if (!strcmp(argv[arg], "-w")) {
			fname = getarg(argc, argv, ++arg);
			addr = strtol(getarg(argc, argv, ++arg), NULL, 0);
			if (goodarg(argv[arg + 1]))
				size = strtol(argv[++arg], NULL, 0); 
			send_from_file(fd, addr, size, fname);
		}
		else if (!strcmp(argv[arg], "-r")) {
			fname = getarg(argc, argv, ++arg);
			addr = strtol(getarg(argc, argv, ++arg), NULL, 0);
			size = strtol(getarg(argc, argv, ++arg), NULL, 0);
			recv_to_file(fd, addr, size, fname);
		}
	}
	send_reset_cmd(fd, HOST_RST_OFF);
	if (!no_exit_t_check)
		run_t_check(fd);

	return 0;
}
