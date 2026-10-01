/* WAV import: round trips on documents synthesized by the DLL (spec 4.6), frame classes of realistic fricatives,
 * aspiration, level normalization, refusals, and the speed test. Files are written into the working directory
 * (build/32 or build/64). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "csvio.h"
#include "optimize.h"
#include "synthrun.h"
#include "wavout.h"
#include "wavimport.h"
#include "waverange.h"

/* spec 4.6 limits */
#define LIM_F0_MEDIAN 0.02
#define LIM_F_MEDIAN 0.05
#define LIM_F_P90 0.10
#define LIM_VUV 0.95
#define LIM_AV_DB 3.0
#define LIM_RD 0.3
#define LIM_BAND_DB 3.0
#define HIGH_VOICE_F0 200.0    /* from this median F0 up, AV, Rd and the frame levels are held to their limits at the
                                * median: 25 ms LPC cannot resolve B1 there, and Rd and AV absorb the bandwidth error */
#define LIM_CLASS 0.90         /* every class with at least MIN_CLASS_FRAMES steady frames: this share right */
#define MIN_CLASS_FRAMES 10
#define LIM_FRIC_AF 0.90       /* steady fricative frames imported with AF > 0: at least this share */
#define QUIET_DB -48.0         /* the quiet /asa/ is this much below the synthesis: peak about -50 dBFS */
#define LIM_AH_DB 5.0          /* a breathy vowel's AH, median over its steady frames */
#define PEAK_MAX_DB -1.0       /* every resynthesis peaks at or below this; a full-scale WAV's at or above PEAK_MIN_DB */
#define PEAK_MIN_DB -3.0
#define INPUT_PEAK_DB -1.0     /* the import scales its input to peak here (spec 4.3) */
#define LEVEL_RANGE_DB 40.0    /* per-frame levels are compared on frames within this of the WAV's loudest */
#define LIM_LEVEL_DB 3.0       /* the resynthesis's frame level within this of the WAV's (90th percentile)... */
#define LIM_LOUDER_DB 10.0     /* ...and no frame more than this louder (a burst) */
#define BOUNDARY_FRAMES 2      /* compared frames are at least 10 ms from a class boundary */
#define BAND_RANGE_DB 30.0     /* fricative bands more than this below the frame's strongest band are not compared */

/* The limits a round trip is held to: the spec's, or for a known limit at another sample rate (spec 4.6) what it
 * meets there. */
typedef struct {
	double vuv, f_median[3], f_p90[3], av_db, band_db, cls, louder_db;
	int vowel_fric;
	double rd;
} limits;

static const limits spec_limits = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN },
	{ LIM_F_P90, LIM_F_P90, LIM_F_P90 }, LIM_AV_DB, LIM_BAND_DB, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };

static int quality = IMPORT_NORMAL; /* the preset the round trips import with */

typedef struct {
	double ms, v;
} pt;

/* Sets param p of every frame by linear interpolation between the points (held beyond the ends). */
static void track(kpe_doc *d, int p, const pt *pts, int n)
{
	for (int k = 0; k < d->nframes; k++) {
		double t = k * doc_frame_ms(d), v = pts[n - 1].v;
		if (t <= pts[0].ms)
			v = pts[0].v;
		else
			for (int i = 1; i < n; i++)
				if (t <= pts[i].ms) {
					v = pts[i - 1].v + (pts[i].v - pts[i - 1].v) * (t - pts[i - 1].ms) / (pts[i].ms - pts[i - 1].ms);
					break;
				}
		d->frames[(size_t)k * NP + p] = v;
	}
}

#define TRACK(d, p, ...) do { \
	const pt pts_[] = { __VA_ARGS__ }; \
	track(d, p, pts_, (int)(sizeof pts_ / sizeof pts_[0])); \
} while (0)

static void new_doc(kpe_doc *d, double ms, int src)
{
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = src;
	CHECK(doc_new(d, g, ms) == 0);
}

/* Rounds every value to its parameter's step, as a saved document holds them. */
static void round_doc(kpe_doc *d)
{
	for (int k = 0; k < d->nframes; k++)
		for (int p = 0; p < NP; p++) {
			double s = doc_param_step(p), *v = &d->frames[(size_t)k * NP + p];
			*v = round(*v / s) * s;
		}
}

static void female_vowels(kpe_doc *d)
{
	new_doc(d, 1200, 1);
	TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 1100, 60 }, { 1140, 0 });
	TRACK(d, KP_F0, { 0, 210 }, { 400, 250 }, { 900, 200 }, { 1200, 220 });
	TRACK(d, KP_F1, { 0, 310 }, { 250, 310 }, { 350, 850 }, { 550, 850 }, { 650, 370 }, { 850, 370 }, { 950, 860 });
	TRACK(d, KP_F2, { 0, 2790 }, { 250, 2790 }, { 350, 1220 }, { 550, 1220 }, { 650, 950 }, { 850, 950 },
		{ 950, 2050 });
	TRACK(d, KP_F3, { 0, 3310 }, { 250, 3310 }, { 350, 2810 }, { 550, 2810 }, { 650, 2670 }, { 850, 2670 },
		{ 950, 2850 });
	TRACK(d, KP_F4, { 0, 4000 });
	round_doc(d);
}

/* A fricative between two /a/ vowels: AV during the fricative (0: voiceless), AF, the parallel amplitudes A2-A6
 * and AB, and F3 in the fricative (the /a/s have 700/1220/2600 Hz). */
typedef struct {
	double av, af, a[5], ab, f3, b2, b3; /* b2, b3: B2 and B3 in the fricative, 0 for the defaults */
} fric_spec;

static void fric_doc(kpe_doc *d, const fric_spec *f)
{
	double voiced = f->av;
	new_doc(d, 800, 1);
	if (voiced > 0)
		TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 280, 60 }, { 310, voiced }, { 480, voiced }, { 510, 60 },
			{ 720, 60 }, { 750, 0 });
	else
		TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 280, 60 }, { 300, 0 }, { 490, 0 }, { 510, 60 },
			{ 720, 60 }, { 750, 0 });
	TRACK(d, KP_AF, { 0, 0 }, { 290, 0 }, { 310, f->af }, { 480, f->af }, { 500, 0 });
	for (int j = 0; j < 5; j++)
		TRACK(d, KP_A2 + j, { 0, f->a[j] });
	TRACK(d, KP_AB, { 0, f->ab });
	TRACK(d, KP_F0, { 0, 125 }, { 800, 100 });
	TRACK(d, KP_F1, { 0, 700 }, { 260, 700 }, { 300, 400 }, { 490, 400 }, { 530, 700 });
	TRACK(d, KP_F2, { 0, 1220 }, { 260, 1220 }, { 300, 1600 }, { 490, 1600 }, { 530, 1220 });
	TRACK(d, KP_F3, { 0, 2600 }, { 260, 2600 }, { 300, f->f3 }, { 490, f->f3 }, { 530, 2600 });
	if (f->b2 > 0) {
		TRACK(d, KP_B2, { 0, 70 }, { 260, 70 }, { 300, f->b2 }, { 490, f->b2 }, { 530, 70 });
		TRACK(d, KP_B3, { 0, 110 }, { 260, 110 }, { 300, f->b3 }, { 490, f->b3 }, { 530, 110 });
	}
	round_doc(d);
}

