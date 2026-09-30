/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995), with the 1.1 extensions: an LF voicing source driven by Rd
 * (Fant 1997; Perrotin et al. 2021), flutter and diplophonia (Klatt & Klatt 1990, JASA 87, 820-857).
 * See docs/klatt-dll.md for the signal path and calibration. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "klatt.h"
#include "klatt_internal.h"
#include "resonator.h"

/* Calibration constants, measured as described in docs/klatt-dll.md. */
#ifndef K_OUT
#define K_OUT   4.125e5 /* neutral vowel, AV = 60, G0 = 47: peak about 16000 */
#endif
#ifndef K_NOISE
#define K_NOISE 2.070e-4 /* AH = 60 alone: same RMS as AV = 60 alone */
#endif
#ifndef K_PAR
#define K_PAR   6.0 /* SW = 1, A1-A4 = 60: same RMS as the cascade vowel */
#endif
#ifndef K_LF
#define K_LF    5.941e-3 /* SRC = 1, Rd = 1: same RMS as the 1980 source's vowel */
#endif

#define K_BYPASS 0.45 /* a formant resonator/antiresonator at or above K_BYPASS*SR is bypassed for the frame */
#define AF_JUMP_DB 50.0 /* an AF rise larger than this jumps at the frame start instead of ramping */

/* One period's LF filter settings: the glottal-formant poles (y = x - a1*y1 - a2*y2), the tilt pole
 * (z = (1-p)*y + p*z1) and the open quotient. */
typedef struct { double a1, a2, p, oq; } lf_shape;

struct klatt_synth {
	double g[KLATT_NUM_GLOBAL]; /* clamped globals */
	double sr, spf, fmax;       /* sample rate, samples per frame, highest allowed frequency */
	int nfc, parallel_only, lf;
	double out_gain;
	double frame;               /* frames synthesized since the last reset */
	double nsamp;               /* samples synthesized since the last reset, for flutter */
	uint32_t rng;
	int overload;

	/* voicing source; phase is the glottal phase in periods, wrapped to [0,1) while voiced, and pinned
	 * at 1.0 while unvoiced so voicing starts with a pulse at once; av/avs/rd/di latch at each pulse */
	double phase;
	double av, avs, rd, di, av_next, avs_next, rd_next, di_next, f0_next;
	double oq;                  /* open quotient of the current period: the LF value, or 0.5 */
	int pulses;                 /* pulses since voicing started, for diplophonia's alternation */
	double pending_at, pending_amp; /* a delayed (diplophonic) pulse: the phase it fires at, or -1 */
	reson rgp, rgs;
	antireson rgz;
	lf_shape lfs;
	double gy1, gy2, tz1;       /* LF glottal-formant and tilt filter state */
	double lf_amp;              /* LF pulse size (lf_norm) */
	double tp80, ty80;          /* 1980 source: Rd's darkening low-pass pole and state (pole 0 = off) */
	double v_prev;              /* previous voicing sample, for the radiation first difference */

	double ah0, ah1, af0, af1;  /* linear noise gains ramped across the frame from *0 to *1 */
	double af_db;               /* AF of the previous frame, for the jump rule */

	reson rnp;                  /* cascade nasal pole */
	antireson rnz;              /* cascade nasal zero */
	reson rc[6];                /* cascade formants F1..F6 */
	int bypass_np, bypass_nz, bypass_r[6]; /* set when the resonator's frequency is >= K_BYPASS*sr */

	reson rp[6];                /* parallel formants F1..F6 */
	reson rnp_par;              /* parallel nasal formant */
	double a[6], an, ab;        /* linear parallel gains of the current frame */
	double par_prev;            /* previous parallel voicing input, for its first difference */
};

static double db_to_lin(double db)
{
	return db <= 0.0 ? 0.0 : pow(10.0, (db - 80.0) / 20.0);
}

/* White, approximately Gaussian noise: the sum of 16 uniform numbers (paper p. 977), centred on zero. */
static double noise(struct klatt_synth *s)
{
	double sum = 0.0;
	for (int i = 0; i < 16; i++) {
		s->rng = s->rng * 1664525u + 1013904223u;
		sum += (double)(s->rng >> 8) / 16777216.0;
	}
	return sum - 8.0;
}

