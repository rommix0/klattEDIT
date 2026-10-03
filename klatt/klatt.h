/* klatt.h - public API of the Klatt 1980 cascade/parallel formant synthesizer DLL.
 *
 * Built as klatt32.dll (i686) and klatt64.dll (x86_64). On 32-bit Windows every function is __stdcall and
 * exported under its plain name (KlattCreate, not _KlattCreate@8). Functions returning int return 0 or more
 * on success and a negative KLATT_ERR_* code on failure. Parameter values are doubles in physical units. */
#ifndef KLATT_H
#define KLATT_H

#ifdef _WIN32
#  define KLATT_CALL __stdcall
#  ifdef KLATT_BUILD
#    define KLATT_API __declspec(dllexport)
#  else
#    define KLATT_API __declspec(dllimport)
#  endif
#else
#  define KLATT_CALL
#  define KLATT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KLATT_VERSION 10300 /* 1.3.0, as major*10000 + minor*100 + patch */

enum { KLATT_FRAME = 0, KLATT_GLOBAL = 1 }; /* parameter kinds */

enum {
	KLATT_OK = 0,
	KLATT_ERR_ARG = -1,    /* NULL pointer, bad kind or index */
	KLATT_ERR_BUFFER = -2, /* output buffer smaller than the frame */
	KLATT_ERR_MEMORY = -3
};

/* Frame parameters, in the order KlattSynthFrame expects them. Rd and DI were added in 1.1. */
enum {
	KP_AV, KP_AF, KP_AH, KP_AVS, KP_F0, KP_F1, KP_F2, KP_F3, KP_F4, KP_FNZ, KP_AN,
	KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB, KP_B1, KP_B2, KP_B3,
	KP_RD, KP_DI,
	KLATT_NUM_FRAME
};

/* Global parameters, in the order KlattCreate expects them. SRC and FL were added in 1.1, SPD in 1.2. */
enum {
	KG_SW, KG_FGP, KG_BGP, KG_FGZ, KG_BGZ, KG_BGS, KG_FNP, KG_BNP, KG_BNZ,
	KG_B4, KG_F5, KG_B5, KG_F6, KG_B6, KG_SR, KG_FRAME_MS, KG_G0, KG_NFC,
	KG_SRC, KG_FL, KG_SPD,
	KLATT_NUM_GLOBAL
};

typedef struct klatt_synth *klatt_h;

typedef struct {
	const char *name, *unit, *description;
	double min, max, def;
} klatt_param_info;

KLATT_API int KLATT_CALL KlattVersion(void);
KLATT_API int KLATT_CALL KlattParamCount(int kind);
KLATT_API int KLATT_CALL KlattParamInfo(int kind, int index, klatt_param_info *out);
KLATT_API int KLATT_CALL KlattParamIndex(int kind, const char *name);
KLATT_API const char *KLATT_CALL KlattErrorText(int code);

KLATT_API int KLATT_CALL KlattCreate(const double *globals, klatt_h *out);
KLATT_API int KLATT_CALL KlattMaxFrameSamples(klatt_h h);
KLATT_API int KLATT_CALL KlattSynthFrame(klatt_h h, const double *frame, short *out, int max);
KLATT_API int KLATT_CALL KlattOverloadCount(klatt_h h);
KLATT_API void KLATT_CALL KlattReset(klatt_h h, unsigned seed);
KLATT_API void KLATT_CALL KlattDestroy(klatt_h h);

#ifdef __cplusplus
}
#endif
#endif
