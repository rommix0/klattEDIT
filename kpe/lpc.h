/* lpc.h - linear prediction by Burg's method, polynomial roots, and formant candidates from the roots. */
#ifndef KPE_LPC_H
#define KPE_LPC_H

#define LPC_MAX_ORDER 24     /* lpc_order(20000) = 22 */
#define LPC_PREEMPHASIS 0.97 /* x[i] - 0.97 x[i-1] before the window */
#define LPC_WINDOW_MS 25.0   /* Hann window length */

#define CAND_MIN_RADIUS 0.7  /* roots closer to the origin are too broad to be formants */
#define CAND_MIN_HZ 90.0     /* candidates must lie above this... */
#define CAND_MAX_FRAC 0.45   /* ...and below this fraction of the sample rate */
#define CAND_MAX_BW 1000.0   /* ...and be narrower than this */
#define CAND_MAX (LPC_MAX_ORDER / 2)

typedef struct {
	double f, bw; /* Hz */
} lpc_cand;

/* The order for a sample rate: 2 + sr/1000 rounded up to an even number (14 at 11025 Hz). */
int lpc_order(int sr);

/* The analysis window's length in samples at sr: LPC_WINDOW_MS, rounded. */
int lpc_window_len(int sr);

/* The analysis frame of len samples centred on sample centre of x (n samples): pre-emphasized by LPC_PREEMPHASIS
 * and Hann-windowed (symmetric, zero at both ends); samples beyond either end of x count as zero. */
void lpc_frame(const double *x, long n, long centre, int len, double *out);

/* Burg's method (Burg 1967, in Andersen's 1974 recursion): the prediction polynomial A(z) = 1 + a[1] z^-1 + ...
 * + a[order] z^-order (a[0] = 1) minimizing the summed forward and backward prediction error of x. Each stage's
 * reflection coefficient is k = -2 sum(f b) / sum(f^2 + b^2). A silent x (or a stage with no error left) gives
 * zeros from that stage on. Returns the final mean prediction error power, or -1 for bad arguments
 * (order < 1, order > LPC_MAX_ORDER, n <= order). */
double lpc_burg(const double *x, int n, int order, double *a);

/* All deg roots of c[0] z^deg + c[1] z^(deg-1) + ... + c[deg] (c[0] != 0, deg <= LPC_MAX_ORDER) by the
 * Aberth-Ehrlich simultaneous iteration, each root then polished by Newton's method on the full polynomial.
 * Returns deg, or -1 for bad arguments or if the iteration did not converge. */
int poly_roots(const double *c, int deg, double *re, double *im);

/* Formant candidates of A(z) (a[0..order], a[0] = 1) at sample rate sr: every root with a positive frequency and
 * radius >= CAND_MIN_RADIUS, as frequency and bandwidth, kept if CAND_MIN_HZ < f < CAND_MAX_FRAC * sr and
 * bw < CAND_MAX_BW; ascending by frequency. Returns the count (0 if the roots cannot be found). */
int lpc_candidates(const double *a, int order, double sr, lpc_cand *out);

#endif
