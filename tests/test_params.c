#include <string.h>
#include "test.h"
#include "klatt.h"

static void test_counts_and_version(void)
{
	CHECK(KlattVersion() == KLATT_VERSION);
	CHECK(KlattParamCount(KLATT_FRAME) == 21);
	CHECK(KlattParamCount(KLATT_GLOBAL) == 18);
	CHECK(KlattParamCount(7) == KLATT_ERR_ARG);
}

static void test_info(void)
{
	klatt_param_info pi;
	CHECK(KlattParamInfo(KLATT_FRAME, KP_F1, &pi) == KLATT_OK);
	CHECK(strcmp(pi.name, "F1") == 0);
	CHECK(strcmp(pi.unit, "Hz") == 0);
	CHECK_NEAR(pi.def, 500, 0);
	CHECK(KlattParamInfo(KLATT_GLOBAL, KG_SR, &pi) == KLATT_OK);
	CHECK(strcmp(pi.name, "SR") == 0);
	CHECK_NEAR(pi.def, 11025, 0);
	CHECK(KlattParamInfo(KLATT_FRAME, 21, &pi) == KLATT_ERR_ARG);
	CHECK(KlattParamInfo(KLATT_FRAME, -1, &pi) == KLATT_ERR_ARG);
	CHECK(KlattParamInfo(KLATT_FRAME, 0, NULL) == KLATT_ERR_ARG);
}

static void test_every_default_within_limits(void)
{
	for (int kind = 0; kind < 2; kind++)
		for (int i = 0; i < KlattParamCount(kind); i++) {
			klatt_param_info pi;
			KlattParamInfo(kind, i, &pi);
			CHECK(pi.min <= pi.def && pi.def <= pi.max);
			CHECK(KlattParamIndex(kind, pi.name) == i);
		}
}

static void test_index_lookup(void)
{
	CHECK(KlattParamIndex(KLATT_FRAME, "av") == KP_AV);
	CHECK(KlattParamIndex(KLATT_FRAME, "B3") == KP_B3);
	CHECK(KlattParamIndex(KLATT_GLOBAL, "frame_ms") == KG_FRAME_MS);
	CHECK(KlattParamIndex(KLATT_FRAME, "F") == KLATT_ERR_ARG);
	CHECK(KlattParamIndex(KLATT_FRAME, "F10") == KLATT_ERR_ARG);
	CHECK(KlattParamIndex(KLATT_FRAME, "SR") == KLATT_ERR_ARG);
	CHECK(KlattParamIndex(KLATT_FRAME, NULL) == KLATT_ERR_ARG);
}

static void test_error_text(void)
{
	CHECK(strcmp(KlattErrorText(KLATT_ERR_BUFFER), "output buffer too small for the frame") == 0);
	CHECK(strcmp(KlattErrorText(-99), "unknown error") == 0);
}

void suite_params(void)
{
	test_counts_and_version();
	test_info();
	test_every_default_within_limits();
	test_index_lookup();
	test_error_text();
}
