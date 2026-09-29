/* test.h - minimal test harness: CHECK macros and suite registration. */
#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdio.h>

extern int t_checks, t_fails;

#define CHECK(c) do { \
	t_checks++; \
	if (!(c)) { t_fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
	double a_ = (a), b_ = (b), t_ = (tol); \
	t_checks++; \
	if (!(fabs(a_ - b_) <= t_)) { \
		t_fails++; \
		printf("  FAIL %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, a_, b_, t_); \
	} \
} while (0)

#endif
