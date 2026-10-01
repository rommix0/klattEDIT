/* optimize.h - track smoothing and simplification for Parameters > Optimize. */
#ifndef KPE_OPTIMIZE_H
#define KPE_OPTIMIZE_H

/* Smooths one track of count values (frames frame_ms apart) in place: a 3-frame running median, then a Gaussian
 * whose full width at half maximum is width_ms (sigma = width_ms / 2.3548), truncated at +-3 sigma and
 * renormalized where it runs past the ends. With runs set (amplitude tracks), only runs of non-zero values are
 * smoothed, each on its own, so an onset stays an onset and a 0 stays 0. width_ms <= 0 leaves the track alone.
 * Returns 0, or -1 when out of memory (the track is then unchanged). */
int opt_smooth(double *v, int count, double frame_ms, double width_ms, int runs);
/* The smoothing width (ms) Optimize and the import's Simplify start from for frame parameter p: F1 10, F2 15,
 * F3 20, F4 25 (the higher formants' estimates wobble more), every other track 0 (off). */
double opt_smooth_default(int p);
/* Is frame parameter p an amplitude (dB) track, smoothed only within runs of non-zero values? */
int opt_smooth_runs(int p);

/* Simplifies one track of count values in place. Breakpoints may only sit on frames whose absolute index
 * (first_frame + i) is a multiple of grid, plus the two ends. Douglas-Peucker: keep the fewest breakpoints for
 * which linear interpolation stays within tol of the original values, then refill the frames between kept
 * breakpoints by linear interpolation. Returns the number of breakpoints kept; *max_err (may be NULL) receives
 * the largest absolute change made. */
int opt_simplify(double *v, int count, int first_frame, int grid, double tol, double *max_err);

/* The Optimize dialog's tolerance classes and their defaults: Hz (formants and bandwidths) 20, F0 2 Hz, dB 1,
 * Rd 0.05, % (DI) 2. */
enum { OPT_TOL_HZ, OPT_TOL_F0, OPT_TOL_DB, OPT_TOL_RD, OPT_TOL_PCT, OPT_NTOL };
extern const double opt_tol_default[OPT_NTOL];
/* The tolerance class of frame parameter p. */
int opt_tol_class(int p);

#endif