static void clear_reson(reson *r) { r->y1 = r->y2 = 0.0; }
static void clear_anti(antireson *r) { r->x1 = r->x2 = 0.0; }

/* The LF settings for one period at f0 with shape rd (Fant 1997, Eq. A1: Rd -> Ra, Rk, Rg; Perrotin et al.
 * 2021, Eqs. C2-C5: glottal formant Fg, Bg and tilt cutoff Fa). Returns 0 when the values are degenerate. */
static int lf_setup(double sr, double f0, double rd, lf_shape *o)
{
	double t0 = 1.0 / f0;
	double ra = (-1.0 + 4.8 * rd) / 100.0, rk = (22.4 + 11.8 * rd) / 100.0;
	double den = 0.44 * rd - 4.0 * ra * (0.5 + 1.2 * rk);
	if (fabs(den) < 1e-6)
		return 0;
	double rg = rk * (0.5 + 1.2 * rk) / den;
	double oq = (1.0 + rk) / (2.0 * rg), am = 1.0 / (1.0 + rk), ta = ra * t0;
	double tn = tan(KLATT_PI * (1.0 - am));
	if (!isfinite(oq) || !(oq > 0.0) || !(ta > 0.0) || !isfinite(tn) || fabs(tn) < 1e-6)
		return 0;
	double fg = 1.0 / (2.0 * oq * t0), bg = 1.0 / (oq * t0 * tn), fa = 1.0 / (2.0 * KLATT_PI * ta);
	double r = exp(-KLATT_PI * bg / sr);
	o->a1 = -2.0 * r * cos(2.0 * KLATT_PI * fg / sr);
	o->a2 = r * r;
	o->p = exp(-2.0 * KLATT_PI * fa / sr);
	o->oq = oq < 1.0 ? oq : 1.0;
	return 1;
}

/* Magnitude of the LF flow filters (glottal formant times tilt, an lf_shape) at f. */
static double lf_gain(const void *shape, double f, double sr)
{
	const lf_shape *o = shape;
	double w = 2.0 * KLATT_PI * f / sr;
	double re = 1.0 + o->a1 * cos(w) + o->a2 * cos(2.0 * w);
	double im = -(o->a1 * sin(w) + o->a2 * sin(2.0 * w));
	double tr = 1.0 - o->p * cos(w), ti = o->p * sin(w);
	return (1.0 - o->p) / sqrt((re * re + im * im) * (tr * tr + ti * ti));
}

/* The LF source's level at min(3000 Hz, 0.45*SR) relative to its level at f0, in dB (0 if degenerate). */
static double lf_tilt_db(double sr, double f0, double rd)
{
	lf_shape o;
	if (!lf_setup(sr, f0, rd, &o))
		return 0.0;
	return 20.0 * log10(lf_gain(&o, fmin(3000.0, K_BYPASS * sr), sr) / lf_gain(&o, f0, sr));
}

/* The sum over the harmonics of f0 below sr/2 of |H(k*f0)|^2, with H a source's magnitude gain(ctx, f, sr) times
 * the first difference (radiation) and the default cascade (F1-F5 = 500/1500/2500/3500/3750 Hz, B1-B5 =
 * 50/70/110/250/200 Hz): a neutral vowel's power, up to a factor. Callers pass f0 >= 1 Hz, so the sum has at most
 * sr/2 terms. */
static double vowel_sum(double (*gain)(const void *, double, double), const void *ctx, double sr, double f0)
{
	static const double nf[5] = { 500.0, 1500.0, 2500.0, 3500.0, 3750.0 };
	static const double nb[5] = { 50.0, 70.0, 110.0, 250.0, 200.0 };
	reson tract[5];
	for (int i = 0; i < 5; i++)
		reson_set(&tract[i], nf[i], nb[i], sr);
	double sum = 0.0;
	for (double k = 1.0; k * f0 < sr / 2.0; k += 1.0) {
		double f = k * f0, d = 2.0 * sin(KLATT_PI * f / sr), h = gain(ctx, f, sr) * d;
		for (int i = 0; i < 5; i++)
			if (nf[i] < K_BYPASS * sr)
				h *= reson_gain(&tract[i], f, sr);
		sum += h * h;
	}
	return sum;
}

