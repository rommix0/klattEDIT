/* wavimport.h - File > Import WAV: analyses a recording into a Klatt parameter document (F0, voicing, F1-F4,
 * B1-B3, Rd, AV, AH, AF and the parallel amplitudes). No Win32 code. */
#ifndef KPE_WAVIMPORT_H
#define KPE_WAVIMPORT_H

#include <stddef.h>
#include <wchar.h>
#include "model.h"

enum { VOICE_MALE, VOICE_FEMALE, VOICE_CHILD, VOICE_COUNT };

/* Analysis presets (spec 3.3 of the speed/crop/smoothing design): how often Rd is fitted and how many formant
 * correction and calibration passes run. Normal is 0, so a zeroed import_options analyses as before the presets. */
enum { IMPORT_NORMAL, IMPORT_FAST, IMPORT_BEST, IMPORT_QUALITY_COUNT };

typedef struct {
	int voice;     /* VOICE_* */
	int sr;        /* target sample rate: the document's SR */
	int simplify;  /* run Optimize on every track afterwards (default smoothing, every frame grid, default tolerances) */
	int quality;   /* IMPORT_FAST, IMPORT_NORMAL or IMPORT_BEST */
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

/* The spike filter (Hampel's) on frame parameter p of frames (nframes x NP), within each run of one class of cls
 * (CLS_*): a value further than max(3 x 1.4826 x MAD, floor) from the median of the five frames centred on it (that
 * run's frames only) is replaced by the median, rounded to the parameter's step; the floor is 3 % of the median for
 * a formant or bandwidth, 3 dB for an amplitude. An amplitude of 0 (off) is neither changed nor counted; a window
 * with fewer than 3 values, a run shorter than 3 frames and a run's first and last frame are left alone. The values
 * are read before any is replaced. Sets changed[k] (when not NULL) for every frame changed. Returns 0, or -1 when out
 * of memory. */
int import_hampel(double *frames, int nframes, const unsigned char *cls, int p, unsigned char *changed);

/* Which voiced frames get aspiration (on[k] = 1): within each run of CLS_VOICED frames AH switches on when the
 * smoothed aperiodicity aps reaches 0.25 and off only when it falls below 0.20, and an on-run shorter than 3 frames
 * is turned off, so a vowel's aspiration does not flicker on for a frame or two. */
void import_aspiration(const double *aps, const unsigned char *cls, int nframes, unsigned char *on);

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
