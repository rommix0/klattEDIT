#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
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
}