/* The pulse size that gives a neutral vowel the same power for every Rd: a pulse train of size A at f0 through
 * H has power 2*A^2*(f0/sr)^2 * vowel_sum. Choosing that power to be f0/sr (one unit of energy per period, as a
 * unit impulse through a flat path) makes the level depend on F0 the way the 1980 source's does. Evaluated at
 * max(f0, 1 Hz), so a tiny F0 cannot make the sum unbounded. */
static double lf_norm(const lf_shape *o, double sr, double f0)
{
	f0 = fmax(f0, 1.0);
	double sum = vowel_sum(lf_gain, o, sr, f0);
	return sum > 0.0 ? sqrt(sr / (2.0 * f0 * sum)) : 0.0;
}

/* The 1980 source's filters as Rd sets them: RGP, RGZ and the darkening low-pass (1-p)/(1 - p/z). */
typedef struct { reson rgp; antireson rgz; double p; } src80;

static double src80_gain(const void *ctx, double f, double sr)
{
	const src80 *c = ctx;
	double w = 2.0 * KLATT_PI * f / sr;
	double lp = (1.0 - c->p) / sqrt(1.0 - 2.0 * c->p * cos(w) + c->p * c->p);
	return reson_gain(&c->rgp, f, sr) * antireson_gain(&c->rgz, f, sr) * lp;
}

/* The pole p of a unity-DC-gain low-pass (1-p)/(1 - p/z) whose level at fh relative to f0 is q_db (< 0) dB:
 * the root in [0, 1) of (1-q)p^2 - 2(c0 - q*ch)p + (1-q) = 0, with q = 10^(q_db/10), c = cos(2*pi*f/sr). */
static double tilt_pole(double q_db, double sr, double f0, double fh)
{
	double q = pow(10.0, q_db / 10.0);
	double c0 = cos(2.0 * KLATT_PI * f0 / sr), ch = cos(2.0 * KLATT_PI * fh / sr);
	double b = c0 - q * ch, disc = b * b - (1.0 - q) * (1.0 - q);
	if (!(q < 1.0))
		return 0.0;
	if (disc < 0.0) /* more than a one-pole low-pass can give: its steepest */
		return 0.999;
	double p = (b - sqrt(disc)) / (1.0 - q);
	return p > 0.0 && p < 1.0 ? p : 0.0;
}

/* RGP's level at fh relative to its level at f0, in dB, for bandwidth bw. */
static double rgp_tilt_db(double fgp, double bw, double sr, double f0, double fh)
{
	reson r;
	reson_set(&r, fgp, bw, sr);
	return 20.0 * log10(reson_gain(&r, fh, sr) / reson_gain(&r, f0, sr));
}

KLATT_API int KLATT_CALL KlattCreate(const double *globals, klatt_h *out)
{
	if (!globals || !out)
		return KLATT_ERR_ARG;
	struct klatt_synth *s = calloc(1, sizeof *s);
	if (!s)
		return KLATT_ERR_MEMORY;
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++)
		s->g[i] = klatt_clamp(KLATT_GLOBAL, i, globals[i]);
	s->sr = s->g[KG_SR];
	s->spf = s->sr * s->g[KG_FRAME_MS] / 1000.0;
	s->fmax = s->sr / 2.0 - 1.0;
	s->nfc = (int)lround(s->g[KG_NFC]);
	s->parallel_only = s->g[KG_SW] >= 0.5;
	s->lf = s->g[KG_SRC] >= 0.5;
	s->out_gain = K_OUT * pow(10.0, s->g[KG_G0] / 20.0);

	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	antireson_set(&s->rgz, fmin(s->g[KG_FGZ], s->fmax), s->g[KG_BGZ], s->sr);
	reson_set(&s->rgs, 0.0, s->g[KG_BGS], s->sr);
	reson_set(&s->rnp, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rnp_par, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rc[4], fmin(s->g[KG_F5], s->fmax), s->g[KG_B5], s->sr);
	reson_set(&s->rc[5], fmin(s->g[KG_F6], s->fmax), s->g[KG_B6], s->sr);
	s->rp[4] = s->rc[4];
	s->rp[5] = s->rc[5];
	s->bypass_np = s->g[KG_FNP] >= K_BYPASS * s->sr;
	s->bypass_r[4] = s->g[KG_F5] >= K_BYPASS * s->sr;
	s->bypass_r[5] = s->g[KG_F6] >= K_BYPASS * s->sr;
	KlattReset(s, 1);
	*out = s;
	return KLATT_OK;
}