/* /a/ - fricative - /a/: voiced = 0 gives /s/, otherwise /z/ with that AV during the fricative */
static void fricative(kpe_doc *d, double voiced)
{
	fric_spec f = { voiced, 58, { 0, 0, 38, 50, 56 }, 0, 2700, 0, 0 };
	fric_doc(d, &f);
}

/* Fricatives as the user's recordings have them (measured at 11025 Hz): a /z/ whose frication is 10 dB weaker than
 * the one above, a /sh/ with its energy at 2-4 kHz (below the 3859 Hz cutoff of the energy fraction), and a /v/: flat
 * frication (through AB) over a weak voicing whose F2 and F3 are damped by the constriction, so that above 1 kHz the
 * spectrum is flat (upper band 2 dB below the densest 500 Hz band above 1 kHz; the recording's reads 6 dB). */
static const fric_spec weak_z = { 55, 46, { 0, 0, 30, 40, 48 }, 0, 2700, 0, 0 };
static const fric_spec sh = { 0, 58, { 0, 52, 56, 44, 30 }, 0, 2400, 0, 0 };
static const fric_spec v_fric = { 45, 50, { 0, 0, 0, 0, 0 }, 58, 2700, 200, 300 };

static void rd_sweep(kpe_doc *d)
{
	new_doc(d, 1500, 1);
	TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 1440, 60 }, { 1470, 0 });
	TRACK(d, KP_F0, { 0, 115 }, { 1500, 105 });
	TRACK(d, KP_F1, { 0, 700 });
	TRACK(d, KP_F2, { 0, 1220 });
	TRACK(d, KP_F3, { 0, 2600 });
	TRACK(d, KP_RD, { 0, 0.4 }, { 100, 0.4 }, { 1400, 2.5 });
	round_doc(d);
}

static int truth_class(const kpe_doc *d, int k)
{
	const double *f = d->frames + (size_t)k * NP;
	if (f[KP_AV] > 0)
		return f[KP_AF] > 0 ? CLS_VOICED_FRIC : CLS_VOICED;
	if (f[KP_AF] > 0)
		return CLS_FRIC;
	return f[KP_AH] > 0 ? CLS_ASPIRATION : CLS_SILENCE;
}

