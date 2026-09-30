/* resample.c - see resample.h.
 *
 * The prototype low-pass is a sinc cut off at RS_CUTOFF of the lower Nyquist frequency, windowed by a Kaiser
 * window (Kaiser's design formulas for the attenuation RS_ATTEN_DB and a transition from 0.9 to 1.0 of the lower
 * Nyquist frequency give its length). It is tabulated once per call, in steps of 1/RS_TABLE_RES of a lower-rate
 * sample, and read with linear interpolation; for downsampling it is stretched by the rate ratio. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "resample.h"

#define PI 3.14159265358979323846

#define RS_ATTEN_DB 70.0   /* design stopband attenuation: 10 dB above the 60 dB required, as margin */
#define RS_CUTOFF 0.95     /* cutoff / lower Nyquist: the middle of the 0.9-1.0 transition band */
#define RS_TRANSITION 0.05 /* transition width in cycles per lower-rate sample (0.9 to 1.0 of Nyquist) */
#define RS_TABLE_RES 1024  /* table steps per lower-rate sample: interpolation error far below -70 dB */

long resample_length(long n, int sr_in, int sr_out)
{
	if (n <= 0 || sr_in <= 0 || sr_out <= 0)
		return 0;
	return (long)(((long long)n * sr_out * 2 + sr_in) / (2LL * sr_in));
}

/* Modified Bessel function of the first kind, order 0 (power series). */
static double bessel_i0(double x)
{
	double sum = 1, term = 1, q = x * x / 4;
	for (int k = 1; k < 200 && term > 1e-17 * sum; k++) {
		term *= q / ((double)k * k);
		sum += term;
	}
	return sum;
}

int resample(const double *x, long n, int sr_in, int sr_out, double **out)
{
	*out = NULL;
	if (n < 0 || sr_in <= 0 || sr_out <= 0 || (n > 0 && !x))
		return -1;
	long m = resample_length(n, sr_in, sr_out);
	double *y = malloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
	if (!y)
		return -1;
	if (sr_in == sr_out) {
		if (n > 0)
			memcpy(y, x, sizeof(double) * (size_t)n);
		*out = y;
		return 0;
	}
	/* Kaiser's formulas: beta for the attenuation, length for the transition width */
	double beta = 0.1102 * (RS_ATTEN_DB - 8.7);
	int half = (int)ceil((RS_ATTEN_DB - 7.95) / (2.285 * 2 * PI * RS_TRANSITION) / 2); /* lower-rate samples */
	int tn = half * RS_TABLE_RES + 2;
	double *tab = malloc(sizeof(double) * tn);
	if (!tab) {
		free(y);
		return -1;
	}
	double fc = 0.5 * RS_CUTOFF, i0b = bessel_i0(beta); /* cutoff in cycles per lower-rate sample */
	for (int i = 0; i < tn; i++) {
		double u = (double)i / RS_TABLE_RES, r = u / half;
		double sinc = u == 0 ? 1 : sin(2 * PI * fc * u) / (2 * PI * fc * u);
		double w = r < 1 ? bessel_i0(beta * sqrt(1 - r * r)) / i0b : 0;
		tab[i] = 2 * fc * sinc * w;
	}
	double stretch = sr_in > sr_out ? (double)sr_in / sr_out : 1; /* input samples per lower-rate sample */
	double reach = half * stretch;
	for (long j = 0; j < m; j++) {
		double t = (double)j * sr_in / sr_out;
		long lo = (long)ceil(t - reach), hi = (long)floor(t + reach);
		if (lo < 0)
			lo = 0;
		if (hi > n - 1)
			hi = n - 1;
		double acc = 0;
		for (long i = lo; i <= hi; i++) {
			double pos = fabs(i - t) / stretch * RS_TABLE_RES;
			int k = (int)pos;
			if (k >= tn - 1)
				continue;
			double fr = pos - k;
			acc += x[i] * (tab[k] + fr * (tab[k + 1] - tab[k]));
		}
		y[j] = acc / stretch;
	}
	free(tab);
	*out = y;
	return 0;
}