KLATT_API void KLATT_CALL KlattReset(klatt_h s, unsigned seed)
{
	if (!s)
		return;
	s->frame = s->nsamp = 0.0;
	s->rng = seed;
	s->overload = 0;
	s->phase = 1.0;
	s->av = s->avs = s->av_next = s->avs_next = s->f0_next = 0.0;
	s->rd = s->rd_next = 1.0;
	s->di = s->di_next = 0.0;
	s->oq = 0.5;
	s->pulses = 0;
	s->pending_at = -1.0;
	s->pending_amp = 0.0;
	s->gy1 = s->gy2 = s->tz1 = s->lf_amp = s->v_prev = s->tp80 = s->ty80 = 0.0;
	s->lfs = (lf_shape){ 0.0, 0.0, 0.0, 0.5 };
	s->ah0 = s->ah1 = s->af0 = s->af1 = s->af_db = 0.0;
	s->par_prev = 0.0;
	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	clear_reson(&s->rgp);
	clear_reson(&s->rgs);
	clear_anti(&s->rgz);
	clear_reson(&s->rnp);
	clear_anti(&s->rnz);
	clear_reson(&s->rnp_par);
	for (int k = 0; k < 6; k++) {
		clear_reson(&s->rc[k]);
		clear_reson(&s->rp[k]);
	}
}

KLATT_API void KLATT_CALL KlattDestroy(klatt_h s)
{
	free(s);
}

KLATT_API int KLATT_CALL KlattMaxFrameSamples(klatt_h s)
{
	return s ? (int)ceil(s->spf) : KLATT_ERR_ARG;
}

KLATT_API int KLATT_CALL KlattOverloadCount(klatt_h s)
{
	return s ? s->overload : KLATT_ERR_ARG;
}

