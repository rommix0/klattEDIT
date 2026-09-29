#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

static short buf[40000];

static void test_create_errors(void)
{
	double g[KLATT_NUM_GLOBAL];
	klatt_h h;
	t_default_globals(g);
	CHECK(KlattCreate(NULL, &h) == KLATT_ERR_ARG);
	CHECK(KlattCreate(g, NULL) == KLATT_ERR_ARG);
	CHECK(KlattMaxFrameSamples(NULL) == KLATT_ERR_ARG);
	CHECK(KlattOverloadCount(NULL) == KLATT_ERR_ARG);
	KlattReset(NULL, 1); /* must not crash */
	KlattDestroy(NULL);  /* must not crash */
}

static void test_frame_lengths_add_up(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	CHECK(KlattMaxFrameSamples(h) == 56); /* 5 ms at 11025 Hz = 55.125 samples */
	int total = 0;
	for (int k = 0; k < 160; k++) {
		int n = KlattSynthFrame(h, f, buf, 56);
		CHECK(n == 55 || n == 56);
		total += n;
	}
	CHECK(total == 8820); /* exactly 800 ms */
	CHECK(KlattSynthFrame(h, f, buf, 10) == KLATT_ERR_BUFFER);
	CHECK(KlattSynthFrame(h, NULL, buf, 56) == KLATT_ERR_ARG);
	CHECK(KlattSynthFrame(h, f, NULL, 56) == KLATT_ERR_ARG);
	KlattDestroy(h);
}

static void test_silence_is_exact_zero(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f); /* all amplitudes default to 0, F0 = 100 */
	int n = t_synth_steady(g, f, 100, buf, 40000);
	CHECK(n == 5512);
	CHECK(m_peak(buf, n) == 0);
}

static void test_globals_clamped(void)
{
	double g[KLATT_NUM_GLOBAL];
	klatt_h h;
	t_default_globals(g);
	g[KG_SR] = 50000; /* above the 20000 limit */
	g[KG_FRAME_MS] = 10;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	CHECK(KlattMaxFrameSamples(h) == 200);
	KlattDestroy(h);
}

static void test_frame_values_clamped(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	static short a[6000], b[6000];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_F1] = 99999;
	int na = t_synth_steady(g, f, 100, a, 6000);
	f[KP_F1] = 1300; /* the F1 maximum */
	int nb = t_synth_steady(g, f, 100, b, 6000);
	CHECK(na == nb && memcmp(a, b, sizeof(short) * na) == 0);
}

/* F3: a NaN frame value must clamp to the parameter's minimum, same as any other out-of-range value, and
 * must not raise an invalid-operation FP exception doing it (hosts with unmasked FP exceptions crash on a
 * signaling NaN compare). */
static void test_nan_frame_value_clamped(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	static short a[6000], b[6000];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_F1] = NAN;
	f[KP_AV] = NAN;
	int na = t_synth_steady(g, f, 100, a, 6000);
	f[KP_F1] = 150; /* the F1 minimum */
	f[KP_AV] = 0;   /* the AV minimum */
	int nb = t_synth_steady(g, f, 100, b, 6000);
	CHECK(na == nb && memcmp(a, b, sizeof(short) * na) == 0);
}

/* F1: formants at or above 0.45*SR are bypassed instead of stacking near Nyquist, so low sample rates
 * do not saturate. */
static void check_no_overload_at_sr(double sr, int nfc)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SR] = sr;
	g[KG_NFC] = nfc;
	f[KP_AV] = 60;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	int n = 0;
	int max = KlattMaxFrameSamples(h);
	for (int k = 0; k < 200; k++) /* 1 s of 5 ms frames */
		n += KlattSynthFrame(h, f, buf + n, max);
	CHECK(KlattOverloadCount(h) == 0);
	KlattDestroy(h);
}

static void test_low_sr_no_overload(void)
{
	check_no_overload_at_sr(8000, 5);
	check_no_overload_at_sr(8000, 6);
	check_no_overload_at_sr(10000, 5);
}

static void test_low_sr_formants_still_measured(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME], fm[8];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SR] = 8000;
	f[KP_AV] = 60;
	f[KP_F1] = 500;
	f[KP_F2] = 1500;
	int n = t_synth_steady(g, f, 160, buf, 40000); /* 800 ms at 8000 Hz, 5 ms frames */
	CHECK(n > 0);
	const short *x = buf + 800; /* skip the first 100 ms */
	int m = n - 800;
	int k = m_formants(x, m, 8000, fm, 8);
	CHECK(k >= 2);
	if (k >= 2) {
		CHECK_NEAR(fm[0], 500, 40);
		CHECK_NEAR(fm[1], 1500, 75);
	}
}

void suite_synth_basic(void)
{
	test_create_errors();
	test_frame_lengths_add_up();
	test_silence_is_exact_zero();
	test_globals_clamped();
	test_frame_values_clamped();
	test_nan_frame_value_clamped();
	test_low_sr_no_overload();
	test_low_sr_formants_still_measured();
}
