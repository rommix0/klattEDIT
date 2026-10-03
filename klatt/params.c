/* params.c - the parameter table (names, units, limits, defaults), version and error text. */
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include "klatt.h"
#include "klatt_internal.h"

static const klatt_param_info frame_params[KLATT_NUM_FRAME] = {
	{ "AV",  "dB", "voicing amplitude",               0,   80,    0 },
	{ "AF",  "dB", "frication amplitude",             0,   80,    0 },
	{ "AH",  "dB", "aspiration amplitude",            0,   80,    0 },
	{ "AVS", "dB", "sinusoidal voicing amplitude",    0,   80,    0 },
	{ "F0",  "Hz", "fundamental frequency",           0,  500,  100 },
	{ "F1",  "Hz", "formant 1 frequency",           150, 1300,  500 },
	{ "F2",  "Hz", "formant 2 frequency",           500, 3000, 1500 },
	{ "F3",  "Hz", "formant 3 frequency",          1300, 4000, 2500 },
	{ "F4",  "Hz", "formant 4 frequency",          2500, 4500, 3500 },
	{ "FNZ", "Hz", "nasal zero frequency",          200,  700,  250 },
	{ "AN",  "dB", "nasal formant amplitude",         0,   80,    0 },
	{ "A1",  "dB", "parallel formant 1 amplitude",    0,   80,    0 },
	{ "A2",  "dB", "parallel formant 2 amplitude",    0,   80,    0 },
	{ "A3",  "dB", "parallel formant 3 amplitude",    0,   80,    0 },
	{ "A4",  "dB", "parallel formant 4 amplitude",    0,   80,    0 },
	{ "A5",  "dB", "parallel formant 5 amplitude",    0,   80,    0 },
	{ "A6",  "dB", "parallel formant 6 amplitude",    0,   80,    0 },
	{ "AB",  "dB", "bypass amplitude",                0,   80,    0 },
	{ "B1",  "Hz", "formant 1 bandwidth",            40,  500,   50 },
	{ "B2",  "Hz", "formant 2 bandwidth",            40,  500,   70 },
	{ "B3",  "Hz", "formant 3 bandwidth",            40,  500,  110 },
	{ "Rd",  "-",  "voice quality (pressed to breathy)", 0.3, 2.7, 1 },
	{ "DI",  "%",  "diplophonia (double pulsing)",    0,  100,    0 },
};

static const klatt_param_info global_params[KLATT_NUM_GLOBAL] = {
	{ "SW",       "-",  "0 = cascade/parallel, 1 = all parallel",  0,     1,     0 },
	{ "FGP",      "Hz", "glottal resonator frequency",             0,   600,     0 },
	{ "BGP",      "Hz", "glottal resonator bandwidth",           100,  2000,   100 },
	{ "FGZ",      "Hz", "glottal zero frequency",                  0,  5000,  1500 },
	{ "BGZ",      "Hz", "glottal zero bandwidth",                100,  9000,  6000 },
	{ "BGS",      "Hz", "sinusoidal-voicing resonator bandwidth", 100,  1000,   200 },
	{ "FNP",      "Hz", "nasal pole frequency",                  200,   500,   250 },
	{ "BNP",      "Hz", "nasal pole bandwidth",                   50,   500,   100 },
	{ "BNZ",      "Hz", "nasal zero bandwidth",                   50,   500,   100 },
	{ "B4",       "Hz", "formant 4 bandwidth",                   100,   500,   250 },
	{ "F5",       "Hz", "formant 5 frequency",                  3500,  4900,  3750 },
	{ "B5",       "Hz", "formant 5 bandwidth",                   150,   700,   200 },
	{ "F6",       "Hz", "formant 6 frequency",                  4000,  8000,  4900 },
	{ "B6",       "Hz", "formant 6 bandwidth",                   200,  2000,  1000 },
	{ "SR",       "Hz", "sample rate",                          5000, 20000, 11025 },
	{ "FRAME_MS", "ms", "frame length",                            1,    20,     5 },
	{ "G0",       "dB", "overall gain",                            0,    80,    47 },
	{ "NFC",      "-",  "number of cascade formants",              4,     6,     5 },
	{ "SRC",      "-",  "voicing source: 0 = Klatt 1980, 1 = LF",  0,     1,     0 },
	{ "FL",       "%",  "flutter",                                 0,   100,     0 },
	{ "SPD",      "-",  "speaking speed: 1 = as written",       0.25,     4,     1 },
};

static const klatt_param_info *table(int kind, int *count)
{
	if (kind == KLATT_FRAME) { *count = KLATT_NUM_FRAME; return frame_params; }
	if (kind == KLATT_GLOBAL) { *count = KLATT_NUM_GLOBAL; return global_params; }
	*count = 0;
	return NULL;
}

double klatt_clamp(int kind, int index, double v)
{
	int n;
	const klatt_param_info *t = table(kind, &n);
	if (!t || index < 0 || index >= n)
		return v;
	if (isnan(v)) /* a signaling compare with an unmasked host would otherwise fault here */
		return t[index].min;
	if (!(v >= t[index].min))
		return t[index].min;
	if (v > t[index].max)
		return t[index].max;
	return v;
}

KLATT_API int KLATT_CALL KlattVersion(void)
{
	return KLATT_VERSION;
}

KLATT_API int KLATT_CALL KlattParamCount(int kind)
{
	int n;
	return table(kind, &n) ? n : KLATT_ERR_ARG;
}

KLATT_API int KLATT_CALL KlattParamInfo(int kind, int index, klatt_param_info *out)
{
	int n;
	const klatt_param_info *t = table(kind, &n);
	if (!t || !out || index < 0 || index >= n)
		return KLATT_ERR_ARG;
	*out = t[index];
	return KLATT_OK;
}

KLATT_API int KLATT_CALL KlattParamIndex(int kind, const char *name)
{
	int n;
	const klatt_param_info *t = table(kind, &n);
	if (!t || !name)
		return KLATT_ERR_ARG;
	for (int i = 0; i < n; i++) {
		const char *a = t[i].name, *b = name;
		while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b))
			a++, b++;
		if (!*a && !*b)
			return i;
	}
	return KLATT_ERR_ARG;
}

KLATT_API const char *KLATT_CALL KlattErrorText(int code)
{
	switch (code) {
	case KLATT_OK:         return "no error";
	case KLATT_ERR_ARG:    return "invalid argument";
	case KLATT_ERR_BUFFER: return "output buffer too small for the frame";
	case KLATT_ERR_MEMORY: return "out of memory";
	default:               return "unknown error";
	}
}
