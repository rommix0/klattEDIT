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

void suite_audio(void)
{
	test_synth_doc();
	test_wav_header();
	test_spectrogram_peak();
	test_spectrogram_levels();
}
