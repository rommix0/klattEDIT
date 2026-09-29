/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995). See docs/klatt-dll.md for the signal path and calibration. */
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
#define K_NOISE 4.071e-4 /* AH = 60 alone: same RMS as AV = 60 alone */
#endif
#ifndef K_PAR
#define K_PAR   6.0 /* SW = 1, A1-A4 = 60: same RMS as the cascade vowel */
#endif

#define NOISE_POLE 0.9 /* noise low-pass y[n] = x[n] + NOISE_POLE*y[n-1]; with the radiation difference it is flat */
#define K_BYPASS 0.45 /* a formant resonator/antiresonator at or above K_BYPASS*SR is bypassed for the frame */

struct klatt_synth {
	double g[KLATT_NUM_GLOBAL]; /* clamped globals */
	double sr, spf, fmax;       /* sample rate, samples per frame, highest allowed frequency */
	int nfc, parallel_only;
	double out_gain;
	double frame;               /* frames synthesized since the last reset */
	uint32_t rng;
	int overload;

	/* voicing source; av/avs/period change only at the start of a glottal period */
	double period_len, period_pos;
	double av, avs, av_next, avs_next, f0_next;
	reson rgp, rgs;
	antireson rgz;

	double noise_lp;
	double ah, af;              /* linear noise gains of the current frame */

	reson rnp;                  /* cascade nasal pole */
	antireson rnz;              /* cascade nasal zero */
	reson rc[6];                /* cascade formants F1..F6 */
	int bypass_np, bypass_nz, bypass_r[6]; /* set when the resonator's frequency is >= K_BYPASS*sr */

	reson rp[6];                /* parallel formants F1..F6 */
	reson rnp_par;              /* parallel nasal formant */
	double a[6], an, ab;        /* linear parallel gains of the current frame */
	double par_prev;            /* previous parallel voicing input, for its first difference */
	double rad_prev;            /* previous output, for the radiation first difference */
};

static double db_to_lin(double db)
{
	return db <= 0.0 ? 0.0 : pow(10.0, (db - 80.0) / 20.0);
}

static double noise(struct klatt_synth *s)
{
	double sum = 0.0;
	for (int i = 0; i < 4; i++) {
		s->rng = s->rng * 1664525u + 1013904223u;
		sum += (double)(s->rng >> 8) / 16777216.0;
	}
	return sum - 2.0; /* approximately Gaussian, mean 0 */
}

static void clear_reson(reson *r) { r->y1 = r->y2 = 0.0; }
static void clear_anti(antireson *r) { r->x1 = r->x2 = 0.0; }

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
	s->out_gain = K_OUT * pow(10.0, s->g[KG_G0] / 20.0);

	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	antireson_set(&s->rgz, fmin(s->g[KG_FGZ], s->fmax), s->g[KG_BGZ], s->sr);
	reson_set(&s->rgs, s->g[KG_FGP], s->g[KG_BGS], s->sr);
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
	s->frame = 0.0;
	s->rng = seed;
	s->overload = 0;
	s->period_len = s->period_pos = 0.0;
	s->av = s->avs = s->av_next = s->avs_next = s->f0_next = 0.0;
	s->noise_lp = s->par_prev = s->rad_prev = 0.0;
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
	s->f0_next = p[KP_F0];
	s->ah = K_NOISE * db_to_lin(p[KP_AH]);
	s->af = K_NOISE * db_to_lin(p[KP_AF]);
	for (int k = 0; k < 6; k++)
		s->a[k] = K_PAR * db_to_lin(p[KP_A1 + k]);
	s->an = K_PAR * db_to_lin(p[KP_AN]);
	s->ab = K_PAR * db_to_lin(p[KP_AB]);
}

static double sample(struct klatt_synth *s)
{
	/* Voicing: an impulse at the start of each glottal period. */
	double pulse = 0.0;
	if (s->period_len <= 0.0 || s->period_pos >= s->period_len) {
		s->period_pos = s->period_len > 0.0 ? s->period_pos - s->period_len : 0.0;
		s->av = s->av_next;
		s->avs = s->avs_next;
		s->period_len = s->f0_next > 0.0 ? s->sr / s->f0_next : 0.0;
		if (s->period_len > 0.0)
			pulse = 1.0;
	}
	if (s->period_len > 0.0)
		s->period_pos += 1.0;
	double gp = reson_step(&s->rgp, pulse);
	double voice = s->av * antireson_step(&s->rgz, gp) + s->avs * reson_step(&s->rgs, gp);

	/* Noise, halved in the second half of each voiced period. */
	s->noise_lp = noise(s) + NOISE_POLE * s->noise_lp;
	double n = s->noise_lp;
	if (s->period_len > 0.0 && s->av > 0.0 && s->period_pos > 0.5 * s->period_len)
		n *= 0.5;
	double asp = s->ah * n, fric = s->af * n;

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

	/* Radiation: first difference. */
	double sum = casc + par;
	double out = sum - s->rad_prev;
	s->rad_prev = sum;
	return out * s->out_gain;
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
		double v = floor(sample(s) + 0.5);
		if (v > 32767.0) { v = 32767.0; s->overload++; }
		if (v < -32768.0) { v = -32768.0; s->overload++; }
		out[i] = (short)v;
	}
	s->frame += 1.0;
	return n;
}
