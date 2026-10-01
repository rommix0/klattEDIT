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
#define TRACK_CONFIRMED 0.5     /* a confirmed candidate's local cost is multiplied by this: without (1) the /aza/
                                 * with 6 dB stronger frication kept F2/F3 90th-percentile errors of 57/45 % in its
                                 * fricative (12/10 %), at 0.25 the same as 0.5 */
#define TRACK_BW_MEDIAN 7       /* frames in the bandwidth median */
#define TRACK_BW_MIN 40.0       /* bandwidth clamp (the B1-B3 parameter range, 40-500 Hz) */
#define TRACK_BW_MAX 500.0
#define MAX_STATES 95           /* C(TRACK_MAX_CANDS, 4) + 4 C(TRACK_MAX_CANDS, 3), an unreliable frame's */

static const double default_bw[TRACK_N] = { 50, 70, 110, 250 }; /* a track that is never found: the DLL defaults */

typedef struct {
	signed char pick[TRACK_N]; /* candidate index per track, -1 empty */
} state;

/* Every assignment of an ascending choice of n candidates to the tracks set in mask, in order. Returns the count. */
static int place(int n, int mask, state *out)
{
	int m = 0, idx[TRACK_N], count = 0;
	for (int i = 0; i < TRACK_N; i++)
		m += mask >> i & 1;
	if (m > n)
		return 0;
	for (int i = 0; i < m; i++)
		idx[i] = i;
	for (;;) {
		state s;
		for (int i = 0, j = 0; i < TRACK_N; i++)
			s.pick[i] = mask >> i & 1 ? (signed char)idx[j++] : -1;
		out[count++] = s;
		int i = m - 1; /* the next choice, in lexicographic order */
		while (i >= 0 && idx[i] == n - m + i)
			i--;
		if (i < 0)
			return count;
		idx[i]++;
		for (int t = i + 1; t < m; t++)
			idx[t] = idx[t - 1] + 1;
	}
}

/* All ordered assignments of frame candidates to the four tracks: every 4-subset when there are at least four
 * (and with skip, every 3-subset with one track left empty too), else every placement of all of them with the
 * rest empty. Returns the count. */
static int states_of(int n, int skip, state *out)
{
	int count = 0;
	for (int mask = 0; mask < 1 << TRACK_N; mask++) {
		int m = 0;
		for (int i = 0; i < TRACK_N; i++)
			m += mask >> i & 1;
		if (n >= TRACK_N ? m == TRACK_N || (skip && m == TRACK_N - 1) : m == n)
			count += place(n, mask, out + count);
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

int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, const double *rel,
	const unsigned char *conf, int nframes, const track_setup *setup, double *f, double *bw)
{
	if (nframes <= 0)
		return 0;
	/* frame k's states are st[off[k] .. off[k + 1] - 1] */
	size_t *off = malloc(sizeof(size_t) * (nframes + 1));
	int *nc = malloc(sizeof(int) * nframes);
	double *tmp = malloc(sizeof(double) * nframes);
	state buf[MAX_STATES], *st = NULL;
	double *cost = NULL;
	unsigned char *back = NULL;
	if (off && nc && tmp) {
		off[0] = 0;
		for (int k = 0; k < nframes; k++) {
			nc[k] = quiet[k] ? 0 : ncand[k] < TRACK_MAX_CANDS ? ncand[k] : TRACK_MAX_CANDS;
			off[k + 1] = off[k] + states_of(nc[k], rel && rel[k] < TRACK_UNRELIABLE, buf);
		}
		st = malloc(sizeof(state) * off[nframes]);
		cost = malloc(sizeof(double) * off[nframes]);
		back = malloc(off[nframes]);
	}
	if (!off || !nc || !tmp || !st || !cost || !back) {
		free(off);
		free(nc);
		free(tmp);
		free(st);
		free(cost);
		free(back);
		return -1;
	}
	for (int k = 0; k < nframes; k++) {
		int n = nc[k];
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		state *s = st + off[k];
		int ns = states_of(n, rel && rel[k] < TRACK_UNRELIABLE, s);
		double wt = rel ? rel[k] : 1;
		const unsigned char *ok = conf ? conf + (size_t)k * CAND_MAX : NULL;
		for (int q = 0; q < ns; q++) {
			double local = 0;
			for (int i = 0; i < TRACK_N; i++) {
				int j = s[q].pick[i];
				if (j < 0)
					local += n > 0 ? TRACK_EMPTY_COST : 0; /* a frame with nothing to offer costs nothing */
				else
					local += wt * (ok && ok[j] ? TRACK_CONFIRMED : 1) *
						(fabs(c[j].f - setup->ref[i]) / setup->ref[i] + c[j].bw / TRACK_BW_SCALE);
			}
			double best = 0;
			int bi = 0;
			if (k > 0) {
				const lpc_cand *pc = cands + (size_t)(k - 1) * CAND_MAX;
				const state *ps = st + off[k - 1];
				best = 1e300;
				for (int r = 0; r < (int)(off[k] - off[k - 1]); r++) {
					double t = cost[off[k - 1] + r];
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
			cost[off[k] + q] = best + local;
			back[off[k] + q] = (unsigned char)bi;
		}
	}
	int q = 0;
	for (int r = 1; r < (int)(off[nframes] - off[nframes - 1]); r++)
		if (cost[off[nframes - 1] + r] < cost[off[nframes - 1] + q])
			q = r;
	for (int k = nframes - 1; k >= 0; k--) {
		const state *s = st + off[k] + q;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		for (int i = 0; i < TRACK_N; i++) {
			int j = s->pick[i];
			f[k * TRACK_N + i] = j >= 0 ? c[j].f : NAN;
			bw[k * TRACK_N + i] = j >= 0 ? c[j].bw : NAN;
		}
		q = back[off[k] + q];
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
	free(off);
	free(nc);
	free(st);
	free(cost);
	free(back);
	free(tmp);
	return 0;
}
