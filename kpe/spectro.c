/* spectro.c - see spectro.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "spectro.h"

#define PI 3.14159265358979323846

void spectro_fft(double *re, double *im, int n, const double *tw_re, const double *tw_im)
{
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			double t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		int step = n / len; /* twiddle index k*step corresponds to angle -2*PI*k/len */
		for (int i = 0; i < n; i += len)
			for (int k = 0; k < len / 2; k++) {
				double wr = tw_re[k * step], wi = tw_im[k * step];
				double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
				double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
				re[i + k + len / 2] = re[i + k] - xr;
				im[i + k + len / 2] = im[i + k] - xi;
				re[i + k] += xr;
				im[i + k] += xi;
			}
	}
}

int spectro_compute(const short *pcm, int n, double sr, double window_ms, double step_ms, kpe_spectro *out)
{
	memset(out, 0, sizeof *out);
	if (n < 0 || sr <= 0 || window_ms <= 0 || step_ms <= 0)
		return -1;
	int win = (int)lround(window_ms * sr / 1000);
	if (win < 2)
		win = 2;
	int nfft = 256;
	while (nfft < 2 * win)
		nfft <<= 1;
	double dur_ms = n * 1000.0 / sr;
	int ncols = (int)floor(dur_ms / step_ms) + 1, nbins = nfft / 2 + 1;
	float *db = malloc(sizeof(float) * (size_t)ncols * nbins);
	double *re = malloc(sizeof(double) * nfft), *im = malloc(sizeof(double) * nfft);
	double *tw_re = malloc(sizeof(double) * (nfft / 2)), *tw_im = malloc(sizeof(double) * (nfft / 2));
	if (!db || !re || !im || !tw_re || !tw_im) {
		free(db);
		free(re);
		free(im);
		free(tw_re);
		free(tw_im);
		return -1;
	}
	for (int j = 0; j < nfft / 2; j++) {
		double ang = -2 * PI * j / nfft;
		tw_re[j] = cos(ang);
		tw_im[j] = sin(ang);
	}
	for (int c = 0; c < ncols; c++) {
		int centre = (int)lround(c * step_ms * sr / 1000), start = centre - win / 2;
		memset(im, 0, sizeof(double) * nfft);
		memset(re, 0, sizeof(double) * nfft);
		for (int i = 0; i < win; i++) {
			int s = start + i;
			double x = s >= 0 && s < n ? pcm[s] : 0;
			double prev = s - 1 >= 0 && s - 1 < n ? pcm[s - 1] : 0;
			re[i] = (x - 0.94 * prev) * (0.5 - 0.5 * cos(2 * PI * i / (win - 1)));
		}
		spectro_fft(re, im, nfft, tw_re, tw_im);
		for (int b = 0; b < nbins; b++)
			db[(size_t)c * nbins + b] = (float)(10 * log10(re[b] * re[b] + im[b] * im[b] + 1e-12));
	}
	free(re);
	free(im);
	free(tw_re);
	free(tw_im);
	out->ncols = ncols;
	out->nbins = nbins;
	out->step_ms = step_ms;
	out->bin_hz = sr / nfft;
	out->db = db;
	return 0;
}

void spectro_free(kpe_spectro *s)
{
	free(s->db);
	memset(s, 0, sizeof *s);
}

void spectro_levels(const kpe_spectro *s, double range_db, unsigned char *out)
{
	if (range_db < 1) /* avoid dividing by zero or a negative range: see spectro.h */
		range_db = 1;
	size_t total = (size_t)s->ncols * s->nbins;
	float top = -1e30f;
	for (size_t i = 0; i < total; i++)
		if (s->db[i] > top)
			top = s->db[i];
	for (size_t i = 0; i < total; i++) {
		double v = s->db[i] < SPECTRO_FLOOR_DB ? 0 : (s->db[i] - (top - range_db)) / range_db;
		out[i] = (unsigned char)lround(255 * (v < 0 ? 0 : v > 1 ? 1 : v));
	}
}