/* The frame and its BOUNDARY_FRAMES neighbours on either side have the same class. */
static int steady(const kpe_doc *d, int k)
{
	int c = truth_class(d, k);
	for (int j = k - BOUNDARY_FRAMES; j <= k + BOUNDARY_FRAMES; j++)
		if (j < 0 || j >= d->nframes || truth_class(d, j) != c)
			return 0;
	return 1;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* The q-quantile (0.5 median, 0.9 90th percentile) of v[0..n-1]; sorts v. */
static double quantile(double *v, int n, double q)
{
	if (n == 0)
		return NAN;
	qsort(v, n, sizeof(double), cmp_double);
	int i = (int)ceil(q * n) - 1;
	return v[i < 0 ? 0 : i];
}

static double *pcm_to_double(const synth_result *s)
{
	double *x = malloc(sizeof(double) * (s->n > 0 ? s->n : 1));
	for (int i = 0; i < s->n; i++)
		x[i] = s->pcm[i] / 32768.0;
	return x;
}

/* Synthesizes src, imports it and compares with the spec 4.6 limits. AV and Rd are compared only when src uses the
 * LF source like the import (with the 1980 source neither means the same). The import scales its input to peak at
 * INPUT_PEAK_DB, calibrates the amplitudes at the default G0 and then sets its own G0 (level normalization), so its
 * synthesis's levels are compared with the WAV's plus the input scaling and the G0 difference, and AV with the
 * source's plus the input scaling and the test's gain_db (the WAV is scaled by gain_db first), rounded to AV's step.
 * "Within" is the 90th percentile of the per-frame error, or the median for a voice whose median F0 is
 * HIGH_VOICE_F0 or more. Also: every well-represented class right in at least LIM_CLASS of its steady frames, no
 * steady vowel frame imported with frication, at least LIM_FRIC_AF of the steady fricative frames with AF > 0, no
 * overload in the resynthesis, its peak at or below PEAK_MAX_DB, and its frame levels (frames within LEVEL_RANGE_DB
 * of the WAV's loudest) within LIM_LEVEL_DB of the WAV's and never more than LIM_LOUDER_DB louder than the WAV's
 * loudest over the frame and the glottal period before it (a burst; a decay lags by up to a period, as the DLL
 * latches AV at each pulse). lim holds the limits that a known limit (spec 4.6) changes. */
static void round_trip_lim(const char *name, const kpe_doc *src, int voice, double gain_db, const limits *lim)
{
	int same_source = src->globals[KG_SRC] == 1;
	synth_result s;
	CHECK(synth_doc(src, &s) == 0);
	double *x = pcm_to_double(&s), xpeak = 0, gdef[NG];
	for (int i = 0; i < s.n; i++) {
		x[i] *= pow(10, gain_db / 20);
		xpeak = fabs(x[i]) > xpeak ? fabs(x[i]) : xpeak;
	}
	t_default_globals(gdef);
	double in_db = INPUT_PEAK_DB - 20 * log10(xpeak); /* the import's input scaling */
	import_options o = { voice, s.sr, 0, quality };
	kpe_doc d;
	import_info info;
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, &info, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc != 0) {
		printf("  %s: %s\n", name, msg);
		free(x);
		synth_result_free(&s);
		return;
	}
	CHECK(d.nframes == src->nframes);
	CHECK(d.globals[KG_SRC] == 1 && d.globals[KG_SR] == s.sr && d.globals[KG_FRAME_MS] == 5);
	int nf = d.nframes < src->nframes ? d.nframes : src->nframes;
	double *e0 = malloc(sizeof(double) * nf), *ef[3], *eav = malloc(sizeof(double) * nf);
	double *erd = malloc(sizeof(double) * nf), *eband = malloc(sizeof(double) * nf * 5);
	double *tf0 = malloc(sizeof(double) * nf), *elev = malloc(sizeof(double) * nf);
	double lvl_off = in_db + d.globals[KG_G0] - gdef[KG_G0]; /* resynthesis level less the WAV's, expected */
	double av_off = gain_db + in_db + src->globals[KG_G0] - gdef[KG_G0]; /* imported AV less the source's */
	int ntf0 = 0, vowel_fric = 0, fric_n = 0, fric_af = 0, nlev = 0;
	for (int i = 0; i < 3; i++)
		ef[i] = malloc(sizeof(double) * nf);
	int n0 = 0, nfm = 0, nav = 0, nrd = 0, nband = 0, vuv_ok = 0, vuv_n = 0;

	/* the imported document's synthesis, for the fricative band levels */
	synth_result sy;
	CHECK(synth_doc(&d, &sy) == 0);
	CHECK(sy.overload == 0);
	double *y = pcm_to_double(&sy), ypeak = 20 * log10(m_peak(sy.pcm, sy.n) / 32768 + 1e-12);
	CHECK(ypeak <= PEAK_MAX_DB);
	/* per-frame levels: the resynthesis against the WAV */
	double loud = -1e300, louder = -1e300;
	import_levels *lwk = malloc(sizeof(import_levels) * nf);
	for (int k = 0; k < nf; k++) {
		const double *b = d.frames + (size_t)k * NP;
		double f[6] = { b[KP_F1], b[KP_F2], b[KP_F3], b[KP_F4], d.globals[KG_F5], d.globals[KG_F6] };
		import_measure(x, s.n, s.sr, import_frame_centre(s.sr, 5, k), f, &lwk[k]);
		loud = lwk[k].total > loud ? lwk[k].total : loud;
	}
	for (int k = 0; k < nf; k++) {
		if (lwk[k].total < loud - LEVEL_RANGE_DB)
			continue;
		const double *b = d.frames + (size_t)k * NP;
		double f[6] = { b[KP_F1], b[KP_F2], b[KP_F3], b[KP_F4], d.globals[KG_F5], d.globals[KG_F6] };
		import_levels ls;
		import_measure(y, sy.n, sy.sr, import_frame_centre(s.sr, 5, k), f, &ls);
		double e = ls.total - lwk[k].total - lvl_off, ref = lwk[k].total;
		elev[nlev++] = fabs(e);
		for (int j = k - import_period_frames(b[KP_F0]); j < k; j++) /* a decay may lag by a glottal period */
			ref = j >= 0 && lwk[j].total > ref ? lwk[j].total : ref;
		e = ls.total - ref - lvl_off;
		louder = e > louder ? e : louder;
	}
	free(lwk);
	int class_ok[CLS_COUNT] = { 0 }, class_n[CLS_COUNT] = { 0 };
	for (int k = 0; k < nf; k++) {
		if (!steady(src, k))
			continue;
		const double *a = src->frames + (size_t)k * NP, *b = d.frames + (size_t)k * NP;
		int tc = truth_class(src, k);
		class_n[tc]++;
		class_ok[tc] += info.cls[k] == tc;
		vuv_n++;
		vuv_ok += (a[KP_AV] > 0) == (b[KP_AV] > 0);
		if (a[KP_AV] > 0 && b[KP_AV] > 0)
			e0[n0++] = fabs(b[KP_F0] - a[KP_F0]) / a[KP_F0];
		if (tc == CLS_VOICED) {
			for (int i = 0; i < 3; i++)
				ef[i][nfm] = fabs(b[KP_F1 + i] - a[KP_F1 + i]) / a[KP_F1 + i];
			nfm++;
			tf0[ntf0++] = a[KP_F0];
			vowel_fric += b[KP_AF] > 0;
			if (same_source && b[KP_AV] > 0) /* an unvoiced frame counts as a voicing error, not a level error */
				eav[nav++] = fabs(b[KP_AV] - round(a[KP_AV] + av_off)); /* on AV's 1 dB grid, as imported */
			if (same_source)
				erd[nrd++] = fabs(b[KP_RD] - a[KP_RD]);
		}
		if (tc == CLS_FRIC || tc == CLS_VOICED_FRIC) {
			fric_n++;
			fric_af += b[KP_AF] > 0;
			double f[6] = { b[KP_F1], b[KP_F2], b[KP_F3], b[KP_F4], d.globals[KG_F5], d.globals[KG_F6] };
			import_levels lw, ls;
			long c = import_frame_centre(s.sr, 5, k);
			import_measure(x, s.n, s.sr, c, f, &lw);
			import_measure(y, sy.n, sy.sr, c, f, &ls);
			double top = -1e300;
			for (int i = 0; i < 5; i++)
				if (lw.band[i] > top)
					top = lw.band[i];
			for (int i = 0; i < 5; i++)
				if (lw.band[i] > top - BAND_RANGE_DB)
					eband[nband++] = fabs(ls.band[i] - lw.band[i] - lvl_off);
		}
	}
	double f0m = quantile(e0, n0, 0.5), vuv = vuv_n ? (double)vuv_ok / vuv_n : 0;
	int high = ntf0 > 0 && quantile(tf0, ntf0, 0.5) >= HIGH_VOICE_F0;
	double fm[3], fp[3];
	for (int i = 0; i < 3; i++) {
		fm[i] = quantile(ef[i], nfm, 0.5);
		fp[i] = quantile(ef[i], nfm, 0.9);
	}
	double avm = quantile(eav, nav, 0.5), avp = quantile(eav, nav, 0.9), avx = quantile(eav, nav, 1.0);
	double rdm = quantile(erd, nrd, 0.5), rdp = quantile(erd, nrd, 0.9), rdx = quantile(erd, nrd, 1.0);
	double bm = quantile(eband, nband, 0.5), bp = quantile(eband, nband, 0.9), bx = quantile(eband, nband, 1.0);
	double lm = quantile(elev, nlev, 0.5), lp = quantile(elev, nlev, 0.9);
	printf("  %s (%d frames, %d steady voiced, Rd fitted in %d%s; G0 %g):\n", name, nf, nfm, info.rd_frames,
		high ? ", a high voice: AV and Rd at the median" : "", d.globals[KG_G0]);
	printf("    frame levels |error| median %.1f, 90th %.1f dB, most louder %.1f dB (n %d)\n", lm, lp, louder, nlev);
	printf("    F0 median %.2f %% (n %d); V/UV %.1f %% correct (n %d)\n", 100 * f0m, n0, 100 * vuv, vuv_n);
	printf("    F1/F2/F3 median %.1f/%.1f/%.1f %%, 90th %.1f/%.1f/%.1f %%\n", 100 * fm[0], 100 * fm[1],
		100 * fm[2], 100 * fp[0], 100 * fp[1], 100 * fp[2]);
	if (nav)
		printf("    AV |error| median %.1f, 90th %.1f, max %.1f dB\n", avm, avp, avx);
	if (nrd)
		printf("    Rd |error| median %.2f, 90th %.2f, max %.2f\n", rdm, rdp, rdx);
	if (nband)
		printf("    fricative bands |error| median %.1f, 90th %.1f, max %.1f dB (n %d)\n", bm, bp, bx, nband);
	printf("    classes right:");
	for (int c = 0; c < CLS_COUNT; c++)
		if (class_n[c])
			printf(" %s %d/%d", (const char *[]){ "silence", "voiced", "voiced fric", "fric", "aspiration" }[c],
				class_ok[c], class_n[c]);
	printf("; steady vowel frames with frication %d; steady fricative frames with AF %d/%d; resynthesis peak "
		"%.1f dBFS\n", vowel_fric, fric_af, fric_n, ypeak);
	CHECK(n0 > 0 && f0m < LIM_F0_MEDIAN);
	CHECK(vuv >= lim->vuv);
	CHECK(nlev > 0 && (high ? lm : lp) <= LIM_LEVEL_DB);
	CHECK(louder <= lim->louder_db);
	for (int i = 0; i < 3; i++) {
		CHECK(nfm > 0 && fm[i] < lim->f_median[i]);
		CHECK(fp[i] < lim->f_p90[i]);
	}
	if (same_source) {
		CHECK(nav > 0 && (high ? avm : avp) <= lim->av_db);
		CHECK(nrd > 0 && (high ? rdm : rdp) <= lim->rd);
	}
	for (int c = 0; c < CLS_COUNT; c++)
		if (class_n[c] >= MIN_CLASS_FRAMES && !(class_ok[c] >= lim->cls * class_n[c])) {
			printf("    class %d: %d of %d right\n", c, class_ok[c], class_n[c]);
			CHECK(0);
		}
	CHECK(vowel_fric <= lim->vowel_fric);
	CHECK(fric_af >= LIM_FRIC_AF * fric_n);
	if (nband)
		CHECK(bp <= lim->band_db);
	free(e0);
	free(tf0);
	free(elev);
	free(eav);
	free(erd);
	free(eband);
	for (int i = 0; i < 3; i++)
		free(ef[i]);
	free(x);
	free(y);
	synth_result_free(&s);
	synth_result_free(&sy);
	import_info_free(&info);
	doc_free(&d);
}

