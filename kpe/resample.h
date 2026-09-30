/* resample.h - sample-rate conversion by a Kaiser-windowed sinc, any ratio. */
#ifndef KPE_RESAMPLE_H
#define KPE_RESAMPLE_H

/* The output length for n input samples: n * sr_out / sr_in, rounded to the nearest integer. */
long resample_length(long n, int sr_in, int sr_out);

/* Resamples x (n samples at sr_in Hz) to sr_out Hz into a new malloc'd array *out of resample_length(n, sr_in,
 * sr_out) samples (the caller frees it). Output sample j sits at input time j * sr_in / sr_out. Passband flat
 * within 0.1 dB up to 0.9 x the lower of the two Nyquist frequencies, stopband at least 60 dB down from that
 * Nyquist frequency up; samples beyond the ends count as zero. Equal rates copy x exactly.
 * Returns 0, or -1 for bad arguments or when out of memory (*out is then NULL). */
int resample(const double *x, long n, int sr_in, int sr_out, double **out);

#endif
