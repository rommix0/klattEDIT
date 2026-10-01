#include <stddef.h>
#include <string.h>
#include "test.h"
#include "klatt.h"

static void test_counts_and_version(void)
{
	CHECK(KlattVersion() == KLATT_VERSION);
	CHECK(KLATT_VERSION == 10200);
	CHECK(KlattParamCount(KLATT_FRAME) == 23);
	CHECK(KlattParamCount(KLATT_GLOBAL) == 21);
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
	CHECK(KlattParamInfo(KLATT_GLOBAL, KG_SPD, &pi) == KLATT_OK); /* appended in 1.2, after FL */
	CHECK(KG_SPD == 20);
	CHECK(strcmp(pi.name, "SPD") == 0);
	CHECK(strcmp(pi.unit, "-") == 0);
	CHECK_NEAR(pi.min, 0.25, 0);
	CHECK_NEAR(pi.max, 4, 0);
	CHECK_NEAR(pi.def, 1, 0);
	CHECK(KlattParamInfo(KLATT_GLOBAL, 21, &pi) == KLATT_ERR_ARG);
	CHECK(KlattParamInfo(KLATT_FRAME, 23, &pi) == KLATT_ERR_ARG);
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
	CHECK(KlattParamIndex(KLATT_GLOBAL, "spd") == KG_SPD);
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

/* F4: klatt_param_info's layout differs between 32- and 64-bit (natural pointer alignment pushes the
 * doubles to a different offset); document the two layouts (docs/klatt-dll.md) and pin the current one. */
static void test_param_info_layout(void)
{
	size_t want_size = sizeof(void *) == 8 ? 48 : 40;
	size_t want_min_off = sizeof(void *) == 8 ? 24 : 16;
	CHECK(sizeof(klatt_param_info) == want_size);
	CHECK(offsetof(klatt_param_info, min) == want_min_off);
}

void suite_params(void)
{
	test_counts_and_version();
	test_info();
	test_every_default_within_limits();
	test_index_lookup();
	test_error_text();
	test_param_info_layout();
}
