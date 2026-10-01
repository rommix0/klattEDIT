/* wavimport.h - File > Import WAV: analyses a recording into a Klatt parameter document (F0, voicing, F1-F4,
 * B1-B3, Rd, AV, AH, AF and the parallel amplitudes). No Win32 code. */
#ifndef KPE_WAVIMPORT_H
#define KPE_WAVIMPORT_H

#include <stddef.h>
#include <wchar.h>
#include "model.h"

enum { VOICE_MALE, VOICE_FEMALE, VOICE_CHILD, VOICE_COUNT };

typedef struct {
	int voice;     /* VOICE_* */
	int sr;        /* target sample rate: the document's SR */
	int simplify;  /* run Optimize on every track afterwards (default smoothing, every frame grid, default tolerances) */
} import_options;

/* The voice types' F0 search ranges and reference F1-F4 (spec 4.4). */
typedef struct {
	const char *name;
	double f0_min, f0_max;
	double ref[4];
} voice_info;
const voice_info *import_voice(int voice);

/* Frame classes. */
enum { CLS_SILENCE, CLS_VOICED, CLS_VOICED_FRIC, CLS_FRIC, CLS_ASPIRATION, CLS_COUNT };

/* Per-frame analysis results the tests and the diagnostics look at (all arrays nframes long). */
typedef struct {
	int nframes;
	unsigned char *cls;     /* CLS_* */
	double *ap;             /* YIN aperiodicity */
	double *hf;             /* fraction of the energy above the fricative cutoff */
	double *level;          /* frame level, dB (arbitrary reference) */
	double *top;            /* import_levels.top of the frame */
	int rd_frames;          /* frames whose Rd was fitted by analysis-by-synthesis */
	double seconds[4];      /* time spent: analysis, Rd, calibration, total */
} import_info;
void import_info_free(import_info *info);

/* Levels of the frame centred on sample centre of x (n samples at sr), in dB with a fixed reference, measured
 * the same way on a recording and on a synthesis: a 25 ms Hann window, no pre-emphasis. f[0..5] are F1-F6: low is
 * the band below the F1/F2 midpoint, band[i] (i = 0..4, for A2-A6) the band from the midpoint below F(i+2) to the
 * one above it (the last one ends at sr/2), hf the fraction of the energy above min(4000 Hz, 0.35 sr), and top the
 * power density (per Hz) from 0.41 sr to sr/2 relative to the densest 500 Hz band between 1 kHz and 0.41 sr, in dB:
 * about 0 for the flat or rising spectrum of a fricative, -15 to -55 for a vowel's falling one. */
typedef struct {
	double total, low, band[5], hf, top;
} import_levels;
void import_measure(const double *x, long n, int sr, long centre, const double *f, import_levels *out);

/* The sample frame k's analysis window is centred on: the middle of the samples the synthesizer renders for it. */
long import_frame_centre(int sr, double frame_ms, int k);

/* The import's frames (5 ms) a glottal period at f0 spans, rounded up (0 for f0 <= 0): the DLL latches AV at each
 * pulse, so a frame's voicing sounds on for that long. */
int import_period_frames(double f0);

/* Analyses x (n samples at sr_in Hz, in [-1, 1)) into *d, a new document (not dirty). info may be NULL; otherwise
 * it receives the per-frame analysis (free it with import_info_free). Returns 0, or -1 with msg set (UTF-8) and
 * *d untouched: too long (over DOC_MAX_DURATION_MS), no sound (no samples, or peak below -60 dBFS), bad arguments,
 * out of memory. */
int import_samples(const double *x, long n, int sr_in, const import_options *o, kpe_doc *d, import_info *info,
	char *msg, size_t msglen);

/* Reads the WAV file (wavin.h) and imports it. Same return and msg as import_samples, plus the reader's refusals. */
int import_wav(const wchar_t *path, const import_options *o, kpe_doc *d, import_info *info, char *msg,
	size_t msglen);

#endif
