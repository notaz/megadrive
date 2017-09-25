#include <stdio.h>
#include <arpa/inet.h>

static uint32_t rom[0x400000 / 4];

int main(int argc, char *argv[])
{
	size_t ret;
	FILE *f;
	int i;

	if (argc != 2) {
		fprintf(stderr, "usage:\n%s <bin>\n", argv[0]);
		return 1;
	}

	f = fopen(argv[1], "r+");
	if (!f) {
		perror("fopen");
		return 1;
	}

	for (i = 0; i < 0x400000; i += 4) {
		uint32_t v = i;
		v = (v << 8) | ((v >> 16) & 0xff);
		rom[i / 4] = htonl(v);
	}

	ret = fread(rom, 1, sizeof(rom), f);
	if (ret == 0) {
		perror("fread");
		fclose(f);
		return 1;
	}
	rewind(f);

	ret = fwrite(rom, 1, sizeof(rom), f);
	fclose(f);
	if (ret != sizeof(rom)) {
		perror("fwrite");
		return 1;
	}

	return 0;
}
