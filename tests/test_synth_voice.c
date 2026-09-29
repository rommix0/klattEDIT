#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

static short buf[40000];

static double tol(double f)
{
	double p = 0.05 * f;
	return p > 40 ? p : 40;
}

/* Synthesizes one second of a steady vowel and checks F0 and F1-F3 measured back. */
static void check_vowel(int sw, double f1, double f2, double f3, double f0)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME], fm[8];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = sw;
	f[KP_AV] = 60;
	f[KP_F0] = f0;
	f[KP_F1] = f1;
	f[KP_F2] = f2;
	f[KP_F3] = f3;
	if (sw)
		f[KP_A1] = f[KP_A2] = f[KP_A3] = f[KP_A4] = 60;
	int n = t_synth_steady(g, f, 200, buf, 40000);
	CHECK(n == 11025);
	const short *x = buf + 1102; /* skip the first 100 ms */
	int m = n - 1102;
	CHECK_NEAR(m_f0(x, m, 11025, 60, 400), f0, 0.02 * f0);
	int k = m_formants(x, m, 11025, fm, 8);
	CHECK(k >= 3);
	if (k >= 3) {
		CHECK_NEAR(fm[0], f1, tol(f1));
		CHECK_NEAR(fm[1], f2, tol(f2));
		CHECK_NEAR(fm[2], f3, tol(f3));
	}
}

static void test_cascade_vowels(void)
{
	check_vowel(0, 500, 1500, 2500, 100); /* neutral */
	check_vowel(0, 700, 1220, 2600, 120); /* /a/ */
	check_vowel(0, 300, 2250, 3000, 100); /* /i/ */
	check_vowel(0, 450, 1000, 2400, 200); /* /u/-like, high F0 */
}

static void test_parallel_vowel(void)
{
	check_vowel(1, 500, 1500, 2500, 100);
}

static void test_level_and_no_overload(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	int n = 0;
	for (int k = 0; k < 200; k++)
		n += KlattSynthFrame(h, f, buf + n, 40000 - n);
	double pk = m_peak(buf + 1102, n - 1102);
	CHECK(pk > 12000 && pk < 22000); /* calibration: about 16000 */
	CHECK(KlattOverloadCount(h) == 0);
	KlattDestroy(h);
}

static void test_overload_counted(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_G0] = 80;
	f[KP_AV] = 80;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	for (int k = 0; k < 50; k++)
		KlattSynthFrame(h, f, buf, 56);
	CHECK(KlattOverloadCount(h) > 0);
	KlattReset(h, 1);
	CHECK(KlattOverloadCount(h) == 0);
	KlattDestroy(h);
}

static void test_sinusoidal_voicing(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AVS] = 60;
	int n = t_synth_steady(g, f, 200, buf, 40000);
	const short *x = buf + 1102;
	int m = n - 1102;
	double ms = m_rms(x, m);
	CHECK(ms > 0);
	CHECK(m_power_at(x, m, 11025, 100) / (ms * ms) > 0.7); /* mostly the fundamental */
}

/* F2: F0 changes take effect within the current period (a phase accumulator, not a period length fixed
 * when the period starts), so a very low F0 does not freeze voicing for up to 1/F0 seconds. */
static void test_low_f0_does_not_freeze_voicing(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_F0] = 1;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	short discard[64];
	for (int k = 0; k < 5; k++)
		CHECK(KlattSynthFrame(h, f, discard, 64) >= 0);
	f[KP_F0] = 200;
	int n = 0;
	for (int k = 0; k < 20; k++)
		n += KlattSynthFrame(h, f, buf + n, 64);
	KlattDestroy(h);
	int win = (int)(0.010 * 11025); /* 10 ms */
	if (win > n) win = n;
	CHECK(m_rms(buf, win) > 100); /* a pulse must show up quickly after F0 jumps up */
}

/* F2: an F0 onset ramp must not leave a silent gap after the first pulse. */
static void test_f0_ramp_has_no_silent_gap(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	int n = 0;
	for (int k = 0; k < 13; k++) { /* 0 -> 120 Hz linear onset ramp */
		f[KP_F0] = 120.0 * k / 13.0;
		n += KlattSynthFrame(h, f, buf + n, 64);
	}
	f[KP_F0] = 120;
	for (int k = 0; k < 40; k++) /* steady 120 Hz */
		n += KlattSynthFrame(h, f, buf + n, 64);
	KlattDestroy(h);

	int first = 0;
	while (first < n && buf[first] == 0)
		first++;
	CHECK(first < n);
	int win = (int)(0.020 * 11025); /* 20 ms */
	int gap = 0;
	for (int i = first; i + win <= n; i += 10)
		if (m_rms(buf + i, win) < 5.0)
			gap = 1;
	CHECK(!gap);
}

void suite_synth_voice(void)
{
	test_cascade_vowels();
	test_parallel_vowel();
	test_level_and_no_overload();
	test_overload_counted();
	test_sinusoidal_voicing();
	test_low_f0_does_not_freeze_voicing();
	test_f0_ramp_has_no_silent_gap();
}
