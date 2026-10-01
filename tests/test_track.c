#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "synthrun.h"
#include "track.h"

enum { NF = 60 };

static lpc_cand cands[NF * CAND_MAX];
static int ncand[NF];
static unsigned char quiet[NF];
static double f[NF * TRACK_N], bw[NF * TRACK_N];

static const track_setup male = {
	{ 500, 1500, 2500, 3500 }, { 150, 500, 1300, 2500 }, { 1300, 3000, 4000, 4500 },
};

static void clear(void)
{
	memset(ncand, 0, sizeof ncand);
	memset(quiet, 0, sizeof quiet);
}

static void add(int k, double fr, double b)
{
	lpc_cand *c = cands + k * CAND_MAX;
	int i = ncand[k]++;
	while (i > 0 && c[i - 1].f > fr) {
		c[i] = c[i - 1];
		i--;
	}
	c[i] = (lpc_cand){ fr, b };
}

/* Frames a..b-1 get the four formants given (0: that one missing). */
static void formants(int a, int b, double f1, double f2, double f3, double f4)
{
	for (int k = a; k < b; k++) {
		if (f1) add(k, f1, 60);
		if (f2) add(k, f2, 80);
		if (f3) add(k, f3, 120);
		if (f4) add(k, f4, 200);
	}
}

static void test_clean(void)
{
	clear();
	formants(0, NF, 520, 1480, 2450, 3600);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	int bad = 0;
	for (int k = 0; k < NF; k++)
		bad += f[k * 4] != 520 || f[k * 4 + 1] != 1480 || f[k * 4 + 2] != 2450 || f[k * 4 + 3] != 3600 ||
			bw[k * 4] != 60 || bw[k * 4 + 1] != 80 || bw[k * 4 + 2] != 120;
	CHECK(bad == 0);
}

static void test_missing_candidate(void)
{
	clear();
	for (int k = 0; k < NF; k++) /* F2 rises 1200 -> 1790, and has no candidate in frames 20-24 */
		formants(k, k + 1, 600, k >= 20 && k < 25 ? 0 : 1200 + 10 * k, 2500, 3500);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	for (int k = 18; k < 27; k++) {
		CHECK_NEAR(f[k * 4 + 1], 1200 + 10 * k, 1e-9); /* interpolated across the gap */
		CHECK_NEAR(f[k * 4 + 2], 2500, 0);             /* F3 did not drop into F2's slot */
		CHECK_NEAR(f[k * 4], 600, 0);
	}
}

static void test_spurious_candidates(void)
{
	clear();
	formants(0, NF, 500, 1500, 2500, 3500);
	for (int k = 10; k < 14; k++)
		add(k, 1000, 90);  /* between F1 and F2 */
	add(30, 280, 150);     /* below F1 */
	add(40, 3000, 100);    /* between F3 and F4 */
	add(41, 3000, 100);
	for (int k = 50; k < 52; k++)
		add(k, 2000, 70);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	int bad = 0;
	for (int k = 0; k < NF; k++)
		bad += f[k * 4] != 500 || f[k * 4 + 1] != 1500 || f[k * 4 + 2] != 2500 || f[k * 4 + 3] != 3500;
	CHECK(bad == 0);
}

static void test_silence_interpolates(void)
{
	clear();
	formants(0, 20, 400, 1800, 2600, 3400);
	formants(40, NF, 700, 1200, 2400, 3600);
	for (int k = 20; k < 40; k++) { /* quiet frames with junk candidates */
		quiet[k] = 1;
		add(k, 150 + 37 * k, 300);
		add(k, 2900, 50);
	}
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	for (int k = 19; k <= 40; k++) {
		double t = (k - 19) / 21.0;
		CHECK_NEAR(f[k * 4], 400 + t * 300, 1e-9);
		CHECK_NEAR(f[k * 4 + 1], 1800 - t * 600, 1e-9);
		CHECK_NEAR(f[k * 4 + 3], 3400 + t * 200, 1e-9);
	}
	/* all frames quiet or without candidates: the references */
	clear();
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	CHECK(f[0] == 500 && f[1] == 1500 && f[2] == 2500 && f[3] == 3500);
	CHECK(bw[0] >= 40 && bw[0] <= 500);
}

static void test_clamps_and_bandwidth_smoothing(void)
{
	clear();
	formants(0, NF, 500, 1500, 2500, 3500);
	for (int k = 0; k < NF; k++) {
		lpc_cand *c = cands + k * CAND_MAX;
		c[0].bw = k == 30 ? 900 : 70; /* one wild bandwidth */
		c[1].bw = 20;                 /* below the clamp */
		c[3].f = 4700;                /* above F4's range */
	}
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	CHECK_NEAR(bw[30 * 4], 70, 0);
	CHECK_NEAR(bw[10 * 4 + 1], 40, 0);
	CHECK_NEAR(f[10 * 4 + 3], 4500, 0);
	track_setup low = male;
	low.hi[2] = 2400; /* e.g. 0.45 SR below F3's maximum */
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &low, f, bw) == 0);
	CHECK_NEAR(f[10 * 4 + 2], 2400, 0);
}

static double rel[NF];
static unsigned char conf[NF * CAND_MAX];

/* A candidate at F2's reference (1500 Hz) beside the true F2 at 1900 Hz for 40 frames: worth taking where the frames
 * are reliable, but in unreliable ones continuity keeps F2 where it was. */
