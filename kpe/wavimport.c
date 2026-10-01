/* wavimport.c - see wavimport.h. The pipeline of spec 4.3: resample, per-frame pitch (pitch.c), LPC candidates
 * (lpc.c) and formant tracks (track.c), frame classes, start levels, the formants of a high voice corrected by
 * analysis-by-synthesis, Rd by analysis-by-synthesis through the DLL, levels calibrated against the synthesizer
 * (the frication fitted with a model of the parallel branch), and the result document. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wavimport.h"
#include "wavin.h"
#include "resample.h"
#include "lpc.h"
#include "pitch.h"
#include "track.h"
#include "spectro.h"
#include "synthrun.h"
#include "optimize.h"

#define PI 3.14159265358979323846

#define IMPORT_FRAME_MS 5.0   /* the result document's FRAME_MS (spec 4.3) */
#define NO_SOUND_DBFS -60.0   /* a recording whose peak is below this is refused as silent (spec 4.2) */
#define SILENCE_DB 50.0       /* frames this far below the loudest are silence (spec 4.3 step 6) */
#define QUIET_DB 40.0         /* frames this far below the loudest add no cost to the formant tracks (step 3) */
#define HF_CUTOFF_HZ 4000.0   /* the fricative energy lies above min(this, HF_CUTOFF_FRAC * SR) (step 6) */
#define HF_CUTOFF_FRAC 0.35
#define FRIC_HF 0.25          /* an unvoiced frame is a fricative from this high-frequency fraction on, else
                               * aspiration: on the test corpus aspiration reads up to 0.06, /s/ 0.76 and up */
#define VFRIC_HF 0.15         /* a voiced frame is a voiced fricative from this fraction on: vowels read up to 0.09
                               * (a female /i/, F3 3310 Hz), /z/ 0.22 and up (both on frames 10 ms from a boundary) */
#define TOP_VFRIC_DB -12.0    /* ...or its upper band is this flat (import_levels.top). At 11025 Hz the fraction above
                               * 3859 Hz misses real voiced fricatives (the user's /z/ 0.035-0.09, /zh/ 0.009-0.027,
                               * /v/ 0.003-0.010 against vowels up to 0.03); the upper band's flatness separates them:
                               * /v/ -9..-4, /z/ -5..2, /zh/ -12..-5 dB (10th-90th percentile), the vowels' middle
                               * parts -40..-11 (90th percentile -11 to -30), the synthetic vowels -19 at most */
#define TOP_FRIC_DB -25.0     /* an unvoiced frame is a fricative by a flat upper band from here on... */
#define LOW_FRIC_DB -8.0      /* ...if at most this share (dB) of its energy lies below the F1/F2 midpoint: the user's
                               * /sh/ reads -18..-10 dB flatness and -6..-15 dB low share, the breathy onsets and
                               * offsets of the vowels -29..-12 and -8..0, synthetic aspiration -46..-32 */
#define CLASS_SMOOTH 5        /* the fricative decision is the majority of the non-silent frames within this many... */
#define FRIC_MIN_FRAMES 8     /* ...and a fricative run shorter than this (40 ms) is none: the user's /ih/ has a 25 ms
                               * noisy patch with a flat upper band mid-vowel */
#define TOP_FROM_FRAC 0.41    /* import_levels.top: the upper band from this fraction of SR to SR/2... */
#define TOP_MAX_SR 11025.0    /* ...or of this SR above it (4520-5512 Hz): the band the thresholds were tuned on, at
                               * 11025 Hz; at 16000 Hz 0.41 SR to SR/2 would lie above every formant the DLL has */
#define TOP_REF_FROM_HZ 1000.0 /* ...against the densest band of TOP_REF_WIDTH_HZ from here up to it */
#define TOP_REF_WIDTH_HZ 500.0
#define TOP_REF_BANDS 40
#define LEVEL_FLOOR -200.0    /* dB given to an empty band */
#define REL_HF_LO 0.1         /* a frame's reliability for the formant tracker (reliability) falls from 1 at this
                               * energy fraction above the cutoff to REL_MIN at REL_HF_HI: vowels read up to 0.09,
                               * /s/ 0.76 and up (0.05-0.2 changed nothing on the user's recordings)... */
#define REL_HF_HI 0.3
#define REL_TOP_LO -20.0      /* ...and from 1 at this upper-band flatness to REL_MIN at REL_TOP_HI: the vowels'
                               * middle parts read -40..-11 dB, the user's /v/ -9..-4, /z/ -5..2, /zh/ -12..-5;
                               * -25..-10 left 11 of the /v/'s 91 frames with F1 above 1 kHz (6 with these) */
#define REL_TOP_HI -5.0
#define REL_MIN 0.1           /* the least reliability, so the distance from the reference still ranks a frication
                               * frame's candidates a little: at 0.05 the /aza/ with 6 dB stronger frication kept
                               * F2/F3 90th-percentile errors of 57/45 % in its fricative (12/10 % at 0.1); at 0.2 the
                               * recordings' F4 had 21 spikes (15), at 0.4 24 */
#define LOW_HZ 3000.0         /* the second analysis low-passes at min(LOW_HZ, LOW_FRAC SR), where the voicing
                               * dominates F1-F3: at 2500 Hz the /aza/'s F2 kept a 57 % 90th-percentile error */
#define LOW_FRAC 0.3
#define CONFIRM_DIST 0.1      /* a full-band candidate this close (fraction) to a low-band one is confirmed (its
                               * local cost halved, track.c): 0.05 and 0.2 moved the recordings' spikes by 3 at most,
                               * 0.2 left 12 /v/ frames with F1 above 1 kHz; without the second analysis the /aza/'s
                               * F2/F3 kept 57/45 % and the /ava/'s F3 38 % 90th-percentile errors (12/10 and 20 %) */
#define AP_ASP_ON 0.25        /* a voiced frame gets aspiration only from this (smoothed) aperiodicity on: the user's
                               * modal vowels read 0.01-0.2 in their middle halves (0.15 left 5-11 frames with AH,
                               * 0.25 none in AE and OW and 3 of 49 in IH's noisy patch); aspiration 5 dB below AV
                               * reads 0.2-0.4, 10 dB below 0.09-0.13 (and stays off) */
#define AP_SMOOTH 5           /* frames in the aperiodicity's median */
#define INPUT_PEAK_DB -1.0    /* the input is scaled to peak here (dBFS) before the analysis */
#define NORM_HEADROOM_DB 24.0 /* a normalization synthesis that overloads is followed by one this far lower */
#define NORM_PEAK_DB -1.0     /* the resynthesis peaks at most here (dBFS) after normalization */

#define REF_LEVEL 60.0        /* the amplitude the reference syntheses use for the start values */
#define CAL_PASSES 2          /* level corrections after the first pass (which synthesizes voicing and aspiration
                               * and fits the frication): three syntheses in all, as spec 4.3 step 8 has it. A
                               * third correction pass moved the fricative bands' 90th percentile by 0.1 dB */
#define CAL_HEADROOM_DB 12.0  /* the calibration's syntheses run this far below the default G0 (and are measured as at
                               * the default), so that a recording scaled to INPUT_PEAK_DB does not clip them */
#define CAL_MAX_STEP_DB 12.0  /* a calibration pass moves AV or AH by at most this: where the cascade's gain is very
                               * high (F1 and F2 close and broad) the 25 ms window's leakage between frames makes a
                               * full correction overshoot */
#define CAL_LOUD_DB 6.0       /* after the last pass a verification synthesis lowers every non-silent frame more
                               * than this louder than the WAV by the excess */
#define FRIC_WEAK_WEIGHT 0.1  /* weight of a band more than BAND_RANGE_DB below the strongest in the frication fit */
#define FRIC_SWEEPS 6         /* coordinate sweeps of the frication fit */
#define FRIC_STEP_DB 12.0     /* its pattern search: first step... */
#define FRIC_STEP_MIN_DB 0.25 /* ...and last */
#define PAR_TOP 70.0          /* the largest parallel amplitude of a fricative frame (AF takes the rest, but at least
                               * one step: the DLL treats AF 0 as off, and a quiet recording's frication, whose
                               * strongest AF + An total is below PAR_TOP, would be switched off and could not be
                               * refitted), clear of the 80 dB limit so a correction never clamps */
#define BAND_RANGE_DB 30.0    /* fricative bands more than this below the frame's strongest weigh less in the fit */
#define BYPASS_FRAC 0.45      /* the DLL bypasses a resonator at or above this fraction of SR (a parallel one gives
                               * nothing) */

#define FCORR_PASSES 3        /* formant correction passes (F1-F3 by analysis-by-synthesis): with the voicing
                               * synthesized below clipping (CAL_HEADROOM_DB) two left the female vowels' F1 at
                               * 10.3 % 90th-percentile error, three 9.4 % */
#define FCORR_MIN_F0 150.0    /* ...for a voice whose median F0 is this or higher, where LPC's bias matters: two passes
                               * took the female vowels' F1 from 5.5 % median / 13.8 % 90th-percentile error to 3.2 /
                               * 9.2 %; a male voice gains less (F1 median 1.9 -> 0.9 %) than the passes cost (about
                               * 0.6 s each per 60 s) */
