/* synthrun, wavout and spectro. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "spectro.h"
#include "synthrun.h"
#include "wavout.h"

static unsigned get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned get16(const unsigned char *p) { return p[0] | p[1] << 8; }

static void test_synth_doc(void)
{
	kpe_doc d;
	synth_result s;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 800) == 0);
	CHECK(synth_doc(&d, &s) == 0);
	CHECK(s.n == 8820 && s.sr == 11025 && s.overload == 0);
	CHECK(m_peak(s.pcm, s.n) == 0); /* a new document is silent */
	synth_result_free(&s);
	doc_paint(&d, KP_AV, 100, 60, 700, 60);
	CHECK(synth_doc(&d, &s) == 0);
	CHECK(m_peak(s.pcm, s.n) > 1000);
	CHECK(m_peak(s.pcm, 1000) == 0); /* still silent before 100 ms */
	synth_result t;
	CHECK(synth_doc(&d, &t) == 0);  /* deterministic */
	CHECK(t.n == s.n && memcmp(t.pcm, s.pcm, sizeof(short) * s.n) == 0);
	synth_result_free(&t);
	synth_result_free(&s);
	doc_free(&d);
}

static void test_wav_header(void)
{
	short pcm[5] = { 0, 1, -1, 32767, -32768 };
	unsigned char buf[64];
	CHECK(wav_write(L"t.wav", pcm, 5, 11025) == 0);
	FILE *f = _wfopen(L"t.wav", L"rb");
	size_t got = fread(buf, 1, sizeof buf, f);
	fclose(f);
	CHECK(got == 54);
	CHECK(memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVEfmt ", 8) == 0 && memcmp(buf + 36, "data", 4) == 0);
	CHECK(get32(buf + 4) == 46);
	CHECK(get32(buf + 16) == 16 && get16(buf + 20) == 1 && get16(buf + 22) == 1);
	CHECK(get32(buf + 24) == 11025 && get32(buf + 28) == 22050);
	CHECK(get16(buf + 32) == 2 && get16(buf + 34) == 16);
	CHECK(get32(buf + 40) == 10);
	CHECK(get16(buf + 44 + 6) == 32767 && get16(buf + 44 + 8) == 0x8000);
	CHECK(wav_write(L"no-such-dir\\t.wav", pcm, 5, 11025) == -1);
}

static void test_spectrogram_peak(void)
{
	static short pcm[11025];
	kpe_spectro s;
	for (int i = 0; i < 11025; i++)
		pcm[i] = (short)(10000 * sin(2 * 3.14159265358979 * 1000 * i / 11025.0));
	double windows[2] = { SPECTRO_WIDE_MS, SPECTRO_NARROW_MS };
	for (int w = 0; w < 2; w++) {
		CHECK(spectro_compute(pcm, 11025, 11025, windows[w], 2, &s) == 0);
		CHECK(s.ncols == 501); /* 0, 2, ... 1000 ms */
		int c = 250, best = 0;
		for (int b = 1; b < s.nbins; b++)
			if (s.db[c * s.nbins + b] > s.db[c * s.nbins + best])
				best = b;
		CHECK_NEAR(best * s.bin_hz, 1000, s.bin_hz);
		spectro_free(&s);
	}
	CHECK(spectro_compute(pcm, 11025, 11025, SPECTRO_WIDE_MS, 2, &s) == 0);
	CHECK_NEAR(s.bin_hz, 11025.0 / 256, 1e-9);
	spectro_free(&s);
	CHECK(spectro_compute(pcm, 11025, 11025, SPECTRO_NARROW_MS, 2, &s) == 0);
	CHECK_NEAR(s.bin_hz, 11025.0 / 1024, 1e-9);
	spectro_free(&s);
	CHECK(spectro_compute(pcm, 100, 11025, 0, 2, &s) == -1);
}

static void test_spectrogram_levels(void)
{
	static short pcm[2205];
	kpe_spectro s;
	for (int i = 1102; i < 2205; i++) /* silence, then a tone */
		pcm[i] = (short)(8000 * sin(i * 0.5));
	CHECK(spectro_compute(pcm, 2205, 11025, SPECTRO_WIDE_MS, 5, &s) == 0);
	unsigned char *lv = malloc((size_t)s.ncols * s.nbins);
	spectro_levels(&s, 70, lv);
	int top = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		if (lv[i] > top)
			top = lv[i];
	CHECK(top == 255);
	CHECK(lv[5 * s.nbins + 10] == 0); /* 25 ms: silence */
	free(lv);
	spectro_free(&s);

	static short quiet[2205]; /* all silence: white, not black */
	CHECK(spectro_compute(quiet, 2205, 11025, SPECTRO_WIDE_MS, 5, &s) == 0);
	lv = malloc((size_t)s.ncols * s.nbins);
	spectro_levels(&s, 50, lv);
	int any = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		any |= lv[i];
	CHECK(any == 0);
	free(lv);
	spectro_free(&s);
}

static void test_synth_frame_start(void)
{
	double frame_mss[3] = { 3.3, 4.1, 6.4 }, spds[3] = { 1, 1.5, 0.3 };
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			kpe_doc d;
			synth_result s;
			double g[NG], spd = spds[j];
			t_default_globals(g);
			g[KG_FRAME_MS] = frame_mss[i];
			CHECK(doc_new(&d, g, 500) == 0);
			CHECK(synth_doc_speed(&d, spd, &s) == 0);
			CHECK(s.n == synth_frame_start(&d, d.nframes, spd));
			int k = d.nframes / 2;
			/* AH (aspiration noise) excites every sample of the frame, unlike AV which is gated by the pitch
			 * period and might not pulse within a single short frame. */
			doc_set(&d, k, KP_AH, 60);
			synth_result_free(&s);
			CHECK(synth_doc_speed(&d, spd, &s) == 0);
			int first_nonzero = -1;
			for (int m = 0; m < s.n; m++)
				if (s.pcm[m] != 0) { first_nonzero = m; break; }
			CHECK(first_nonzero >= 0);
			CHECK(first_nonzero >= synth_frame_start(&d, k, spd));
			CHECK(first_nonzero < synth_frame_start(&d, k + 1, spd));
			synth_result_free(&s);
			doc_free(&d);
		}
	}
}