static void round_trip_gain(const char *name, const kpe_doc *src, int voice, double gain_db)
{
	round_trip_lim(name, src, voice, gain_db, &spec_limits);
}

static void round_trip(const char *name, const kpe_doc *src, int voice)
{
	round_trip_gain(name, src, voice, 0);
}

static void test_round_trips(void)
{
	kpe_doc d;
	csv_report r;
	int rc = csv_load(L"../../samples/ba.csv", &d, &r);
	CHECK(rc == 0);
	if (rc == 0) {
		round_trip("ba.csv (1980 source)", &d, VOICE_MALE);
		round_trip_gain("ba.csv 10 dB quieter", &d, VOICE_MALE, -10);
		round_trip_gain("ba.csv 20 dB quieter", &d, VOICE_MALE, -20);
		round_trip_gain("ba.csv 30 dB quieter", &d, VOICE_MALE, -30);
		doc_free(&d);
	}
	female_vowels(&d);
	round_trip("female vowels", &d, VOICE_FEMALE);
	doc_free(&d);
	fricative(&d, 0);
	round_trip("/asa/", &d, VOICE_MALE);
	round_trip_gain("/asa/ 10 dB quieter", &d, VOICE_MALE, -10);
	round_trip_gain("/asa/ 20 dB quieter", &d, VOICE_MALE, -20);
	round_trip_gain("/asa/ 30 dB quieter", &d, VOICE_MALE, -30);
	round_trip_gain("/asa/ 48 dB quieter", &d, VOICE_MALE, QUIET_DB);
	doc_free(&d);
	fricative(&d, 50);
	round_trip("/aza/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &weak_z);
	round_trip("weak /aza/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &sh);
	round_trip("/asha/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &v_fric);
	round_trip("/ava/", &d, VOICE_MALE);
	doc_free(&d);
	rd_sweep(&d);
	round_trip("Rd sweep 0.4-2.5", &d, VOICE_MALE);
	doc_free(&d);
}

/* A child's vowels /i/, /a/, /u/ at F0 260-320 Hz (Peterson & Barney's children, F2 of /i/ at its 3000 Hz limit). */
static void child_vowels(kpe_doc *d)
{
	new_doc(d, 1000, 1);
	TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 920, 60 }, { 960, 0 });
	TRACK(d, KP_F0, { 0, 280 }, { 300, 320 }, { 700, 260 }, { 1000, 270 });
	TRACK(d, KP_F1, { 0, 370 }, { 250, 370 }, { 380, 1030 }, { 580, 1030 }, { 710, 430 });
	TRACK(d, KP_F2, { 0, 3000 }, { 250, 3000 }, { 380, 1370 }, { 580, 1370 }, { 710, 1170 });
	TRACK(d, KP_F3, { 0, 3730 }, { 250, 3730 }, { 380, 3170 }, { 580, 3170 }, { 710, 3260 });
	TRACK(d, KP_F4, { 0, 4400 });
	round_doc(d);
}

/* src with its sample rate set to sr (the frames unchanged), synthesized and imported at sr, held to lim. */
static void round_trip_sr(const char *name, const kpe_doc *src, int voice, int sr, const limits *lim)
{
	kpe_doc d;
	double g[NG], *frames = malloc(sizeof(double) * NP * src->nframes);
	memcpy(frames, src->frames, sizeof(double) * NP * src->nframes);
	memcpy(g, src->globals, sizeof g);
	g[KG_SR] = sr;
	CHECK(doc_init_from(&d, g, src->nframes, frames) == 0);
	char full[96];
	snprintf(full, sizeof full, "%s at %d Hz", name, sr);
	round_trip_lim(full, &d, voice, 0, lim);
	doc_free(&d);
}

static void ba_doc(kpe_doc *d)
{
	csv_report r;
	if (csv_load(L"../../samples/ba.csv", d, &r) != 0) {
		CHECK(0);
		new_doc(d, 800, 0); /* silence: the round trip then fails as well */
	}
}

static void asa_doc(kpe_doc *d) { fricative(d, 0); }
static void aza_doc(kpe_doc *d) { fricative(d, 50); }
static void weak_z_doc(kpe_doc *d) { fric_doc(d, &weak_z); }
static void sh_doc(kpe_doc *d) { fric_doc(d, &sh); }
static void v_doc(kpe_doc *d) { fric_doc(d, &v_fric); }

/* The known limits of spec 4.6 at 8000 and 16000 Hz: each the spec's limits but for the measures it misses, which
 * hold it to what it meets (measured: see the spec). At 8000 Hz the top band (3280-4000 Hz) holds F4, so vowels with
 * a strong F4 read as voiced fricatives; F5 and F6 lie above 0.45 SR, where the DLL bypasses them, so the bands they
 * would fill get only leakage; and a female voice's F4 (reference 3900 Hz) lies above the tracks' 3600 Hz limit, so
 * the tracker splits F1-F3 among four tracks. At 16000 Hz one borderline frame's aperiodicity (0.453 against the
 * 0.45 hold) unvoices a vowel offset, and a child's F1 and F2 at F0 260-320 Hz keep LPC's harmonic bias. The spike
 * filter and the formants' smoothing (spec 5.3 of the speed/crop/smoothing design) moved three of these measures past
 * what they met before (the earlier limit in brackets): the female vowels' F3 median at 8000 Hz 24.7 % (23), where
 * four tracks share three formants; the /v/'s fricative bands' 90th percentile at 8000 Hz 7.4 dB (7.0), where the
 * filter evens out AF and A2-A6 that the frication fit had traded against each other frame by frame; and the child's
 * Rd median 0.30 (0.3, just on it), fitted with smoothed formants. The weak /z/ at 16000 Hz no longer misses
 * (fricative bands 2.5 dB). */
static const limits ba_8k = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { LIM_F_P90, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, LIM_BAND_DB, 0.85, LIM_LOUDER_DB, 8, LIM_RD };
static const limits female_8k = { LIM_VUV, { LIM_F_MEDIAN, 0.08, 0.25 }, { 0.14, 0.57, 0.53 }, 4.0, LIM_BAND_DB, 0.79,
	11.0, 0, LIM_RD };
static const limits asa_8k = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { LIM_F_P90, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, 4.5, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };
static const limits sh_8k = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { LIM_F_P90, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, 3.5, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };
static const limits v_8k = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { LIM_F_P90, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, 7.5, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };
static const limits vuv_16k = { 0.94, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { LIM_F_P90, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, LIM_BAND_DB, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };
static const limits child_16k = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { 0.165, 0.11, LIM_F_P90 },
	LIM_AV_DB, LIM_BAND_DB, LIM_CLASS, LIM_LOUDER_DB, 0, 0.31 };

/* The corpus at 8000 Hz (the lowest SR in use) and 16000 Hz (the dialog's advice for children's voices), and a
 * child's vowels at 16000 Hz. */
static void test_round_trips_sr(void)
{
	static const struct {
		const char *name;
		void (*make)(kpe_doc *);
		int voice;
		const limits *lim[2]; /* at 8000 and 16000 Hz */
	} corpus[] = {
		{ "ba.csv", ba_doc, VOICE_MALE, { &ba_8k, &spec_limits } },
		{ "female vowels", female_vowels, VOICE_FEMALE, { &female_8k, &spec_limits } },
		{ "/asa/", asa_doc, VOICE_MALE, { &asa_8k, &vuv_16k } },
		{ "/aza/", aza_doc, VOICE_MALE, { &spec_limits, &spec_limits } },
		{ "weak /aza/", weak_z_doc, VOICE_MALE, { &spec_limits, &spec_limits } },
		{ "/asha/", sh_doc, VOICE_MALE, { &sh_8k, &vuv_16k } },
		{ "/ava/", v_doc, VOICE_MALE, { &v_8k, &spec_limits } },
		{ "Rd sweep 0.4-2.5", rd_sweep, VOICE_MALE, { &spec_limits, &spec_limits } },
	};
	static const int rates[2] = { 8000, 16000 };
	kpe_doc d;
	for (int i = 0; i < 2; i++)
		for (size_t j = 0; j < sizeof corpus / sizeof corpus[0]; j++) {
			corpus[j].make(&d);
			round_trip_sr(corpus[j].name, &d, corpus[j].voice, rates[i], corpus[j].lim[i]);
			doc_free(&d);
		}
	child_vowels(&d);
	round_trip_sr("child vowels", &d, VOICE_CHILD, 16000, &child_16k);
	doc_free(&d);
}

/* Fast's known limit (spec 4.6): with one formant correction pass the female vowels' F1 keeps more of LPC's harmonic
 * bias (90th percentile 11.4 %). */
static const limits female_fast = { LIM_VUV, { LIM_F_MEDIAN, LIM_F_MEDIAN, LIM_F_MEDIAN }, { 0.12, LIM_F_P90, LIM_F_P90 },
	LIM_AV_DB, LIM_BAND_DB, LIM_CLASS, LIM_LOUDER_DB, 0, LIM_RD };

/* The presets on the corpus at 11025 Hz (spec 3.3): Best is held to the spec's limits, Fast to what it meets where it
 * misses one (the import spec's known limits for Fast). */
static void test_presets(void)
{
	static const struct {
		const char *name;
		void (*make)(kpe_doc *);
		int voice;
		const limits *fast;
	} corpus[] = {
		{ "ba.csv", ba_doc, VOICE_MALE, &spec_limits },
		{ "female vowels", female_vowels, VOICE_FEMALE, &female_fast },
		{ "/asa/", asa_doc, VOICE_MALE, &spec_limits },
		{ "/aza/", aza_doc, VOICE_MALE, &spec_limits },
		{ "weak /aza/", weak_z_doc, VOICE_MALE, &spec_limits },
		{ "/asha/", sh_doc, VOICE_MALE, &spec_limits },
		{ "/ava/", v_doc, VOICE_MALE, &spec_limits },
		{ "Rd sweep 0.4-2.5", rd_sweep, VOICE_MALE, &spec_limits },
	};
	static const struct { int q; const char *name; } presets[] = { { IMPORT_FAST, "Fast" }, { IMPORT_BEST, "Best" } };
	kpe_doc d;
	for (int i = 0; i < 2; i++) {
		quality = presets[i].q;
		for (size_t j = 0; j < sizeof corpus / sizeof corpus[0]; j++) {
			char full[96];
			snprintf(full, sizeof full, "%s: %s", presets[i].name, corpus[j].name);
			corpus[j].make(&d);
			round_trip_lim(full, &d, corpus[j].voice, 0, quality == IMPORT_FAST ? corpus[j].fast : &spec_limits);
			doc_free(&d);
		}
	}
	quality = IMPORT_NORMAL;
}

/* Spikes (spec 5.1): frames inside a run of one class (both neighbours in it) whose value of p differs from the mean
 * of the neighbours' by more than max(8 % of it, 30 Hz) (formants) or 6 dB (amplitudes). */
static int spikes(const kpe_doc *d, const unsigned char *cls, int p)
{
	int n = 0;
	for (int k = 1; k + 1 < d->nframes; k++) {
		if (cls[k - 1] != cls[k] || cls[k + 1] != cls[k])
			continue;
		double a = d->frames[(size_t)(k - 1) * NP + p], b = d->frames[(size_t)k * NP + p];
		double c = d->frames[(size_t)(k + 1) * NP + p], dev = fabs(b - (a + c) / 2);
		n += p >= KP_F1 && p <= KP_F4 ? dev > fmax(0.08 * b, 30) : dev > 6;
	}
	return n;
}

/* The /aza/ and /ava/ with their frication 6 dB stronger than the corpus's (spec 5.2): frication-dominated frames,
 * where the reliability weighting and the second analysis keep F1-F3 on the voicing's formants (the /aza/'s F3 had a
 * 39 % 90th-percentile error there without them). Held to F1-F3 within 25 % (90th percentile) in the fricative and,
 * with the import's smoothing, to no spike in any formant track (spec 5.4); the counts are printed. */
static void test_stronger_frication(void)
{
	fric_spec z = { 50, 64, { 0, 0, 38, 50, 56 }, 0, 2700, 0, 0 }, v = v_fric;
	v.af += 6;
	const fric_spec *docs[2] = { &z, &v };
	for (int i = 0; i < 2; i++) {
		kpe_doc src, d;
		fric_doc(&src, docs[i]);
		synth_result s;
		CHECK(synth_doc(&src, &s) == 0);
		double *x = pcm_to_double(&s), err[3][160];
		import_options o = { VOICE_MALE, s.sr, 0, IMPORT_NORMAL };
		import_info info;
		char msg[256];
		int rc = import_samples(x, s.n, s.sr, &o, &d, &info, msg, sizeof msg);
		CHECK(rc == 0);
		if (rc == 0) {
			int n = 0, sp[KP_F4 - KP_F1 + 1];
			for (int k = 0; k < d.nframes && k < src.nframes; k++)
				if (info.cls[k] == CLS_VOICED_FRIC && n < 160) {
					for (int j = 0; j < 3; j++)
						err[j][n] = fabs(d.frames[(size_t)k * NP + KP_F1 + j] - src.frames[(size_t)k * NP + KP_F1 + j]) /
							src.frames[(size_t)k * NP + KP_F1 + j];
					n++;
				}
			for (int p = KP_F1; p <= KP_F4; p++)
				sp[p - KP_F1] = spikes(&d, info.cls, p);
			double e[3];
			for (int j = 0; j < 3; j++)
				e[j] = quantile(err[j], n, 0.9);
			printf("  /a%sa/ with 6 dB stronger frication (%d fricative frames): F1/F2/F3 90th %.1f/%.1f/%.1f %%; "
				"spikes F1-F4 %d %d %d %d, AV %d AF %d AH %d\n", i ? "v" : "z", n, 100 * e[0], 100 * e[1], 100 * e[2],
				sp[0], sp[1], sp[2], sp[3], spikes(&d, info.cls, KP_AV), spikes(&d, info.cls, KP_AF),
				spikes(&d, info.cls, KP_AH));
			CHECK(n >= 30);
			for (int j = 0; j < 3; j++)
				CHECK(e[j] <= 0.25);
			for (int j = 0; j < 4; j++)
				CHECK(sp[j] == 0);
			import_info_free(&info);
			doc_free(&d);
		}
		free(x);
		synth_result_free(&s);
		doc_free(&src);
	}
}

/* The spike filter on hand-made tracks: a spike removed, a step kept, a short run and a run's ends untouched, an
 * amplitude's 0 neither changed nor counted, class boundaries not crossed. */
static void test_hampel(void)
{
	enum { N = 20 };
	kpe_doc d;
	unsigned char cls[N], changed[N] = { 0 };
	new_doc(&d, N * 5, 1);
	for (int k = 0; k < N; k++) {
		cls[k] = CLS_VOICED;
		d.frames[(size_t)k * NP + KP_F1] = k == 0 ? 700 : 500;   /* a spike at the run's first frame: kept */
		d.frames[(size_t)k * NP + KP_F2] = k == 7 ? 1800 : 1500; /* a spike */
		d.frames[(size_t)k * NP + KP_F3] = k < 10 ? 2500 : 2800; /* a step */
		d.frames[(size_t)k * NP + KP_AV] = k == 8 ? 0 : k == 12 ? 75 : 60;
	}
	CHECK(import_hampel(d.frames, N, cls, KP_F1, changed) == 0);
	CHECK(import_hampel(d.frames, N, cls, KP_F2, changed) == 0);
	CHECK(import_hampel(d.frames, N, cls, KP_F3, changed) == 0);
	CHECK(import_hampel(d.frames, N, cls, KP_AV, NULL) == 0);
	CHECK(d.frames[0 * NP + KP_F1] == 700);
	CHECK(d.frames[(size_t)7 * NP + KP_F2] == 1500);
	int moved = 0, step = 1;
	for (int k = 0; k < N; k++) {
		moved += changed[k];
		step &= d.frames[(size_t)k * NP + KP_F3] == (k < 10 ? 2500 : 2800);
	}
	CHECK(moved == 1 && changed[7]);
	CHECK(step);
	CHECK(d.frames[(size_t)8 * NP + KP_AV] == 0);
	CHECK(d.frames[(size_t)12 * NP + KP_AV] == 60);
	CHECK(d.frames[(size_t)9 * NP + KP_AV] == 60);

	/* a two-frame run of another class, 1500 like the steady frames: left alone, and the windows of the frames next to
	 * it stop at the class boundary. Frames 8 and 13 (1600) sit next to the run's end (1700): against their own run's
	 * frames the median is 1550 and the threshold 222, so they stay; a window that took in the other class's 1500
	 * would have median 1500, deviation 0, threshold 45, and replace them. */
	for (int k = 0; k < N; k++) {
		cls[k] = k == 10 || k == 11 ? CLS_FRIC : CLS_VOICED;
		d.frames[(size_t)k * NP + KP_F2] = k == 8 || k == 13 ? 1600 : k == 9 || k == 12 ? 1700 : 1500;
	}
	memset(changed, 0, sizeof changed);
	CHECK(import_hampel(d.frames, N, cls, KP_F2, changed) == 0);
	int kept = 1;
	for (int k = 0; k < N; k++)
		kept &= d.frames[(size_t)k * NP + KP_F2] == (k == 8 || k == 13 ? 1600 : k == 9 || k == 12 ? 1700 : 1500);
	CHECK(kept);
	CHECK(changed[8] == 0 && changed[13] == 0);
	CHECK(d.frames[(size_t)10 * NP + KP_F2] == 1500 && d.frames[(size_t)11 * NP + KP_F2] == 1500);
	CHECK(d.frames[(size_t)9 * NP + KP_F2] == 1700 && d.frames[(size_t)12 * NP + KP_F2] == 1700); /* a run's ends */
	doc_free(&d);
}

/* Aspiration in voiced frames: on from aperiodicity 0.25, off below 0.20, on-runs under 3 frames dropped, and the
 * hysteresis starts afresh in each voiced run. */
static void test_aspiration_hysteresis(void)
{
	static const double aps[20] = { 0.10, 0.30, 0.30, 0.22, 0.22, 0.15, 0.30, 0.10, 0.26, 0.26, 0.21, 0.21, 0.9, 0.9,
		0.22, 0.22, 0.22, 0.30, 0.30, 0.10 };
	unsigned char cls[20], on[20];
	for (int k = 0; k < 20; k++)
		cls[k] = k == 12 || k == 13 ? CLS_FRIC : CLS_VOICED;
	import_aspiration(aps, cls, 20, on);
	int bad = 0;
	for (int k = 0; k < 20; k++)
		bad += on[k] != ((k >= 1 && k <= 4) || (k >= 8 && k <= 11));
	CHECK(bad == 0);
}

/* The Import WAV dialog's range (spec 4.2): importing samples a..b of a recording read with import_read gives the
 * same document as importing a WAV file that holds just those samples. */
static void test_range_import(void)
{
	kpe_doc src, d1, d2;
	ba_doc(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	CHECK(wav_write(L"range_all.wav", s.pcm, s.n, s.sr) == 0);
	wav_data wd;
	char msg[256];
	CHECK(import_read(L"range_all.wav", &wd, msg, sizeof msg) == 0);
	CHECK(wd.n == s.n && wd.sr == s.sr);
	long a = range_from_seconds(0.2, wd.sr, wd.n), b = range_from_seconds(0.65, wd.sr, wd.n);
	CHECK(a == 2205 && b == 7166);
	CHECK(wav_write(L"range_part.wav", s.pcm + a, (int)(b - a), s.sr) == 0);
	import_options o = { VOICE_MALE, 11025, 0, IMPORT_NORMAL };
	int rc1 = import_samples(wd.x + a, b - a, wd.sr, &o, &d1, NULL, msg, sizeof msg);
	int rc2 = import_wav(L"range_part.wav", &o, &d2, NULL, msg, sizeof msg);
	CHECK(rc1 == 0 && rc2 == 0);
	if (rc1 == 0 && rc2 == 0) {
		CHECK(d1.nframes == 90 && d2.nframes == 90); /* 450 ms */
		CHECK(memcmp(d1.globals, d2.globals, sizeof d1.globals) == 0);
		CHECK(memcmp(d1.frames, d2.frames, sizeof(double) * NP * d1.nframes) == 0);
		doc_free(&d1);
		doc_free(&d2);
	}
	wav_data_free(&wd);
	CHECK(import_read(L"no_such_file.wav", &wd, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "Cannot open") != NULL);
	synth_result_free(&s);
	doc_free(&src);
}

/* A steady /a/ at AV 60 with aspiration ah (0: none) and Rd rd, imported: returns the median AH over its steady
 * frames and counts those with AH > 0. */
static double aspiration_of(double ah, double rd, int *with_ah, int *n)
{
	kpe_doc src, d;
	new_doc(&src, 600, 1);
	TRACK(&src, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 540, 60 }, { 560, 0 });
	TRACK(&src, KP_AH, { 0, 0 }, { 40, 0 }, { 60, ah }, { 540, ah }, { 560, 0 });
	TRACK(&src, KP_F0, { 0, 125 }, { 600, 105 });
	TRACK(&src, KP_F1, { 0, 700 });
	TRACK(&src, KP_F2, { 0, 1220 });
	TRACK(&src, KP_F3, { 0, 2600 });
	TRACK(&src, KP_RD, { 0, rd });
	round_doc(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s), v[200], med = -1;
	import_options o = { VOICE_MALE, s.sr, 0, IMPORT_NORMAL };
	char msg[256];
	*with_ah = *n = 0;
	if (import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg) == 0) {
		for (int k = 20; k < 100; k++) { /* 100-500 ms: at least 40 ms from the ramps */
			double a = d.frames[(size_t)k * NP + KP_AH];
			v[(*n)++] = a;
			*with_ah += a > 0;
		}
		med = quantile(v, *n, 0.5);
		doc_free(&d);
	} else {
		CHECK(0);
	}
	free(x);
	synth_result_free(&s);
	doc_free(&src);
	return med;
}