#define FCORR_SEARCH 0.25     /* the synthesis's candidate must lie within this fraction of the current value */
#define FCORR_MAX_STEP 0.2    /* and a pass moves a formant by at most this fraction */

#define RD_STEP 12            /* Rd is fitted on every RD_STEP-th voiced frame (60 ms) and interpolated between: the
                               * candidate syntheses are the import's main cost (every 4th frame took 7.7 s of a
                               * 60 s import; every 12th 1.6 s, with the same accuracy on the test corpus)... */
#define RD_EDGE 3             /* ...and only on a frame whose RD_EDGE neighbours on either side are voiced too, so the
                               * 25 ms window lies in the vowel: fitting every 4th frame (Best), fits whose window
                               * reached a /z/'s frication read Rd 0.77-0.86 for 1.0, and the fricative bands' 90th
                               * percentile error rose from 2.6 to 4.0 dB (2.8 with this rule); at every 12th frame
                               * the rule moved the corpus by little (the weak /z/ at 16000 Hz: bands 4.4 -> 2.4 dB) */
#define RD_TOL 0.1           /* golden-section search stops when the Rd bracket is this narrow: the midpoint is
                               * then within 0.05 (Optimize's default Rd tolerance) */
#define RD_NEAR 0.5           /* a fit in the same voiced run as the previous one first searches this far on either
                               * side of it (the whole range again if the minimum lands on that edge): 7 syntheses
                               * instead of 9. 0.3 fell back to the whole range too often and was slower */
#define RD_MAX_ITER 20
#define RD_AV 60.0            /* candidate level: the comparison is mean-normalized, so AV only has to keep the
                               * candidate well above the 16-bit rounding */
#define RD_WARM_MS 10.0       /* the candidate is synthesized at least this long before the window... */
#define RD_WARM_PERIODS 3.0   /* ...and at least this many periods: with 1.5 the Rd sweep's 90th-percentile error
                               * grew from 0.09 to 0.38 (the source is not steady yet) */
#define RD_BAND_MAX_HZ 5000.0 /* comparison band: F0 to min(5 F0, this) (fextr's Method B) */
#define RD_MEDIAN 3           /* fitted Rd values are median-smoothed over this many fits */

/* The analysis presets (IMPORT_*): Normal is the analysis above (RD_STEP, FCORR_PASSES, CAL_PASSES); Best fits Rd
 * on every 4th voiced frame; Fast fits it on every 24th and runs one formant correction pass and one calibration
 * correction: a 60 s high voice took 6.9 s with Normal's numbers and 4.4 s with these on the 32-bit build, and the
 * female vowels' F1 90th-percentile error rose from 9.4 to 11.4 % (the import spec's known limits list it). */
typedef struct {
	int rd_step, fcorr_passes, cal_passes;
} preset;
static const preset presets[IMPORT_QUALITY_COUNT] = {
	[IMPORT_NORMAL] = { RD_STEP, FCORR_PASSES, CAL_PASSES },
	[IMPORT_FAST] = { 24, 1, 1 },
	[IMPORT_BEST] = { 4, FCORR_PASSES, CAL_PASSES },
};

static const voice_info voices[VOICE_COUNT] = {
	{ "Adult male", 60, 250, { 500, 1500, 2500, 3500 } },
	{ "Adult female", 120, 400, { 560, 1680, 2800, 3900 } },
	{ "Child", 180, 500, { 650, 1950, 3250, 4400 } },
};

const voice_info *import_voice(int voice)
{
	return voice >= 0 && voice < VOICE_COUNT ? &voices[voice] : NULL;
}

void import_info_free(import_info *info)
{
	free(info->cls);
	free(info->ap);
	free(info->hf);
	free(info->level);
	free(info->top);
	memset(info, 0, sizeof *info);
}

static int fail(char *msg, size_t msglen, const char *fmt, ...)
{
	if (msglen) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(msg, msglen, fmt, ap);
		va_end(ap);
	}
	return -1;
}

long import_frame_centre(int sr, double frame_ms, int k)
{
	return (long)floor((k + 0.5) * sr * frame_ms / 1000 + 1e-9);
}

int import_period_frames(double f0)
{
	return f0 > 0 ? (int)ceil(1000 / (f0 * IMPORT_FRAME_MS) - 1e-9) : 0;
}

static double param_min(int p)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	return pi.min;
}

static double param_max(int p)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	return pi.max;
}

static int sr_ok(int sr)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_GLOBAL, KG_SR, &pi);
	return sr >= pi.min && sr <= pi.max;
}

/* Clamped to the parameter's range and rounded to its step. */
static double settle(int p, double v)
{
	double lo = param_min(p), hi = param_max(p), s = doc_param_step(p);
	v = v < lo ? lo : v > hi ? hi : v;
	return round(v / s) * s;
}

/* ---- spectra ---- */

typedef struct {
	int sr, len, nfft;
	double *re, *im, *tw_re, *tw_im, *hann, *buf;
	double *hw_re, *hw_im;   /* twiddles of the nfft/2-point FFT the real input is packed into */
	double *cw, *sw;         /* cos and sin of w and 2w for each bin (w = 2 pi bin / nfft): resonator responses */
} spec_ctx;

static void spec_free(spec_ctx *c)
{
	free(c->re);
	free(c->im);
	free(c->tw_re);
	free(c->tw_im);
	free(c->hann);
	free(c->buf);
	free(c->hw_re);
	free(c->hw_im);
	free(c->cw);
	free(c->sw);
	memset(c, 0, sizeof *c);
}

static int spec_init(spec_ctx *c, int sr)
{
	memset(c, 0, sizeof *c);
	c->sr = sr;
	c->len = lpc_window_len(sr);
	c->nfft = 256;
	while (c->nfft < c->len)
		c->nfft <<= 1;
	c->re = malloc(sizeof(double) * c->nfft);
	c->im = malloc(sizeof(double) * c->nfft);
	c->tw_re = malloc(sizeof(double) * c->nfft / 2);
	c->tw_im = malloc(sizeof(double) * c->nfft / 2);
	c->hann = malloc(sizeof(double) * c->len);
	c->buf = malloc(sizeof(double) * c->len);
	c->hw_re = malloc(sizeof(double) * c->nfft / 4);
	c->hw_im = malloc(sizeof(double) * c->nfft / 4);
	c->cw = malloc(sizeof(double) * (c->nfft + 2));
	c->sw = malloc(sizeof(double) * (c->nfft + 2));
	if (!c->re || !c->im || !c->tw_re || !c->tw_im || !c->hann || !c->buf || !c->hw_re || !c->hw_im || !c->cw ||
	    !c->sw) {
		spec_free(c);
		return -1;
	}
	for (int j = 0; j < c->nfft / 2; j++) {
		c->tw_re[j] = cos(-2 * PI * j / c->nfft);
		c->tw_im[j] = sin(-2 * PI * j / c->nfft);
	}
	for (int b = 0; b <= c->nfft / 2; b++) {
		double w = 2 * PI * b / c->nfft;
		c->cw[2 * b] = cos(w);
		c->cw[2 * b + 1] = cos(2 * w);
		c->sw[2 * b] = sin(w);
		c->sw[2 * b + 1] = sin(2 * w);
	}
	for (int j = 0; j < c->nfft / 4; j++) {
		c->hw_re[j] = c->tw_re[2 * j];
		c->hw_im[j] = c->tw_im[2 * j];
	}
	for (int i = 0; i < c->len; i++)
		c->hann[i] = 0.5 - 0.5 * cos(2 * PI * i / (c->len - 1));
	return 0;
}

/* Power spectrum of v[0..len-1] (zero-padded to nfft) left in re[0..nfft/2]. The real input is packed into an
 * nfft/2-point complex FFT (even samples real, odd imaginary) and split afterwards: half the work. */
static void power_spectrum(spec_ctx *c, const double *v)
{
	int m = c->nfft / 2;
	double *zr = c->im, *zi = c->im + m; /* the packed transform, in the spare im buffer */
	for (int i = 0; i < m; i++) {
		zr[i] = 2 * i < c->len ? v[2 * i] : 0;
		zi[i] = 2 * i + 1 < c->len ? v[2 * i + 1] : 0;
	}
	spectro_fft(zr, zi, m, c->hw_re, c->hw_im);
	for (int k = 0; k <= m; k++) {
		int a = k % m, b = (m - k) % m;
		double er = 0.5 * (zr[a] + zr[b]), ei = 0.5 * (zi[a] - zi[b]);   /* even part: (Z[k] + conj Z[m-k]) / 2 */
		double orr = 0.5 * (zi[a] + zi[b]), oi = -0.5 * (zr[a] - zr[b]); /* odd part: (Z[k] - conj Z[m-k]) / 2i */
		double wr = k < m ? c->tw_re[k] : -1, wi = k < m ? c->tw_im[k] : 0;
		double xr = er + wr * orr - wi * oi, xi = ei + wr * oi + wi * orr;
		c->re[k] = xr * xr + xi * xi;
	}
}

static double to_db(double p)
{
	return p > 0 ? 10 * log10(p) : LEVEL_FLOOR;
}

