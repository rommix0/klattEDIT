/* optimize.h - track simplification for Parameters > Optimize. */
#ifndef KPE_OPTIMIZE_H
#define KPE_OPTIMIZE_H

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