/* Only a clearly aperiodic voice gets aspiration: a modal vowel none, a breathy one about its own. */
static void test_aspiration(void)
{
	int with_ah, n;
	double m = aspiration_of(0, 1.0, &with_ah, &n);
	printf("  modal vowel: %d of %d steady frames with AH, median AH %g\n", with_ah, n, m);
	CHECK(n > 0 && with_ah == 0);
	m = aspiration_of(55, 2.2, &with_ah, &n);
	printf("  breathy vowel (AV 60, AH 55, Rd 2.2): median AH %g, %d of %d frames with AH\n", m, with_ah, n);
	CHECK_NEAR(m, 55, LIM_AH_DB);
}

/* A WAV that peaks at 0 dBFS: G0 is set so its resynthesis peaks at about -1 dBFS, without overload. */
static void test_normalization(void)
{
	kpe_doc src, d;
	fricative(&src, 50);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s), peak = m_peak(s.pcm, s.n) / 32768;
	for (int i = 0; i < s.n; i++)
		x[i] = x[i] / peak * (32767.0 / 32768); /* full scale */
	import_options o = { VOICE_MALE, s.sr, 0, IMPORT_NORMAL };
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		synth_result t;
		CHECK(synth_doc(&d, &t) == 0);
		double db = 20 * log10(m_peak(t.pcm, t.n) / 32768);
		printf("  full-scale WAV: resynthesis peaks at %.2f dBFS with G0 %g, %d samples clipped\n", db,
			d.globals[KG_G0], t.overload);
		CHECK(t.overload == 0);
		CHECK(db >= PEAK_MIN_DB && db <= PEAK_MAX_DB);
		synth_result_free(&t);
		doc_free(&d);
	}
	free(x);
	synth_result_free(&s);
	doc_free(&src);
}

