#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "optimize.h"

static void test_straight_line_keeps_ends(void)
{
	double v[50], err;
	for (int i = 0; i < 50; i++)
		v[i] = 100 + 3 * i;
	CHECK(opt_simplify(v, 50, 0, 1, 1, &err) == 2);
	CHECK_NEAR(err, 0, 1e-9);
	CHECK_NEAR(v[25], 175, 1e-9);
}

static void test_small_wiggles_removed(void)
{
	double v[40], err;
	for (int i = 0; i < 40; i++)
		v[i] = 1500 + ((i % 3) - 1) * 8; /* +-8 Hz around a flat line */
	CHECK(opt_simplify(v, 40, 0, 1, 20, &err) == 2);
	CHECK(err <= 20);
}

static void test_corner_kept(void)
{
	double v[21], orig[21], err;
	for (int i = 0; i <= 20; i++)
		v[i] = orig[i] = i <= 10 ? 500 + 20 * i : 700 - 30 * (i - 10); /* up then down */
	CHECK(opt_simplify(v, 21, 0, 1, 5, &err) == 3);
	CHECK_NEAR(v[10], 700, 0);
	CHECK(memcmp(v, orig, sizeof v) == 0); /* piecewise linear already: nothing changes */
}

static void test_tolerance_respected(void)
{
	double v[200], orig[200], err;
	for (int i = 0; i < 200; i++)
		v[i] = orig[i] = 1500 + 400 * sin(i * 0.05) + 30 * sin(i * 0.9);
	int kept = opt_simplify(v, 200, 0, 1, 20, &err);
	CHECK(kept > 2 && kept < 200);
	CHECK(err <= 20);
	double worst = 0;
	for (int i = 0; i < 200; i++)
		worst = fmax(worst, fabs(v[i] - orig[i]));
	CHECK_NEAR(worst, err, 1e-9);
}

static void test_grid_alignment(void)
{
	double v[31], orig[31];
	for (int i = 0; i < 31; i++)
		v[i] = orig[i] = 100 * sin(i * 0.4);
	/* The range starts at absolute frame 3 with a grid of 2 frames (10 ms at 5 ms frames), so breakpoints may
	 * only be at odd i, plus the ends. */
	opt_simplify(v, 31, 3, 2, 1, NULL);
	CHECK_NEAR(v[0], orig[0], 0);
	CHECK_NEAR(v[30], orig[30], 0);
	/* An off-grid frame is never a breakpoint, so it lies on the line through its two neighbours. */
	for (int i = 2; i < 30; i += 2)
		CHECK_NEAR(v[i], (v[i - 1] + v[i + 1]) / 2, 1e-9);
}

static void test_step_survives(void)
{
	double v[40], err;
	for (int i = 0; i < 40; i++)
		v[i] = i < 20 ? 0 : 60; /* a voicing onset */
	int kept = opt_simplify(v, 40, 0, 1, 1, &err);
	CHECK(kept == 4);
	CHECK_NEAR(v[19], 0, 0);
	CHECK_NEAR(v[20], 60, 0);
	CHECK_NEAR(err, 0, 1e-9);
}

static void test_edge_cases(void)
{
	double one[1] = { 5 }, err = -1;
	CHECK(opt_simplify(one, 1, 0, 1, 1, &err) == 1);
	CHECK(err == 0);
	CHECK(opt_simplify(one, 0, 0, 1, 1, NULL) == 0);
	double two[2] = { 1, 9 };
	CHECK(opt_simplify(two, 2, 0, 4, 1, NULL) == 2);
}

static void test_grid_less_than_one_normalized(void)
{
	double v[10], err;
	for (int i = 0; i < 10; i++)
		v[i] = i;
	int kept = opt_simplify(v, 10, 0, 0, 1, &err); /* grid 0 normalized to 1, same as grid 1 */
	CHECK(kept == 2); /* a straight line: only the two ends are needed */
	CHECK_NEAR(err, 0, 1e-9);
	CHECK_NEAR(v[5], 5, 1e-9);
}

static void test_grid_too_coarse_reports_max_err(void)
{
	double v[9], err;
	/* A sharp corner at frame 4, but grid 8 only allows breakpoints at the two ends (0 and 8). */
	for (int i = 0; i <= 8; i++)
		v[i] = i <= 4 ? 10 * i : 40 - 10 * (i - 4);
	int kept = opt_simplify(v, 9, 0, 8, 1, &err);
	CHECK(kept == 2); /* grid too coarse to add the corner */
	CHECK(err > 1);   /* max_err exceeds the tolerance */
}

static void test_long_jagged_track(void)
{
	/* Stress test: 200000-frame track with alternating values and growing amplitude.
	 * Tests that deep recursion does not cause stack overflow. */
	double *v = malloc(200000 * sizeof(double));
	CHECK(v != NULL);
	for (int i = 0; i < 200000; i++)
		v[i] = (i % 2 ? 1 : -1) * i * 0.001;
	double err;
	int kept = opt_simplify(v, 200000, 0, 1, 1e-9, &err);
	CHECK(kept >= 2 && kept <= 200000);
	CHECK(err <= 1e-9);
	free(v);
}

static void test_smooth_removes_a_spike(void)
{
	double v[40];
	for (int i = 0; i < 40; i++)
		v[i] = 2500;
	v[17] = 3100; /* one frame off: the median takes it out before the Gaussian can spread it */
	CHECK(opt_smooth(v, 40, 5, 20, 0) == 0);
	double worst = 0;
	for (int i = 0; i < 40; i++)
		worst = fmax(worst, fabs(v[i] - 2500));
	CHECK(worst < 1e-9);
}

