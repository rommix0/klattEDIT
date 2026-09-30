/* Voicing-source and noise tests for DLL 1.1: the LF source and Rd, Rd's tilt on the 1980 source, flutter,
 * diplophonia, white noise and the AF/AH ramps. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

#define SR 11025.0
#define PI 3.14159265358979323846
#define SKIP 1102 /* the first 100 ms of every steady sound */

static short buf[80000], ref[80000];

static void test_noise_is_white(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AF] = 60;
	f[KP_AB] = 60;
	int n = t_synth_steady(g, f, 200, buf, 80000);
	static const double edge[] = { 30, 100, 500, 1000, 2000, 3000, 4000, 4961 };
	double dens[7], mean = 0;
	for (int b = 0; b < 7; b++) {
		double frac = 0;
		for (int s = 0; s < 4; s++) /* four 2048-sample stretches */
			frac += m_band_fraction(buf + SKIP + s * 2048, n - SKIP - s * 2048, SR, edge[b], edge[b + 1]);
		dens[b] = 10 * log10(frac / (edge[b + 1] - edge[b]));
		mean += dens[b] / 7;
	}
	for (int b = 0; b < 7; b++)
		CHECK_NEAR(dens[b], mean, 3.0);
}

/* AF-only frication through AB, one AF value per frame. Runs with the same seed draw the same noise, so the
 * sample-by-sample ratio of two runs is the ratio of their AF gains. */
static void af_run(const double *af, int nframes, short *out)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AB] = 60;
	KlattCreate(g, &h);
	int n = 0;
	for (int k = 0; k < nframes; k++) {
		f[KP_AF] = af[k];
		n += KlattSynthFrame(h, f, out + n, 80000 - n);
	}
	KlattDestroy(h);
}

/* The largest error of a/b against a ramp from lo to hi over samples [from, from + len), counting only samples
 * where |b| is large enough for the rounding to 16 bits not to matter. */
static double ratio_error(const short *a, const short *b, int from, int len, double lo, double hi)
{
	double worst = 0;
	for (int i = 0; i < len; i++) {
		if (abs(b[from + i]) < 1000)
			continue;
		double want = lo + (hi - lo) * (i + 1.0) / len;
		worst = fmax(worst, fabs((double)a[from + i] / b[from + i] - want));
	}
	return worst;
}

static void test_af_ramps_and_jumps(void)
{
	double step[12], flat[12];
	for (int k = 0; k < 12; k++) {
		step[k] = k < 10 ? 60 : 70;
		flat[k] = 70;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	double lo = pow(10, -10 / 20.0);
	CHECK(ratio_error(buf, ref, 496, 55, lo, lo) < 0.01);  /* frame 9, samples 496-550: 60 dB */
	CHECK(ratio_error(buf, ref, 551, 55, lo, 1.0) < 0.01); /* frame 10, samples 551-605: the ramp */

	for (int k = 0; k < 12; k++) { /* a burst: 0 -> 60 dB is more than 50 dB, so it jumps */
		step[k] = k < 10 ? 0 : 60;
		flat[k] = 60;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	CHECK(ratio_error(buf, ref, 551, 55, 1.0, 1.0) < 0.01);
}

void suite_synth_source(void)
{
	test_noise_is_white();
	test_af_ramps_and_jumps();
}
