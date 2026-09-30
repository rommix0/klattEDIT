#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "resample.h"

#define PI 3.14159265358979323846

static double *tone(long n, double sr, double f, double amp)
{
	double *x = malloc(sizeof(double) * n);
	for (long i = 0; i < n; i++)
		x[i] = amp * sin(2 * PI * f * i / sr);
	return x;
}

/* RMS over the middle of x, leaving out a tenth at each end (where the filter meets the zero padding). */
static double mid_rms(const double *x, long n)
{
	double s = 0;
	long a = n / 10, b = n - n / 10;
	for (long i = a; i < b; i++)
		s += x[i] * x[i];
	return sqrt(s / (b - a));
}

/* Power of the sinusoid at exactly f over the same middle part (Goertzel), as its RMS. */
static double mid_rms_at(const double *x, long n, double sr, double f)
{
	double re = 0, im = 0;
	long a = n / 10, b = n - n / 10;
	for (long i = a; i < b; i++) {
		re += x[i] * cos(2 * PI * f * i / sr);
		im -= x[i] * sin(2 * PI * f * i / sr);
	}
	return sqrt(2 * (re * re + im * im)) / (b - a);
}

static double db(double v, double ref)
{
	return 20 * log10(v / ref);
}

/* A tone of f Hz resampled from sr_in to sr_out: returns its level change in dB, and checks it stays at f. */
static double tone_gain_db(int sr_in, int sr_out, double f)
{
	long n = sr_in; /* one second */
	double *x = tone(n, sr_in, f, 0.5), *y = NULL;
	CHECK(resample(x, n, sr_in, sr_out, &y) == 0);
	double g = -999;
	if (y) {
		long m = resample_length(n, sr_in, sr_out);
		g = db(mid_rms(y, m), 0.5 / sqrt(2));
		/* all of the output's power is at f: the tone kept its frequency */
		CHECK_NEAR(db(mid_rms_at(y, m, sr_out, f), mid_rms(y, m)), 0, 0.05);
	}
	free(x);
	free(y);
	return g;
}

static void test_lengths(void)
{
	CHECK(resample_length(48000, 48000, 11025) == 11025);
	CHECK(resample_length(1000, 44100, 11025) == 250);
	CHECK(resample_length(7, 3, 2) == 5);  /* 4.67 rounds up */
	CHECK(resample_length(10, 3, 2) == 7); /* 6.67 */
	CHECK(resample_length(0, 48000, 11025) == 0);
	CHECK(resample_length(3000000, 48000, 20000) == 1250000); /* no overflow */
}

static void test_equal_rates_copy_exactly(void)
{
	double *x = tone(5000, 11025, 440, 0.7), *y = NULL;
	x[17] = 0.123456789;
	CHECK(resample(x, 5000, 11025, 11025, &y) == 0);
	CHECK(y && memcmp(x, y, sizeof(double) * 5000) == 0);
	free(x);
	free(y);
}

static void test_tones_keep_level(void)
{
	CHECK_NEAR(tone_gain_db(48000, 11025, 1000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(48000, 11025, 0.9 * 5512.5), 0, 0.1); /* the passband edge */
	CHECK_NEAR(tone_gain_db(44100, 11025, 3000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(16000, 11025, 3000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(16000, 11025, 4961), 0, 0.1);
	CHECK_NEAR(tone_gain_db(22050, 11025, 200), 0, 0.1);
	CHECK_NEAR(tone_gain_db(11025, 16000, 4000), 0, 0.1);         /* upsampling */
	CHECK_NEAR(tone_gain_db(8000, 11025, 0.9 * 4000), 0, 0.1);    /* upsampling: the lower Nyquist is the input's */
}

/* Above the new Nyquist frequency: at least 60 dB down (whatever frequency it aliases to). */
static void test_images_rejected(void)
{
	const struct { int sr_in, sr_out; double f; } c[] = {
		{ 48000, 11025, 5600 }, { 48000, 11025, 6000 }, { 48000, 11025, 15000 }, { 44100, 11025, 21000 },
		{ 16000, 11025, 5700 }, { 16000, 11025, 7900 },
	};
	for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
		long n = c[i].sr_in;
		double *x = tone(n, c[i].sr_in, c[i].f, 0.5), *y = NULL;
		CHECK(resample(x, n, c[i].sr_in, c[i].sr_out, &y) == 0);
		if (y) {
			double g = db(mid_rms(y, resample_length(n, c[i].sr_in, c[i].sr_out)) + 1e-300, 0.5 / sqrt(2));
			if (!(g <= -60))
				printf("  %d -> %d, %g Hz: %.1f dB\n", c[i].sr_in, c[i].sr_out, c[i].f, g);
			CHECK(g <= -60);
		}
		free(x);
		free(y);
	}
}

static void test_bad_arguments(void)
{
	double x[4] = { 0 }, *y = x;
	CHECK(resample(x, 4, 0, 11025, &y) == -1 && y == NULL);
	CHECK(resample(x, 4, 11025, -1, &y) == -1);
	CHECK(resample(x, -1, 11025, 11025, &y) == -1);
}

void suite_resample(void)
{
	test_lengths();
	test_equal_rates_copy_exactly();
	test_tones_keep_level();
	test_images_rejected();
	test_bad_arguments();
}