/* Loads one frame's parameters into the filters and gains. */
static void load_frame(struct klatt_synth *s, const double *in)
{
	double p[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++)
		p[i] = klatt_clamp(KLATT_FRAME, i, in[i]);

	static const int fi[4] = { KP_F1, KP_F2, KP_F3, KP_F4 };
	double bw[4] = { p[KP_B1], p[KP_B2], p[KP_B3], s->g[KG_B4] };
	for (int k = 0; k < 4; k++) {
		s->bypass_r[k] = p[fi[k]] >= K_BYPASS * s->sr;
		reson_set(&s->rc[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
		reson_set(&s->rp[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
	}
	s->bypass_nz = p[KP_FNZ] >= K_BYPASS * s->sr;
	antireson_set(&s->rnz, fmin(p[KP_FNZ], s->fmax), s->g[KG_BNZ], s->sr);

	s->av_next = db_to_lin(p[KP_AV]);
	s->avs_next = db_to_lin(p[KP_AVS]);
	s->rd_next = p[KP_RD];
	s->di_next = p[KP_DI];
	s->f0_next = p[KP_F0];

	/* AF and AH ramp linearly across the frame (paper p. 978), except that an AF rise of more than
	 * AF_JUMP_DB jumps at once, for plosive bursts. */
	s->ah0 = s->ah1;
	s->af0 = s->af1;
	s->ah1 = K_NOISE * db_to_lin(p[KP_AH]);
	s->af1 = K_NOISE * db_to_lin(p[KP_AF]);
	if (p[KP_AF] - s->af_db > AF_JUMP_DB)
		s->af0 = s->af1;
	s->af_db = p[KP_AF];

	for (int k = 0; k < 6; k++)
		s->a[k] = K_PAR * db_to_lin(p[KP_A1 + k]);
	s->an = K_PAR * db_to_lin(p[KP_AN]);
	s->ab = K_PAR * db_to_lin(p[KP_AB]);
}

/* Starts a glottal period at f0: latches the per-pulse values and sets the source filters for the period.
 * Returns the size of the pulse to issue now; a diplophonic pulse is instead scheduled for later. */
static double start_period(struct klatt_synth *s, double f0)
{
	s->av = s->av_next;
	s->avs = s->avs_next;
	s->rd = s->rd_next;
	s->di = s->di_next;
	double amp = 1.0;
	if (s->lf) {
		if (lf_setup(s->sr, f0, s->rd, &s->lfs)) {
			s->lf_amp = lf_norm(&s->lfs, s->sr, f0);
			s->oq = s->lfs.oq;
		} else {
			amp = 0.0;
		}
	} else {
		/* Rd tilts the 1980 source by as much as the LF source's level at fh relative to F0 changes from
		 * Rd = 1 to this Rd. Brighter (Rd < 1): RGP widens, to the bandwidth (up to 0.45*SR, found by
		 * bisection) that gives that change. Darker (Rd > 1): RGP at its default BGP is already as steep
		 * as it gets, so a one-pole low-pass on the voicing gives the change instead. At Rd = 1 the change
		 * is exactly 0: RGP keeps BGP and the low-pass is off, so the output is unchanged. Otherwise the pulse
		 * is scaled so that the neutral vowel keeps the power it has at Rd = 1. */
		double fh = fmin(3000.0, K_BYPASS * s->sr), fgp = s->g[KG_FGP], bw = s->g[KG_BGP];
		double d = lf_tilt_db(s->sr, f0, s->rd) - lf_tilt_db(s->sr, f0, 1.0);
		s->tp80 = 0.0;
		if (d > 0.0) {
			double want = rgp_tilt_db(fgp, bw, s->sr, f0, fh) + d;
			double lo = log(bw), hi = log(K_BYPASS * s->sr);
			for (int i = 0; i < 40; i++) {
				double mid = 0.5 * (lo + hi);
				if (rgp_tilt_db(fgp, exp(mid), s->sr, f0, fh) < want)
					lo = mid; /* a wider RGP is flatter */
				else
					hi = mid;
			}
			bw = exp(0.5 * (lo + hi));
		} else if (d < 0.0) {
			s->tp80 = tilt_pole(d, s->sr, f0, fh);
		}
		reson_set(&s->rgp, fgp, bw, s->sr);
		if (d != 0.0) {
			src80 now = { s->rgp, s->rgz, s->tp80 }, ref = { s->rgp, s->rgz, 0.0 };
			double fe = fmax(f0, 1.0);
			reson_set(&ref.rgp, fgp, s->g[KG_BGP], s->sr);
			amp = sqrt(vowel_sum(src80_gain, &ref, s->sr, fe) / vowel_sum(src80_gain, &now, s->sr, fe));
		}
		s->oq = 0.5;
	}
	/* Diplophonia (Klatt & Klatt 1990, p. 840): every second pulse is delayed by DI% of the closed phase
	 * and attenuated by DI%, so at 100 % it closes as the next pulse opens (and vanishes). */
	s->pulses++;
	if (s->di > 0.0 && s->pulses % 2 == 0) {
		s->pending_at = s->di / 100.0 * (1.0 - s->oq);
		s->pending_amp = amp * (1.0 - s->di / 100.0);
		return 0.0;
	}
	return amp;
}

/* One output sample; frac is how far through the frame this sample is, in (0, 1]. */
static double sample(struct klatt_synth *s, double frac)
{
	/* Voicing: an impulse each time the glottal phase wraps past 1, F0 read fresh every sample so a
	 * change takes effect within the current period instead of waiting for it to finish. Flutter
	 * (Klatt & Klatt 1990, Eq. 1) wobbles F0 by three slow sinusoids. */
	double f0 = s->f0_next;
	if (f0 > 0.0 && s->g[KG_FL] > 0.0) {
		double t = s->nsamp / s->sr;
		f0 += s->g[KG_FL] / 50.0 * f0 / 100.0 *
		      (sin(2.0 * KLATT_PI * 12.7 * t) + sin(2.0 * KLATT_PI * 7.1 * t) + sin(2.0 * KLATT_PI * 4.7 * t));
		f0 = klatt_clamp(KLATT_FRAME, KP_F0, f0);
	}
	s->nsamp += 1.0;

	double pulse = 0.0;
	int voiced = s->f0_next > 0.0;
	if (voiced) {
		s->phase += f0 / s->sr;
		if (s->phase >= 1.0) {
			s->phase -= 1.0;
			if (s->pending_at >= 0.0) { /* a delayed pulse that the new period overtook fires now */
				pulse += s->pending_amp;
				s->pending_at = -1.0;
			}
			pulse += start_period(s, f0 > 0.0 ? f0 : s->f0_next);
		}
		if (s->pending_at >= 0.0 && s->phase >= s->pending_at) {
			pulse += s->pending_amp;
			s->pending_at = -1.0;
		}
	} else {
		s->phase = 1.0;
		s->av = s->av_next;
		s->avs = s->avs_next;
		s->pulses = 0;
		s->pending_at = -1.0;
	}

	/* The voicing source as glottal flow, then its first difference (lip radiation, moved here from the
	 * output so the noise can enter the tract white; see docs/klatt-dll.md). */
	double v;
	if (s->lf) {
		double y = pulse * s->lf_amp - s->lfs.a1 * s->gy1 - s->lfs.a2 * s->gy2;
		s->gy2 = s->gy1;
		s->gy1 = y;
		double z = (1.0 - s->lfs.p) * y + s->lfs.p * s->tz1;
		s->tz1 = z;
		v = K_LF * (s->av * z + s->avs * reson_step(&s->rgs, z));
	} else {
		double gp = reson_step(&s->rgp, pulse);
		double tv = (1.0 - s->tp80) * antireson_step(&s->rgz, gp) + s->tp80 * s->ty80; /* Rd's darkening */
		s->ty80 = tv;
		v = s->av * tv + s->avs * reson_step(&s->rgs, gp);
	}
	double voice = v - s->v_prev;
	s->v_prev = v;

	/* Noise, halved in the closed phase of each voiced period (from the open quotient on). */
	double n = noise(s);
	if (voiced && s->av > 0.0 && s->phase >= s->oq)
		n *= 0.5;
	double asp = (s->ah0 + (s->ah1 - s->ah0) * frac) * n;
	double fric = (s->af0 + (s->af1 - s->af0) * frac) * n;

	/* Cascade branch: voicing and aspiration (unless SW = 1). A resonator/antiresonator whose frequency is
	 * at or above K_BYPASS*sr is bypassed: its input passes through unchanged. */
	double casc = 0.0, par_voice = 0.0;
	if (s->parallel_only) {
		par_voice = voice + asp;
	} else {
		double x = voice + asp;
		if (!s->bypass_np) x = reson_step(&s->rnp, x);
		if (!s->bypass_nz) x = antireson_step(&s->rnz, x);
		for (int k = s->nfc - 1; k >= 0; k--)
			if (!s->bypass_r[k]) x = reson_step(&s->rc[k], x);
		casc = x;
	}

	/* Parallel branch: frication, plus voicing when SW = 1. F1 gets undifferenced voicing only;
	 * the others get the first difference of the voicing plus frication. Signs alternate. A bypassed
	 * resonator contributes nothing. */
	double dv = par_voice - s->par_prev;
	s->par_prev = par_voice;
	double src = dv + fric;
	double t0 = s->bypass_r[0] ? 0.0 : s->a[0] * reson_step(&s->rp[0], par_voice);
	double tn = s->bypass_np ? 0.0 : s->an * reson_step(&s->rnp_par, dv);
	double t1 = s->bypass_r[1] ? 0.0 : s->a[1] * reson_step(&s->rp[1], src);
	double t2 = s->bypass_r[2] ? 0.0 : s->a[2] * reson_step(&s->rp[2], src);
	double t3 = s->bypass_r[3] ? 0.0 : s->a[3] * reson_step(&s->rp[3], src);
	double t4 = s->bypass_r[4] ? 0.0 : s->a[4] * reson_step(&s->rp[4], src);
	double t5 = s->bypass_r[5] ? 0.0 : s->a[5] * reson_step(&s->rp[5], src);
	double par = t0 + tn - t1 + t2 - t3 + t4 - t5 + s->ab * src;

	return (casc + par) * s->out_gain;
}

KLATT_API int KLATT_CALL KlattSynthFrame(klatt_h s, const double *frame, short *out, int max)
{
	if (!s || !frame || !out)
		return KLATT_ERR_ARG;
	int n = (int)(floor((s->frame + 1.0) * s->spf + 1e-9) - floor(s->frame * s->spf + 1e-9));
	if (max < n)
		return KLATT_ERR_BUFFER;
	load_frame(s, frame);
	for (int i = 0; i < n; i++) {
		double v = floor(sample(s, (i + 1.0) / n) + 0.5);
		if (v > 32767.0) { v = 32767.0; s->overload++; }
		if (v < -32768.0) { v = -32768.0; s->overload++; }
		out[i] = (short)v;
	}
	s->frame += 1.0;
	return n;
}
