/* Writes xarch.raw (in the working directory) for the 32/64-bit comparison done by tools/xarch_cmp.c. */
#include <stdio.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

void suite_xarch(void)
{
	static short buf[20000];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_AH] = 40;
	f[KP_AF] = 50;
	f[KP_A3] = 50;
	f[KP_A4] = 50;
	f[KP_FNZ] = 330;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	int n = 0;
	for (int k = 0; k < 300; k++) {
		f[KP_F0] = 90 + k * 0.2;   /* a slow pitch glide */
		f[KP_F2] = 1100 + k * 3;   /* and a moving F2 */
		n += KlattSynthFrame(h, f, buf + n, 20000 - n);
	}
	KlattDestroy(h);
	FILE *fp = fopen("xarch.raw", "wb");
	CHECK(fp != NULL);
	if (fp) {
		CHECK(fwrite(buf, sizeof(short), n, fp) == (size_t)n);
		fclose(fp);
	}
}
