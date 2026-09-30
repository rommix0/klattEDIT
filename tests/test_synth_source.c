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
#define WHOLE 9702 /* 22 * 441 samples: a whole number of periods at 100 and 200 Hz */

static short buf[80000], ref[80000];

/* The LF flow filters' magnitude at f (spec 3.2), written independently of klatt.c. */
static double lf_mag(double f0, double rd, double f)
{
	double t0 = 1 / f0, ra = (-1 + 4.8 * rd) / 100, rk = (22.4 + 11.8 * rd) / 100;
	double rg = rk * (0.5 + 1.2 * rk) / (0.44 * rd - 4 * ra * (0.5 + 1.2 * rk));
	double oq = (1 + rk) / (2 * rg), am = 1 / (1 + rk);
	double fg = 1 / (2 * oq * t0), bg = 1 / (oq * t0 * tan(PI * (1 - am))), fa = 1 / (2 * PI * ra * t0);
	double r = exp(-PI * bg / SR), a1 = -2 * r * cos(2 * PI * fg / SR), a2 = r * r, p = exp(-2 * PI * fa / SR);
	double w = 2 * PI * f / SR;
	double re = 1 + a1 * cos(w) + a2 * cos(2 * w), im = -(a1 * sin(w) + a2 * sin(2 * w));
	double tr = 1 - p * cos(w), ti = p * sin(w);
	return (1 - p) / sqrt((re * re + im * im) * (tr * tr + ti * ti));
}

/* nframes of voicing through the AB bypass only (SW = 1): the output is the source's second difference. */
static int source_only(int src, double f0, double rd, double bgp, double di, double fl, int nframes, short *out)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	g[KG_SRC] = src;
	g[KG_BGP] = bgp;
	g[KG_FL] = fl;
	g[KG_G0] = bgp > 1000 ? 0 : 20; /* the second difference is loud, more so with a wide RGP: stay inside 16 bits */
	f[KP_AV] = 60;
	f[KP_AB] = 60;
	f[KP_F0] = f0;
	f[KP_RD] = rd;
	f[KP_DI] = di;
	return t_synth_steady(g, f, nframes, out, 80000);
}

/* Harmonic k's level in dB over WHOLE samples after SKIP, with the AB path's two first differences divided out. */
static double harmonic_db(const short *x, double f0, int k)
{
	double w = 2 * PI * k * f0 / SR, d = 2 * sin(w / 2);
	return 10 * log10(m_power_at(x + SKIP, WHOLE, SR, k * f0)) - 20 * log10(d * d);
}

static void test_lf_harmonics_follow_the_formula(void)
{
	static const double rds[] = { 0.5, 1.0, 2.5 };
	double hf[3];
	for (int i = 0; i < 3; i++) {
		source_only(1, 100, rds[i], 100, 0, 0, 200, buf);
		double h1 = harmonic_db(buf, 100, 1), worst = 0;
		for (int k = 2; k <= 30; k++) {
			double got = harmonic_db(buf, 100, k) - h1;
			double want = 20 * log10(lf_mag(100, rds[i], k * 100) / lf_mag(100, rds[i], 100));
			if (want > -60) /* quieter harmonics sink into the 16-bit rounding */
				worst = fmax(worst, fabs(got - want));
		}
		CHECK(worst < 1.0);
		hf[i] = harmonic_db(buf, 100, 30) - h1;
	}
	CHECK(hf[0] > hf[1] && hf[1] > hf[2]); /* a larger Rd rolls off more */
}

static double vowel_rms(int src, double f0, double rd)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SRC] = src;
	f[KP_AV] = 60;
	f[KP_F0] = f0;
	f[KP_RD] = rd;
	t_synth_steady(g, f, 200, buf, 80000);
	return m_rms(buf + SKIP, WHOLE);
}

static void test_lf_level(void)
{
	static const double rds[] = { 0.3, 0.6, 1.5, 2.0, 2.7 };
	for (int j = 0; j < 2; j++) {
		double f0 = j ? 200 : 100, r1 = vowel_rms(1, f0, 1.0);
		for (int i = 0; i < 5; i++)
			CHECK_NEAR(20 * log10(vowel_rms(1, f0, rds[i]) / r1), 0, 1.0);
	}
	CHECK_NEAR(20 * log10(vowel_rms(1, 100, 1.0) / vowel_rms(0, 100, 1.0)), 0, 0.5); /* calibration */
}

/* Level of the harmonic nearest 3 kHz relative to H1, from the source through AB. */
static double tilt_db(int src, double f0, double rd)
{
	source_only(src, f0, rd, 100, 0, 0, 200, buf);
	int k = (int)lround(3000 / f0);
	return harmonic_db(buf, f0, k) - harmonic_db(buf, f0, 1);
}

