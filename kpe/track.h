/* track.h - formant tracks F1-F4 from per-frame LPC candidates by a Viterbi search over the whole signal. */
#ifndef KPE_TRACK_H
#define KPE_TRACK_H

#include "lpc.h"

#define TRACK_N 4         /* tracks: F1-F4 */
#define TRACK_MAX_CANDS 6 /* the lowest this many candidates of a frame are considered: at most 15 states (95
                          * in an unreliable frame) */
#define TRACK_UNRELIABLE 0.7 /* a frame whose reliability is below this may leave one track empty although it
                             * has candidates for all four: a /v/'s full-band LPC loses the voice bar in a few
                             * frames, and without the empty choice every state there gives F1 a candidate above
                             * 1380 Hz (the user's /v/ kept F1 above 1 kHz in 64 of its 91 frames, 6 with it). The
                             * import also runs its second analysis there. At 0.3 21 of the /v/'s frames and 10
                             * of the /zh/'s kept F1 above 1 kHz, at 0.5 6 and 5, at 0.7 and 0.9 6 and 0 */

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
 * rel[k] (NULL: 1 for every frame) is frame k's reliability in [0, 1]: it multiplies the frame's local cost (the
 * distance from the reference and the bandwidth), so that in an unreliable frame continuity decides; a frame below
 * TRACK_UNRELIABLE also gets the states that leave one track empty. conf (NULL: none), CAND_MAX per frame like
 * cands, marks candidates a second analysis confirmed: their local cost is halved. The transition and empty-track
 * costs do not depend on either.
 * Writes f[k * TRACK_N + i] (clamped to lo..hi) and bw[k * TRACK_N + i] (the chosen candidates' bandwidths,
 * interpolated the same way, median-smoothed over 7 frames and clamped to 40-500 Hz). Returns 0, or -1 when out
 * of memory. */
int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, const double *rel,
	const unsigned char *conf, int nframes, const track_setup *setup, double *f, double *bw);

#endif