static void test_smooth_keeps_a_step(void)
{
	double v[60];
	for (int i = 0; i < 60; i++)
		v[i] = i < 30 ? 1200 : 1600;
	CHECK(opt_smooth(v, 60, 5, 15, 0) == 0); /* sigma 1.27 frames: the Gaussian reaches 4 frames */
	int monotonic = 1;
	for (int i = 1; i < 60; i++)
		monotonic &= v[i] >= v[i - 1];
	CHECK(monotonic);
	CHECK_NEAR(v[25], 1200, 1e-9); /* more than 3 sigma from the step: untouched */
	CHECK_NEAR(v[34], 1600, 1e-9);
	CHECK_NEAR(v[29] + v[30], 2800, 1e-9); /* symmetric about the step */
	CHECK(v[29] > 1200 && v[30] < 1600);
}

/* The Gaussian's full width at half maximum, measured on a 3-frame impulse (a single frame would fall to the
 * median) at 1 ms frames: the 3-frame box widens a 60 ms FWHM by well under 1 %. */
static void test_smooth_fwhm(void)
{
	static double v[401];
	memset(v, 0, sizeof v);
	v[199] = v[200] = v[201] = 1000;
	CHECK(opt_smooth(v, 401, 1, 60, 0) == 0);
	double peak = v[200], lo = -1, hi = -1;
	for (int i = 1; i < 401; i++) { /* the half-maximum crossings, linearly interpolated */
		if (v[i - 1] < peak / 2 && v[i] >= peak / 2)
			lo = i - 1 + (peak / 2 - v[i - 1]) / (v[i] - v[i - 1]);
		if (v[i - 1] >= peak / 2 && v[i] < peak / 2)
			hi = i - 1 + (v[i - 1] - peak / 2) / (v[i - 1] - v[i]);
	}
	CHECK_NEAR(hi - lo, 60, 0.6);
	double sum = 0;
	for (int i = 0; i < 401; i++)
		sum += v[i];
	CHECK_NEAR(sum, 3000, 1e-6); /* away from the ends nothing is lost */
	CHECK(v[200 - 80] == 0 && v[200 + 80] == 0); /* truncated at 3 sigma (76 frames) */
}

static void test_smooth_amplitude_runs(void)
{
	double v[50];
	for (int i = 0; i < 50; i++)
		v[i] = i < 10 || i >= 40 ? 0 : 40 + (i % 2) * 6; /* an onset at 10, an offset at 40, +-3 dB wobble */
	CHECK(opt_smooth(v, 50, 5, 20, 1) == 0);
	CHECK(v[9] == 0 && v[40] == 0); /* zeros stay zeros */
	CHECK(v[10] >= 40 && v[39] >= 40); /* the onset and the offset stay sharp: the run is renormalized at its ends */
	double lo = 99, hi = 0;
	for (int i = 14; i < 36; i++) {
		lo = fmin(lo, v[i]);
		hi = fmax(hi, v[i]);
	}
	CHECK(hi - lo < 1); /* the wobble is gone inside the run */
	CHECK_NEAR(lo, 43, 0.5);
}

static void test_smooth_edge_cases(void)
{
	double v[30], orig[30];
	for (int i = 0; i < 30; i++)
		v[i] = orig[i] = 500 + 37 * sin(i);
	CHECK(opt_smooth(v, 30, 5, 0, 0) == 0); /* width 0: off */
	CHECK(memcmp(v, orig, sizeof v) == 0);
	CHECK(opt_smooth(v, 30, 5, -3, 1) == 0);
	CHECK(memcmp(v, orig, sizeof v) == 0);
	for (int i = 0; i < 30; i++)
		v[i] = 1800;
	CHECK(opt_smooth(v, 30, 5, 25, 0) == 0); /* a constant stays constant, ends included */
	for (int i = 0; i < 30; i++)
		CHECK_NEAR(v[i], 1800, 1e-9);
	double two[2] = { 1, 9 };
	CHECK(opt_smooth(two, 2, 5, 25, 0) == 0);
	CHECK_NEAR(two[0] + two[1], 10, 1e-9);
	CHECK(opt_smooth(two, 0, 5, 25, 0) == 0);
}

static void test_smooth_defaults(void)
{
	CHECK(opt_smooth_default(KP_F1) == 10 && opt_smooth_default(KP_F2) == 15);
	CHECK(opt_smooth_default(KP_F3) == 20 && opt_smooth_default(KP_F4) == 25);
	int others = 0;
	for (int p = 0; p < KLATT_NUM_FRAME; p++)
		if (p < KP_F1 || p > KP_F4)
			others += opt_smooth_default(p) != 0;
	CHECK(others == 0);
	CHECK(opt_smooth_runs(KP_AV) && opt_smooth_runs(KP_A3) && opt_smooth_runs(KP_AH));
	CHECK(!opt_smooth_runs(KP_F0) && !opt_smooth_runs(KP_F2) && !opt_smooth_runs(KP_B1) && !opt_smooth_runs(KP_RD));
}

void suite_optimize(void)
{
	test_straight_line_keeps_ends();
	test_small_wiggles_removed();
	test_corner_kept();
	test_tolerance_respected();
	test_grid_alignment();
	test_step_survives();
	test_edge_cases();
	test_grid_less_than_one_normalized();
	test_grid_too_coarse_reports_max_err();
	test_long_jagged_track();
	test_smooth_removes_a_spike();
	test_smooth_keeps_a_step();
	test_smooth_fwhm();
	test_smooth_amplitude_runs();
	test_smooth_edge_cases();
	test_smooth_defaults();
}
