/* pitch.h - F0, aperiodicity and voicing per frame: YIN (de Cheveigne & Kawahara 2002) with a dynamic-programming
 * pass over the whole signal against octave jumps, then smoothing. */
#ifndef KPE_PITCH_H
#define KPE_PITCH_H

#define PITCH_WINDOW_MS 40.0 /* the span each frame's difference function looks at, centred on the frame */
#define PITCH_NCAND 4        /* candidates kept per frame */

typedef struct {
	int ncand;
	double f0[PITCH_NCAND];  /* candidate F0s (Hz), best (lowest d) first */
	double d[PITCH_NCAND];   /* their cumulative-mean-normalized difference: 0 periodic, about 1 noise */
	double yin_f0;           /* YIN's own choice: the first dip below the threshold, else the lowest dip */
	double ap;               /* aperiodicity: the normalized difference at yin_f0, at most 1 */
	int voiced;              /* a dip fell below the threshold */
} pitch_frame;

/* YIN on the PITCH_WINDOW_MS of x (n samples at sr) centred on sample centre, searching fmin..fmax. Samples
 * beyond the ends count as zero. A frame without any dip in range gets ncand 0, ap 1, not voiced. */
void pitch_yin_frame(const double *x, long n, int sr, long centre, double fmin, double fmax, pitch_frame *out);

/* The whole signal, low-passed at 1.5 kHz first: nframes frames centred on centres[k]. Fills f0[k] (Hz, smoothed;
 * 0 where unvoiced), ap[k] and voiced[k]. Frames more than 40 dB below the loudest are unvoiced; a frame next to a
 * voiced one stays voiced while its aperiodicity is below 0.45. Returns 0, or -1 when out of memory. */
int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
	double *f0, double *ap, unsigned char *voiced);

#endif
