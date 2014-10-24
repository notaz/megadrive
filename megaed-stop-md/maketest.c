#include <stdio.h>

int main()
{
	FILE *f;
	int i;

	f = fopen("test.bin", "wb");
	if (f == NULL)
		return 1;

	for (i = 0; i < 0x200000 - 0x10000; i++)
		fwrite(&i, 1, 1, f);
	fclose(f);

	return 0;
}