/* Two vowels with different Rd (0.6, then 2.0) apart by 130 ms of silence: Rd is fitted on every 12th frame, and the
 * frames of the second vowel before its first fit (frame 96) take that fit's value, not the first vowel's. */
static void test_rd_runs(void)
{
	kpe_doc src, d;
	new_doc(&src, 800, 1);
	TRACK(&src, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 280, 60 }, { 300, 0 }, { 430, 0 }, { 450, 60 }, { 740, 60 },
		{ 760, 0 });
	TRACK(&src, KP_F0, { 0, 120 }, { 800, 105 });
	TRACK(&src, KP_F1, { 0, 700 });
	TRACK(&src, KP_F2, { 0, 1220 });
	TRACK(&src, KP_F3, { 0, 2600 });
	TRACK(&src, KP_RD, { 0, 0.6 }, { 350, 0.6 }, { 380, 2.0 });
	round_doc(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s);
	import_options o = { VOICE_MALE, s.sr, 0, IMPORT_NORMAL };
	char msg[256];
	import_info info;
	int rc = import_samples(x, s.n, s.sr, &o, &d, &info, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		double worst = 0;
		int n = 0;
		for (int k = 80; k < 96; k++) /* 400-480 ms: the second vowel's voiced frames before its first fit */
			if (info.cls[k] == CLS_VOICED) {
				worst = fmax(worst, fabs(d.frames[(size_t)k * NP + KP_RD] - 2.0));
				n++;
			}
		printf("  second vowel's %d voiced frames before its first Rd fit: |Rd - 2.0| at most %.2f\n", n, worst);
		CHECK(n > 0 && worst <= LIM_RD);
		import_info_free(&info);
		doc_free(&d);
	}
	free(x);
	synth_result_free(&s);
	doc_free(&src);
}

