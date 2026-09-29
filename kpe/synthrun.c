/* synthrun.c - see synthrun.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "synthrun.h"

int synth_doc(const kpe_doc *d, synth_result *out)
{
	memset(out, 0, sizeof *out);
	klatt_h h;
	int r = KlattCreate(d->globals, &h);
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

void synth_result_free(synth_result *r)
{
	free(r->pcm);
	memset(r, 0, sizeof *r);
}

long synth_frame_start(const kpe_doc *d, int k)
{
	double spf = d->globals[KG_SR] * d->globals[KG_FRAME_MS] / 1000.0;
	return (long)floor(k * spf + 1e-9);
}
