/* measure.h - signal measurements and synthesis helpers for the tests. */
#ifndef MEASURE_H
#define MEASURE_H

/* Fills the table defaults. */
void t_default_globals(double *g);
void t_default_frame(double *f);

/* Synthesizes nframes copies of frame through a new handle (seed 1); returns the sample count or an error. */
int t_synth_steady(const double *globals, const double *frame, int nframes, short *out, int max);

double m_rms(const short *x, int n);
double m_peak(const short *x, int n);
/* F0 by normalized autocorrelation, searched between fmin and fmax. */
double m_f0(const short *x, int n, double sr, double fmin, double fmax);
/* Formant candidates: peaks of an LPC envelope (order 12, pre-emphasis 0.9) over 1024 samples from the middle
 * of x, ascending. Returns how many were found (at most maxf). */
int m_formants(const short *x, int n, double sr, double *f, int maxf);
/* Power of the component at exactly f Hz (Goertzel over all n samples), as the mean square of that sinusoid. */
double m_power_at(const short *x, int n, double sr, double f);
/* Fraction of the energy of the first 2048 samples (or n if fewer) between lo and hi Hz (naive DFT). */
double m_band_fraction(const short *x, int n, double sr, double lo, double hi);

#endif