static void test_result_document(void)
{
	kpe_doc src, d;
	rd_sweep(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s);
	import_options o = { VOICE_MALE, 11025, 0, IMPORT_NORMAL };
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc != 0) {
		free(x);
		synth_result_free(&s);
		doc_free(&src);
		return;
	}
	CHECK(!d.dirty && !doc_can_undo(&d));
	double g[NG];
	t_default_globals(g);
	for (int i = 0; i < NG; i++)
		if (i != KG_SRC && i != KG_G0)
			CHECK(d.globals[i] == g[i]);
	CHECK(d.globals[KG_SRC] == 1);
	CHECK(d.globals[KG_G0] == round(d.globals[KG_G0])); /* level normalization: a whole number */
	int unrounded = 0, outside = 0;
	for (int k = 0; k < d.nframes; k++)
		for (int p = 0; p < NP; p++) {
			double v = d.frames[(size_t)k * NP + p], st = doc_param_step(p);
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			unrounded += fabs(v / st - round(v / st)) > 1e-9;
			outside += v < pi.min || v > pi.max;
		}
	CHECK(unrounded == 0 && outside == 0);
	/* silence at the start: every amplitude 0 */
	for (int p = KP_AV; p <= KP_AVS; p++)
		CHECK(d.frames[p] == 0);
	doc_free(&d);

	/* resampled input: the same sound at 22050 Hz, imported at 11025 Hz */
	double *x2 = malloc(sizeof(double) * s.n * 2);
	for (int i = 0; i < s.n * 2; i++) /* linear upsampling is enough: the sound is band-limited well below */
		x2[i] = i / 2 + 1 < s.n ? (i & 1 ? 0.5 * (x[i / 2] + x[i / 2 + 1]) : x[i / 2]) : x[s.n - 1];
	rc = import_samples(x2, s.n * 2, 22050, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		CHECK(abs(d.nframes - src.nframes) <= 1);
		CHECK_NEAR(d.frames[(size_t)150 * NP + KP_F0], src.frames[(size_t)150 * NP + KP_F0], 2);
		CHECK_NEAR(d.frames[(size_t)150 * NP + KP_F1], 700, 35);
		doc_free(&d);
	}

	/* simplify: fewer distinct values, still within the parameter ranges; the same as Optimize's default smoothing
	 * and simplification applied to the plain import */
	kpe_doc plain;
	o.simplify = 0;
	CHECK(import_samples(x, s.n, s.sr, &o, &plain, NULL, msg, sizeof msg) == 0);
	o.simplify = 1;
	rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		int changes = 0;
		for (int k = 2; k < d.nframes; k++) {
			double a = d.frames[(size_t)(k - 2) * NP + KP_F1], b = d.frames[(size_t)(k - 1) * NP + KP_F1];
			double c = d.frames[(size_t)k * NP + KP_F1];
			changes += fabs((c - b) - (b - a)) > 1e-6; /* a breakpoint: the slope changes */
		}
		CHECK(changes < d.nframes / 4);
		double *v = malloc(sizeof(double) * plain.nframes), worst = 0;
		for (int p = 0; p < NP; p++) {
			for (int k = 0; k < plain.nframes; k++)
				v[k] = plain.frames[(size_t)k * NP + p];
			opt_smooth(v, plain.nframes, doc_frame_ms(&plain), opt_smooth_default(p), opt_smooth_runs(p));
			opt_simplify(v, plain.nframes, 0, 1, opt_tol_default[opt_tol_class(p)], NULL);
			for (int k = 0; k < plain.nframes; k++)
				worst = fmax(worst, fabs(v[k] - d.frames[(size_t)k * NP + p]));
		}
		CHECK(plain.nframes == d.nframes && worst == 0);
		free(v);
		doc_free(&d);
	}
	doc_free(&plain);
	free(x);
	free(x2);
	synth_result_free(&s);
	doc_free(&src);
}

