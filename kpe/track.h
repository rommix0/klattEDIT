/* track.h - formant tracks F1-F4 from per-frame LPC candidates by a Viterbi search over the whole signal. */
#ifndef KPE_TRACK_H
#define KPE_TRACK_H

#include "lpc.h"

#define TRACK_N 4         /* tracks: F1-F4 */
#define TRACK_MAX_CANDS 6 /* the lowest this many candidates of a frame are considered: at most 15 states */

typedef struct {
	double ref[TRACK_N];      /* the voice type's reference F1-F4 */
	double lo[TRACK_N];       /* each track is clamped to lo..hi (the parameter range, and below 0.45 SR) */
	double hi[TRACK_N];
} track_setup;

/* Tracks nframes frames. cands[k * CAND_MAX + j], j < ncand[k], are frame k's candidates (ascending);
 * quiet[k] marks frames that add no cost at all (too quiet to trust: their values are interpolated from the
 * neighbouring frames, like a slot a frame has no candidate for). Each frame's states are the ordered choices of
 * up to four of its candidates for F1-F4 (a frame with fewer than four candidates leaves some tracks empty);
 * the Viterbi cost adds |f - ref| / ref and bw / 500 per track, a fixed cost per empty track, and |df| / 200 per
 * track between neighbouring frames.
 * Writes f[k * TRACK_N + i] (clamped to lo..hi) and bw[k * TRACK_N + i] (the chosen candidates' bandwidths,
 * interpolated the same way, median-smoothed over 7 frames and clamped to 40-500 Hz). Returns 0, or -1 when out
 * of memory. */
int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, int nframes,
	const track_setup *setup, double *f, double *bw);

#endif
