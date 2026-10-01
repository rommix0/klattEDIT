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
void suite_synth_speed(void);
void suite_model(void);
void suite_csvio(void);
void suite_optimize(void);
void suite_audio(void);
void suite_view(void);
void suite_golden(void);
void suite_xarch(void);
void suite_wavin(void);
void suite_resample(void);
void suite_lpc(void);
void suite_pitch(void);
void suite_track(void);
void suite_wavimport(void);
void suite_waverange(void);

static const struct { const char *name; void (*fn)(void); } suites[] = {
	{ "params", suite_params },
	{ "resonator", suite_resonator },
	{ "synth_basic", suite_synth_basic },
	{ "synth_voice", suite_synth_voice },
	{ "synth_noise", suite_synth_noise },
	{ "synth_source", suite_synth_source },
	{ "synth_speed", suite_synth_speed },
	{ "model", suite_model },
	{ "csvio", suite_csvio },
	{ "optimize", suite_optimize },
	{ "audio", suite_audio },
	{ "view", suite_view },
	{ "golden", suite_golden },
	{ "xarch", suite_xarch },
	{ "wavin", suite_wavin },
	{ "resample", suite_resample },
	{ "lpc", suite_lpc },
	{ "pitch", suite_pitch },
	{ "track", suite_track },
	{ "wavimport", suite_wavimport },
	{ "waverange", suite_waverange },
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
