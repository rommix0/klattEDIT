/* pitch.c - see pitch.h. YIN steps 1-5 as published: difference function over a fixed integration window,
 * cumulative mean normalization, absolute threshold, parabolic interpolation. The dynamic-programming pass
 * replaces YIN's step 6 (best local estimate) with a Viterbi search over each voiced run. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pitch.h"

#define PI 3.14159265358979323846

#define YIN_THRESHOLD 0.2    /* YIN's absolute threshold; the value the fextr probe used (1.4 Hz median error) */
#define PITCH_LAG_COST 0.15  /* DP cost per octave below fmax: a preference for the shorter period, as YIN's
                              * threshold rule has, since a periodic signal also repeats at 2, 3... periods. Tuned on
                              * synthesized voices: at 330 Hz the whole-sample pulse timing of the DLL makes the dip
                              * at 2 periods up to 0.08 deeper than the true one; 0.15 keeps the true octave there */
#define PITCH_FLOOR_DB 40.0  /* frames this far below the loudest are unvoiced: the ringing of a resonance
                              * decaying after a sound is periodic too (tuned: 25 ms after an aspiration it is still
                              * voiced by YIN at 43 dB down) */
#define PITCH_JUMP_COST 1.0  /* DP cost per octave of F0 change between neighbouring frames: an octave jump
                              * must be backed by a clearly deeper dip in several frames */
#define PITCH_HOLD 0.4       /* hysteresis: a frame next to a voiced one stays voiced while its aperiodicity is below
                              * this (the threshold starts voicing; transitions, e.g. into diplophonia, reach 0.25) */
#define PITCH_GATE_MS 10.0   /* the window whose energy PITCH_FLOOR_DB applies to */
#define PITCH_MEDIAN 5       /* frames in the median that smooths each voiced run's F0 */

#define MAX_LAG 1024         /* longest lag: sr / fmin = 20000 / 60 = 334 fits */
#define INTERP_HALF 8        /* windowed-sinc taps each side for the difference at a fractional lag */
#define REFINE_MAX 8         /* dips (the deepest at whole lags) evaluated at their fractional lag */

/* The difference function at the fractional lag tau over w samples of buf (which has INTERP_HALF samples of
 * margin before index 0 and after w + tau): x[j + tau] comes from a Hann-windowed sinc interpolation. Evaluating
 * the dip where it really is matters because the difference at the nearest whole lag of a period that falls
 * between samples can be large for a bright voice (0.15 instead of 0 at F0 = 250 Hz, 11025 Hz). */
static double frac_difference(const double *buf, int w, double tau)
{
	int ti = (int)floor(tau);
	double fr = tau - ti, h[2 * INTERP_HALF];
	for (int m = 0; m < 2 * INTERP_HALF; m++) {
		double u = m - (INTERP_HALF - 1) - fr; /* tap offset from the fractional position */
		double sinc = fabs(u) < 1e-12 ? 1 : sin(PI * u) / (PI * u);
		h[m] = sinc * (0.5 + 0.5 * cos(PI * u / INTERP_HALF));
	}
	double d = 0;
	for (int j = 0; j < w; j++) {
		const double *p = buf + j + ti - (INTERP_HALF - 1);
		double v = 0;
		for (int m = 0; m < 2 * INTERP_HALF; m++)
			v += h[m] * p[m];
		double e = buf[j] - v;
		d += e * e;
	}
	return d;
}

