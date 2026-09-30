/* lpc.c - see lpc.h. Written from the published algorithms: Burg (1967) in Andersen's (1974) recursion, and the
 * Aberth-Ehrlich iteration (Aberth 1973, Ehrlich 1967) for the roots. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lpc.h"

#define PI 3.14159265358979323846

#define ROOT_MAX_ITER 500  /* Aberth sweeps before giving up (a few dozen are typical) */
#define ROOT_TOL 1e-14     /* a sweep whose largest relative correction is below this has converged */
#define ROOT_POLISH 3      /* Newton steps per root on the full polynomial afterwards */

int lpc_order(int sr)
{
	int k = (sr + 999) / 1000; /* sr/1000 rounded up... */
	if (k & 1)
		k++;                   /* ...to an even number */
	return 2 + k;
}

int lpc_window_len(int sr)
{
	return (int)lround(LPC_WINDOW_MS * sr / 1000);
}

void lpc_frame(const double *x, long n, long centre, int len, double *out)
{
	long start = centre - len / 2;
	for (int i = 0; i < len; i++) {
		long s = start + i;
		double v = s >= 0 && s < n ? x[s] : 0;
		double prev = s - 1 >= 0 && s - 1 < n ? x[s - 1] : 0;
		double w = len > 1 ? 0.5 - 0.5 * cos(2 * PI * i / (len - 1)) : 1;
		out[i] = (v - LPC_PREEMPHASIS * prev) * w;
	}
}

double lpc_burg(const double *x, int n, int order, double *a)
{
	if (order < 1 || order > LPC_MAX_ORDER || n <= order || !x || !a)
		return -1;
	double *f = malloc(sizeof(double) * n * 2), *b = f + n;
	if (!f)
		return -1;
	memcpy(f, x, sizeof(double) * n); /* forward prediction errors of the current stage */
	memcpy(b, x, sizeof(double) * n); /* backward prediction errors */
	a[0] = 1;
	for (int i = 1; i <= order; i++)
		a[i] = 0;
	double err = 0;
	for (int i = 0; i < n; i++)
		err += x[i] * x[i];
	err /= n;
	for (int m = 1; m <= order; m++) {
		/* stage m pairs f[i] with b[i - 1] for i = m .. n-1 */
		double num = 0, den = 0;
		for (int i = m; i < n; i++) {
			num += f[i] * b[i - 1];
			den += f[i] * f[i] + b[i - 1] * b[i - 1];
		}
		if (!(den > 0))
			break; /* nothing left to predict: the remaining coefficients stay zero */
		double k = -2 * num / den;
		/* Levinson update of the polynomial: a_m[i] = a[i] + k a[m - i] */
		for (int i = 1, j = m - 1; i <= j; i++, j--) {
			double ai = a[i], aj = a[j];
			a[i] = ai + k * aj;
			if (i != j)
				a[j] = aj + k * ai;
		}
		a[m] = k;
		/* errors of stage m, from the top down so b[i - 1] is still stage m-1's */
		for (int i = n - 1; i >= m; i--) {
			double fi = f[i], bi = b[i - 1];
			f[i] = fi + k * bi;
			b[i] = bi + k * fi;
		}
		err *= 1 - k * k;
	}
	free(f);
	return err;
}

typedef struct {
	double re, im;
} cplx;

