/* run_tests.c - runs every suite; exit code 0 when all checks pass. */
#include <stdio.h>
#include "test.h"

int t_checks, t_fails;

void suite_params(void);
void suite_resonator(void);
void suite_synth_basic(void);
void suite_synth_voice(void);
void suite_synth_noise(void);
void suite_synth_source(void);
void suite_model(void);
void suite_csvio(void);
void suite_optimize(void);
void suite_audio(void);
void suite_view(void);
void suite_golden(void);
void suite_xarch(void);
void suite_wavin(void);

static const struct { const char *name; void (*fn)(void); } suites[] = {
	{ "params", suite_params },
	{ "resonator", suite_resonator },
	{ "synth_basic", suite_synth_basic },
	{ "synth_voice", suite_synth_voice },
	{ "synth_noise", suite_synth_noise },
	{ "synth_source", suite_synth_source },
	{ "model", suite_model },
	{ "csvio", suite_csvio },
	{ "optimize", suite_optimize },
	{ "audio", suite_audio },
	{ "view", suite_view },
	{ "golden", suite_golden },
	{ "xarch", suite_xarch },
	{ "wavin", suite_wavin },
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