/* The band [lo, hi) of each formant F1-F6 (f[0..5]): edges midway to the neighbouring formants in frequency order
 * (formants need not be ordered: F4 may lie above F5), the first from 0, the last to sr/2. */
static void band_edges(const double *f, double sr, double *lo, double *hi)
{
	int idx[6] = { 0, 1, 2, 3, 4, 5 };
	for (int i = 1; i < 6; i++)
		for (int j = i; j > 0 && f[idx[j - 1]] > f[idx[j]]; j--) {
			int t = idx[j];
			idx[j] = idx[j - 1];
			idx[j - 1] = t;
		}
	for (int r = 0; r < 6; r++) {
		lo[idx[r]] = r == 0 ? 0 : 0.5 * (f[idx[r - 1]] + f[idx[r]]);
		hi[idx[r]] = r == 5 ? sr / 2 + 1 : 0.5 * (f[idx[r]] + f[idx[r + 1]]);
	}
}

static void measure(spec_ctx *c, const double *x, long n, long centre, const double *f, import_levels *out)
{
	long start = centre - c->len / 2;
	for (int i = 0; i < c->len; i++) {
		long s = start + i;
		c->buf[i] = (s >= 0 && s < n ? x[s] : 0) * c->hann[i];
	}
	power_spectrum(c, c->buf);
	double lo[6], hi[6], total = 0, low = 0, high = 0, band[5] = { 0 }, upper = 0, ref[TOP_REF_BANDS] = { 0 };
	int nupper = 0, nref[TOP_REF_BANDS] = { 0 };
	double cut = HF_CUTOFF_HZ < HF_CUTOFF_FRAC * c->sr ? HF_CUTOFF_HZ : HF_CUTOFF_FRAC * c->sr;
	double top_sr = c->sr < TOP_MAX_SR ? c->sr : TOP_MAX_SR, top_lo = TOP_FROM_FRAC * top_sr, top_hi = top_sr / 2;
	band_edges(f, c->sr, lo, hi);
	double low_edge = 0.5 * (f[0] + f[1]);
	for (int b = 1; b <= c->nfft / 2; b++) {
		double fr = (double)b * c->sr / c->nfft, p = c->re[b];
		total += p;
		if (fr < low_edge)
			low += p;
		if (fr >= cut)
			high += p;
		for (int i = 0; i < 5; i++)
			if (fr >= lo[i + 1] && fr < hi[i + 1])
				band[i] += p;
		if (fr >= top_lo) {
			if (fr <= top_hi) {
				upper += p;
				nupper++;
			}
		} else if (fr >= TOP_REF_FROM_HZ) {
			int q = (int)((fr - TOP_REF_FROM_HZ) / TOP_REF_WIDTH_HZ);
			if (q < TOP_REF_BANDS) {
				ref[q] += p;
				nref[q]++;
			}
		}
	}
	double dense = 0;
	for (int q = 0; q < TOP_REF_BANDS; q++)
		if (nref[q] && ref[q] / nref[q] > dense)
			dense = ref[q] / nref[q];
	out->top = nupper && upper > 0 && dense > 0 ? 10 * log10(upper / nupper / dense) : LEVEL_FLOOR;
	out->total = to_db(total);
	out->low = to_db(low);
	for (int i = 0; i < 5; i++)
		out->band[i] = to_db(band[i]);
	out->hf = total > 0 ? high / total : 0;
}

void import_measure(const double *x, long n, int sr, long centre, const double *f, import_levels *out)
{
	spec_ctx c;
	memset(out, 0, sizeof *out);
	if (spec_init(&c, sr) != 0)
		return;
	measure(&c, x, n, centre, f, out);
	spec_free(&c);
}

/* ---- Rd by analysis-by-synthesis (step 7) ---- */

typedef struct {
	spec_ctx *spec;
	klatt_h h;
	int order, per;          /* LPC order; DLL samples per frame */
	short *pcm;              /* candidate synthesis */
	double *x;               /* the same as doubles */
	int nsyn, nframes_syn;   /* samples allocated; frames synthesized per candidate for the current frame */
	double *w, *res;         /* windowed frame, residual */
	double a[LPC_MAX_ORDER + 1];
	double frame[NP];        /* the candidate's frame parameters */
	int b0, b1;              /* comparison band bins */
	double *target;          /* the WAV frame's mean-normalized dB spectrum over the band */
	double *cand;
} rd_ctx;

/* Inverse-filters the windowed frame w with a (FIR, zero history) and leaves the band's mean-normalized dB
 * spectrum in out. */
static void residual_band_db(rd_ctx *r, const double *w, double *out)
{
	int len = r->spec->len;
	for (int i = 0; i < len; i++) {
		double e = 0;
		for (int j = 0; j <= r->order && j <= i; j++)
			e += r->a[j] * w[i - j];
		r->res[i] = e;
	}
	power_spectrum(r->spec, r->res);
	double mean = 0;
	for (int b = r->b0; b <= r->b1; b++) {
		out[b - r->b0] = 10 * log10(r->spec->re[b] + 1e-30);
		mean += out[b - r->b0];
	}
	mean /= r->b1 - r->b0 + 1;
	for (int b = r->b0; b <= r->b1; b++)
		out[b - r->b0] -= mean;
}

static double rd_error(rd_ctx *r, double rd)
{
	r->frame[KP_RD] = rd;
	KlattReset(r->h, 1);
	int n = 0;
	for (int k = 0; k < r->nframes_syn; k++) {
		int got = KlattSynthFrame(r->h, r->frame, r->pcm + n, r->per);
		if (got < 0)
			return 1e300;
		n += got;
	}
	for (int i = 0; i < n; i++)
		r->x[i] = r->pcm[i] / 32768.0;
	int len = r->spec->len;
	lpc_frame(r->x, n, n - len + len / 2, len, r->w); /* the window ends with the synthesis */
	residual_band_db(r, r->w, r->cand);
	double e = 0;
	for (int b = 0; b <= r->b1 - r->b0; b++) {
		double d = r->cand[b] - r->target[b];
		e += d * d;
	}
	return e;
}

/* Golden-section search for the Rd between lo and hi with the least rd_error. */
static double golden(rd_ctx *r, double lo, double hi)
{
	double g = (sqrt(5.0) - 1) / 2;
	double c = hi - g * (hi - lo), d = lo + g * (hi - lo), fc = rd_error(r, c), fd = rd_error(r, d);
	for (int it = 0; it < RD_MAX_ITER && hi - lo > RD_TOL; it++) {
		if (fc < fd) {
			hi = d;
			d = c;
			fd = fc;
			c = hi - g * (hi - lo);
			fc = rd_error(r, c);
		} else {
			lo = c;
			c = d;
			fc = fd;
			d = lo + g * (hi - lo);
			fd = rd_error(r, d);
		}
	}
	return 0.5 * (lo + hi);
}

/* Fits Rd for one frame: x is the (resampled) recording, fr the frame's analysed parameters, prior an earlier fit
 * nearby (0: none). Returns the Rd, or NAN if the comparison band is empty. */
