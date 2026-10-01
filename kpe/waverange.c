/* waverange.c - see waverange.h. */
#include <math.h>
#include "waverange.h"

long range_sample_at(long n, int width, double px)
{
	if (width <= 0 || !(px > 0))
		return 0;
	double s = floor(px * n / width);
	return s >= n ? n : (long)s;
}

double range_pixel_of(long n, int width, long s)
{
	return n > 0 ? (double)s * width / n : 0;
}

long range_snap(long s, int sr, long n)
{
	if (s >= n)
		return n;
	if (s <= 0)
		return 0;
	double ms = floor(s * 1000.0 / sr + 0.5);
	long t = (long)floor(ms * sr / 1000 + 0.5);
	return t > n ? n : t;
}

void range_order(long s0, long s1, int sr, long n, long *a, long *b)
{
	long x = range_snap(s0, sr, n), y = range_snap(s1, sr, n);
	*a = x < y ? x : y;
	*b = x < y ? y : x;
}

long range_from_seconds(double sec, int sr, long n)
{
	if (!(sec > 0))
		return 0;
	double s = floor(sec * sr + 0.5);
	return s >= n ? n : range_snap((long)s, sr, n);
}

int range_long_enough(long a, long b, int sr)
{
	return (b - a) * 1000.0 >= RANGE_MIN_MS * sr;
}

void range_envelope(const double *x, long n, int width, double *lo, double *hi)
{
	for (int c = 0; c < width; c++) {
		if (n <= 0) {
			lo[c] = hi[c] = 0;
			continue;
		}
		long a = range_sample_at(n, width, c), b = range_sample_at(n, width, c + 1);
		if (a >= n)
			a = n - 1;
		if (b <= a)
			b = a + 1;
		lo[c] = hi[c] = x[a];
		for (long i = a + 1; i < b; i++) {
			if (x[i] < lo[c])
				lo[c] = x[i];
			if (x[i] > hi[c])
				hi[c] = x[i];
		}
	}
}