static void test_rd_tilts_the_1980_source(void)
{
	static const double rds[] = { 0.5, 1.0, 1.5, 2.0, 2.5 };
	for (int j = 0; j < 2; j++) {
		double f0 = j ? 200 : 100, t80[5], tlf[5];
		for (int i = 0; i < 5; i++) {
			t80[i] = tilt_db(0, f0, rds[i]);
			tlf[i] = tilt_db(1, f0, rds[i]);
		}
		for (int i = 0; i < 4; i++)
			CHECK(t80[i + 1] < t80[i]);
		for (int i = 0; i < 5; i++)
			CHECK_NEAR(t80[i] - t80[1], tlf[i] - tlf[1], 4.0);
	}
}

static void test_lf_pitch_is_not_rounded(void)
{
	/* 361.475 Hz is a period of 30.5 samples: rounding periods to whole samples would be 1.7 % off. */
	int n = source_only(1, 361.475, 1.0, 100, 0, 0, 200, buf);
	CHECK_NEAR(m_f0(buf + SKIP, n - SKIP, SR, 200, 450), 361.475, 0.005 * 361.475);
}

/* Finds pulses as the largest |sample| of each cluster above thresh (clusters are at least 20 samples apart).
 * The source must be short-lived (a wide RGP). Returns the count. */
static int find_pulses(const short *x, int n, double thresh, int *pos, double *amp, int max)
{
	int np = 0;
	for (int i = 1; i < n - 1; i++) {
		double a = fabs((double)x[i]);
		if (a < thresh)
			continue;
		if (np && i - pos[np - 1] < 20) {
			if (a > amp[np - 1]) {
				pos[np - 1] = i;
				amp[np - 1] = a;
			}
		} else if (np < max) {
			pos[np] = i;
			amp[np++] = a;
		}
	}
	return np;
}

/* Flutter's F0 over four periods at a time (the phase accumulator makes that exact to a sample) against the
 * formula's average over the same stretch. */
static void test_flutter(void)
{
	static int pos[400];
	static double amp[400];
	int n = source_only(0, 100, 1.0, 2000, 0, 100, 400, buf); /* 2 s */
	double pk = m_peak(buf, n);
	int np = find_pulses(buf, n, 0.3 * pk, pos, amp, 400);
	CHECK(np > 150);
	double lo = 1e9, hi = -1e9, worst = 0;
	for (int i = 1; i + 4 < np; i += 4) {
		double got = 4 * SR / (pos[i + 4] - pos[i]), want = 0;
		for (int j = pos[i]; j < pos[i + 4]; j++) {
			double t = j / SR;
			want += 100 + 2 * (sin(2 * PI * 12.7 * t) + sin(2 * PI * 7.1 * t) + sin(2 * PI * 4.7 * t));
		}
		want /= pos[i + 4] - pos[i];
		lo = fmin(lo, want);
		hi = fmax(hi, want);
		worst = fmax(worst, fabs(got - want));
	}
	CHECK(hi - lo > 4);              /* flutter moves F0 by several Hz */
	CHECK(worst < 0.10 * (hi - lo)); /* and the pulses follow the formula */
}

static void test_diplophonia(void)
{
	static int pos[64];
	static double amp[64];
	int n = source_only(0, 100, 1.0, 2000, 50, 0, 200, buf);
	int np = find_pulses(buf, n, 0.2 * m_peak(buf, n), pos, amp, 64);
	double t0 = SR / 100, delay = 0.5 * (1 - 0.5) * t0; /* DI = 50 %, open quotient 0.5 */
	CHECK(np > 20);
	int ok_ratio = 1, ok_gap = 1;
	for (int i = 2; i + 1 < np; i += 2) { /* pulses 1, 3, ... (0-based) are the delayed ones */
		double r = amp[i + 1] / amp[i];
		if (fabs(r - 0.5) > 0.05)
			ok_ratio = 0;
		if (fabs(pos[i + 1] - pos[i] - (t0 + delay)) > 1.5)
			ok_gap = 0;
	}
	CHECK(ok_ratio);
	CHECK(ok_gap);
}

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

/* The largest error of a/b against want(i) over samples [from, from + len) where |b| is large enough for the
 * rounding to 16 bits not to matter. */
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
	test_lf_harmonics_follow_the_formula();
	test_lf_level();
	test_rd_tilts_the_1980_source();
	test_lf_pitch_is_not_rounded();
	test_flutter();
	test_diplophonia();
	test_noise_is_white();
	test_af_ramps_and_jumps();
}
