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
	speed_t ispeed, ospeed;
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
	write_to_cart(fd, cmd, sizeof(cmd) - 1)

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
	write_with_check(fd, cmd, sizeof(cmd) - 1, 'k')

int main(int argc, char *argv[])
{
	const char *portname = "/dev/ttyUSB0";
	char buf[0x10000];
	size_t size;
	int blocks;
	FILE *f;
	int ret;
	int fd;
	int i;

	fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	setup(fd);

	send_cmd_check_k(fd, "    *T");

	if (argv[1] != NULL)
	{
		f = fopen(argv[1], "rb");
		if (f == NULL)
		{
			perror("fopen");
			return 1;
		}
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size > 0xf00000) {
			fprintf(stderr, "size too large: %zd\n", size);
			return 1;
		}

		blocks = (size + 0xffff) >> 16;
		send_cmd(fd, "*g");
		write_with_check(fd, &blocks, 1, 'k');

		// at this point, 'd' will arrive even if we don't
		// write any more data, what's the point of that?

		for (i = 0; i < blocks; i++) {
			ret = fread(buf, 1, sizeof(buf), f);
			if (i != blocks - 1 && ret != sizeof(buf)) {
				fprintf(stderr, "failed to read block %d/%d\n",
					i, blocks);
			}
			if (ret < sizeof(buf))
				memset(buf + ret, 0, sizeof(buf) - ret);

			write_to_cart(fd, buf, sizeof(buf));
		}

		ret = read_check_byte(fd, 'd');
		if (ret)
			return 1;

		send_cmd_check_k(fd, "*rm");
	}

	printf("all ok\n");

	return 0;
}
