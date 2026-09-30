/* spectro.h - spectrogram of the synthesized sound, for the formant strip. */
#ifndef KPE_SPECTRO_H
#define KPE_SPECTRO_H

#define SPECTRO_WIDE_MS 6.0    /* wideband window: resolves formants */
#define SPECTRO_NARROW_MS 30.0 /* narrowband window: resolves harmonics */

#define SPECTRO_MIN_STEP_MS 2.0   /* the narrowest column spacing, used up to SPECTRO_MAX_COLUMNS * this long */
#define SPECTRO_MAX_COLUMNS 16000 /* longer documents use a wider step instead, so column count stays bounded */

typedef struct {
	int ncols, nbins;   /* column c is centred at c * step_ms; bin b is at b * bin_hz */
	double step_ms, bin_hz;
	float *db;          /* power in dB, ncols * nbins, column by column: db[c * nbins + b] */
} kpe_spectro;

/* Pre-emphasis 0.94, Hann window of window_ms centred on each column (zeros beyond the ends), FFT of the
 * next power of two >= max(256, 2 * window). Returns 0, or -1 when out of memory or for bad arguments. */
int spectro_compute(const short *pcm, int n, double sr, double window_ms, double step_ms, kpe_spectro *out);
void spectro_free(kpe_spectro *s);

/* In-place iterative radix-2 FFT; n is a power of two. tw_re/tw_im[j] hold cos/sin(-2*pi*j/n) for j in
 * [0, n/2), precomputed once by the caller instead of recomputed in every butterfly. */
void spectro_fft(double *re, double *im, int n, const double *tw_re, const double *tw_im);

#define SPECTRO_FLOOR_DB 20.0 /* below the power of a 1-LSB signal: cells under it are silence */

/* Darkness per cell: 255 at the loudest cell, falling linearly to 0 at range_db below it. Cells below
 * SPECTRO_FLOOR_DB are 0, so silence stays white. range_db is clamped to a minimum of 1 dB, so a caller
 * passing 0 or a negative range cannot divide by zero. */
void spectro_levels(const kpe_spectro *s, double range_db, unsigned char *out);

#endif
