/* xarch_cmp.c - compares the xarch.raw files written by the 32- and 64-bit test runs; they must have the same
 * length and differ by at most 1 in any sample. Usage: xarch_cmp a.raw b.raw */
#include <stdio.h>
#include <stdlib.h>

static short *load(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long bytes = ftell(f);
	fseek(f, 0, SEEK_SET);
	short *p = malloc(bytes > 0 ? bytes : 1);
	*n = (long)fread(p, sizeof(short), bytes / sizeof(short), f);
	fclose(f);
	return p;
}

int main(int argc, char **argv)
{
	long na, nb;
	if (argc != 3) {
		fprintf(stderr, "usage: xarch_cmp a.raw b.raw\n");
		return 2;
	}
	short *a = load(argv[1], &na), *b = load(argv[2], &nb);
	if (!a || !b) {
		fprintf(stderr, "xarch_cmp: cannot read %s\n", !a ? argv[1] : argv[2]);
		return 2;
	}
	if (na != nb || na == 0) {
		printf("xarch: FAILED, lengths %ld and %ld\n", na, nb);
		return 1;
	}
	int worst = 0;
	for (long i = 0; i < na; i++) {
		int d = abs(a[i] - b[i]);
		if (d > worst)
			worst = d;
	}
	printf("xarch: %ld samples, max difference %d LSB: %s\n", na, worst, worst <= 1 ? "ok" : "FAILED");
	return worst <= 1 ? 0 : 1;
}
