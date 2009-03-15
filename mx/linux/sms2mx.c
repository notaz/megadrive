#include <stdio.h>
#include <string.h>

#define IN_SIZE 0x8000
static unsigned char buff[0x400000], buff_in[IN_SIZE];

int main(int argc, char *argv[])
{
	FILE *fi, *fo;
	int size, bsize;
	int i, o;

	if (argc != 3) {
		fprintf(stderr, "usage:\n%s <sms ROM> <mx img>\n", argv[0]);
		return 1;
	}

	fi = fopen(argv[1], "rb");
	if (fi == NULL) {
		fprintf(stderr, "can't open: %s\n", argv[1]);
		return 1;
	}

	fo = fopen(argv[2], "wb");
	if (fo == NULL) {
		fprintf(stderr, "can't open: %s\n", argv[2]);
		return 1;
	}

	fseek(fi, 0, SEEK_END);
	size = ftell(fi);
	fseek(fi, 0, SEEK_SET);

	if (size > IN_SIZE) {
		fprintf(stderr, "ROMs > 32k won't work\n");
		size = IN_SIZE;
	}

	if (fread(buff_in, 1, size, fi) != size)
		fprintf(stderr, "failed to read %s\n", argv[1]);

	memset(buff, 0, sizeof(buff));

	for (bsize = 1; bsize < size; bsize <<= 1)
		;

	for (i = 0, o = 0; o < sizeof(buff); i = (i + 1) & (bsize - 1), o += 2)
		buff[o] = buff[o+1] = buff_in[i];

	if (fwrite(buff, 1, 0x400000, fo) != 0x400000)
		fprintf(stderr, "failed to write to %s\n", argv[2]);
	fclose(fi);
	fclose(fo);

	return 0;
}

