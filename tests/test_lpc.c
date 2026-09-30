#include <math.h>
#include <stdlib.h>
#include "test.h"
#include "measure.h"
#include "klatt.h"
#include "lpc.h"

#define PI 3.14159265358979323846

/* Deterministic, roughly Gaussian noise: the sum of 12 uniform numbers from a 32-bit LCG, centred on zero. */
static double noise(unsigned *s)
{
	double v = 0;
	for (int i = 0; i < 12; i++) {
		*s = *s * 1664525u + 1013904223u;
		v += (*s >> 8) / 16777216.0;
	}
	return v - 6;
}

/* x[i] = e[i] - sum a[j] x[i-j]: the AR process whose prediction polynomial is a. */
static double *ar_process(const double *a, int p, int n)
{
	double *x = calloc(n, sizeof(double));
	unsigned s = 12345;
	for (int i = 0; i < n; i++) {
		double v = noise(&s);
		for (int j = 1; j <= p && j <= i; j++)
			v -= a[j] * x[i - j];
		x[i] = v;
	}
	return x;
}

/* A(z) from conjugate pole pairs (radius r[k], angle th[k]). */
static void poly_from_pairs(const double *r, const double *th, int pairs, double *a)
{
	a[0] = 1;
	for (int i = 1; i <= 2 * pairs; i++)
		a[i] = 0;
	for (int k = 0; k < pairs; k++) {
		double b1 = -2 * r[k] * cos(th[k]), b2 = r[k] * r[k];
		for (int i = 2 * k + 2; i >= 1; i--)
			a[i] += b1 * a[i - 1] + (i >= 2 ? b2 * a[i - 2] : 0);
	}
}

static void check_burg(const double *a, int p, int n)
{
	double *x = ar_process(a, p, n), est[LPC_MAX_ORDER + 1];
	CHECK(lpc_burg(x, n, p, est) > 0);
	CHECK_NEAR(est[0], 1, 0);
	for (int j = 1; j <= p; j++)
		CHECK_NEAR(est[j], a[j], 0.01 * fabs(a[j]));
	free(x);
}

static void test_burg_recovers_ar(void)
{
	double a2[3] = { 1, -1.6, 0.8 };
	check_burg(a2, 2, 20000);
	double r[2] = { 0.95, 0.9 }, th[2] = { 0.3, 1.2 }, a4[5];
	poly_from_pairs(r, th, 2, a4);
	check_burg(a4, 4, 20000);
}

static void test_burg_degenerate(void)
{
	double z[100] = { 0 }, a[5];
	CHECK_NEAR(lpc_burg(z, 100, 4, a), 0, 0); /* silence: no error, all coefficients zero */
	CHECK(a[0] == 1 && a[1] == 0 && a[4] == 0);
	CHECK(lpc_burg(z, 4, 4, a) == -1);
	CHECK(lpc_burg(z, 100, 0, a) == -1);
	CHECK(lpc_burg(z, 100, LPC_MAX_ORDER + 1, a) == -1);
}

/* Every expected root is found (to tol) exactly once. */
static void check_roots(const double *c, int deg, const double *wre, const double *wim, double tol)
{
	double re[LPC_MAX_ORDER], im[LPC_MAX_ORDER];
	int used[LPC_MAX_ORDER] = { 0 };
	CHECK(poly_roots(c, deg, re, im) == deg);
	for (int k = 0; k < deg; k++) {
		int best = -1;
		double bd = 1e300;
		for (int j = 0; j < deg; j++) {
			double d = hypot(re[j] - wre[k], im[j] - wim[k]);
			if (!used[j] && d < bd) {
				bd = d;
				best = j;
			}
		}
		if (best >= 0)
			used[best] = 1;
		CHECK(bd <= tol);
	}
}

static void test_roots(void)
{
	/* (z - 0.5)(z + 0.25)(z - 2) = z^3 - 2.25 z^2 + 0.375 z + 0.25 */
	double c3[4] = { 1, -2.25, 0.375, 0.25 }, r3[3] = { 0.5, -0.25, 2 }, i3[3] = { 0 };
	check_roots(c3, 3, r3, i3, 1e-12);
	/* 2 (z^2 + 1)(z - 3): leading coefficient not 1, roots on the unit circle */
	double c4[4] = { 2, -6, 2, -6 }, r4[3] = { 0, 0, 3 }, i4[3] = { 1, -1, 0 };
	check_roots(c4, 3, r4, i4, 1e-12);
	/* z^2 (z - 1): zero roots */
	double c5[4] = { 1, -1, 0, 0 }, r5[3] = { 0, 0, 1 }, i5[3] = { 0 };
	check_roots(c5, 3, r5, i5, 1e-12);
	/* (z - 1)^2 (z + 2): a double root converges more slowly and less precisely */
	double c6[4] = { 1, 0, -3, 2 }, r6[3] = { 1, 1, -2 }, i6[3] = { 0 };
	check_roots(c6, 3, r6, i6, 1e-6);
	/* degree 22 from eleven conjugate pairs near the unit circle, as LPC at 20 kHz gives */
	double rr[11], th[11], a[23], wre[22], wim[22];
	for (int k = 0; k < 11; k++) {
		rr[k] = 0.75 + 0.024 * k;
		th[k] = 0.1 + 0.27 * k;
		wre[2 * k] = wre[2 * k + 1] = rr[k] * cos(th[k]);
		wim[2 * k] = rr[k] * sin(th[k]);
		wim[2 * k + 1] = -wim[2 * k];
	}
	poly_from_pairs(rr, th, 11, a);
	check_roots(a, 22, wre, wim, 1e-9);
}

