/* waverange.h - the range of a recording the Import WAV dialog selects on its waveform strip: pixel and sample
 * positions, snapping to whole milliseconds, and the strip's min/max envelope. No Win32 code. */
#ifndef KPE_WAVERANGE_H
#define KPE_WAVERANGE_H

#define RANGE_MIN_MS 50.0 /* a shorter range is refused */

/* The sample at pixel position px of a strip width pixels wide that shows n samples: px * n / width, rounded down
 * and clamped to 0..n (px = width is the end of the recording). */
long range_sample_at(long n, int width, double px);
/* The pixel position of sample s on that strip: s * width / n. */
double range_pixel_of(long n, int width, long s);
/* s moved to the nearest whole millisecond at sr Hz and clamped to 0..n; n itself, the end of the recording, is
 * kept although it may lie between two milliseconds. */
long range_snap(long s, int sr, long n);
/* The range between two positions given in either order, each snapped: *a <= *b. */
void range_order(long s0, long s1, int sr, long n, long *a, long *b);
/* The snapped sample at sec seconds (not a number: 0). */
long range_from_seconds(double sec, int sr, long n);
/* Whether a..b lasts at least RANGE_MIN_MS. */
int range_long_enough(long a, long b, int sr);
/* The strip's envelope: for each of its width columns, the least and the greatest of the samples of x (n of them)
 * the column covers, from range_sample_at(column) up to range_sample_at(column + 1); a column narrower than a sample
 * takes the sample under it. With no samples every column is 0. */
void range_envelope(const double *x, long n, int width, double *lo, double *hi);

#endif