/* The display synthesis shows the document as written: the document's own SPD does not change it. */
static void test_display_ignores_spd(void)
{
	kpe_doc d;
	synth_result s, t;
	double g[NG];
	t_default_globals(g);
	g[KG_SPD] = 2;
	CHECK(doc_new(&d, g, 800) == 0);
	doc_paint(&d, KP_AV, 100, 60, 700, 60);
	CHECK(synth_doc(&d, &s) == 0);
	CHECK(s.n == 8820);
	CHECK(synth_doc_speed(&d, 1, &t) == 0);
	CHECK(t.n == s.n && memcmp(t.pcm, s.pcm, sizeof(short) * s.n) == 0);
	synth_result_free(&t);
	CHECK(synth_doc_speed(&d, d.globals[KG_SPD], &t) == 0);
	CHECK(t.n == 4410);
	synth_result_free(&t);
	synth_result_free(&s);
	doc_free(&d);
}

/* The playhead: a sample of the sound played at spd maps back to the document time of the frame playing it. */
static void test_synth_sample_ms(void)
{
	kpe_doc d;
	double g[NG];
	t_default_globals(g);
	g[KG_FRAME_MS] = 4.1;
	CHECK(doc_new(&d, g, 500) == 0);
	static const double spds[4] = { 1, 2, 0.5, 1.5 };
	for (int j = 0; j < 4; j++) {
		double spd = spds[j], worst = 0, prev = -1;
		int monotonic = 1;
		for (int k = 0; k <= d.nframes; k++)
			worst = fmax(worst, fabs(synth_sample_ms(&d, synth_frame_start(&d, k, spd), spd) - k * 4.1));
		CHECK(worst < 1e-9);
		for (long n = 0; n < synth_frame_start(&d, d.nframes, spd); n++) {
			double ms = synth_sample_ms(&d, n, spd);
			monotonic &= ms > prev;
			prev = ms;
		}
		CHECK(monotonic);
		/* inside a frame it moves linearly: halfway through frame 10 is halfway through its 4.1 ms */
		long a = synth_frame_start(&d, 10, spd), b = synth_frame_start(&d, 11, spd);
		CHECK_NEAR(synth_sample_ms(&d, a + (b - a) / 2, spd), 41 + 4.1 * ((b - a) / 2) / (double)(b - a), 1e-9);
	}
	CHECK_NEAR(synth_sample_ms(&d, 11025, 1), 1000, 4.1); /* SPD 1: the sample's own time, to within a frame */
	CHECK_NEAR(synth_sample_ms(&d, 11025, 2), 2000, 4.1);
	CHECK_NEAR(synth_sample_ms(&d, 0, 2), 0, 0);
	doc_free(&d);
}

static void test_spectrogram_levels_range_db_guard(void)
{
	static short pcm[2205];
	kpe_spectro s;
	for (int i = 0; i < 2205; i++)
		pcm[i] = (short)(8000 * sin(i * 0.5));
	CHECK(spectro_compute(pcm, 2205, 11025, SPECTRO_WIDE_MS, 5, &s) == 0);
	unsigned char *lv = malloc((size_t)s.ncols * s.nbins);
	CHECK(lv != NULL);
	spectro_levels(&s, 0, lv); /* range_db <= 0 must not divide by zero */
	int top = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		if (lv[i] > top)
			top = lv[i];
	CHECK(top == 255);
	spectro_levels(&s, -10, lv); /* negative too */
	top = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		if (lv[i] > top)
			top = lv[i];
	CHECK(top == 255);
	free(lv);
	spectro_free(&s);
}

void suite_audio(void)
{
	test_synth_doc();
	test_wav_header();
	test_spectrogram_peak();
	test_spectrogram_levels();
	test_synth_frame_start();
	test_display_ignores_spd();
	test_synth_sample_ms();
	test_spectrogram_levels_range_db_guard();
}
