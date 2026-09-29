/* run_tests.c - runs every suite; exit code 0 when all checks pass. */
#include <stdio.h>
#include "test.h"

int t_checks, t_fails;

void suite_params(void);

static const struct { const char *name; void (*fn)(void); } suites[] = {
	{ "params", suite_params },
};

int main(void)
{
	for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
		int before = t_fails;
		printf("%s\n", suites[i].name);
		suites[i].fn();
		printf("  %s\n", t_fails == before ? "ok" : "FAILED");
	}
	printf("%d checks, %d failures\n", t_checks, t_fails);
	return t_fails ? 1 : 0;
}
