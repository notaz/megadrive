#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/io.h>

int main()
{
	int i, ret, fd;
	int dpad = 0;

	for (i = 0;; i++)
	{
		int support = 0;
		char name[64];

		snprintf(name, sizeof(name), "/dev/input/event%d", i);
		fd = open(name, O_RDONLY);
		if (fd == -1)
			break;

		/* check supported events */
		ret = ioctl(fd, EVIOCGBIT(0, sizeof(support)), &support);
		if (ret == -1) {
			printf("ioctl failed on %s\n", name);
			goto skip;
		}

		if (!(support & (1 << EV_KEY)))
			goto skip;
		if (!(support & (1 << EV_ABS)))
			goto skip;

		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		printf("found \"%s\" (type %08x)\n", name, support);
		break;

skip:
		close(fd);
		fd = -1;
	}

	if (fd == -1) {
		printf("no suitable devs\n");
		return 1;
	}

	ret = ioperm(888, 3, 1);
	if (ret != 0) {
		perror("ioperm");
		return 1;
	}

	outb(0xc0, 890);	/* switch to output mode, TL and TR high */
	outb(0x0f, 888);	/* Dx high */

	while (1)
	{
		struct input_event ev;
		int rd;

		rd = read(fd, &ev, sizeof(ev));
		if (rd < (int) sizeof(ev)) {
			perror("in_evdev: error reading");
			return 1;
		}

		if (ev.type == EV_KEY) {
			int val = 0xc0;
			if (ev.code & 1) {
				if (ev.value)
					val |= 2;
				else
					val &= ~2;
			} else {
				if (ev.value)
					val |= 8;
				else
					val &= ~8;
			}
			outb(val, 890);
		}
		else if (ev.type == EV_ABS)
		{
			if (ev.code == ABS_X) {
				if (ev.value < 128)
					dpad |= 4;
				else if (ev.value > 128)
					dpad |= 8;
				else
					dpad &= ~0x0c;
			}
			if (ev.code == ABS_Y) {
				if (ev.value < 128)
					dpad |= 1;
				else if (ev.value > 128)
					dpad |= 2;
				else
					dpad &= ~3;
			}
			outb(~dpad & 0x0f, 888);
		}
	}

	return 0;
}