static void test_reliability_weighting(void)
{
	clear();
	formants(0, NF, 520, 1900, 2450, 3600);
	for (int k = 10; k < 50; k++)
		add(k, 1500, 80);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	int at_ref = 0;
	for (int k = 10; k < 50; k++)
		at_ref += f[k * 4 + 1] == 1500;
	CHECK(at_ref == 40);
	for (int k = 0; k < NF; k++)
		rel[k] = k >= 10 && k < 50 ? 0.1 : 1;
	CHECK(track_formants(cands, ncand, quiet, rel, NULL, NF, &male, f, bw) == 0);
	int kept = 0;
	for (int k = 0; k < NF; k++)
		kept += f[k * 4] == 520 && f[k * 4 + 1] == 1900 && f[k * 4 + 2] == 2450 && f[k * 4 + 3] == 3600;
	CHECK(kept == NF);
}

/* Two F1 candidates almost equally far from the reference: the confirmed one wins. */
static void test_confirmation(void)
{
	clear();
	formants(0, NF, 450, 1500, 2500, 3500);
	for (int k = 0; k < NF; k++)
		add(k, 560, 60);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	CHECK(f[30 * 4] == 450);
	memset(conf, 0, sizeof conf);
	for (int k = 0; k < NF; k++)
		conf[k * CAND_MAX + 1] = 1; /* 560 Hz, the second candidate */
	CHECK(track_formants(cands, ncand, quiet, NULL, conf, NF, &male, f, bw) == 0);
	int confirmed = 0;
	for (int k = 0; k < NF; k++)
		confirmed += f[k * 4] == 560 && f[k * 4 + 1] == 1500;
	CHECK(confirmed == NF);
}

/* A voice bar at 150 Hz that the analysis misses for three frames, with four other candidates there: a reliable
 * frame must give F1 one of them (it jumps to the clamp at 1300 Hz), an unreliable one may leave F1 empty, and the
 * gap is interpolated. */
static void test_empty_track_when_unreliable(void)
{
	clear();
	for (int k = 0; k < NF; k++)
		formants(k, k + 1, k >= 20 && k < 23 ? 0 : 150, 1450, 2250, 3100);
	for (int k = 0; k < NF; k++)
		add(k, 4100, 300);
	CHECK(track_formants(cands, ncand, quiet, NULL, NULL, NF, &male, f, bw) == 0);
	CHECK(f[21 * 4] == 1300);
	for (int k = 0; k < NF; k++)
		rel[k] = 0.1;
	CHECK(track_formants(cands, ncand, quiet, rel, NULL, NF, &male, f, bw) == 0);
	int kept = 0;
	for (int k = 0; k < NF; k++)
		kept += f[k * 4] == 150 && f[k * 4 + 1] == 1450 && f[k * 4 + 2] == 2250 && f[k * 4 + 3] == 3100;
	CHECK(kept == NF);
}

/* Synthesized F2 glide 1000 -> 2000 Hz under steady F1 and F3: LPC candidates, then the tracker. */
static void test_synthesized_glide(void)
{
	enum { N = 100 };
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = 1;
	kpe_doc d;
	CHECK(doc_new(&d, g, N * 5.0) == 0);
	for (int k = 0; k < N; k++) {
		double *fr = d.frames + (size_t)k * NP;
		fr[KP_AV] = 60;
		fr[KP_F0] = 110;
		fr[KP_F1] = 450;
		fr[KP_F2] = 1000 + 10.0 * k;
		fr[KP_F3] = 2500;
	}
	synth_result s;
	CHECK(synth_doc(&d, &s) == 0);
	double *x = malloc(sizeof(double) * s.n), w[400], a[LPC_MAX_ORDER + 1];
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	static lpc_cand gc[N * CAND_MAX];
	int gn[N], len = lpc_window_len(s.sr), p = lpc_order(s.sr);
	unsigned char gq[N] = { 0 };
	for (int k = 0; k < N; k++) {
		long c = (synth_frame_start(&d, k, 1) + synth_frame_start(&d, k + 1, 1)) / 2;
		lpc_frame(x, s.n, c, len, w);
		lpc_burg(w, len, p, a);
		gn[k] = lpc_candidates(a, p, s.sr, gc + k * CAND_MAX);
	}
	static double gf[N * TRACK_N], gb[N * TRACK_N];
	CHECK(track_formants(gc, gn, gq, NULL, NULL, N, &male, gf, gb) == 0);
	double worst[3] = { 0 };
	for (int k = 5; k < N - 5; k++) {
		double want[3] = { 450, 1000 + 10.0 * k, 2500 };
		for (int i = 0; i < 3; i++) {
			double e = fabs(gf[k * 4 + i] - want[i]) / want[i];
			if (e > worst[i])
				worst[i] = e;
		}
	}
	if (!(worst[0] < 0.05 && worst[1] < 0.05 && worst[2] < 0.05))
		printf("  glide: worst F1 %.1f %%, F2 %.1f %%, F3 %.1f %%\n", 100 * worst[0], 100 * worst[1],
			100 * worst[2]);
	CHECK(worst[0] < 0.05 && worst[1] < 0.05 && worst[2] < 0.05);
	free(x);
	synth_result_free(&s);
	doc_free(&d);
}

void suite_track(void)
{
	test_clean();
	test_missing_candidate();
	test_spurious_candidates();
	test_silence_interpolates();
	test_clamps_and_bandwidth_smoothing();
	test_reliability_weighting();
	test_confirmation();
	test_empty_track_when_unreliable();
	test_synthesized_glide();
}
