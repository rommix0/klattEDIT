/* Speaking-speed tests for DLL 1.2: SPD scales how long each frame lasts and nothing else. */
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

#define SR 11025.0

static short buf[80000];

/* The total length of nframes default frames at speed spd, frame by frame; *longest gets the longest frame. */
static int speed_total(double spd, int nframes, int *longest, int *max_samples)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SPD] = spd;
	if (KlattCreate(g, &h) != KLATT_OK)
		return -1;
	*max_samples = KlattMaxFrameSamples(h);
	*longest = 0;
	int total = 0;
	for (int k = 0; k < nframes; k++) {
		int n = KlattSynthFrame(h, f, buf, 80000);
		if (n < 0) {
			KlattDestroy(h);
			return n;
		}
		if (n > *longest)
			*longest = n;
		total += n;
	}
	KlattDestroy(h);
	return total;
}

static void test_lengths_scale(void)
{
	int longest, per;
	int one = speed_total(1, 101, &longest, &per); /* 101 * 55.125 = 5567.6 samples */
	CHECK(one == 5567);
	CHECK(per == 56);
	CHECK_NEAR(speed_total(2, 101, &longest, &per), one / 2.0, 1);
	CHECK(per == 28); /* 27.5625 samples a frame */
	CHECK_NEAR(speed_total(0.5, 101, &longest, &per), one * 2.0, 1);
	CHECK(per == 111); /* 110.25 */
	CHECK(speed_total(1.5, 160, &longest, &per) == 5880); /* 160 frames of 36.75 samples */
	CHECK(per == 37);
}

static void test_max_frame_samples(void)
{
	int longest, per;
	CHECK(speed_total(0.25, 50, &longest, &per) == 11025); /* 50 frames of 220.5 samples */
	CHECK(per == 221);
	CHECK(longest == per);
	CHECK(speed_total(4, 50, &longest, &per) == 689); /* 13.78 samples a frame */
	CHECK(per == 14);
	CHECK(longest == per);
}

static void test_spd_clamped(void)
{
	int longest, per;
	CHECK(speed_total(10, 50, &longest, &per) == 689); /* above the limit of 4 */
	CHECK(per == 14);
	CHECK(speed_total(0, 50, &longest, &per) == 11025); /* below the limit of 0.25 */
	CHECK(per == 221);
}

/* The same steady vowel at every speed: one second of it, F0 and F1 measured after the first 100 ms. */
static void vowel_at(double spd, double *f0, double *f1)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME], fm[8];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SPD] = spd;
	f[KP_AV] = 60;
	f[KP_F0] = 120;
	f[KP_F1] = 700;
	f[KP_F2] = 1220;
	f[KP_F3] = 2600;
	int n = t_synth_steady(g, f, (int)(200 * spd), buf, 80000);
	CHECK_NEAR(n, 11025, 1);
	*f0 = m_f0(buf + 1102, n - 1102, SR, 60, 400);
	*f1 = m_formants(buf + 1102, n - 1102, SR, fm, 8) >= 1 ? fm[0] : 0;
}

static void test_pitch_and_formants_unchanged(void)
{
	double f0[3], f1[3];
	static const double spd[3] = { 0.5, 1, 2 };
	for (int i = 0; i < 3; i++)
		vowel_at(spd[i], &f0[i], &f1[i]);
	CHECK_NEAR(f1[1], 700, 40);
	for (int i = 0; i < 3; i++) {
		CHECK_NEAR(f0[i], 120, 0.01 * 120);
		CHECK_NEAR(f1[i], f1[1], 0.01 * f1[1]);
	}
}

/* KlattReset starts the frame count over, so the frame lengths repeat from the start. */
static void test_reset_restarts_the_frames(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	int first[8], again[8];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SPD] = 1.5;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	for (int k = 0; k < 8; k++)
		first[k] = KlattSynthFrame(h, f, buf, 80000);
	KlattReset(h, 1);
	for (int k = 0; k < 8; k++)
		again[k] = KlattSynthFrame(h, f, buf, 80000);
	CHECK(memcmp(first, again, sizeof first) == 0);
	CHECK(first[0] == 36 && first[1] == 37); /* floor(36.75) and floor(73.5) - 36 */
	KlattDestroy(h);
}

void suite_synth_speed(void)
{
	test_lengths_scale();
	test_max_frame_samples();
	test_spd_clamped();
	test_pitch_and_formants_unchanged();
	test_reset_restarts_the_frames();
}
