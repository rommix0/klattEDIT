#include <math.h>
#include <stdlib.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "synthrun.h"
#include "pitch.h"

/* A document of nframes frames at defaults with SRC = 1; the caller sets the frames. */
static void new_doc(kpe_doc *d, int nframes)
{
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = 1;
	CHECK(doc_new(d, g, nframes * 5.0) == 0);
}

static void set(kpe_doc *d, int k, int p, double v)
{
	d->frames[(size_t)k * NP + p] = v;
}

/* Synthesizes d and runs pitch_track with frames centred like the import's; the arrays have d->nframes entries. */
static void track(const kpe_doc *d, double fmin, double fmax, double *f0, double *ap, unsigned char *voiced,
	double **xout, long *nout)
{
	synth_result s;
	CHECK(synth_doc(d, &s) == 0);
	double *x = malloc(sizeof(double) * s.n);
	long *c = malloc(sizeof(long) * d->nframes);
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	for (int k = 0; k < d->nframes; k++)
		c[k] = (synth_frame_start(d, k, 1) + synth_frame_start(d, k + 1, 1)) / 2;
	CHECK(pitch_track(x, s.n, s.sr, c, d->nframes, fmin, fmax, f0, ap, voiced) == 0);
	free(c);
	if (xout) {
		*xout = x;
		*nout = s.n;
	} else {
		free(x);
	}
	synth_result_free(&s);
}

static void test_steady_vowel(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	for (int k = 0; k < 100; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 120);
		set(&d, k, KP_F1, 700);
		set(&d, k, KP_F2, 1250);
		set(&d, k, KP_F3, 2600);
	}
	double f0[100], ap[100];
	unsigned char v[100];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	int bad = 0;
	for (int k = 5; k < 95; k++)
		bad += !v[k] || fabs(f0[k] - 120) > 1.2 || ap[k] > 0.2;
	CHECK(bad == 0);
	CHECK_NEAR(f0[50], 120, 1.2);
	doc_free(&d);
}

static void test_glide(void)
{
	kpe_doc d;
	new_doc(&d, 200);
	for (int k = 0; k < 200; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 90 + 110.0 * k / 199);
	}
	double f0[200], ap[200];
	unsigned char v[200];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	double worst = 0;
	for (int k = 5; k < 195; k++) {
		double want = 90 + 110.0 * k / 199, e = v[k] ? fabs(f0[k] - want) / want : 1;
		if (e > worst)
			worst = e;
	}
	if (!(worst < 0.02))
		printf("  glide: worst F0 error %.1f %%\n", 100 * worst);
	CHECK(worst < 0.02);
	doc_free(&d);

	new_doc(&d, 120); /* a female-range fall with the female search range */
	for (int k = 0; k < 120; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 330 - 130.0 * k / 119);
		set(&d, k, KP_F1, 850);
		set(&d, k, KP_F2, 1650);
		set(&d, k, KP_F3, 2900);
	}
	track(&d, 120, 400, f0, ap, v, NULL, NULL);
	worst = 0;
	for (int k = 5; k < 115; k++) {
		double want = 330 - 130.0 * k / 119, e = v[k] ? fabs(f0[k] - want) / want : 1;
		if (e > worst)
			worst = e;
	}
	if (!(worst < 0.02))
		printf("  female fall: worst F0 error %.1f %%\n", 100 * worst);
	CHECK(worst < 0.02);
	doc_free(&d);
}

/* silence, vowel, voiceless fricative, vowel, aspiration, silence: 30 frames each */
static void test_voicing_sequence(void)
{
	enum { SEG = 30, N = 6 * SEG };
	kpe_doc d;
	new_doc(&d, N);
	for (int k = 0; k < N; k++) {
		int seg = k / SEG;
		set(&d, k, KP_F0, 110);
		set(&d, k, KP_AV, seg == 1 || seg == 3 ? 60 : 0);
		set(&d, k, KP_AF, seg == 2 ? 60 : 0);
		set(&d, k, KP_A5, 50);
		set(&d, k, KP_A6, 55);
		set(&d, k, KP_AH, seg == 4 ? 55 : 0);
	}
	double f0[N], ap[N];
	unsigned char v[N];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	int wrong = 0, counted = 0;
	for (int k = 0; k < N; k++) {
		int pos = k % SEG;
		if (pos < 4 || pos >= SEG - 4) /* at least 20 ms (half the YIN window) from a boundary */
			continue;
		int seg = k / SEG, want = seg == 1 || seg == 3;
		counted++;
		if (v[k] != want) {
			wrong++;
			printf("  frame %d (segment %d): voiced %d, ap %.2f\n", k, seg, v[k], ap[k]);
		}
		if (want)
			CHECK_NEAR(f0[k], 110, 2.2);
		else
			CHECK(f0[k] == 0);
	}
	CHECK(counted > 100 && wrong == 0);
	doc_free(&d);
}

/* Diplophonia makes every second pulse weaker and later, so the signal really repeats every two periods: in
 * those 60 ms the deepest dip is an octave down (DI = 10 already does it; from about DI = 20 the true period is
 * no longer a dip at all), and only the frames around them say otherwise. */
static void test_octave_trap(void)
{
	enum { N = 120 };
	kpe_doc d;
	new_doc(&d, N);
	for (int k = 0; k < N; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 120);
		set(&d, k, KP_DI, k >= 54 && k < 66 ? 10 : 0);
	}
	double f0[N], ap[N], *x;
	long n;
	unsigned char v[N];
	track(&d, 60, 250, f0, ap, v, &x, &n);
	int trapped = 0;
	for (int k = 56; k < 64; k++) {
		pitch_frame pf;
		long c = (synth_frame_start(&d, k, 1) + synth_frame_start(&d, k + 1, 1)) / 2;
		pitch_yin_frame(x, n, 11025, c, 60, 250, &pf);
		trapped += pf.ncand > 0 && fabs(pf.f0[0] - 60) < 3; /* the best single-frame candidate: an octave down */
	}
	if (trapped < 6)
		printf("  only %d of 8 frames trap a single-frame choice\n", trapped);
	CHECK(trapped >= 6);
	int bad = 0;
	for (int k = 5; k < N - 5; k++)
		bad += !v[k] || fabs(f0[k] - 120) > 2.4;
	if (bad)
		printf("  %d frames off 120 Hz\n", bad);
	CHECK(bad == 0);
	free(x);
	doc_free(&d);
}

static void test_single_frame_basics(void)
{
	static double x[4410];
	for (int i = 0; i < 4410; i++)
		x[i] = sin(2 * 3.14159265358979323846 * 150 * i / 11025.0);
	pitch_frame pf;
	pitch_yin_frame(x, 4410, 11025, 2205, 60, 250, &pf);
	CHECK(pf.voiced && pf.ncand >= 1);
	CHECK_NEAR(pf.yin_f0, 150, 0.3);
	CHECK(pf.ap < 0.05);
	for (int i = 0; i < 4410; i++)
		x[i] = 0;
	pitch_yin_frame(x, 4410, 11025, 2205, 60, 250, &pf);
	CHECK(!pf.voiced && pf.ap == 1);
}

void suite_pitch(void)
{
	test_single_frame_basics();
	test_steady_vowel();
	test_glide();
	test_voicing_sequence();
	test_octave_trap();
}