static double fit_rd(rd_ctx *r, const double *x, long n, long centre, const double *fr, double prior)
{
	int len = r->spec->len;
	lpc_frame(x, n, centre, len, r->w);
	if (lpc_burg(r->w, len, r->order, r->a) <= 0)
		return NAN;
	double f0 = fr[KP_F0], top = 5 * f0 < RD_BAND_MAX_HZ ? 5 * f0 : RD_BAND_MAX_HZ;
	int sr = r->spec->sr, nfft = r->spec->nfft;
	r->b0 = (int)ceil(f0 * nfft / sr);
	r->b1 = (int)floor(top * nfft / sr);
	if (r->b1 > nfft / 2)
		r->b1 = nfft / 2;
	if (r->b1 - r->b0 < 2)
		return NAN;
	residual_band_db(r, r->w, r->target);
	for (int p = 0; p < NP; p++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		r->frame[p] = pi.def;
	}
	r->frame[KP_AV] = RD_AV;
	r->frame[KP_F0] = f0;
	for (int p = KP_F1; p <= KP_F4; p++)
		r->frame[p] = fr[p];
	for (int p = KP_B1; p <= KP_B3; p++)
		r->frame[p] = fr[p];
	/* long enough to reach a steady state before the window */
	double spf = sr * IMPORT_FRAME_MS / 1000, warm = RD_WARM_MS * sr / 1000;
	if (warm < RD_WARM_PERIODS * sr / f0)
		warm = RD_WARM_PERIODS * sr / f0;
	r->nframes_syn = (int)ceil((len + warm) / spf) + 1;
	if (r->nframes_syn * r->per > r->nsyn)
		r->nframes_syn = r->nsyn / r->per;
	double rmin = param_min(KP_RD), rmax = param_max(KP_RD);
	if (prior > 0) {
		double lo = prior - RD_NEAR < rmin ? rmin : prior - RD_NEAR, hi = prior + RD_NEAR > rmax ? rmax : prior + RD_NEAR;
		double v = golden(r, lo, hi);
		if ((v - lo > RD_TOL || lo == rmin) && (hi - v > RD_TOL || hi == rmax))
			return v;
	}
	return golden(r, rmin, rmax);
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Rd for every frame of fr (nframes x NP): fitted on every step-th CLS_VOICED frame (at least RD_EDGE frames from
 * another class), median-smoothed over RD_MEDIAN fits, interpolated between fits within a run of voiced frames, and
 * held (the last value) elsewhere. */
static int fit_rd_track(const double *x, long n, int sr, const unsigned char *cls, int nframes, double *fr,
	spec_ctx *spec, int step, int *fitted)
{
	rd_ctx r;
	memset(&r, 0, sizeof r);
	r.spec = spec;
	r.order = lpc_order(sr);
	double g[NG];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SR] = sr;
	g[KG_SRC] = 1;
	g[KG_FRAME_MS] = IMPORT_FRAME_MS;
	if (KlattCreate(g, &r.h) < 0)
		return -1;
	r.per = KlattMaxFrameSamples(r.h);
	double spf = sr * IMPORT_FRAME_MS / 1000;
	int *idx = malloc(sizeof(int) * (nframes + 1));
	double *val = malloc(sizeof(double) * (nframes + 1)), *sm = malloc(sizeof(double) * (nframes + 1));
	/* room for the longest warm-up: RD_WARM_PERIODS at the lowest F0 any voice type searches */
	double maxwarm = RD_WARM_PERIODS * sr / 60.0 > RD_WARM_MS * sr / 1000 ? RD_WARM_PERIODS * sr / 60.0
		: RD_WARM_MS * sr / 1000;
	r.nsyn = ((int)ceil((spec->len + maxwarm) / spf) + 1) * r.per;
	r.pcm = malloc(sizeof(short) * r.nsyn);
	r.x = malloc(sizeof(double) * r.nsyn);
	r.w = malloc(sizeof(double) * spec->len);
	r.res = malloc(sizeof(double) * spec->len);
	r.target = malloc(sizeof(double) * (spec->nfft / 2 + 1));
	r.cand = malloc(sizeof(double) * (spec->nfft / 2 + 1));
	int rc = -1;
	if (!idx || !val || !sm || !r.pcm || !r.x || !r.w || !r.res || !r.target || !r.cand)
		goto done;
	int nfit = 0;
	for (int k = 0; k < nframes; k++) {
		int steady = k % step == 0;
		for (int j = k - RD_EDGE; j <= k + RD_EDGE && steady; j++)
			steady = j >= 0 && j < nframes && cls[j] == CLS_VOICED;
		if (!steady)
			continue;
		double prior = 0; /* the previous fit, if no unvoiced frame lies between */
		if (nfit > 0) {
			int gap = 0;
			for (int j = idx[nfit - 1]; j <= k && !gap; j++)
				gap = cls[j] != CLS_VOICED && cls[j] != CLS_VOICED_FRIC;
			prior = gap ? 0 : val[nfit - 1];
		}
		double v = fit_rd(&r, x, n, import_frame_centre(sr, IMPORT_FRAME_MS, k), fr + (size_t)k * NP, prior);
		if (isnan(v))
			continue;
		idx[nfit] = k;
		val[nfit++] = v;
	}
	*fitted = nfit;
	/* median over neighbouring fits of the same voiced run (no voiced gap between them) */
	for (int i = 0; i < nfit; i++) {
		double win[RD_MEDIAN];
		int m = 0;
		for (int j = i - RD_MEDIAN / 2; j <= i + RD_MEDIAN / 2; j++) {
			if (j < 0 || j >= nfit)
				continue;
			int a = idx[j < i ? j : i], b = idx[j < i ? i : j], gap = 0;
			for (int k = a; k <= b && !gap; k++)
				gap = cls[k] != CLS_VOICED && cls[k] != CLS_VOICED_FRIC;
			if (!gap)
				win[m++] = val[j];
		}
		qsort(win, m, sizeof(double), cmp_double);
		sm[i] = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
	}
	/* fill: interpolate between consecutive fits when every frame between them is voiced; a voiced run's frames
	 * before its first fit take that fit; elsewhere hold */
	double last = 1.0;
	int next = 0;
	for (int k = 0; k < nframes; k++) {
		while (next < nfit && idx[next] < k)
			next++;
		double v;
		if (next < nfit && idx[next] == k) {
			v = sm[next];
		} else if (next > 0 && next < nfit) {
			int a = idx[next - 1], b = idx[next], gap = 0, gap_after = 0;
			for (int j = a; j <= b && !gap; j++)
				gap = cls[j] != CLS_VOICED && cls[j] != CLS_VOICED_FRIC;
			for (int j = k; j <= b && !gap_after; j++) /* k in the next fit's run, before that fit: its value */
				gap_after = cls[j] != CLS_VOICED && cls[j] != CLS_VOICED_FRIC;
			v = !gap ? sm[next - 1] + (sm[next] - sm[next - 1]) * (k - a) / (b - a) : gap_after ? last : sm[next];
		} else if (next == 0 && nfit > 0) {
			v = sm[0]; /* before the first fit: its value */
		} else {
			v = last;
		}
		fr[(size_t)k * NP + KP_RD] = settle(KP_RD, v);
		last = v;
	}
	rc = 0;
done:
	KlattDestroy(r.h);
	free(idx);
	free(val);
	free(sm);
	free(r.pcm);
	free(r.x);
	free(r.w);
	free(r.res);
	free(r.target);
	free(r.cand);
	return rc;
}

/* ---- levels (step 8) ---- */

/* The six formant frequencies F1-F6 the bands of frame fr are centred on. */
static void formants_of(const double *fr, const double *g, double *f)
{
	f[0] = fr[KP_F1];
	f[1] = fr[KP_F2];
	f[2] = fr[KP_F3];
	f[3] = fr[KP_F4];
	f[4] = g[KG_F5];
	f[5] = g[KG_F6];
}

/* Levels of a steady synthesis of frame fr (60 frames, measured in the middle) with globals g. */
static int reference_levels(const double *g, const double *fr, spec_ctx *spec, import_levels *out)
{
	kpe_doc d;
	double *frames = malloc(sizeof(double) * NP * 60);
	if (!frames)
		return -1;
	for (int k = 0; k < 60; k++)
		memcpy(frames + (size_t)k * NP, fr, sizeof(double) * NP);
	doc_init_from(&d, g, 60, frames);
	synth_result s;
	int rc = synth_doc(&d, &s);
	if (rc == 0) {
		double *x = malloc(sizeof(double) * s.n), f[6];
		if (x) {
			for (int i = 0; i < s.n; i++)
				x[i] = s.pcm[i] / 32768.0;
			formants_of(fr, g, f);
			measure(spec, x, s.n, s.n / 2, f, out);
			free(x);
		} else {
			rc = -1;
		}
		synth_result_free(&s);
	}
	doc_free(&d);
	return rc;
}

/* The peak of synthesis s in dBFS (-1e300 when it is silent). */
static double peak_db(const synth_result *s)
{
	int peak = 0;
	for (int i = 0; i < s->n; i++)
		if (abs(s->pcm[i]) > peak)
			peak = abs(s->pcm[i]);
	return peak > 0 ? 20 * log10(peak / 32768.0) : -1e300;
}

/* Measures every frame of the synthesis of d, its samples scaled by gain, into lv; the synthesis's peak (dBFS) and
 * overload count go to *pdb and *over. */
static int measure_synth(const kpe_doc *d, spec_ctx *spec, double gain, import_levels *lv, double *pdb, int *over)
{
	synth_result s;
	if (synth_doc(d, &s) != 0)
		return -1;
	double *x = malloc(sizeof(double) * (s.n > 0 ? s.n : 1));
	if (!x) {
		synth_result_free(&s);
		return -1;
	}
	*pdb = peak_db(&s);
	*over = s.overload;
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] * gain / 32768.0;
	for (int k = 0; k < d->nframes; k++) {
		double f[6];
		formants_of(d->frames + (size_t)k * NP, d->globals, f);
		measure(spec, x, s.n, import_frame_centre(s.sr, IMPORT_FRAME_MS, k), f, &lv[k]);
	}
	free(x);
	synth_result_free(&s);
	return 0;
}

/* AH in a voiced frame of amplitude av: off (0) below the aperiodicity AP_ASP_ON, else av + 10 log10(ap / (1 - ap))
 * (spec 4.3 step 8), ap being YIN's aperiodicity median-smoothed over AP_SMOOTH frames. Checked against the
 * synthesizer: vowels synthesized with AH 15, 10 and 5 dB below AV import with AH 15, 10 and 5 dB below (Rd 1) and
 * 13, 8 and 3 (Rd 2.2) when the threshold is off, so the formula needs no correction factor. */
static double voiced_ah(double av, double ap)
{
	if (ap < AP_ASP_ON)
		return 0;
	ap = ap > 1 - 1e-4 ? 1 - 1e-4 : ap;
	return settle(KP_AH, av + 10 * log10(ap / (1 - ap)));
}

/* Amplitude p after a calibration pass: v moved by diff (the WAV's level less the synthesis's, dB), at most
 * CAL_MAX_STEP_DB. An amplitude that was off (0) in the synthesis measured only its neighbours' leakage, so no
 * correction applies: it stays off if that leakage reaches the WAV's level, else it restarts from its start value
 * less the share of the WAV's power the leakage already supplies. */
