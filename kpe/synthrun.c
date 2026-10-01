/* synthrun.c - see synthrun.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "synthrun.h"

int synth_doc_speed(const kpe_doc *d, double spd, synth_result *out)
{
	memset(out, 0, sizeof *out);
	double g[NG];
	memcpy(g, d->globals, sizeof g);
	g[KG_SPD] = spd;
	klatt_h h;
	int r = KlattCreate(g, &h);
	if (r < 0)
		return r;
	KlattReset(h, 1);
	int per = KlattMaxFrameSamples(h);
	short *pcm = malloc(sizeof(short) * ((size_t)per * d->nframes + 1));
	if (!pcm) {
		KlattDestroy(h);
		return KLATT_ERR_MEMORY;
	}
	int n = 0;
	for (int k = 0; k < d->nframes; k++) {
		r = KlattSynthFrame(h, d->frames + (size_t)k * NP, pcm + n, per);
		if (r < 0) {
			free(pcm);
			KlattDestroy(h);
			return r;
		}
		n += r;
	}
	out->pcm = pcm;
	out->n = n;
	out->sr = (int)lround(d->globals[KG_SR]);
	out->overload = KlattOverloadCount(h);
	KlattDestroy(h);
	return 0;
}

int synth_doc(const kpe_doc *d, synth_result *out)
{
	return synth_doc_speed(d, 1.0, out);
}

void synth_result_free(synth_result *r)
{
	free(r->pcm);
	memset(r, 0, sizeof *r);
}

/* Samples per frame at speed spd, computed in the same order as the DLL's KlattCreate. */
static double frame_samples(const kpe_doc *d, double spd)
{
	return d->globals[KG_SR] * d->globals[KG_FRAME_MS] / 1000.0 / spd;
}

long synth_frame_start(const kpe_doc *d, int k, double spd)
{
	return (long)floor(k * frame_samples(d, spd) + 1e-9);
}

double synth_sample_ms(const kpe_doc *d, long n, double spd)
{
	long k = (long)floor(n / frame_samples(d, spd));
	if (k < 0)
		k = 0;
	while (k > 0 && synth_frame_start(d, (int)k, spd) > n) /* the estimate can be one off either way */
		k--;
	while (synth_frame_start(d, (int)k + 1, spd) <= n)
		k++;
	long a = synth_frame_start(d, (int)k, spd), b = synth_frame_start(d, (int)k + 1, spd);
	return (k + (double)(n - a) / (b - a)) * d->globals[KG_FRAME_MS];
}
