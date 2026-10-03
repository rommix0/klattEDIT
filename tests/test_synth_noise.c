#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

static short a[40000], b[40000];

static void setup(double *g, double *f)
{
	t_default_globals(g);
	t_default_frame(f);
}

static void test_same_seed_same_output(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	setup(g, f);
	f[KP_AV] = 60;
	f[KP_AH] = 50;
	f[KP_AF] = 50;
	f[KP_A3] = 50;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	int na = 0, nb = 0;
	for (int k = 0; k < 100; k++)
		na += KlattSynthFrame(h, f, a + na, 56);
	KlattReset(h, 1);
	for (int k = 0; k < 100; k++)
		nb += KlattSynthFrame(h, f, b + nb, 56);
	CHECK(na == nb && memcmp(a, b, sizeof(short) * na) == 0);
	KlattReset(h, 2);
	for (int k = 0, n = 0; k < 100; k++)
		n += KlattSynthFrame(h, f, b + n, 56);
	CHECK(memcmp(a, b, sizeof(short) * na) != 0);
	KlattDestroy(h);
}

static void test_aspiration_level(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	f[KP_AV] = 60;
	int n = t_synth_steady(g, f, 200, a, 40000);
	double rv = m_rms(a + 1102, n - 1102);
	f[KP_AV] = 0;
	f[KP_AH] = 60;
	n = t_synth_steady(g, f, 200, b, 40000);
	double ra = m_rms(b + 1102, n - 1102);
	CHECK(ra > 0);
	CHECK_NEAR(20 * log10(ra / rv), 0, 1.5); /* calibration: equal dB, equal level */
}

static void test_frication_is_high_frequency(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	f[KP_AF] = 60;
	f[KP_A5] = 60;
	f[KP_A6] = 60;
	int n = t_synth_steady(g, f, 200, a, 40000);
	CHECK(m_rms(a, n) > 0);
	CHECK(m_band_fraction(a + 1102, n - 1102, 11025, 3000, 5512) > 0.8);
}

/* 1.3: F6 above the old 4999 Hz limit is used as given. Frication through R6 alone at SR 16000 with F6 = 6000 Hz
 * puts most of its energy between 5500 and 6500 Hz (0.88 measured; 0.06 with 1.2's limit of 4999 Hz). */
static void test_f6_above_4999(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	g[KG_SR] = 16000;
	g[KG_F6] = 6000;
	g[KG_B6] = 200;
	f[KP_AF] = 60;
	f[KP_A6] = 60;
	int n = t_synth_steady(g, f, 200, a, 40000);
	CHECK(m_band_fraction(a + 1600, n - 1600, 16000, 5500, 6500) > 0.5);
}

static void test_bypass_passes_frication(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	f[KP_AF] = 60;
	f[KP_AB] = 60;
	int n = t_synth_steady(g, f, 100, a, 40000);
	CHECK(m_rms(a, n) > 0);
}

static void test_nasal_zero_notch(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	f[KP_AV] = 60;
	f[KP_FNZ] = 250; /* equals FNP: pole and zero cancel */
	int n = t_synth_steady(g, f, 200, a, 40000);
	double flat = m_power_at(a + 1102, n - 1102, 11025, 400);
	f[KP_FNZ] = 400;
	n = t_synth_steady(g, f, 200, b, 40000);
	double notched = m_power_at(b + 1102, n - 1102, 11025, 400);
	CHECK(10 * log10(flat / notched) > 10);
}

static void test_parallel_nasal(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	setup(g, f);
	g[KG_SW] = 1;
	f[KP_AV] = 60;
	f[KP_AN] = 60;
	int n = t_synth_steady(g, f, 100, a, 40000);
	CHECK(m_rms(a, n) > 0);
}

void suite_synth_noise(void)
{
	test_same_seed_same_output();
	test_aspiration_level();
	test_frication_is_high_frequency();
	test_f6_above_4999();
	test_bypass_passes_frication();
	test_nasal_zero_notch();
	test_parallel_nasal();
}