static cplx c_sub(cplx a, cplx b) { return (cplx){ a.re - b.re, a.im - b.im }; }
static cplx c_mul(cplx a, cplx b) { return (cplx){ a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; }
static double c_abs2(cplx a) { return a.re * a.re + a.im * a.im; }

static cplx c_div(cplx a, cplx b)
{
	double d = c_abs2(b);
	return (cplx){ (a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d };
}

/* p(z) and p'(z) of the monic polynomial z^deg + c[1] z^(deg-1) + ... + c[deg] (Horner). */
static void horner(const double *c, int deg, cplx z, cplx *p, cplx *dp)
{
	cplx v = { 1, 0 }, d = { 0, 0 };
	for (int i = 1; i <= deg; i++) {
		d = c_mul(d, z);
		d.re += v.re;
		d.im += v.im;
		v = c_mul(v, z);
		v.re += c[i];
	}
	*p = v;
	*dp = d;
}

int poly_roots(const double *c0, int deg, double *re, double *im)
{
	if (deg < 1 || deg > LPC_MAX_ORDER || !(c0[0] != 0))
		return -1;
	double c[LPC_MAX_ORDER + 1];
	for (int i = 0; i <= deg; i++)
		c[i] = c0[i] / c0[0];
	int nz = 0; /* roots at exactly zero: trailing zero coefficients */
	while (deg - nz > 0 && c[deg - nz] == 0)
		nz++;
	int d = deg - nz;
	for (int i = 0; i < nz; i++)
		re[d + i] = im[d + i] = 0;
	if (d == 0)
		return deg;
	/* start on a circle whose radius is the geometric mean of the root magnitudes, at angles offset from the
	 * real axis so no start is real (real starts stay real for a real polynomial) */
	cplx z[LPC_MAX_ORDER];
	double rad = pow(fabs(c[d]), 1.0 / d);
	for (int k = 0; k < d; k++) {
		double ang = 2 * PI * k / d + 0.4;
		z[k] = (cplx){ rad * cos(ang), rad * sin(ang) };
	}
	int converged = 0;
	for (int it = 0; it < ROOT_MAX_ITER && !converged; it++) {
		double worst = 0;
		for (int k = 0; k < d; k++) {
			cplx p, dp;
			horner(c, d, z[k], &p, &dp);
			if (c_abs2(p) == 0)
				continue;
			cplx ratio = c_div(p, dp), sum = { 0, 0 };
			for (int j = 0; j < d; j++)
				if (j != k) {
					cplx inv = c_div((cplx){ 1, 0 }, c_sub(z[k], z[j]));
					sum.re += inv.re;
					sum.im += inv.im;
				}
			cplx den = c_sub((cplx){ 1, 0 }, c_mul(ratio, sum));
			cplx w = c_div(ratio, den);
			if (!isfinite(w.re) || !isfinite(w.im))
				w = ratio; /* a degenerate denominator: plain Newton step */
			z[k] = c_sub(z[k], w);
			double rel = sqrt(c_abs2(w) / (1 + c_abs2(z[k])));
			if (rel > worst)
				worst = rel;
		}
		converged = worst < ROOT_TOL;
	}
	if (!converged)
		return -1;
	for (int k = 0; k < d; k++) {
		for (int s = 0; s < ROOT_POLISH; s++) {
			cplx p, dp;
			horner(c, d, z[k], &p, &dp);
			if (c_abs2(dp) == 0 || c_abs2(p) == 0)
				break;
			z[k] = c_sub(z[k], c_div(p, dp));
		}
		re[k] = z[k].re;
		im[k] = z[k].im;
	}
	return deg;
}

int lpc_candidates(const double *a, int order, double sr, lpc_cand *out)
{
	double re[LPC_MAX_ORDER], im[LPC_MAX_ORDER];
	if (poly_roots(a, order, re, im) != order)
		return 0;
	int n = 0;
	for (int k = 0; k < order; k++) {
		double r = hypot(re[k], im[k]);
		if (!(im[k] > 0) || r < CAND_MIN_RADIUS)
			continue;
		double f = atan2(im[k], re[k]) * sr / (2 * PI), bw = -log(r) * sr / PI;
		if (f <= CAND_MIN_HZ || f >= CAND_MAX_FRAC * sr || bw >= CAND_MAX_BW)
			continue;
		int i = n++; /* insertion sort by frequency */
		while (i > 0 && out[i - 1].f > f) {
			out[i] = out[i - 1];
			i--;
		}
		out[i] = (lpc_cand){ f, bw };
	}
	return n;
}