static double cal_step(int p, double v, double start, double diff)
{
	if (v <= 0)
		return diff > 0 ? settle(p, start + 10 * log10(1 - pow(10, -diff / 10))) : 0;
	diff = diff > CAL_MAX_STEP_DB ? CAL_MAX_STEP_DB : diff < -CAL_MAX_STEP_DB ? -CAL_MAX_STEP_DB : diff;
	return settle(p, v + diff);
}

/* ---- the frication model: the parallel resonators A2-A6 as the DLL sums them ---- */

/* Complex response a / (1 - b e^-jw - c e^-2jw) of a Klatt (1980) resonator at FFT bin `bin`. */
static void reson_response(const spec_ctx *s, double a, double b, double c, int bin, double *re, double *im)
{
	double dr = 1 - b * s->cw[2 * bin] - c * s->cw[2 * bin + 1], di = b * s->sw[2 * bin] + c * s->sw[2 * bin + 1];
	double d = dr * dr + di * di;
	*re = a * dr / d;
	*im = -a * di / d;
}

/* q[b][j][k]: band b's power is the quadratic form sum_jk x_j x_k q[b][j][k] of the resonators' gains x (A2-A6,
 * F2-F6 with the synthesizer's bandwidths, summed with the DLL's alternating signs: -R2 +R3 -R4 +R5 -R6), for
 * white noise, over the FFT bins of the band. Neighbouring resonators leak into each other's bands and interfere,
 * so band-by-band corrections alone can settle on a wrong split (a /z/ ended 5-10 dB off in its weak bands). */
static void fric_model(const spec_ctx *c, const double *fr, const double *g, double q[5][5][5])
{
	static const double sign[5] = { -1, 1, -1, 1, -1 };
	double f[6], lo[6], hi[6];
	formants_of(fr, g, f);
	band_edges(f, c->sr, lo, hi);
	const double bw[5] = { fr[KP_B2], fr[KP_B3], g[KG_B4], g[KG_B5], g[KG_B6] };
	double ra[5], rb[5], rc[5]; /* the resonators' coefficients, as the DLL sets them (Klatt 1980) */
	for (int j = 0; j < 5; j++) {
		double fc = f[j + 1] < c->sr * 0.5 ? f[j + 1] : c->sr * 0.5;
		rc[j] = -exp(-2 * PI * bw[j] / c->sr);
		rb[j] = 2 * exp(-PI * bw[j] / c->sr) * cos(2 * PI * fc / c->sr);
		ra[j] = f[j + 1] < BYPASS_FRAC * c->sr ? 1 - rb[j] - rc[j] : 0; /* the DLL drops a bypassed one */
	}
	memset(q, 0, sizeof(double) * 125);
	for (int bin = 1; bin <= c->nfft / 2; bin++) {
		double fq = (double)bin * c->sr / c->nfft, re[5], im[5];
		int b = -1;
		for (int i = 0; i < 5; i++)
			if (fq >= lo[i + 1] && fq < hi[i + 1])
				b = i;
		if (b < 0)
			continue;
		for (int j = 0; j < 5; j++) {
			reson_response(c, ra[j], rb[j], rc[j], bin, &re[j], &im[j]);
			re[j] *= sign[j];
			im[j] *= sign[j];
		}
		for (int j = 0; j < 5; j++)
			for (int k = 0; k < 5; k++)
				q[b][j][k] += re[j] * re[k] + im[j] * im[k];
	}
}

static double fric_power(double q[5][5][5], const double *x, int b)
{
	double p = 0;
	for (int j = 0; j < 5; j++)
		for (int k = 0; k < 5; k++)
			p += x[j] * x[k] * q[b][j][k];
	return p;
}

/* Squared log errors of the model (scaled by scale) against the target powers t, weight 1 for bands within
 * BAND_RANGE_DB of the strongest target and FRIC_WEAK_WEIGHT below. */
static double fric_cost(double q[5][5][5], double scale, const double *x, const double *t)
{
	double top = 0, e = 0;
	for (int b = 0; b < 5; b++)
		if (t[b] > top)
			top = t[b];
	for (int b = 0; b < 5; b++) {
		double d = log(scale * fric_power(q, x, b) + 1e-30) - log(t[b]);
		e += (t[b] > top * pow(10, -BAND_RANGE_DB / 10) ? 1 : FRIC_WEAK_WEIGHT) * d * d;
	}
	return e;
}

/* Fits the gains x >= 0 (amplitude ratios; x_j = 10^((AF + A_j - 2 REF_LEVEL) / 20)) to the target band powers t
 * by a pattern search on each gain in dB (steps from FRIC_STEP_DB down to FRIC_STEP_MIN_DB), trying 0 too. */
static void fric_fit(double q[5][5][5], double scale, const double *t, double *x)
{
	for (int sweep = 0; sweep < FRIC_SWEEPS; sweep++)
		for (int j = 0; j < 5; j++) {
			double keep = x[j];
			x[j] = 0;
			double c0 = fric_cost(q, scale, x, t);
			x[j] = keep > 0 ? keep : 1e-3;
			double cur = fric_cost(q, scale, x, t);
			for (double step = FRIC_STEP_DB; step >= FRIC_STEP_MIN_DB; step /= 2)
				for (int moved = 1; moved;) {
					moved = 0;
					for (int dir = -1; dir <= 1 && !moved; dir += 2) {
						double old = x[j];
						x[j] = old * pow(10, dir * step / 20);
						double c = fric_cost(q, scale, x, t);
						if (c < cur) {
							cur = c;
							moved = 1;
						} else {
							x[j] = old;
						}
					}
				}
			if (c0 <= cur)
				x[j] = 0;
		}
}

/* AF and A2-A6 of frame v from the fitted gains: the largest amplitude is PAR_TOP, AF takes the rest but is at
 * least one step, so any frication stays on (fric_get then inverts this: AF > 0 whenever a gain is positive). */
static void fric_set(double *v, const double *x)
{
	double top = -1e300, tot[5];
	for (int j = 0; j < 5; j++) {
		tot[j] = x[j] > 0 ? 2 * REF_LEVEL + 20 * log10(x[j]) : -1e300; /* AF + A(j+2), dB */
		if (tot[j] > top)
			top = tot[j];
	}
	if (top < -1e299) {
		v[KP_AF] = 0;
		for (int j = 0; j < 5; j++)
			v[KP_A2 + j] = 0;
		return;
	}
	double af = settle(KP_AF, top - PAR_TOP), step = doc_param_step(KP_AF);
	if (af < step)
		af = step;
	v[KP_AF] = af;
	for (int j = 0; j < 5; j++)
		v[KP_A2 + j] = tot[j] > -1e299 ? settle(KP_A2 + j, tot[j] - af) : 0;
}

/* The gains x of frame v's AF and A2-A6 (see fric_fit). */
static void fric_get(const double *v, double *x)
{
	for (int j = 0; j < 5; j++)
		x[j] = v[KP_AF] > 0 && v[KP_A2 + j] > 0 ? pow(10, (v[KP_AF] + v[KP_A2 + j] - 2 * REF_LEVEL) / 20) : 0;
}

/* Lowers every amplitude of frame v by db: AV, AH, and the frication through its gains, so that it stays on. */
static void lower_frame(double *v, double db)
{
	double x[5];
	fric_get(v, x);
	for (int j = 0; j < 5; j++)
		x[j] *= pow(10, -db / 20);
	fric_set(v, x);
	v[KP_AV] = settle(KP_AV, v[KP_AV] - db);
	v[KP_AH] = settle(KP_AH, v[KP_AH] - db);
}

/* ---- formant correction by analysis-by-synthesis ---- */

/* LPC's formant estimates are biased, most at high F0, where a formant between two harmonics is pulled towards
 * one (a female /u/'s F1 of 370 Hz at F0 215 Hz read 420). The synthesizer's output analysed the same way has the
 * same bias, so each pass synthesizes the voicing of the document, measures F1-F3 exactly as on the WAV (the
 * candidate nearest the current value), and moves each formant by the WAV's measurement less the synthesis's.
 * Runs `passes` passes. Returns 1 when it corrected the formants, 0 for a lower voice (nothing done), -1 when out of
 * memory. */
