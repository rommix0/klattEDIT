/* track.c - see track.h. The costs are those the fextr probe found to work on ba.csv (F1-F3 median errors 5-12 Hz):
 * distance from the reference, bandwidth, and frame-to-frame jump; extended to F4 and to empty tracks. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "track.h"

#define TRACK_BW_SCALE 500.0    /* local cost bw / this per track: a broad candidate is a weak formant */
#define TRACK_JUMP_SCALE 200.0  /* transition cost |df| / this per track */
#define TRACK_EMPTY_COST 1.5    /* local cost per empty track: more than a typical candidate's (0.1-1), so a track
                                 * stays empty only when the frame really lacks a candidate for it */
#define TRACK_BW_MEDIAN 7       /* frames in the bandwidth median */
#define TRACK_BW_MIN 40.0       /* bandwidth clamp (the B1-B3 parameter range, 40-500 Hz) */
#define TRACK_BW_MAX 500.0
#define MAX_STATES 15           /* C(TRACK_MAX_CANDS, 4) */

static const double default_bw[TRACK_N] = { 50, 70, 110, 250 }; /* a track that is never found: the DLL defaults */

typedef struct {
	signed char pick[TRACK_N]; /* candidate index per track, -1 empty */
} state;

/* All ordered assignments of frame candidates to the four tracks: every 4-subset when there are at least four,
 * else every placement of all of them with the rest empty. Returns the count. */
static int states_of(int n, state *out)
{
	int count = 0;
	if (n >= TRACK_N) {
		for (int a = 0; a < n; a++)
			for (int b = a + 1; b < n; b++)
				for (int c = b + 1; c < n; c++)
					for (int d = c + 1; d < n; d++)
						out[count++] = (state){ { (signed char)a, (signed char)b, (signed char)c, (signed char)d } };
		return count;
	}
	/* choose which n of the four tracks are filled, in order */
	for (int mask = 0; mask < 1 << TRACK_N; mask++) {
		int bits = 0;
		for (int i = 0; i < TRACK_N; i++)
			bits += mask >> i & 1;
		if (bits != n)
			continue;
		state s;
		int j = 0;
		for (int i = 0; i < TRACK_N; i++)
			s.pick[i] = mask >> i & 1 ? (signed char)j++ : -1;
		out[count++] = s;
	}
	return count;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Fills the NAN entries of v[k * TRACK_N + i] (one track i) by linear interpolation between the nearest known
 * frames, holding the end values; fallback if the track is never known. */
static void fill_gaps(double *v, int nframes, int i, double fallback)
{
	int prev = -1;
	for (int k = 0; k <= nframes; k++) {
		if (k < nframes && isnan(v[k * TRACK_N + i]))
			continue;
		/* frames prev+1 .. k-1 are gaps */
		for (int j = prev + 1; j < k; j++) {
			if (prev < 0 && k == nframes)
				v[j * TRACK_N + i] = fallback;
			else if (prev < 0)
				v[j * TRACK_N + i] = v[k * TRACK_N + i];
			else if (k == nframes)
				v[j * TRACK_N + i] = v[prev * TRACK_N + i];
			else {
				double a = v[prev * TRACK_N + i], b = v[k * TRACK_N + i];
				v[j * TRACK_N + i] = a + (b - a) * (j - prev) / (k - prev);
			}
		}
		prev = k;
	}
}

int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, int nframes,
	const track_setup *setup, double *f, double *bw)
{
	if (nframes <= 0)
		return 0;
	int *nst = malloc(sizeof(int) * nframes);
	state *st = malloc(sizeof(state) * MAX_STATES * nframes);
	double *cost = malloc(sizeof(double) * MAX_STATES * nframes);
	unsigned char *back = malloc((size_t)MAX_STATES * nframes);
	double *tmp = malloc(sizeof(double) * nframes);
	if (!nst || !st || !cost || !back || !tmp) {
		free(nst);
		free(st);
		free(cost);
		free(back);
		free(tmp);
		return -1;
	}
	for (int k = 0; k < nframes; k++) {
		int n = quiet[k] ? 0 : ncand[k] < TRACK_MAX_CANDS ? ncand[k] : TRACK_MAX_CANDS;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		state *s = st + (size_t)k * MAX_STATES;
		nst[k] = states_of(n, s);
		for (int q = 0; q < nst[k]; q++) {
			double local = 0;
			for (int i = 0; i < TRACK_N; i++) {
				int j = s[q].pick[i];
				if (j < 0)
					local += n > 0 ? TRACK_EMPTY_COST : 0; /* a frame with nothing to offer costs nothing */
				else
					local += fabs(c[j].f - setup->ref[i]) / setup->ref[i] + c[j].bw / TRACK_BW_SCALE;
			}
			double best = 0;
			int bi = 0;
			if (k > 0) {
				const lpc_cand *pc = cands + (size_t)(k - 1) * CAND_MAX;
				const state *ps = st + (size_t)(k - 1) * MAX_STATES;
				best = 1e300;
				for (int r = 0; r < nst[k - 1]; r++) {
					double t = cost[(size_t)(k - 1) * MAX_STATES + r];
					for (int i = 0; i < TRACK_N; i++) {
						int a = s[q].pick[i], b = ps[r].pick[i];
						if (a >= 0 && b >= 0)
							t += fabs(c[a].f - pc[b].f) / TRACK_JUMP_SCALE;
					}
					if (t < best) {
						best = t;
						bi = r;
					}
				}
			}
			cost[(size_t)k * MAX_STATES + q] = best + local;
			back[(size_t)k * MAX_STATES + q] = (unsigned char)bi;
		}
	}
	int q = 0;
	for (int r = 1; r < nst[nframes - 1]; r++)
		if (cost[(size_t)(nframes - 1) * MAX_STATES + r] < cost[(size_t)(nframes - 1) * MAX_STATES + q])
			q = r;
	for (int k = nframes - 1; k >= 0; k--) {
		const state *s = st + (size_t)k * MAX_STATES + q;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		for (int i = 0; i < TRACK_N; i++) {
			int j = s->pick[i];
			f[k * TRACK_N + i] = j >= 0 ? c[j].f : NAN;
			bw[k * TRACK_N + i] = j >= 0 ? c[j].bw : NAN;
		}
		q = back[(size_t)k * MAX_STATES + q];
	}
	for (int i = 0; i < TRACK_N; i++) {
		fill_gaps(f, nframes, i, setup->ref[i]);
		fill_gaps(bw, nframes, i, default_bw[i]);
		for (int k = 0; k < nframes; k++) {
			double *v = &f[k * TRACK_N + i];
			*v = *v < setup->lo[i] ? setup->lo[i] : *v > setup->hi[i] ? setup->hi[i] : *v;
		}
		/* bandwidth: median over TRACK_BW_MEDIAN frames (shrinking at the ends), then clamped */
		for (int k = 0; k < nframes; k++) {
			double win[TRACK_BW_MEDIAN];
			int m = 0;
			for (int j = k - TRACK_BW_MEDIAN / 2; j <= k + TRACK_BW_MEDIAN / 2; j++)
				if (j >= 0 && j < nframes)
					win[m++] = bw[j * TRACK_N + i];
			qsort(win, m, sizeof(double), cmp_double);
			double v = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
			tmp[k] = v < TRACK_BW_MIN ? TRACK_BW_MIN : v > TRACK_BW_MAX ? TRACK_BW_MAX : v;
		}
		for (int k = 0; k < nframes; k++)
			bw[k * TRACK_N + i] = tmp[k];
	}
	free(nst);
	free(st);
	free(cost);
	free(back);
	free(tmp);
	return 0;
}
