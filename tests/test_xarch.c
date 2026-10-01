/* Writes xarch.raw (in the working directory) for the 32/64-bit comparison done by tools/xarch_cmp.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"
#include "csvio.h"
#include "synthrun.h"
#include "wavimport.h"

#define XARCH_MAX 80000

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

/* Synthesizes src, imports it (voice type voice) and appends the synthesis of the imported document, so the
 * whole import has to agree between the architectures too. Returns the new n. */
static int imported(const kpe_doc *src, int voice, short *buf, int n, int max)
{
	synth_result s, t;
	kpe_doc d;
	char msg[256];
	CHECK(synth_doc(src, &s) == 0);
	double *x = malloc(sizeof(double) * s.n);
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	import_options o = { voice, s.sr, 0 };
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		CHECK(synth_doc(&d, &t) == 0);
		int m = t.n < max - n ? t.n : max - n;
		memcpy(buf + n, t.pcm, sizeof(short) * m);
		n += m;
		synth_result_free(&t);
		doc_free(&d);
	}
	free(x);
	synth_result_free(&s);
	return n;
}

void suite_xarch(void)
{
	static short buf[XARCH_MAX];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_AH] = 40;
	f[KP_AF] = 50;
	f[KP_A3] = 50;
	f[KP_A4] = 50;
	f[KP_FNZ] = 330;
	int n = glide(g, f, buf, 0, XARCH_MAX);    /* the 1980 source, Rd tilting it */
	g[KG_SRC] = 1;
	g[KG_FL] = 50;
	f[KP_DI] = 30;
	n = glide(g, f, buf, n, XARCH_MAX);        /* the LF source with flutter and diplophonia */
	g[KG_SPD] = 1.5;
	n = glide(g, f, buf, n, XARCH_MAX);        /* the same at one and a half times the speed */

	kpe_doc d;
	csv_report r;
	if (csv_load(L"../../samples/ba.csv", &d, &r) == 0) { /* WAV import, male path */
		n = imported(&d, VOICE_MALE, buf, n, XARCH_MAX);
		doc_free(&d);
	} else {
		CHECK(0);
	}
	t_default_globals(g);
	g[KG_SRC] = 1;
	if (doc_new(&d, g, 300) == 0) { /* WAV import, a high voice: formant correction */
		for (int k = 0; k < d.nframes; k++) {
			double *v = d.frames + (size_t)k * NP;
			v[KP_AV] = k < 4 || k > 55 ? 0 : 60;
			v[KP_F0] = 220 + k * 0.3;
			v[KP_F1] = 850;
			v[KP_F2] = 1220;
			v[KP_F3] = 2810;
		}
		n = imported(&d, VOICE_FEMALE, buf, n, XARCH_MAX);
		doc_free(&d);
	} else {
		CHECK(0);
	}

	FILE *fp = fopen("xarch.raw", "wb");
	CHECK(fp != NULL);
	if (fp) {
		CHECK(fwrite(buf, sizeof(short), n, fp) == (size_t)n);
		fclose(fp);
	}
}