void pitch_yin_frame(const double *x, long n, int sr, long centre, double fmin, double fmax, pitch_frame *out)
{
	memset(out, 0, sizeof *out);
	out->ap = 1;
	int span = (int)lround(PITCH_WINDOW_MS * sr / 1000);
	int tmin = (int)floor(sr / fmax), tmax = (int)ceil(sr / fmin);
	if (tmin < 2)
		tmin = 2;
	if (tmax + 2 >= MAX_LAG)
		tmax = MAX_LAG - 3;
	if (tmax <= tmin)
		return;
	/* fixed integration window of one longest period (every lag compares the same w samples), so a gliding F0
	 * drifts little within it; shortened if the span cannot hold it and the longest lag */
	int w = tmax + 1;
	if (w > span - (tmax + 1))
		w = span - (tmax + 1);
	if (w < tmin)
		w = tmin;
	long start = centre - (w + tmax + 1) / 2; /* the samples compared straddle the centre */
	/* the samples used, zero beyond the ends, with INTERP_HALF samples of margin on either side */
	static const int M = INTERP_HALF;
	double store[MAX_LAG * 2 + 2 * INTERP_HALF + 2], *buf = store + M;
	int total = w + tmax + 2;
	for (int i = -M; i < total + M; i++) {
		long s = start + i;
		buf[i] = s >= 0 && s < n ? x[s] : 0;
	}
	/* difference function and its cumulative mean normalization, lags 1..tmax+1 */
	double cm[MAX_LAG], mean[MAX_LAG], run = 0;
	cm[0] = mean[0] = 1;
	for (int tau = 1; tau <= tmax + 1; tau++) {
		double d = 0;
		for (int j = 0; j < w; j++) {
			double e = buf[j] - buf[j + tau];
			d += e * e;
		}
		run += d;
		mean[tau] = run / tau;
		cm[tau] = run > 0 ? d / mean[tau] : 1;
	}
	/* local minima in tmin..tmax */
	int lag[MAX_LAG / 2], nmin = 0;
	for (int tau = tmin; tau <= tmax; tau++)
		if (cm[tau] < 1 && cm[tau] <= cm[tau - 1] && cm[tau] < cm[tau + 1])
			lag[nmin++] = tau;
	if (nmin == 0)
		return;
	/* the REFINE_MAX deepest, in lag order */
	while (nmin > REFINE_MAX) {
		int worst = 0;
		for (int i = 1; i < nmin; i++)
			if (cm[lag[i]] > cm[lag[worst]])
				worst = i;
		memmove(lag + worst, lag + worst + 1, sizeof(int) * (nmin - worst - 1));
		nmin--;
	}
	/* each at its parabolically interpolated lag, with the difference evaluated there */
	double tf[REFINE_MAX], dv[REFINE_MAX];
	for (int i = 0; i < nmin; i++) {
		int t = lag[i];
		double a = cm[t - 1], b = cm[t], c = cm[t + 1], den = a - 2 * b + c;
		double shift = den > 0 ? 0.5 * (a - c) / den : 0;
		if (shift > 0.5)
			shift = 0.5;
		if (shift < -0.5)
			shift = -0.5;
		tf[i] = t + shift;
		double m = shift >= 0 ? mean[t] + shift * (mean[t + 1] - mean[t]) : mean[t] + shift * (mean[t] - mean[t - 1]);
		dv[i] = m > 0 ? frac_difference(buf, w, tf[i]) / m : 1;
		if (dv[i] > b)
			dv[i] = b; /* never worse than the whole lag */
	}
	/* YIN's choice: the first dip below the threshold, else the deepest */
	int choice = -1;
	for (int i = 0; i < nmin && choice < 0; i++)
		if (dv[i] < YIN_THRESHOLD)
			choice = i;
	out->voiced = choice >= 0;
	if (choice < 0) {
		choice = 0;
		for (int i = 1; i < nmin; i++)
			if (dv[i] < dv[choice])
				choice = i;
	}
	/* keep the PITCH_NCAND deepest, making sure YIN's choice is among them */
	int order[REFINE_MAX];
	for (int i = 0; i < nmin; i++) {
		int j = i;
		while (j > 0 && dv[order[j - 1]] > dv[i]) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	int nc = nmin < PITCH_NCAND ? nmin : PITCH_NCAND, has_choice = 0;
	for (int i = 0; i < nc; i++)
		has_choice |= order[i] == choice;
	if (!has_choice)
		order[nc - 1] = choice;
	for (int i = 0; i < nc; i++) {
		out->f0[i] = sr / tf[order[i]];
		out->d[i] = dv[order[i]];
		if (order[i] == choice) {
			out->yin_f0 = out->f0[i];
			out->ap = out->d[i] < 1 ? out->d[i] : 1;
		}
	}
	out->ncand = nc;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Viterbi over frames a..b-1 (all voiced): writes the chosen candidate's F0 into f0. */
static int best_path(const pitch_frame *pf, int a, int b, double fmax, double *f0)
{
	int len = b - a;
	double *cost = malloc(sizeof(double) * len * PITCH_NCAND);
	int *back = malloc(sizeof(int) * len * PITCH_NCAND);
	if (!cost || !back) {
		free(cost);
		free(back);
		return -1;
	}
	for (int t = 0; t < len; t++) {
		const pitch_frame *p = &pf[a + t];
		for (int i = 0; i < p->ncand; i++) {
			double local = p->d[i] + PITCH_LAG_COST * log2(fmax / p->f0[i]);
			double best = 0;
			int bi = 0;
			if (t > 0) {
				const pitch_frame *q = &pf[a + t - 1];
				best = 1e300;
				for (int j = 0; j < q->ncand; j++) {
					double c = cost[(t - 1) * PITCH_NCAND + j] + PITCH_JUMP_COST * fabs(log2(p->f0[i] / q->f0[j]));
					if (c < best) {
						best = c;
						bi = j;
					}
				}
			}
			cost[t * PITCH_NCAND + i] = best + local;
			back[t * PITCH_NCAND + i] = bi;
		}
	}
	int s = 0;
	const pitch_frame *last = &pf[b - 1];
	for (int i = 1; i < last->ncand; i++)
		if (cost[(len - 1) * PITCH_NCAND + i] < cost[(len - 1) * PITCH_NCAND + s])
			s = i;
	for (int t = len - 1; t >= 0; t--) {
		f0[a + t] = pf[a + t].f0[s];
		s = back[t * PITCH_NCAND + s];
	}
	free(cost);
	free(back);
	return 0;
}

int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
	double *f0, double *ap, unsigned char *voiced)
{
	pitch_frame *pf = malloc(sizeof(pitch_frame) * (nframes > 0 ? nframes : 1));
	double *raw = malloc(sizeof(double) * (nframes > 0 ? nframes : 1));
	if (!pf || !raw) {
		free(pf);
		free(raw);
		return -1;
	}
	double top = 0;
	int span = (int)lround(PITCH_GATE_MS * sr / 1000);
	for (int k = 0; k < nframes; k++) {
		double e = 0;
		for (long i = centres[k] - span / 2; i < centres[k] - span / 2 + span; i++)
			if (i >= 0 && i < n)
				e += x[i] * x[i];
		raw[k] = e;
		if (e > top)
			top = e;
	}
	for (int k = 0; k < nframes; k++) {
		pitch_yin_frame(x, n, sr, centres[k], fmin, fmax, &pf[k]);
		if (!(raw[k] > top * pow(10, -PITCH_FLOOR_DB / 10))) {
			pf[k].voiced = 0;
			pf[k].ap = 1;
		}
		ap[k] = pf[k].ap;
		voiced[k] = (unsigned char)pf[k].voiced;
		f0[k] = raw[k] = 0;
	}
	/* hysteresis, both ways */
	for (int k = 1; k < nframes; k++)
		if (!voiced[k] && voiced[k - 1] && pf[k].ncand > 0 && ap[k] < PITCH_HOLD)
			voiced[k] = 1;
	for (int k = nframes - 2; k >= 0; k--)
		if (!voiced[k] && voiced[k + 1] && pf[k].ncand > 0 && ap[k] < PITCH_HOLD)
			voiced[k] = 1;
	int rc = 0;
	for (int a = 0; a < nframes && rc == 0;) {
		if (!voiced[a]) {
			a++;
			continue;
		}
		int b = a;
		while (b < nframes && voiced[b])
			b++;
		rc = best_path(pf, a, b, fmax, raw);
		/* median smoothing inside the run (the window shrinks at its ends) */
		for (int k = a; k < b && rc == 0; k++) {
			double v[PITCH_MEDIAN];
			int m = 0;
			for (int j = k - PITCH_MEDIAN / 2; j <= k + PITCH_MEDIAN / 2; j++)
				if (j >= a && j < b)
					v[m++] = raw[j];
			qsort(v, m, sizeof(double), cmp_double);
			f0[k] = m & 1 ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]);
		}
		a = b;
	}
	free(pf);
	free(raw);
	return rc;
}