static int correct_formants(const double *g, double *frames, int nframes, const unsigned char *cls,
	const double *ftr, spec_ctx *spec, int passes)
{
	/* only for a high voice: the median F0 of the voiced frames */
	double *fr = malloc(sizeof(double) * NP * nframes), *w = malloc(sizeof(double) * spec->len), *x = NULL;
	if (!fr || !w) {
		free(fr);
		free(w);
		return -1;
	}
	int nv = 0;
	for (int k = 0; k < nframes; k++)
		if (cls[k] == CLS_VOICED)
			fr[nv++] = frames[(size_t)k * NP + KP_F0];
	qsort(fr, nv, sizeof(double), cmp_double);
	if (nv == 0 || fr[nv / 2] < FCORR_MIN_F0) {
		free(fr);
		free(w);
		return 0;
	}
	int sr = spec->sr, order = lpc_order(sr);
	for (int pass = 0; pass < passes; pass++) {
		/* the voicing alone: the analysis sees the same source and tract as in the WAV's voiced frames */
		for (int k = 0; k < nframes; k++) {
			double *v = fr + (size_t)k * NP;
			memcpy(v, frames + (size_t)k * NP, sizeof(double) * NP);
			v[KP_AH] = v[KP_AF] = 0;
			for (int p = KP_A1; p <= KP_AB; p++)
				v[p] = 0;
		}
		kpe_doc d;
		doc_init_from(&d, g, nframes, fr);
		synth_result s;
		int rc = synth_doc(&d, &s);
		d.frames = NULL; /* fr stays ours */
		doc_free(&d);
		if (rc != 0 || !(x = realloc(x, sizeof(double) * (s.n > 0 ? s.n : 1)))) {
			if (rc == 0)
				synth_result_free(&s);
			free(fr);
			free(w);
			free(x);
			return -1;
		}
		for (int i = 0; i < s.n; i++)
			x[i] = s.pcm[i] / 32768.0;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP, a[LPC_MAX_ORDER + 1];
			if (cls[k] != CLS_VOICED)
				continue;
			lpc_frame(x, s.n, import_frame_centre(sr, IMPORT_FRAME_MS, k), spec->len, w);
			lpc_cand c[CAND_MAX];
			int nc = lpc_burg(w, spec->len, order, a) > 0 ? lpc_candidates(a, order, sr, c) : 0;
			for (int i = 0; i < 3; i++) {
				double cur = v[KP_F1 + i];
				int best = -1;
				for (int j = 0; j < nc; j++)
					if (fabs(c[j].f - cur) < FCORR_SEARCH * cur && (best < 0 || fabs(c[j].f - cur) < fabs(c[best].f - cur)))
						best = j;
				if (best < 0)
					continue;
				double step = ftr[k * TRACK_N + i] - c[best].f, lim = FCORR_MAX_STEP * cur;
				step = step > lim ? lim : step < -lim ? -lim : step;
				v[KP_F1 + i] = settle(KP_F1 + i, cur + step);
			}
		}
		synth_result_free(&s);
	}
	free(fr);
	free(w);
	free(x);
	return 1;
}

/* ---- reliability and the second analysis (formant tracking, step 3) ---- */

/* 1 at or below lo, 0 at or above hi, linear between. */
static double falling(double v, double lo, double hi)
{
	return v <= lo ? 1 : v >= hi ? 0 : (hi - v) / (hi - lo);
}

/* A frame's reliability for the formant tracker, from the frication measures with neutral formants (step 6): 1 for
 * a frame with little frication, REL_MIN for a frication-dominated one. hf is the energy fraction above the
 * cutoff; top, the upper band's flatness, counts from TOP_MAX_SR up only, as below it the band holds formants (a
 * female /i/ at 8000 Hz reads -8 to +1 dB there). YIN's aperiodicity is not used: weighting the aperiodic frames
 * down too left the recordings' spikes as they were (their voiced fricatives are periodic, ap 0-0.03) but moved
 * the female vowels at 8000 Hz past their known limits (F2 median 7.4 -> 8.2-17 %) through a few onset frames. */
static double reliability(int sr, double hf, double top)
{
	double fric = falling(hf, REL_HF_LO, REL_HF_HI);
	if (sr >= TOP_MAX_SR)
		fric = fmin(fric, falling(top, REL_TOP_LO, REL_TOP_HI));
	return REL_MIN + (1 - REL_MIN) * fric;
}

/* The second analysis: in every frame (not quiet) whose reliability is below TRACK_UNRELIABLE, an LPC of the recording
 * low-passed at min(LOW_HZ, LOW_FRAC sr), where the voicing dominates F1-F3, and a full-band candidate within
 * CONFIRM_DIST of one of its candidates is confirmed (conf, CAND_MAX per frame like cands). The low pass is a
 * resampling to the rate at which that band is the candidates' range (CAND_MAX_FRAC of it), with its own LPC order.
 * Returns 0, or -1 when out of memory. */
static int confirm_candidates(const double *x, long n, int sr, const long *centre, const double *rel,
	const unsigned char *quiet, int nframes, const lpc_cand *cands, const int *ncand, unsigned char *conf)
{
	int any = 0;
	for (int k = 0; k < nframes && !any; k++)
		any = rel[k] < TRACK_UNRELIABLE && !quiet[k];
	if (!any)
		return 0;
	double fc = LOW_HZ < LOW_FRAC * sr ? LOW_HZ : LOW_FRAC * sr, *xl;
	int srl = (int)ceil(fc / CAND_MAX_FRAC), len = lpc_window_len(srl), order = lpc_order(srl);
	if (resample(x, n, sr, srl, &xl) != 0)
		return -1;
	long nl = resample_length(n, sr, srl);
	double *w = malloc(sizeof(double) * len), a[LPC_MAX_ORDER + 1];
	if (!w) {
		free(xl);
		return -1;
	}
	for (int k = 0; k < nframes; k++) {
		if (!(rel[k] < TRACK_UNRELIABLE) || quiet[k])
			continue;
		lpc_cand lc[CAND_MAX];
		lpc_frame(xl, nl, (long)floor(centre[k] * (double)srl / sr + 0.5), len, w);
		int nc = lpc_burg(w, len, order, a) > 0 ? lpc_candidates(a, order, srl, lc) : 0;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		for (int j = 0; j < ncand[k]; j++)
			for (int i = 0; i < nc; i++)
				if (fabs(c[j].f - lc[i].f) <= CONFIRM_DIST * lc[i].f)
					conf[(size_t)k * CAND_MAX + j] = 1;
	}
	free(w);
	free(xl);
	return 0;
}

/* ---- the pipeline ---- */

