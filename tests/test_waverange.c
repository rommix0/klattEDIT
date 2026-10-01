/* The Import WAV dialog's range logic: pixel/sample mapping, snapping, clamping, the envelope. */
#include <math.h>
#include "test.h"
#include "waverange.h"

static void test_pixels_and_samples(void)
{
	CHECK(range_sample_at(48000, 400, 0) == 0);
	CHECK(range_sample_at(48000, 400, 200) == 24000);
	CHECK(range_sample_at(48000, 400, 400) == 48000);
	CHECK(range_sample_at(48000, 400, 199.99) == 23998);
	CHECK(range_sample_at(48000, 400, -5) == 0);   /* dragged past the left edge */
	CHECK(range_sample_at(48000, 400, 512) == 48000); /* and the right one */
	CHECK_NEAR(range_pixel_of(48000, 400, 24000), 200, 1e-9);
	CHECK_NEAR(range_pixel_of(48000, 400, 48000), 400, 1e-9);
	CHECK_NEAR(range_pixel_of(0, 400, 0), 0, 0);
}

static void test_snapping(void)
{
	CHECK(range_snap(24023, 48000, 96000) == 24000); /* 500.48 ms -> 500 ms */
	CHECK(range_snap(24025, 48000, 96000) == 24048); /* 500.52 ms -> 501 ms */
	CHECK(range_snap(4433, 44100, 96000) == 4454);   /* 100.52 ms -> 101 ms: 4454.1 samples */
	CHECK(range_snap(-10, 48000, 96000) == 0);
	CHECK(range_snap(96100, 48000, 96000) == 96000);
	CHECK(range_snap(48009, 48000, 48010) == 48000); /* the last whole millisecond... */
	CHECK(range_snap(48010, 48000, 48010) == 48010); /* ...and the end itself */
	long a, b;
	range_order(30011, 10013, 48000, 96000, &a, &b);
	CHECK(a == 10032 && b == 30000); /* 208.6 ms -> 209 ms, 625.2 ms -> 625 ms */
	CHECK(range_from_seconds(0.5, 48000, 96000) == 24000);
	CHECK(range_from_seconds(0.5004, 48000, 96000) == 24000);
	CHECK(range_from_seconds(99, 48000, 96000) == 96000);
	CHECK(range_from_seconds(-1, 48000, 96000) == 0);
	CHECK(range_from_seconds(NAN, 48000, 96000) == 0);
	CHECK(range_long_enough(0, 2400, 48000));  /* 50 ms */
	CHECK(!range_long_enough(0, 2399, 48000));
	CHECK(!range_long_enough(2400, 0, 48000));
}

static void test_envelope(void)
{
	static double x[1000], lo[20], hi[20];
	for (int i = 0; i < 1000; i++)
		x[i] = 0.5 * sin(i * 0.3);
	x[523] = 0.9;
	x[777] = -0.95;
	range_envelope(x, 1000, 10, lo, hi);
	CHECK(hi[5] == 0.9 && lo[7] == -0.95);
	CHECK(hi[4] < 0.51 && lo[4] > -0.51 && hi[4] > 0.49);
	int sane = 1;
	for (int c = 0; c < 10; c++)
		sane &= lo[c] <= hi[c];
	CHECK(sane);
	/* more columns than samples: every column shows the sample under it */
	static const double y[5] = { 0.1, -0.2, 0.3, -0.4, 0.5 };
	range_envelope(y, 5, 20, lo, hi);
	CHECK(lo[0] == 0.1 && hi[0] == 0.1);
	CHECK(lo[9] == 0.3 && hi[9] == 0.3);   /* column 9 covers sample 2 */
	CHECK(lo[19] == 0.5 && hi[19] == 0.5);
	range_envelope(y, 0, 20, lo, hi);
	CHECK(lo[3] == 0 && hi[3] == 0);
}

void suite_waverange(void)
{
	test_pixels_and_samples();
	test_snapping();
	test_envelope();
}
