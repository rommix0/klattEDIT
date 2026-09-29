/* resonator.h - Klatt (1980) digital resonator and antiresonator.
 *
 * Resonator:      y[n] = A*x[n] + B*y[n-1] + C*y[n-2]
 *                 C = -exp(-2*pi*BW*T), B = 2*exp(-pi*BW*T)*cos(2*pi*F*T), A = 1 - B - C (unity gain at DC)
 * Antiresonator:  y[n] = A'*x[n] + B'*x[n-1] + C'*x[n-2], with A' = 1/A, B' = -B/A, C' = -C/A
 *                 (the inverse of the resonator with the same F and BW). */
#ifndef KLATT_RESONATOR_H
#define KLATT_RESONATOR_H

#include <math.h>

#define KLATT_PI 3.14159265358979323846

typedef struct { double a, b, c, y1, y2; } reson;
typedef struct { double a, b, c, x1, x2; } antireson;

/* Sets the coefficients; keeps the filter state so parameters can change between frames. */
static inline void reson_set(reson *r, double f, double bw, double sr)
{
	double t = 1.0 / sr;
	r->c = -exp(-2.0 * KLATT_PI * bw * t);
	r->b = 2.0 * exp(-KLATT_PI * bw * t) * cos(2.0 * KLATT_PI * f * t);
	r->a = 1.0 - r->b - r->c;
}

static inline double reson_step(reson *r, double x)
{
	double y = r->a * x + r->b * r->y1 + r->c * r->y2;
	r->y2 = r->y1;
	r->y1 = y;
	return y;
}

static inline void antireson_set(antireson *r, double f, double bw, double sr)
{
	reson t;
	reson_set(&t, f, bw, sr);
	r->a = 1.0 / t.a;
	r->b = -t.b / t.a;
	r->c = -t.c / t.a;
}

static inline double antireson_step(antireson *r, double x)
{
	double y = r->a * x + r->b * r->x1 + r->c * r->x2;
	r->x2 = r->x1;
	r->x1 = x;
	return y;
}

/* Magnitude responses at frequency f, for tests: |A / (1 - B z^-1 - C z^-2)| and |A + B z^-1 + C z^-2|. */
static inline double reson_gain(const reson *r, double f, double sr)
{
	double w = 2.0 * KLATT_PI * f / sr;
	double re = 1.0 - r->b * cos(w) - r->c * cos(2.0 * w);
	double im = r->b * sin(w) + r->c * sin(2.0 * w);
	return fabs(r->a) / sqrt(re * re + im * im);
}

static inline double antireson_gain(const antireson *r, double f, double sr)
{
	double w = 2.0 * KLATT_PI * f / sr;
	double re = r->a + r->b * cos(w) + r->c * cos(2.0 * w);
	double im = -r->b * sin(w) - r->c * sin(2.0 * w);
	return sqrt(re * re + im * im);
}

#endif
