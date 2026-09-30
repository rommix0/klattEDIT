/* Writes xarch.raw (in the working directory) for the 32/64-bit comparison done by tools/xarch_cmp.c. */
#include <stdio.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

/* 300 frames with a slow pitch glide, a moving F2 and an Rd glide, appended to buf at n; returns the new n. */
static int glide(const double *g, double *f, short *buf, int n, int max)
{
	klatt_h h;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	for (int k = 0; k < 300; k++) {
		f[KP_F0] = 90 + k * 0.2;
		f[KP_F2] = 1100 + k * 3;
		f[KP_RD] = 0.5 + k * (2.0 / 300);
		n += KlattSynthFrame(h, f, buf + n, max - n);
	}
	KlattDestroy(h);
	return n;
}

void suite_xarch(void)
{
	static short buf[40000];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_AH] = 40;
	f[KP_AF] = 50;
	f[KP_A3] = 50;
	f[KP_A4] = 50;
	f[KP_FNZ] = 330;
	int n = glide(g, f, buf, 0, 40000);    /* the 1980 source, Rd tilting it */
	g[KG_SRC] = 1;
	g[KG_FL] = 50;
	f[KP_DI] = 30;
	n = glide(g, f, buf, n, 40000);        /* the LF source with flutter and diplophonia */
	FILE *fp = fopen("xarch.raw", "wb");
	CHECK(fp != NULL);
	if (fp) {
		CHECK(fwrite(buf, sizeof(short), n, fp) == (size_t)n);
		fclose(fp);
	}
}