int import_samples(const double *x0, long n0, int sr_in, const import_options *o, kpe_doc *d, import_info *info,
	char *msg, size_t msglen)
{
	clock_t t_start = clock();
	if (info)
		memset(info, 0, sizeof *info);
	if (msglen)
		msg[0] = 0;
	const voice_info *vt = o ? import_voice(o->voice) : NULL;
	if (!vt || !x0 || n0 < 0 || sr_in <= 0 || !sr_ok(o->sr) || o->quality < 0 || o->quality >= IMPORT_QUALITY_COUNT)
		return fail(msg, msglen, "Cannot import: bad arguments.");
	const preset *ps = &presets[o->quality];
	if (n0 == 0)
		return fail(msg, msglen, "No sound found (the recording is empty).");
	if (n0 * 1000.0 / sr_in > DOC_MAX_DURATION_MS)
		return fail(msg, msglen, "The recording is longer than 10 minutes.");
	double peak = 0;
	for (long i = 0; i < n0; i++)
		if (fabs(x0[i]) > peak)
			peak = fabs(x0[i]);
	if (!(peak >= pow(10, NO_SOUND_DBFS / 20)))
		return fail(msg, msglen, "No sound found (the recording's peak is below %.0f dBFS).", NO_SOUND_DBFS);

	int sr = o->sr, rc = -1;
	double *x = NULL;
	if (resample(x0, n0, sr_in, sr, &x) != 0)
		return fail(msg, msglen, "Out of memory.");
	long n = resample_length(n0, sr_in, sr);
	double in_gain = pow(10, INPUT_PEAK_DB / 20) / peak; /* the amplitudes do not depend on the recording's level */
	for (long i = 0; i < n; i++)
		x[i] *= in_gain;
	double spf = sr * IMPORT_FRAME_MS / 1000;
	int nframes = (int)ceil(n / spf - 1e-9);
	if (nframes < 1)
		nframes = 1;

	double g[NG];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SR] = sr;
	g[KG_SRC] = 1;
	g[KG_FRAME_MS] = IMPORT_FRAME_MS;

	spec_ctx spec = { 0 };
	long *centre = malloc(sizeof(long) * nframes);
	double *f0 = malloc(sizeof(double) * nframes), *ap = malloc(sizeof(double) * nframes);
	unsigned char *voiced = malloc(nframes), *quiet = malloc(nframes), *cls = malloc(nframes);
	lpc_cand *cands = malloc(sizeof(lpc_cand) * CAND_MAX * nframes);
	int *ncand = malloc(sizeof(int) * nframes);
	double *ftr = malloc(sizeof(double) * TRACK_N * nframes), *btr = malloc(sizeof(double) * TRACK_N * nframes);
	import_levels *lw = malloc(sizeof(import_levels) * nframes), *ls = malloc(sizeof(import_levels) * nframes);
	double *frames = malloc(sizeof(double) * NP * nframes), *w = NULL, *level = malloc(sizeof(double) * nframes);
	double *hf = malloc(sizeof(double) * nframes), *upper = malloc(sizeof(double) * nframes);
	double *lowrel = malloc(sizeof(double) * nframes), *aps = malloc(sizeof(double) * nframes);
	double *start = malloc(sizeof(double) * nframes); /* AV's (voiced classes) or AH's (aspiration) start value */
	unsigned char *fric = malloc(nframes), *conf = calloc((size_t)CAND_MAX * nframes, 1);
	double *vb = NULL, *rel = malloc(sizeof(double) * nframes);
	kpe_doc tmp = { 0 };
	int have_tmp = 0;
	if (!centre || !f0 || !ap || !voiced || !quiet || !cls || !cands || !ncand || !ftr || !btr || !lw || !ls ||
	    !frames || !level || !hf || !upper || !lowrel || !aps || !start || !fric || !conf || !rel ||
	    spec_init(&spec, sr) != 0 || !(w = malloc(sizeof(double) * spec.len))) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}

	/* per frame: LPC candidates (step 2) and the levels with neutral formants (for classes and quiet frames) */
	int order = lpc_order(sr);
	double top = LEVEL_FLOOR;
	for (int k = 0; k < nframes; k++) {
		centre[k] = import_frame_centre(sr, IMPORT_FRAME_MS, k);
		double a[LPC_MAX_ORDER + 1];
		lpc_frame(x, n, centre[k], spec.len, w);
		ncand[k] = lpc_burg(w, spec.len, order, a) > 0 ? lpc_candidates(a, order, sr, cands + (size_t)k * CAND_MAX)
			: 0;
		double fn[6] = { vt->ref[0], vt->ref[1], vt->ref[2], vt->ref[3], g[KG_F5], g[KG_F6] };
		measure(&spec, x, n, centre[k], fn, &lw[k]);
		level[k] = lw[k].total;
		hf[k] = lw[k].hf;
		upper[k] = lw[k].top;
		lowrel[k] = lw[k].low - lw[k].total;
		if (level[k] > top)
			top = level[k];
	}
	/* pitch (step 1) */
	if (pitch_track(x, n, sr, centre, nframes, vt->f0_min, vt->f0_max, f0, ap, voiced) != 0) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}
	for (int k = 0; k < nframes; k++) { /* aperiodicity median-smoothed, for AH */
		double win[AP_SMOOTH];
		int m = 0;
		for (int j = k - AP_SMOOTH / 2; j <= k + AP_SMOOTH / 2; j++)
			if (j >= 0 && j < nframes)
				win[m++] = ap[j];
		qsort(win, m, sizeof(double), cmp_double);
		aps[k] = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
	}
	/* formant tracks (steps 3 and 4) */
	track_setup ts;
	for (int i = 0; i < TRACK_N; i++) {
		int p = KP_F1 + i;
		ts.ref[i] = vt->ref[i];
		ts.lo[i] = param_min(p);
		ts.hi[i] = param_max(p) < CAND_MAX_FRAC * sr ? param_max(p) : CAND_MAX_FRAC * sr;
		if (ts.ref[i] > ts.hi[i])
			ts.ref[i] = ts.hi[i];
	}
	for (int k = 0; k < nframes; k++) {
		quiet[k] = level[k] < top - QUIET_DB;
		rel[k] = reliability(sr, hf[k], upper[k]);
	}
	if (confirm_candidates(x, n, sr, centre, rel, quiet, nframes, cands, ncand, conf) != 0 ||
	    track_formants(cands, ncand, quiet, rel, conf, nframes, &ts, ftr, btr) != 0) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}
	/* frame classes (step 6): a frame is fricative by its energy fraction above the cutoff or by a flat upper band
	 * (import_levels.top), then by the majority of the non-silent frames within CLASS_SMOOTH frames */
	for (int k = 0; k < nframes; k++)
		fric[k] = voiced[k] ? hf[k] >= VFRIC_HF || upper[k] >= TOP_VFRIC_DB
			: hf[k] >= FRIC_HF || (upper[k] >= TOP_FRIC_DB && lowrel[k] <= LOW_FRIC_DB);
	for (int k = 0; k < nframes; k++) {
		int yes = 0, all = 0;
		for (int j = k - CLASS_SMOOTH / 2; j <= k + CLASS_SMOOTH / 2; j++)
			if (j >= 0 && j < nframes && level[j] >= top - SILENCE_DB) {
				all++;
				yes += fric[j] & 1;
			}
		fric[k] |= (2 * yes > all) << 1; /* bit 1: the smoothed decision */
	}
	for (int k = 0; k < nframes;) { /* runs shorter than FRIC_MIN_FRAMES are not fricatives */
		if (!(fric[k] & 2) || level[k] < top - SILENCE_DB) {
			fric[k++] = 0;
			continue;
		}
		int e = k;
		while (e < nframes && (fric[e] & 2) && level[e] >= top - SILENCE_DB)
			e++;
		for (int j = k; j < e; j++)
			fric[j] = e - k >= FRIC_MIN_FRAMES;
		k = e;
	}
	for (int k = 0; k < nframes; k++) {
		if (level[k] < top - SILENCE_DB)
			cls[k] = CLS_SILENCE;
		else
			cls[k] = voiced[k] ? (fric[k] ? CLS_VOICED_FRIC : CLS_VOICED) : (fric[k] ? CLS_FRIC : CLS_ASPIRATION);
	}
	/* the frames: F0 held across unvoiced frames (interpolated between voiced ones), formants and bandwidths */
	double f0_default = sqrt(vt->f0_min * vt->f0_max);
	for (int k = 0; k < nframes; k++) {
		double *fr = frames + (size_t)k * NP;
		for (int p = 0; p < NP; p++) {
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			fr[p] = pi.def;
		}
		for (int i = 0; i < TRACK_N; i++)
			fr[KP_F1 + i] = settle(KP_F1 + i, ftr[k * TRACK_N + i]);
		for (int i = 0; i < 3; i++)
			fr[KP_B1 + i] = settle(KP_B1 + i, btr[k * TRACK_N + i]);
		if (!(f0[k] > 0))
			f0[k] = NAN;
	}
	{
		int prev = -1;
		for (int k = 0; k <= nframes; k++) {
			if (k < nframes && isnan(f0[k]))
				continue;
			for (int j = prev + 1; j < k; j++)
				f0[j] = prev < 0 && k == nframes ? f0_default : prev < 0 ? f0[k] : k == nframes ? f0[prev]
					: f0[prev] + (f0[k] - f0[prev]) * (j - prev) / (k - prev);
			prev = k;
		}
		for (int k = 0; k < nframes; k++)
			frames[(size_t)k * NP + KP_F0] = settle(KP_F0, f0[k]);
	}
	/* levels (step 8): start values for AV and AH from reference syntheses at REF_LEVEL */
	double fric_scale = 1; /* measured / modelled band power of the reference fricative (AF, A2-A6 at REF_LEVEL) */
	vb = malloc(sizeof(double) * 5 * nframes); /* per frame and band: the voicing's and aspiration's power */
	if (!vb)
		goto oom;
	{
		double fr[NP];
		import_levels rv, rh, rf;
		for (int p = 0; p < NP; p++) {
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			fr[p] = pi.def;
		}
		fr[KP_AV] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rv) != 0)
			goto oom;
		fr[KP_AV] = 0;
		fr[KP_AH] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rh) != 0)
			goto oom;
		fr[KP_AH] = 0;
		fr[KP_AF] = REF_LEVEL;
		for (int p = KP_A2; p <= KP_A6; p++)
			fr[p] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rf) != 0)
			goto oom;
		double q[5][5][5], lsum = 0;
		static const double one[5] = { 1, 1, 1, 1, 1 };
		int nb = 0;
		fric_model(&spec, fr, g, q);
		for (int b = 0; b < 5; b++) {
			double model = fric_power(q, one, b);
			if (model > 0 && rf.band[b] > LEVEL_FLOOR) {
				lsum += log(pow(10, rf.band[b] / 10) / model);
				nb++;
			}
		}
		fric_scale = nb ? exp(lsum / nb) : 1;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP, f[6];
			formants_of(v, g, f);
			measure(&spec, x, n, centre[k], f, &lw[k]);
			int c = cls[k];
			start[k] = 0;
			if (c == CLS_VOICED || c == CLS_VOICED_FRIC)
				v[KP_AV] = start[k] = settle(KP_AV, REF_LEVEL + lw[k].low - rv.low);
			if (c == CLS_VOICED)
				v[KP_AH] = voiced_ah(v[KP_AV], aps[k]);
			if (c == CLS_ASPIRATION)
				v[KP_AH] = start[k] = settle(KP_AH, REF_LEVEL + lw[k].total - rh.total);
		}
	}
	/* formants corrected by analysis-by-synthesis (step 5), then Rd (step 7) with the corrected formants */
	clock_t t_rd = clock();
	double gc[NG], cal_gain = pow(10, CAL_HEADROOM_DB / 20), pdb;
	int over;
	memcpy(gc, g, sizeof gc);
	gc[KG_G0] -= CAL_HEADROOM_DB;
	int corrected = correct_formants(gc, frames, nframes, cls, ftr, &spec, ps->fcorr_passes);
	if (corrected < 0)
		goto oom;
	for (int k = 0; corrected && k < nframes; k++) { /* the WAV's levels in the corrected formants' bands */
		double f[6];
		formants_of(frames + (size_t)k * NP, g, f);
		measure(&spec, x, n, centre[k], f, &lw[k]);
	}
	int fitted = 0;
	if (fit_rd_track(x, n, sr, cls, nframes, frames, &spec, ps->rd_step, &fitted) != 0)
		goto oom;
	clock_t t_cal = clock();

	if (doc_init_from(&tmp, gc, nframes, frames) != 0)
		goto oom;
	have_tmp = 1;
	/* calibration against the synthesizer (step 8): each pass synthesizes the document (CAL_HEADROOM_DB below the
	 * default G0, measured as at the default), measures it like the WAV and corrects AV by the low band's dB
	 * difference (AH keeping its offset in voiced frames) and AH in aspiration frames by the overall level's
	 * (cal_step). The first pass has no frication yet: in fricative frames what the WAV has beyond the voicing and
	 * aspiration is the frication, and AF and A2-A6 are fitted to its five band powers with a model of the parallel
	 * resonators (fric_model); later passes scale each band's target by the WAV's frication over the synthesis's
	 * and fit again */
	for (int pass = 0; pass <= ps->cal_passes; pass++) {
		if (measure_synth(&tmp, &spec, cal_gain, ls, &pdb, &over) != 0)
			goto oom;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP;
			int c = cls[k];
			if (c == CLS_VOICED || c == CLS_VOICED_FRIC)
				v[KP_AV] = cal_step(KP_AV, v[KP_AV], start[k], lw[k].low - ls[k].low);
			if (c == CLS_VOICED) /* in voiced fricatives the aperiodic energy is the frication's */
				v[KP_AH] = voiced_ah(v[KP_AV], aps[k]);
			if (c == CLS_ASPIRATION)
				v[KP_AH] = cal_step(KP_AH, v[KP_AH], start[k], lw[k].total - ls[k].total);
			if (c != CLS_FRIC && c != CLS_VOICED_FRIC)
				continue;
			/* the frication's band powers: the WAV's (and in later passes the synthesis's) less the voicing and
			 * aspiration the first pass measured */
			double tw[5], q[5][5][5], xj[5], top = 0;
			fric_model(&spec, v, g, q);
			for (int b = 0; b < 5; b++) {
				if (pass == 0)
					vb[(size_t)k * 5 + b] = pow(10, ls[k].band[b] / 10);
				tw[b] = pow(10, lw[k].band[b] / 10) - vb[(size_t)k * 5 + b];
				if (tw[b] > top)
					top = tw[b];
			}
			if (!(top > 0)) {
				static const double none[5] = { 0 };
				fric_set(v, none);
				continue;
			}
			for (int b = 0; b < 5; b++)
				if (!(tw[b] > top * 1e-6))
					tw[b] = top * 1e-6; /* nothing left in this band: 60 dB below the strongest */
			if (pass == 0) {
				for (int j = 0; j < 5; j++) /* start: each band from its own resonator */
					xj[j] = q[j][j][j] > 0 ? sqrt(tw[j] / (fric_scale * q[j][j][j])) : 0;
			} else {
				/* correct the targets by what the synthesis really gave: the model's prediction times the ratio
				 * of the WAV's frication to the synthesis's, band by band */
				fric_get(v, xj);
				for (int b = 0; b < 5; b++) {
					double syn = pow(10, ls[k].band[b] / 10) - vb[(size_t)k * 5 + b];
					double model = fric_scale * fric_power(q, xj, b);
					if (syn > top * 1e-6 && model > 0)
						tw[b] = model * tw[b] / syn;
				}
			}
			fric_fit(q, fric_scale, tw, xj);
			fric_set(v, xj);
		}
	}
	/* verification: a non-silent frame whose synthesis is more than CAL_LOUD_DB louder than the WAV is lowered by the
	 * excess (a burst where the last pass overshot). The WAV's level is its loudest over the frame and the frames of
	 * the glottal period before it: the DLL latches AV at each pulse, so a frame's voicing sounds on for up to a
	 * period, and a decay can only follow the WAV's as fast as that */
	if (measure_synth(&tmp, &spec, cal_gain, ls, &pdb, &over) != 0)
		goto oom;
	for (int k = 0; k < nframes; k++) {
		double ref = lw[k].total;
		for (int j = k - import_period_frames(frames[(size_t)k * NP + KP_F0]); j < k; j++)
			ref = j >= 0 && lw[j].total > ref ? lw[j].total : ref;
		if (cls[k] != CLS_SILENCE && ls[k].total - ref > CAL_LOUD_DB)
			lower_frame(frames + (size_t)k * NP, ls[k].total - ref);
	}
	/* silence: every amplitude 0 */
	for (int k = 0; k < nframes; k++)
		if (cls[k] == CLS_SILENCE)
			for (int p = 0; p < NP; p++)
				if (p == KP_AV || p == KP_AF || p == KP_AH || p == KP_AVS || p == KP_AN || (p >= KP_A1 && p <= KP_AB))
					frames[(size_t)k * NP + p] = 0;
	if (o->simplify) { /* Optimize's defaults: smoothing, then simplification */
		double *v = malloc(sizeof(double) * nframes);
		if (!v)
			goto oom;
		for (int p = 0; p < NP; p++) {
			for (int k = 0; k < nframes; k++)
				v[k] = frames[(size_t)k * NP + p];
			if (opt_smooth(v, nframes, IMPORT_FRAME_MS, opt_smooth_default(p), opt_smooth_runs(p)) != 0) {
				free(v);
				goto oom;
			}
			opt_simplify(v, nframes, 0, 1, opt_tol_default[opt_tol_class(p)], NULL);
			for (int k = 0; k < nframes; k++)
				frames[(size_t)k * NP + p] = v[k];
		}
		free(v);
	}
	/* level normalization: G0 so the resynthesis peaks at or just below NORM_PEAK_DB; the amplitudes keep their
	 * levels. Each synthesis without overload predicts the G0 that puts its peak there (the output is linear in
	 * G0); one that overloads moves NORM_HEADROOM_DB down. The first prediction comes from the verification
	 * synthesis, made before its corrections, so G0 is set only once a synthesis of the final document at that G0
	 * predicts that G0 again: then it has no overload and its peak is at or below NORM_PEAK_DB (unless G0 is at its
	 * minimum), as a G0 that failed is never tried again */
	{
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, KG_G0, &pi);
		double g0 = gc[KG_G0], fail_g0 = pi.max + 1; /* the lowest G0 that overloaded or peaked too high */
		for (int fresh = 0;; fresh = 1) {
			if (over || pdb > NORM_PEAK_DB)
				fail_g0 = fresh && g0 < fail_g0 ? g0 : fail_g0;
			double next = over ? g0 - NORM_HEADROOM_DB : pdb > -1e299 ? floor(g0 + NORM_PEAK_DB - pdb) : g[KG_G0];
			next = next >= fail_g0 ? fail_g0 - 1 : next;
			next = next < pi.min ? pi.min : next > pi.max ? pi.max : next;
			if (fresh && next == g0)
				break;
			g0 = next;
			tmp.globals[KG_G0] = g0;
			synth_result s;
			if (synth_doc(&tmp, &s) != 0)
				goto oom;
			pdb = peak_db(&s);
			over = s.overload;
			synth_result_free(&s);
		}
		tmp.globals[KG_G0] = g0;
	}
	*d = tmp; /* takes the frames */
	d->dirty = 0;
	have_tmp = 0;
	frames = NULL;
	if (info) {
		info->nframes = nframes;
		info->cls = cls;
		info->ap = ap;
		info->hf = hf;
		info->top = upper;
		info->level = level;
		info->rd_frames = fitted;
		info->seconds[0] = (double)(t_rd - t_start) / CLOCKS_PER_SEC;
		info->seconds[1] = (double)(t_cal - t_rd) / CLOCKS_PER_SEC;
		info->seconds[2] = (double)(clock() - t_cal) / CLOCKS_PER_SEC;
		info->seconds[3] = (double)(clock() - t_start) / CLOCKS_PER_SEC;
		cls = NULL;
		ap = hf = level = upper = NULL;
	}
	rc = 0;
	goto done;