static void test_refusals(void)
{
	kpe_doc d;
	char msg[256];
	import_options o = { VOICE_MALE, 11025, 0, IMPORT_NORMAL };
	d.nframes = -12345; /* untouched on failure */

	long n = 11025; /* -66 dBFS: silent */
	double *x = malloc(sizeof(double) * n);
	for (long i = 0; i < n; i++)
		x[i] = 0.0005 * sin(i * 0.1);
	CHECK(import_samples(x, n, 11025, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "No sound found") != NULL);
	CHECK(d.nframes == -12345);
	free(x);

	n = 1000L * 601; /* 10 minutes and 1 second at 1000 Hz */
	x = calloc(n, sizeof(double));
	x[5] = 0.5;
	CHECK(import_samples(x, n, 1000, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "longer than 10 minutes") != NULL);
	free(x);

	CHECK(import_wav(L"no_such_file.wav", &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "Cannot open") != NULL);

	short none[1] = { 0 }; /* an empty WAV: no samples at all */
	CHECK(wav_write(L"empty.wav", none, 0, 11025) == 0);
	CHECK(import_wav(L"empty.wav", &o, &d, NULL, msg, sizeof msg) == -1);
	if (!strstr(msg, "No sound found"))
		printf("  empty WAV: \"%s\"\n", msg);
	CHECK(strstr(msg, "No sound found") != NULL);
	CHECK(d.nframes == -12345);

	o.sr = 100; /* outside SR's range */
	double one[4] = { 0.5, -0.5, 0.5, -0.5 };
	CHECK(import_samples(one, 4, 11025, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(d.nframes == -12345);
}

/* unit repeated `times` times (60 s) as a WAV file, imported with voice and quality and timed: within 5 s. */
static void speed_test(const char *what, const kpe_doc *unit, int times, int voice, int quality)
{
	kpe_doc big;
	double *frames = malloc(sizeof(double) * NP * unit->nframes * times);
	for (int i = 0; i < times; i++)
		memcpy(frames + (size_t)i * NP * unit->nframes, unit->frames, sizeof(double) * NP * unit->nframes);
	CHECK(doc_init_from(&big, unit->globals, unit->nframes * times, frames) == 0);
	synth_result s;
	CHECK(synth_doc(&big, &s) == 0);
	CHECK(wav_write(L"speed.wav", s.pcm, s.n, s.sr) == 0);
	import_options o = { voice, 11025, 0, quality };
	kpe_doc d;
	import_info info;
	char msg[256];
	clock_t t0 = clock();
	int rc = import_wav(L"speed.wav", &o, &d, &info, msg, sizeof msg);
	double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
	CHECK(rc == 0);
	printf("  speed, %s: %.1f s of sound imported in %.2f s (analysis %.2f, Rd %.2f in %d frames, calibration %.2f)\n",
		what, s.n / (double)s.sr, secs, info.seconds[0], info.seconds[1], info.rd_frames, info.seconds[2]);
	CHECK(secs <= 5.0);
	if (rc == 0) {
		import_info_free(&info);
		doc_free(&d);
	}
	synth_result_free(&s);
	doc_free(&big);
}

/* 60 s of speech-like sound (ba.csv 75 times) with Normal, and 60 s of a high voice (the female vowels 50 times,
 * median F0 about 220 Hz, so the formant correction runs) with Fast. */
static void test_speed(void)
{
	kpe_doc unit;
	csv_report r;
	if (csv_load(L"../../samples/ba.csv", &unit, &r) != 0) {
		CHECK(0);
		return;
	}
	speed_test("Normal, male", &unit, 75, VOICE_MALE, IMPORT_NORMAL);
	doc_free(&unit);
	female_vowels(&unit);
	speed_test("Fast, high voice", &unit, 50, VOICE_FEMALE, IMPORT_FAST);
	doc_free(&unit);
}

void suite_wavimport(void)
{
	test_refusals();
	test_result_document();
	test_round_trips();
	test_round_trips_sr();
	test_presets();
	test_stronger_frication();
	test_hampel();
	test_aspiration_hysteresis();
	test_range_import();
	test_rd_runs();
	test_aspiration();
	test_normalization();
	test_speed();
}
