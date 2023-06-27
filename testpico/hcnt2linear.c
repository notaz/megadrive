#include <stdio.h>
#include <assert.h>

//|         Mode |H32     (RSx=00) |H40     (RSx=11) |
//|HCounter      |[1]0x000-0x127   |[1]0x000-0x16C   | 00-93 00-b6
//|progression   |[2]0x1D2-0x1FF   |[2]0x1C9-0x1FF   | e9-ff e4-ff

int main()
{
	unsigned char result[256];
	int i, j, val, vals[420];

	for (i = val = 0; val < 0x200; i++)
	{
		vals[i] = val++;
		if (val == 0x16d)
			val = 0x1C9;
	}
	assert(i == 420);
	for (i = 0; i < 256; i++)
	{
		result[i] = 0;
		for (j = 0; j < 420; j++)
			if (vals[j] / 2 == i)
				break;
		if (j < 420)
			result[i] = (255 * j + 210) / 419;
	}

	printf("{");
	for (i = 0; i < 256; i++)
		printf(" 0x%02x%s", result[i], i < 255 ? "," : "");
	printf(" }\n");

	printf("{");
	for (i = 0; i < 64; i++)
		printf(" 0x%02x%s", result[i*4+2], i < 63 ? "," : "");
	printf(" }\n");

	printf("{");
	for (i = 0; i < 16; i++)
		printf(" 0x%02x%s", result[i*16+8], i < 15 ? "," : "");
	printf(" }\n");

	return 0;
}