oom:
	fail(msg, msglen, "Out of memory.");
done:
	if (have_tmp) {
		tmp.frames = NULL; /* frames is freed below */
		doc_free(&tmp);
	}
	spec_free(&spec);
	free(x);
	free(w);
	free(centre);
	free(f0);
	free(ap);
	free(voiced);
	free(quiet);
	free(cls);
	free(cands);
	free(ncand);
	free(ftr);
	free(btr);
	free(lw);
	free(ls);
	free(frames);
	free(level);
	free(hf);
	free(upper);
	free(lowrel);
	free(aps);
	free(start);
	free(fric);
	free(vb);
	free(rel);
	free(conf);
	return rc;
}

int import_wav(const wchar_t *path, const import_options *o, kpe_doc *d, import_info *info, char *msg,
	size_t msglen)
{
	wav_data wd;
	if (info)
		memset(info, 0, sizeof *info);
	if (wav_read(path, DOC_MAX_DURATION_MS / 1000, &wd, msg, msglen) != 0) {
		if (msglen && strstr(msg, "too long"))
			snprintf(msg, msglen, "The recording is longer than 10 minutes.");
		return -1;
	}
	int rc = import_samples(wd.x, wd.n, wd.sr, o, d, info, msg, msglen);
	wav_data_free(&wd);
	return rc;
}