static void test_order_and_window(void)
{
	CHECK(lpc_order(11025) == 14);
	CHECK(lpc_order(16000) == 18);
	CHECK(lpc_order(8000) == 10);
	CHECK(lpc_order(12500) == 16); /* 12.5 rounds up to 14 */
	CHECK(lpc_order(20000) == 22);
	CHECK(lpc_window_len(11025) == 276); /* 275.6 */
	CHECK(lpc_window_len(16000) == 400);
}

static void test_frame_window(void)
{
	double x[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, w[5];
	lpc_frame(x, 10, 5, 5, w); /* samples 3..7 */
	CHECK_NEAR(w[0], 0, 1e-15);           /* the Hann window is zero at the ends */
	CHECK_NEAR(w[2], 1 - 0.97, 1e-15);    /* peak 1 at the centre, times the pre-emphasized 0.03 */
	CHECK_NEAR(w[1], 0.5 * 0.03, 1e-15);
	lpc_frame(x, 10, 0, 5, w);            /* samples -2..2: zeros before the start; x[0] has no predecessor */
	CHECK_NEAR(w[2], 1, 1e-15);
	CHECK_NEAR(w[1], 0, 0);
}

static void test_candidates_from_known_poly(void)
{
	double sr = 10000;
	/* pairs: 500 Hz/60 Hz, 1500 Hz/1200 Hz (too broad), 2500 Hz/r 0.6 (too close to 0), 60 Hz (too low),
	 * 4600 Hz (above 0.45 sr), 3300 Hz/200 Hz */
	double f[6] = { 500, 1500, 2500, 60, 4600, 3300 }, bw[6] = { 60, 1200, 0, 50, 100, 200 }, r[6], th[6], a[13];
	for (int k = 0; k < 6; k++) {
		r[k] = k == 2 ? 0.6 : exp(-PI * bw[k] / sr);
		th[k] = 2 * PI * f[k] / sr;
	}
	poly_from_pairs(r, th, 6, a);
	lpc_cand c[CAND_MAX];
	int n = lpc_candidates(a, 12, sr, c);
	CHECK(n == 2);
	if (n == 2) {
		CHECK_NEAR(c[0].f, 500, 1e-6);
		CHECK_NEAR(c[0].bw, 60, 1e-6);
		CHECK_NEAR(c[1].f, 3300, 1e-6);
		CHECK_NEAR(c[1].bw, 200, 1e-6);
	}
}

/* A steady synthesized vowel: its F1-F3 each have a candidate within 5 %. */
static void check_vowel(double f0, double f1, double f2, double f3)
{
	static short pcm[11025];
	static double x[11025];
	double g[KLATT_NUM_GLOBAL], fr[KLATT_NUM_FRAME], w[400], a[LPC_MAX_ORDER + 1];
	t_default_globals(g);
	t_default_frame(fr);
	g[KG_SRC] = 1;
	fr[KP_AV] = 60;
	fr[KP_F0] = f0;
	fr[KP_F1] = f1;
	fr[KP_F2] = f2;
	fr[KP_F3] = f3;
	int n = t_synth_steady(g, fr, 100, pcm, 11025);
	CHECK(n > 5000);
	for (int i = 0; i < n; i++)
		x[i] = pcm[i] / 32768.0;
	int len = lpc_window_len(11025), p = lpc_order(11025);
	lpc_frame(x, n, n / 2, len, w);
	CHECK(lpc_burg(w, len, p, a) > 0);
	lpc_cand c[CAND_MAX];
	int nc = lpc_candidates(a, p, 11025, c);
	double want[3] = { f1, f2, f3 };
	for (int k = 0; k < 3; k++) {
		double best = 1e9;
		for (int j = 0; j < nc; j++)
			if (fabs(c[j].f - want[k]) < fabs(best - want[k]))
				best = c[j].f;
		if (!(fabs(best - want[k]) <= 0.05 * want[k]))
			printf("  F%d = %g: nearest candidate %g\n", k + 1, want[k], best);
		CHECK(fabs(best - want[k]) <= 0.05 * want[k]);
	}
	for (int j = 0; j < nc; j++) {
		CHECK(c[j].f > CAND_MIN_HZ && c[j].f < CAND_MAX_FRAC * 11025 && c[j].bw < CAND_MAX_BW);
		if (j > 0)
			CHECK(c[j].f > c[j - 1].f);
	}
}

static void test_candidates_of_synthesized_vowels(void)
{
	check_vowel(120, 700, 1250, 2600); /* /a/ as in ba.csv */
	check_vowel(100, 300, 2300, 3000); /* /i/ */
	check_vowel(110, 350, 900, 2400);  /* /u/ */
	check_vowel(210, 850, 1650, 2900); /* female /ae/-like */
}

void suite_lpc(void)
{
	test_burg_recovers_ar();
	test_burg_degenerate();
	test_roots();
	test_order_and_window();
	test_frame_window();
	test_candidates_from_known_poly();
	test_candidates_of_synthesized_vowels();
}
