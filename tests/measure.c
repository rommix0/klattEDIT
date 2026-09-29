#include <math.h>
#include <stdlib.h>
#include "klatt.h"
#include "measure.h"

#define PI 3.14159265358979323846

void t_default_globals(double *g)
{
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
}

void t_default_frame(double *f)
{
	for (int i = 0; i < KLATT_NUM_FRAME; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		f[i] = pi.def;
	}
}

int t_synth_steady(const double *globals, const double *frame, int nframes, short *out, int max)
{
	klatt_h h;
	int r = KlattCreate(globals, &h);
	if (r < 0)
		return r;
	int total = 0;
	for (int k = 0; k < nframes; k++) {
		r = KlattSynthFrame(h, frame, out + total, max - total);
		if (r < 0) {
			KlattDestroy(h);
			return r;
		}
		total += r;
	}
	KlattDestroy(h);
	return total;
}

double m_rms(const short *x, int n)
{
	double s = 0;
	for (int i = 0; i < n; i++)
		s += (double)x[i] * x[i];
	return n ? sqrt(s / n) : 0;
}

double m_peak(const short *x, int n)
{
	double p = 0;
	for (int i = 0; i < n; i++)
		if (fabs((double)x[i]) > p)
			p = fabs((double)x[i]);
	return p;
}

double m_f0(const short *x, int n, double sr, double fmin, double fmax)
{
	int lo = (int)(sr / fmax), hi = (int)(sr / fmin) + 1;
	double best = -2, r[3] = { 0 };
	int bl = lo;
	for (int lag = lo; lag <= hi + 1; lag++) {
		double xy = 0, xx = 0, yy = 0;
		for (int i = 0; i + lag < n; i++) {
			xy += (double)x[i] * x[i + lag];
			xx += (double)x[i] * x[i];
			yy += (double)x[i + lag] * x[i + lag];
		}
		double c = xy / sqrt(xx * yy + 1e-12);
		if (lag <= hi && c > best) { best = c; bl = lag; }
	}
	/* parabolic refinement around the best lag */
	for (int k = -1; k <= 1; k++) {
		double xy = 0, xx = 0, yy = 0;
		int lag = bl + k;
		for (int i = 0; i + lag < n; i++) {
			xy += (double)x[i] * x[i + lag];
			xx += (double)x[i] * x[i];
			yy += (double)x[i + lag] * x[i + lag];
		}
		r[k + 1] = xy / sqrt(xx * yy + 1e-12);
	}
	double d = r[0] - 2 * r[1] + r[2];
	double lag = bl + (d != 0 ? 0.5 * (r[0] - r[2]) / d : 0);
	return sr / lag;
}

int m_formants(const short *x, int n, double sr, double *f, int maxf)
{
	enum { N = 1024, P = 12 };
	if (n < N + 2)
		return 0;
	int start = (n - N) / 2 + 1; /* leaves x[start - 1] for the pre-emphasis */
	double w[N], r[P + 1], a[P + 1] = { 1 }, tmp[P + 1];
	for (int i = 0; i < N; i++) {
		double pre = x[start + i] - 0.9 * x[start + i - 1];
		w[i] = pre * (0.54 - 0.46 * cos(2 * PI * i / (N - 1)));
	}
	for (int k = 0; k <= P; k++) {
		r[k] = 0;
		for (int i = k; i < N; i++)
			r[k] += w[i] * w[i - k];
	}
	double e = r[0];
	for (int i = 1; i <= P; i++) { /* Levinson-Durbin */
		double acc = r[i];
		for (int j = 1; j < i; j++)
			acc += a[j] * r[i - j];
		double k = -acc / e;
		for (int j = 0; j <= i; j++)
			tmp[j] = a[j];
		for (int j = 1; j < i; j++)
			a[j] = tmp[j] + k * tmp[i - j];
		a[i] = k;
		e *= 1 - k * k;
	}
	int count = 0;
	double prev2 = 0, prev1 = 0;
	for (double fr = 50; fr < sr / 2 - 50 && count < maxf; fr += 2) {
		double re = 0, im = 0, wv = 2 * PI * fr / sr;
		for (int j = 0; j <= P; j++) {
			re += a[j] * cos(wv * j);
			im -= a[j] * sin(wv * j);
		}
		double env = 1 / sqrt(re * re + im * im);
		if (fr > 54 && prev1 > prev2 && prev1 > env)
			f[count++] = fr - 2;
		prev2 = prev1;
		prev1 = env;
	}
	return count;
}

double m_power_at(const short *x, int n, double sr, double f)
{
	double w = 2 * PI * f / sr, re = 0, im = 0;
	for (int i = 0; i < n; i++) {
		re += x[i] * cos(w * i);
		im -= x[i] * sin(w * i);
	}
	return 2 * (re * re + im * im) / ((double)n * n);
}

double m_band_fraction(const short *x, int n, double sr, double lo, double hi)
{
	int N = n < 2048 ? n : 2048;
	double in = 0, total = 0;
	for (int k = 1; k < N / 2; k++) {
		double re = 0, im = 0, w = 2 * PI * k / N;
		for (int i = 0; i < N; i++) {
			double h = 0.5 - 0.5 * cos(2 * PI * i / (N - 1));
			re += h * x[i] * cos(w * i);
			im -= h * x[i] * sin(w * i);
		}
		double p = re * re + im * im, fr = k * sr / N;
		total += p;
		if (fr >= lo && fr <= hi)
			in += p;
	}
	return total > 0 ? in / total : 0;
}
