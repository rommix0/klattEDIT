#include "test.h"
#include "resonator.h"

#define SR 11025.0

/* Peak frequency and -3 dB width of r, searched on a 0.1 Hz grid. */
static void peak_and_width(const reson *r, double *peak_f, double *width)
{
	double best = 0, bf = 0;
	for (double f = 1; f < SR / 2; f += 0.1) {
		double g = reson_gain(r, f, SR);
		if (g > best) { best = g; bf = f; }
	}
	double half = best / sqrt(2.0), lo = bf, hi = bf;
	while (lo > 0.1 && reson_gain(r, lo, SR) > half) lo -= 0.1;
	while (hi < SR / 2 && reson_gain(r, hi, SR) > half) hi += 0.1;
	*peak_f = bf;
	*width = hi - lo;
}

static void test_resonator_peak_bandwidth_dc(void)
{
	static const double cases[][2] = { { 500, 60 }, { 1500, 90 }, { 2500, 150 }, { 3500, 250 } };
	for (int i = 0; i < 4; i++) {
		reson r = { 0 };
		reson_set(&r, cases[i][0], cases[i][1], SR);
		double pf, w;
		peak_and_width(&r, &pf, &w);
		CHECK_NEAR(pf, cases[i][0], cases[i][0] * 0.01);
		CHECK_NEAR(w, cases[i][1], cases[i][1] * 0.02);
		CHECK_NEAR(reson_gain(&r, 0, SR), 1.0, 1e-9);
	}
}

static void test_resonator_step_matches_response(void)
{
	/* A long sine through the resonator settles to the analytic gain. */
	reson r = { 0 };
	reson_set(&r, 1000, 100, SR);
	double peak = 0;
	for (int n = 0; n < 20000; n++) {
		double y = reson_step(&r, sin(2 * KLATT_PI * 1000 * n / SR));
		if (n > 10000 && fabs(y) > peak) peak = fabs(y);
	}
	CHECK_NEAR(peak, reson_gain(&r, 1000, SR), 0.01 * reson_gain(&r, 1000, SR));
}

static void test_antiresonator_notch_and_dc(void)
{
	antireson z = { 0 };
	antireson_set(&z, 400, 100, SR);
	double best = 1e9, bf = 0;
	for (double f = 1; f < SR / 2; f += 0.1) {
		double g = antireson_gain(&z, f, SR);
		if (g < best) { best = g; bf = f; }
	}
	CHECK_NEAR(bf, 400, 4);
	CHECK_NEAR(antireson_gain(&z, 0, SR), 1.0, 1e-9);
	/* The antiresonator exactly undoes the matching resonator. */
	reson r = { 0 };
	reson_set(&r, 400, 100, SR);
	antireson_set(&z, 400, 100, SR);
	double maxerr = 0;
	for (int n = 0; n < 2000; n++) {
		double x = sin(n * 0.37) + 0.5 * cos(n * 1.91);
		double y = antireson_step(&z, reson_step(&r, x));
		if (fabs(y - x) > maxerr) maxerr = fabs(y - x);
	}
	CHECK(maxerr < 1e-9);
}

void suite_resonator(void)
{
	test_resonator_peak_bandwidth_dc();
	test_resonator_step_matches_response();
	test_antiresonator_notch_and_dc();
}
