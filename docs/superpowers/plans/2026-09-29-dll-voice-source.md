# Klatt DLL 1.1: LF voice source, flutter, diplophonia, noise corrections: implementation plan (plan 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Klatt DLL 1.1: a selectable LF voicing source shaped by Rd, Rd's tilt on the 1980 source, flutter (FL)
and diplophonia (DI) from Klatt & Klatt (1990), white 16-sum noise with the paper's AF/AH ramps, and the editor
support for the four new parameters.

**Architecture:** The new parameters are appended to the DLL's tables (Rd and DI per frame, SRC and FL global), so
no existing index moves. `klatt/klatt.c` gains a per-pulse `start_period` that latches the pulse values and sets the
source filters: the LF glottal-formant and tilt filters for SRC = 1, or RGP's bandwidth and a darkening low-pass
for Rd on the 1980 source. Both sources produce glottal flow; its first difference (radiation) is taken before the
branches, so noise can enter white. The editor learns a per-parameter paint step (0.01 for Rd), a Voice group in a
now scrollable left panel, and two more Optimize tolerances.

**Tech Stack:** C (gnu11), winlibs GCC 14.2 (i686 and x86_64), GNU make run from Git Bash, Win32 for the editor.

**Spec:** `docs/superpowers/specs/2026-09-29-voice-source-and-wav-import-design.md`, section 3 (Part A) and the
Part A items of section 5. Plan 5 (WAV import) builds on this one.

**Status of the code below:** every task was built and run in order before the plan was written, each on the
previous task's result. Expected check counts on both architectures: Task 1: 812, Task 2: 820, Task 3: 854,
Task 4: 861, Task 5: 870 (Task 7 adds 2 when the approved recording is committed). The 32- and 64-bit outputs were
bit-identical after every task. The calibration constants are the measured values. `tools/ui/check_main.ps1` and
`tools/ui/check_edit.ps1` passed after Task 5, and the screenshots were checked by eye.

## Global Constraints

- Repository root: `C:\Users\abart\Desktop\KPE\kpe_open` (git, branch as set up by the controller).
- Shell: Git Bash. All commands run from the repository root. `MAKE` means `/c/winlibs/mingw32/bin/make.exe`.
- Flags: `-std=gnu11 -O2 -Wall -Wextra` (from the Makefile). The build must produce **zero warnings**.
- The DLL contains only synthesis, the parameter table and error text: no file I/O, no Win32 UI, no documents.
- 32-bit exports stay `__stdcall` with plain names; the DLLs may import only `KERNEL32.dll` and `msvcrt.dll`.
- Clean room: the synthesizer follows Klatt (1980) JASA 67, 971-995; FL and DI follow Klatt & Klatt (1990) JASA 87,
  820-857; the LF source follows Fant (1997) and Perrotin et al. (2021) as in the user's own
  `C:\Users\abart\Desktop\fextr_sept2026\_vtm.py`. Never read `C:\klsyn\src` or
  `C:\Users\abart\Desktop\prose2000_reborn`.
- The approved recording `tests/golden/ba.raw` is removed in Task 2 (the sound changes on purpose) and recorded
  again only in Task 7, after the user has listened and approved.
- Code style: tabs, K&R braces, short comments only where the code does not explain itself.
- Commit messages end with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File map

| File | Change |
|---|---|
| `klatt/klatt.h` | version 1.1.0; `KP_RD`, `KP_DI`, `KG_SRC`, `KG_FL` |
| `klatt/params.c` | the four new table rows |
| `klatt/klatt.c` | noise, radiation and ramps (Task 2); LF source and Rd (Task 3); flutter and diplophonia (Task 4) |
| `tests/calibrate.c` | two-argument `sample`, the ramps, and the LF measurement |
| `tests/test_synth_source.c` | new suite: noise, ramps, LF, Rd, flutter, diplophonia |
| `tests/run_tests.c` | registers `synth_source` |
| `tests/test_params.c`, `tests/test_csvio.c` | the new counts and CSV columns |
| `tests/test_xarch.c` | adds a stretch with the LF source, flutter, diplophonia and an Rd glide |
| `tests/golden/ba.raw` | removed in Task 2, recorded again in Task 7 |
| `kpe/model.c`, `kpe/model.h` | `doc_param_step` (0.01 for Rd) used by painting; SRC rounded like SW |
| `kpe/view.c` | Rd and DI strips after AVS |
| `kpe/main.c` | Voice group, scrolling left panel, SRC in the globals text |
| `kpe/strips.c` | Rd/DI colour, unitless axis title, pointer value at the parameter's step |
| `kpe/dialogs.c`, `kpe/kpe.rc`, `kpe/resource.h` | Optimize tolerances for Rd and % |
| `tests/test_model.c`, `tests/test_view.c` | tests for the above |
| `docs/klatt-dll.md`, `docs/manual-checklist.md`, `README.md`, base spec | documentation |

---

### Task 1: Parameter table 1.1

**Files:**
- Modify: `klatt/klatt.h`, `klatt/params.c`, `tests/test_params.c`, `tests/test_csvio.c`

**Interfaces:**
- Produces: `KP_RD` (21), `KP_DI` (22), `KG_SRC` (18), `KG_FL` (19); `KLATT_NUM_FRAME` = 23, `KLATT_NUM_GLOBAL` = 20;
  `KLATT_VERSION` = 10100. Later tasks read these; nothing in `klatt.c` uses them yet, so the sound does not
  change in this task.

- [ ] **Step 1: Write the failing tests**

In `tests/test_params.c`, replace:

```c
	CHECK(KlattParamCount(KLATT_FRAME) == 21);
	CHECK(KlattParamCount(KLATT_GLOBAL) == 18);
```

with:

```c
	CHECK(KlattParamCount(KLATT_FRAME) == 23);
	CHECK(KlattParamCount(KLATT_GLOBAL) == 20);
```

In `tests/test_params.c`, replace:

```c
	CHECK(KlattParamInfo(KLATT_FRAME, 21, &pi) == KLATT_ERR_ARG);
```

with:

```c
	CHECK(KlattParamInfo(KLATT_FRAME, 23, &pi) == KLATT_ERR_ARG);
```

In `tests/test_csvio.c`, replace:

```c
	CHECK(strcmp(line, "time_ms,AV,AF,AH,AVS,F0,F1,F2,F3,F4,FNZ,AN,A1,A2,A3,A4,A5,A6,AB,B1,B2,B3\r\n") == 0);
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strcmp(line, "0,0,0,0,0,100,500,1500,2500,3500,250,0,0,0,0,0,0,0,0,50,70,110\r\n") == 0);
```

with:

```c
	CHECK(strcmp(line, "time_ms,AV,AF,AH,AVS,F0,F1,F2,F3,F4,FNZ,AN,A1,A2,A3,A4,A5,A6,AB,B1,B2,B3,Rd,DI\r\n") == 0);
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strcmp(line, "0,0,0,0,0,100,500,1500,2500,3500,250,0,0,0,0,0,0,0,0,50,70,110,1,0\r\n") == 0);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: 4 failures out of 804 checks: `test_params.c` lines 9 and 10 (the counts) and `test_csvio.c` lines 81 and
83 (the header and first row).

- [ ] **Step 3: Add the parameters**

In `klatt/klatt.h`, replace:

```c
#define KLATT_VERSION 10000 /* 1.0.0, as major*10000 + minor*100 + patch */
```

with:

```c
#define KLATT_VERSION 10100 /* 1.1.0, as major*10000 + minor*100 + patch */
```

In `klatt/klatt.h`, replace:

```c
/* Frame parameters, in the order KlattSynthFrame expects them. */
enum {
	KP_AV, KP_AF, KP_AH, KP_AVS, KP_F0, KP_F1, KP_F2, KP_F3, KP_F4, KP_FNZ, KP_AN,
	KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB, KP_B1, KP_B2, KP_B3,
	KLATT_NUM_FRAME
};

/* Global parameters, in the order KlattCreate expects them. */
enum {
	KG_SW, KG_FGP, KG_BGP, KG_FGZ, KG_BGZ, KG_BGS, KG_FNP, KG_BNP, KG_BNZ,
	KG_B4, KG_F5, KG_B5, KG_F6, KG_B6, KG_SR, KG_FRAME_MS, KG_G0, KG_NFC,
	KLATT_NUM_GLOBAL
};
```

with:

```c
/* Frame parameters, in the order KlattSynthFrame expects them. Rd and DI were added in 1.1. */
enum {
	KP_AV, KP_AF, KP_AH, KP_AVS, KP_F0, KP_F1, KP_F2, KP_F3, KP_F4, KP_FNZ, KP_AN,
	KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB, KP_B1, KP_B2, KP_B3,
	KP_RD, KP_DI,
	KLATT_NUM_FRAME
};

/* Global parameters, in the order KlattCreate expects them. SRC and FL were added in 1.1. */
enum {
	KG_SW, KG_FGP, KG_BGP, KG_FGZ, KG_BGZ, KG_BGS, KG_FNP, KG_BNP, KG_BNZ,
	KG_B4, KG_F5, KG_B5, KG_F6, KG_B6, KG_SR, KG_FRAME_MS, KG_G0, KG_NFC,
	KG_SRC, KG_FL,
	KLATT_NUM_GLOBAL
};
```

In `klatt/params.c`, replace:

```c
	{ "B3",  "Hz", "formant 3 bandwidth",            40,  500,  110 },
};
```

with:

```c
	{ "B3",  "Hz", "formant 3 bandwidth",            40,  500,  110 },
	{ "Rd",  "-",  "voice quality (pressed to breathy)", 0.3, 2.7, 1 },
	{ "DI",  "%",  "diplophonia (double pulsing)",    0,  100,    0 },
};
```

In `klatt/params.c`, replace:

```c
	{ "NFC",      "-",  "number of cascade formants",              4,     6,     5 },
};
```

with:

```c
	{ "NFC",      "-",  "number of cascade formants",              4,     6,     5 },
	{ "SRC",      "-",  "voicing source: 0 = Klatt 1980, 1 = LF",  0,     1,     0 },
	{ "FL",       "%",  "flutter",                                 0,   100,     0 },
};
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `812 checks, 0 failures` twice, then `xarch: 16537 samples, max difference 0 LSB: ok`. The golden suite
still passes: the synthesizer does not read the new parameters yet.

- [ ] **Step 5: Commit**

```bash
git add klatt/klatt.h klatt/params.c tests/test_params.c tests/test_csvio.c
git commit -m "Add Rd, DI, SRC and FL to the parameter table (DLL 1.1.0)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: White noise, radiation on the voicing, AF/AH ramps

**Files:**
- Create: `tests/test_synth_source.c`
- Modify: `klatt/klatt.c` (replaced whole), `tests/calibrate.c` (replaced whole), `tests/run_tests.c`
- Delete: `tests/golden/ba.raw`

**Interfaces:**
- Consumes: the Task 1 table.
- Produces: `static double sample(struct klatt_synth *s, double frac)` (frac = position in the frame, in (0, 1]),
  used by `KlattSynthFrame` and by `tests/calibrate.c`; `K_NOISE` = 2.070e-4. Task 3 replaces `klatt.c` again.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_synth_source.c`:

```c
/* Voicing-source and noise tests for DLL 1.1: the LF source and Rd, Rd's tilt on the 1980 source, flutter,
 * diplophonia, white noise and the AF/AH ramps. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

#define SR 11025.0
#define PI 3.14159265358979323846
#define SKIP 1102 /* the first 100 ms of every steady sound */

static short buf[80000], ref[80000];

static void test_noise_is_white(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AF] = 60;
	f[KP_AB] = 60;
	int n = t_synth_steady(g, f, 200, buf, 80000);
	static const double edge[] = { 30, 100, 500, 1000, 2000, 3000, 4000, 4961 };
	double dens[7], mean = 0;
	for (int b = 0; b < 7; b++) {
		double frac = 0;
		for (int s = 0; s < 4; s++) /* four 2048-sample stretches */
			frac += m_band_fraction(buf + SKIP + s * 2048, n - SKIP - s * 2048, SR, edge[b], edge[b + 1]);
		dens[b] = 10 * log10(frac / (edge[b + 1] - edge[b]));
		mean += dens[b] / 7;
	}
	for (int b = 0; b < 7; b++)
		CHECK_NEAR(dens[b], mean, 3.0);
}

/* AF-only frication through AB, one AF value per frame. Runs with the same seed draw the same noise, so the
 * sample-by-sample ratio of two runs is the ratio of their AF gains. */
static void af_run(const double *af, int nframes, short *out)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AB] = 60;
	KlattCreate(g, &h);
	int n = 0;
	for (int k = 0; k < nframes; k++) {
		f[KP_AF] = af[k];
		n += KlattSynthFrame(h, f, out + n, 80000 - n);
	}
	KlattDestroy(h);
}

/* The largest error of a/b against a ramp from lo to hi over samples [from, from + len), counting only samples
 * where |b| is large enough for the rounding to 16 bits not to matter. */
static double ratio_error(const short *a, const short *b, int from, int len, double lo, double hi)
{
	double worst = 0;
	for (int i = 0; i < len; i++) {
		if (abs(b[from + i]) < 1000)
			continue;
		double want = lo + (hi - lo) * (i + 1.0) / len;
		worst = fmax(worst, fabs((double)a[from + i] / b[from + i] - want));
	}
	return worst;
}

static void test_af_ramps_and_jumps(void)
{
	double step[12], flat[12];
	for (int k = 0; k < 12; k++) {
		step[k] = k < 10 ? 60 : 70;
		flat[k] = 70;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	double lo = pow(10, -10 / 20.0);
	CHECK(ratio_error(buf, ref, 496, 55, lo, lo) < 0.01);  /* frame 9, samples 496-550: 60 dB */
	CHECK(ratio_error(buf, ref, 551, 55, lo, 1.0) < 0.01); /* frame 10, samples 551-605: the ramp */

	for (int k = 0; k < 12; k++) { /* a burst: 0 -> 60 dB is more than 50 dB, so it jumps */
		step[k] = k < 10 ? 0 : 60;
		flat[k] = 60;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	CHECK(ratio_error(buf, ref, 551, 55, 1.0, 1.0) < 0.01);
}

void suite_synth_source(void)
{
	test_noise_is_white();
	test_af_ramps_and_jumps();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_synth_noise(void);
```

with:

```c
void suite_synth_noise(void);
void suite_synth_source(void);
```

In `tests/run_tests.c`, replace:

```c
	{ "synth_noise", suite_synth_noise },
```

with:

```c
	{ "synth_noise", suite_synth_noise },
	{ "synth_source", suite_synth_source },
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: 3 failures out of 822 checks: `test_synth_source.c:34` (the 30-100 Hz band is about 7 dB below the mean:
the old noise low-pass thins the lows), and lines 81 and 89 (AF steps instead of ramping, and the burst case).

- [ ] **Step 3: Replace the synthesizer**

Replace the whole of `klatt/klatt.c` with:

```c
/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995). See docs/klatt-dll.md for the signal path and calibration. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "klatt.h"
#include "klatt_internal.h"
#include "resonator.h"

/* Calibration constants, measured as described in docs/klatt-dll.md. */
#ifndef K_OUT
#define K_OUT   4.125e5 /* neutral vowel, AV = 60, G0 = 47: peak about 16000 */
#endif
#ifndef K_NOISE
#define K_NOISE 2.070e-4 /* AH = 60 alone: same RMS as AV = 60 alone */
#endif
#ifndef K_PAR
#define K_PAR   6.0 /* SW = 1, A1-A4 = 60: same RMS as the cascade vowel */
#endif

#define K_BYPASS 0.45 /* a formant resonator/antiresonator at or above K_BYPASS*SR is bypassed for the frame */
#define AF_JUMP_DB 50.0 /* an AF rise larger than this jumps at the frame start instead of ramping */

struct klatt_synth {
	double g[KLATT_NUM_GLOBAL]; /* clamped globals */
	double sr, spf, fmax;       /* sample rate, samples per frame, highest allowed frequency */
	int nfc, parallel_only;
	double out_gain;
	double frame;               /* frames synthesized since the last reset */
	uint32_t rng;
	int overload;

	/* voicing source; phase is the glottal phase in periods, wrapped to [0,1) while voiced, and pinned
	 * at 1.0 while unvoiced so voicing starts with a pulse at once; av/avs latch at each pulse */
	double phase;
	double av, avs, av_next, avs_next, f0_next;
	reson rgp, rgs;
	antireson rgz;
	double v_prev;              /* previous voicing sample, for the radiation first difference */

	double ah0, ah1, af0, af1;  /* linear noise gains ramped across the frame from *0 to *1 */
	double af_db;               /* AF of the previous frame, for the jump rule */

	reson rnp;                  /* cascade nasal pole */
	antireson rnz;              /* cascade nasal zero */
	reson rc[6];                /* cascade formants F1..F6 */
	int bypass_np, bypass_nz, bypass_r[6]; /* set when the resonator's frequency is >= K_BYPASS*sr */

	reson rp[6];                /* parallel formants F1..F6 */
	reson rnp_par;              /* parallel nasal formant */
	double a[6], an, ab;        /* linear parallel gains of the current frame */
	double par_prev;            /* previous parallel voicing input, for its first difference */
};

static double db_to_lin(double db)
{
	return db <= 0.0 ? 0.0 : pow(10.0, (db - 80.0) / 20.0);
}

/* White, approximately Gaussian noise: the sum of 16 uniform numbers (paper p. 977), centred on zero. */
static double noise(struct klatt_synth *s)
{
	double sum = 0.0;
	for (int i = 0; i < 16; i++) {
		s->rng = s->rng * 1664525u + 1013904223u;
		sum += (double)(s->rng >> 8) / 16777216.0;
	}
	return sum - 8.0;
}

static void clear_reson(reson *r) { r->y1 = r->y2 = 0.0; }
static void clear_anti(antireson *r) { r->x1 = r->x2 = 0.0; }

KLATT_API int KLATT_CALL KlattCreate(const double *globals, klatt_h *out)
{
	if (!globals || !out)
		return KLATT_ERR_ARG;
	struct klatt_synth *s = calloc(1, sizeof *s);
	if (!s)
		return KLATT_ERR_MEMORY;
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++)
		s->g[i] = klatt_clamp(KLATT_GLOBAL, i, globals[i]);
	s->sr = s->g[KG_SR];
	s->spf = s->sr * s->g[KG_FRAME_MS] / 1000.0;
	s->fmax = s->sr / 2.0 - 1.0;
	s->nfc = (int)lround(s->g[KG_NFC]);
	s->parallel_only = s->g[KG_SW] >= 0.5;
	s->out_gain = K_OUT * pow(10.0, s->g[KG_G0] / 20.0);

	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	antireson_set(&s->rgz, fmin(s->g[KG_FGZ], s->fmax), s->g[KG_BGZ], s->sr);
	reson_set(&s->rgs, 0.0, s->g[KG_BGS], s->sr);
	reson_set(&s->rnp, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rnp_par, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rc[4], fmin(s->g[KG_F5], s->fmax), s->g[KG_B5], s->sr);
	reson_set(&s->rc[5], fmin(s->g[KG_F6], s->fmax), s->g[KG_B6], s->sr);
	s->rp[4] = s->rc[4];
	s->rp[5] = s->rc[5];
	s->bypass_np = s->g[KG_FNP] >= K_BYPASS * s->sr;
	s->bypass_r[4] = s->g[KG_F5] >= K_BYPASS * s->sr;
	s->bypass_r[5] = s->g[KG_F6] >= K_BYPASS * s->sr;
	KlattReset(s, 1);
	*out = s;
	return KLATT_OK;
}

KLATT_API void KLATT_CALL KlattReset(klatt_h s, unsigned seed)
{
	if (!s)
		return;
	s->frame = 0.0;
	s->rng = seed;
	s->overload = 0;
	s->phase = 1.0;
	s->av = s->avs = s->av_next = s->avs_next = s->f0_next = 0.0;
	s->v_prev = s->par_prev = 0.0;
	s->ah0 = s->ah1 = s->af0 = s->af1 = s->af_db = 0.0;
	clear_reson(&s->rgp);
	clear_reson(&s->rgs);
	clear_anti(&s->rgz);
	clear_reson(&s->rnp);
	clear_anti(&s->rnz);
	clear_reson(&s->rnp_par);
	for (int k = 0; k < 6; k++) {
		clear_reson(&s->rc[k]);
		clear_reson(&s->rp[k]);
	}
}

KLATT_API void KLATT_CALL KlattDestroy(klatt_h s)
{
	free(s);
}

KLATT_API int KLATT_CALL KlattMaxFrameSamples(klatt_h s)
{
	return s ? (int)ceil(s->spf) : KLATT_ERR_ARG;
}

KLATT_API int KLATT_CALL KlattOverloadCount(klatt_h s)
{
	return s ? s->overload : KLATT_ERR_ARG;
}

/* Loads one frame's parameters into the filters and gains. */
static void load_frame(struct klatt_synth *s, const double *in)
{
	double p[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++)
		p[i] = klatt_clamp(KLATT_FRAME, i, in[i]);

	static const int fi[4] = { KP_F1, KP_F2, KP_F3, KP_F4 };
	double bw[4] = { p[KP_B1], p[KP_B2], p[KP_B3], s->g[KG_B4] };
	for (int k = 0; k < 4; k++) {
		s->bypass_r[k] = p[fi[k]] >= K_BYPASS * s->sr;
		reson_set(&s->rc[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
		reson_set(&s->rp[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
	}
	s->bypass_nz = p[KP_FNZ] >= K_BYPASS * s->sr;
	antireson_set(&s->rnz, fmin(p[KP_FNZ], s->fmax), s->g[KG_BNZ], s->sr);

	s->av_next = db_to_lin(p[KP_AV]);
	s->avs_next = db_to_lin(p[KP_AVS]);
	s->f0_next = p[KP_F0];

	/* AF and AH ramp linearly across the frame (paper p. 978), except that an AF rise of more than
	 * AF_JUMP_DB jumps at once, for plosive bursts. */
	s->ah0 = s->ah1;
	s->af0 = s->af1;
	s->ah1 = K_NOISE * db_to_lin(p[KP_AH]);
	s->af1 = K_NOISE * db_to_lin(p[KP_AF]);
	if (p[KP_AF] - s->af_db > AF_JUMP_DB)
		s->af0 = s->af1;
	s->af_db = p[KP_AF];

	for (int k = 0; k < 6; k++)
		s->a[k] = K_PAR * db_to_lin(p[KP_A1 + k]);
	s->an = K_PAR * db_to_lin(p[KP_AN]);
	s->ab = K_PAR * db_to_lin(p[KP_AB]);
}

/* One output sample; frac is how far through the frame this sample is, in (0, 1]. */
static double sample(struct klatt_synth *s, double frac)
{
	/* Voicing: an impulse each time the glottal phase wraps past 1, F0 read fresh every sample so a
	 * change takes effect within the current period instead of waiting for it to finish. */
	double pulse = 0.0;
	int voiced = s->f0_next > 0.0;
	if (voiced) {
		s->phase += s->f0_next / s->sr;
		if (s->phase >= 1.0) {
			s->phase -= 1.0;
			s->av = s->av_next;
			s->avs = s->avs_next;
			pulse = 1.0;
		}
	} else {
		s->phase = 1.0;
		s->av = s->av_next;
		s->avs = s->avs_next;
	}

	/* The voicing source as glottal flow, then its first difference (lip radiation, moved here from the
	 * output so the noise can enter the tract white; see docs/klatt-dll.md). */
	double gp = reson_step(&s->rgp, pulse);
	double v = s->av * antireson_step(&s->rgz, gp) + s->avs * reson_step(&s->rgs, gp);
	double voice = v - s->v_prev;
	s->v_prev = v;

	/* Noise, halved in the second half of each voiced period. */
	double n = noise(s);
	if (voiced && s->av > 0.0 && s->phase >= 0.5)
		n *= 0.5;
	double asp = (s->ah0 + (s->ah1 - s->ah0) * frac) * n;
	double fric = (s->af0 + (s->af1 - s->af0) * frac) * n;

	/* Cascade branch: voicing and aspiration (unless SW = 1). A resonator/antiresonator whose frequency is
	 * at or above K_BYPASS*sr is bypassed: its input passes through unchanged. */
	double casc = 0.0, par_voice = 0.0;
	if (s->parallel_only) {
		par_voice = voice + asp;
	} else {
		double x = voice + asp;
		if (!s->bypass_np) x = reson_step(&s->rnp, x);
		if (!s->bypass_nz) x = antireson_step(&s->rnz, x);
		for (int k = s->nfc - 1; k >= 0; k--)
			if (!s->bypass_r[k]) x = reson_step(&s->rc[k], x);
		casc = x;
	}

	/* Parallel branch: frication, plus voicing when SW = 1. F1 gets undifferenced voicing only;
	 * the others get the first difference of the voicing plus frication. Signs alternate. A bypassed
	 * resonator contributes nothing. */
	double dv = par_voice - s->par_prev;
	s->par_prev = par_voice;
	double src = dv + fric;
	double t0 = s->bypass_r[0] ? 0.0 : s->a[0] * reson_step(&s->rp[0], par_voice);
	double tn = s->bypass_np ? 0.0 : s->an * reson_step(&s->rnp_par, dv);
	double t1 = s->bypass_r[1] ? 0.0 : s->a[1] * reson_step(&s->rp[1], src);
	double t2 = s->bypass_r[2] ? 0.0 : s->a[2] * reson_step(&s->rp[2], src);
	double t3 = s->bypass_r[3] ? 0.0 : s->a[3] * reson_step(&s->rp[3], src);
	double t4 = s->bypass_r[4] ? 0.0 : s->a[4] * reson_step(&s->rp[4], src);
	double t5 = s->bypass_r[5] ? 0.0 : s->a[5] * reson_step(&s->rp[5], src);
	double par = t0 + tn - t1 + t2 - t3 + t4 - t5 + s->ab * src;

	return (casc + par) * s->out_gain;
}

KLATT_API int KLATT_CALL KlattSynthFrame(klatt_h s, const double *frame, short *out, int max)
{
	if (!s || !frame || !out)
		return KLATT_ERR_ARG;
	int n = (int)(floor((s->frame + 1.0) * s->spf + 1e-9) - floor(s->frame * s->spf + 1e-9));
	if (max < n)
		return KLATT_ERR_BUFFER;
	load_frame(s, frame);
	for (int i = 0; i < n; i++) {
		double v = floor(sample(s, (i + 1.0) / n) + 0.5);
		if (v > 32767.0) { v = 32767.0; s->overload++; }
		if (v < -32768.0) { v = -32768.0; s->overload++; }
		out[i] = (short)v;
	}
	s->frame += 1.0;
	return n;
}
```

- [ ] **Step 4: Update the calibration tool and check the constants**

Replace the whole of `tests/calibrate.c` with:

```c
/* calibrate.c - measures the unscaled levels that set K_OUT, K_NOISE and K_PAR in klatt.c.
 * Built against the synthesizer source directly (not the DLL) so it can read the pre-quantization signal. */
#define KLATT_BUILD
#define K_OUT 1.0
#define K_NOISE 1.0
#define K_PAR 1.0
#include "../klatt/params.c"
#include "../klatt/klatt.c"
#include <stdio.h>

static void measure(const double *frame, int sw, double *peak, double *rms)
{
	double g[KLATT_NUM_GLOBAL];
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SW] = sw;
	klatt_h h;
	KlattCreate(g, &h);
	load_frame(h, frame);
	load_frame(h, frame); /* twice, so the AF/AH ramps start at their targets */
	int n = (int)h->sr; /* one second; skip the first 0.1 s */
	double pk = 0, sq = 0;
	int cnt = 0;
	for (int i = 0; i < n; i++) {
		double v = sample(h, 1.0);
		if (i < n / 10) continue;
		if (fabs(v) > pk) pk = fabs(v);
		sq += v * v;
		cnt++;
	}
	*peak = pk;
	*rms = sqrt(sq / cnt);
	KlattDestroy(h);
}

int main(void)
{
	double f[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		f[i] = pi.def;
	}
	double pv, rv, pa, ra, pp, rp;
	f[KP_AV] = 60;
	measure(f, 0, &pv, &rv);               /* cascade vowel, AV = 60 */
	f[KP_AV] = 0; f[KP_AH] = 60;
	measure(f, 0, &pa, &ra);               /* aspiration only, AH = 60 */
	f[KP_AH] = 0; f[KP_AV] = 60;
	f[KP_A1] = f[KP_A2] = f[KP_A3] = f[KP_A4] = 60;
	measure(f, 1, &pp, &rp);               /* all-parallel vowel, AV = A1..A4 = 60 */
	printf("cascade vowel: peak %.6g rms %.6g\n", pv, rv);
	printf("aspiration:    peak %.6g rms %.6g\n", pa, ra);
	printf("parallel:      peak %.6g rms %.6g\n", pp, rp);
	printf("K_OUT   = %.4g  (cascade vowel peak -> 16000)\n", 16000.0 / pv);
	printf("K_NOISE = %.4g  (aspiration rms -> cascade vowel rms)\n", rv / ra);
	printf("K_PAR   = %.4g  (parallel vowel rms -> cascade vowel rms)\n", rv / rp);
	return 0;
}
```

Run: `MAKE calibrate`
Expected (the last three lines):

```
K_OUT   = 4.125e+05  (cascade vowel peak -> 16000)
K_NOISE = 0.000207  (aspiration rms -> cascade vowel rms)
K_PAR   = 5.995  (parallel vowel rms -> cascade vowel rms)
```

`K_OUT` and `K_PAR` are unchanged (the voicing path is equivalent); `K_NOISE` = 2.070e-4 is already in the new
`klatt.c`.

- [ ] **Step 5: Remove the approved recording**

The sound of every document changes slightly (the noise), so the approved recording no longer matches. It is
recorded again in Task 7 after the user listens.

Run: `git rm tests/golden/ba.raw`

- [ ] **Step 6: Run the tests on both architectures**

Run: `MAKE test`
Expected: the golden suite prints `ba: no approved golden file yet (see make golden)`; `820 checks, 0 failures`
twice; `xarch: 16537 samples, max difference 0 LSB: ok`.

- [ ] **Step 7: Commit**

```bash
git add klatt/klatt.c tests/calibrate.c tests/run_tests.c tests/test_synth_source.c
git commit -m "White 16-sum noise, radiation on the voicing, AF/AH ramps

Brings the noise source in line with Klatt (1980): the sum of 16 uniform
numbers, entering the tract white, with the radiation difference taken on
the voicing instead of the output. AF and AH ramp across each frame; an AF
rise of more than 50 dB jumps. The approved recording is removed until the
new sound is approved.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: LF source and Rd

**Files:**
- Modify: `klatt/klatt.c` (replaced whole), `tests/calibrate.c` (replaced whole), `tests/test_synth_source.c`
  (replaced whole)

**Interfaces:**
- Consumes: Task 2's `sample(s, frac)` structure, `KP_RD`, `KG_SRC`.
- Produces: `static double start_period(struct klatt_synth *s, double f0)`, called at each pulse; `lf_setup`,
  `lf_gain`, `lf_tilt_db`, `lf_norm`, `tilt_pole`, `rgp_tilt_db`; `K_LF` = 5.941e-3. Task 4 edits
  `start_period` and `sample`.

- [ ] **Step 1: Write the failing tests**

Replace the whole of `tests/test_synth_source.c` with:

```c
/* Voicing-source and noise tests for DLL 1.1: the LF source and Rd, Rd's tilt on the 1980 source, flutter,
 * diplophonia, white noise and the AF/AH ramps. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

#define SR 11025.0
#define PI 3.14159265358979323846
#define SKIP 1102 /* the first 100 ms of every steady sound */
#define WHOLE 9702 /* 22 * 441 samples: a whole number of periods at 100 and 200 Hz */

static short buf[80000], ref[80000];

/* The LF flow filters' magnitude at f (spec 3.2), written independently of klatt.c. */
static double lf_mag(double f0, double rd, double f)
{
	double t0 = 1 / f0, ra = (-1 + 4.8 * rd) / 100, rk = (22.4 + 11.8 * rd) / 100;
	double rg = rk * (0.5 + 1.2 * rk) / (0.44 * rd - 4 * ra * (0.5 + 1.2 * rk));
	double oq = (1 + rk) / (2 * rg), am = 1 / (1 + rk);
	double fg = 1 / (2 * oq * t0), bg = 1 / (oq * t0 * tan(PI * (1 - am))), fa = 1 / (2 * PI * ra * t0);
	double r = exp(-PI * bg / SR), a1 = -2 * r * cos(2 * PI * fg / SR), a2 = r * r, p = exp(-2 * PI * fa / SR);
	double w = 2 * PI * f / SR;
	double re = 1 + a1 * cos(w) + a2 * cos(2 * w), im = -(a1 * sin(w) + a2 * sin(2 * w));
	double tr = 1 - p * cos(w), ti = p * sin(w);
	return (1 - p) / sqrt((re * re + im * im) * (tr * tr + ti * ti));
}

/* nframes of voicing through the AB bypass only (SW = 1): the output is the source's second difference. */
static int source_only(int src, double f0, double rd, double bgp, double di, double fl, int nframes, short *out)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	g[KG_SRC] = src;
	g[KG_BGP] = bgp;
	g[KG_FL] = fl;
	g[KG_G0] = bgp > 1000 ? 0 : 20; /* the second difference is loud, more so with a wide RGP: stay inside 16 bits */
	f[KP_AV] = 60;
	f[KP_AB] = 60;
	f[KP_F0] = f0;
	f[KP_RD] = rd;
	f[KP_DI] = di;
	return t_synth_steady(g, f, nframes, out, 80000);
}

/* Harmonic k's level in dB over WHOLE samples after SKIP, with the AB path's two first differences divided out. */
static double harmonic_db(const short *x, double f0, int k)
{
	double w = 2 * PI * k * f0 / SR, d = 2 * sin(w / 2);
	return 10 * log10(m_power_at(x + SKIP, WHOLE, SR, k * f0)) - 20 * log10(d * d);
}

static void test_lf_harmonics_follow_the_formula(void)
{
	static const double rds[] = { 0.5, 1.0, 2.5 };
	double hf[3];
	for (int i = 0; i < 3; i++) {
		source_only(1, 100, rds[i], 100, 0, 0, 200, buf);
		double h1 = harmonic_db(buf, 100, 1), worst = 0;
		for (int k = 2; k <= 30; k++) {
			double got = harmonic_db(buf, 100, k) - h1;
			double want = 20 * log10(lf_mag(100, rds[i], k * 100) / lf_mag(100, rds[i], 100));
			if (want > -60) /* quieter harmonics sink into the 16-bit rounding */
				worst = fmax(worst, fabs(got - want));
		}
		CHECK(worst < 1.0);
		hf[i] = harmonic_db(buf, 100, 30) - h1;
	}
	CHECK(hf[0] > hf[1] && hf[1] > hf[2]); /* a larger Rd rolls off more */
}

static double vowel_rms(int src, double f0, double rd)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SRC] = src;
	f[KP_AV] = 60;
	f[KP_F0] = f0;
	f[KP_RD] = rd;
	t_synth_steady(g, f, 200, buf, 80000);
	return m_rms(buf + SKIP, WHOLE);
}

static void test_lf_level(void)
{
	static const double rds[] = { 0.3, 0.6, 1.5, 2.0, 2.7 };
	for (int j = 0; j < 2; j++) {
		double f0 = j ? 200 : 100, r1 = vowel_rms(1, f0, 1.0);
		for (int i = 0; i < 5; i++)
			CHECK_NEAR(20 * log10(vowel_rms(1, f0, rds[i]) / r1), 0, 1.0);
	}
	CHECK_NEAR(20 * log10(vowel_rms(1, 100, 1.0) / vowel_rms(0, 100, 1.0)), 0, 0.5); /* calibration */
}

/* Level of the harmonic nearest 3 kHz relative to H1, from the source through AB. */
static double tilt_db(int src, double f0, double rd)
{
	source_only(src, f0, rd, 100, 0, 0, 200, buf);
	int k = (int)lround(3000 / f0);
	return harmonic_db(buf, f0, k) - harmonic_db(buf, f0, 1);
}

static void test_rd_tilts_the_1980_source(void)
{
	static const double rds[] = { 0.5, 1.0, 1.5, 2.0, 2.5 };
	for (int j = 0; j < 2; j++) {
		double f0 = j ? 200 : 100, t80[5], tlf[5];
		for (int i = 0; i < 5; i++) {
			t80[i] = tilt_db(0, f0, rds[i]);
			tlf[i] = tilt_db(1, f0, rds[i]);
		}
		for (int i = 0; i < 4; i++)
			CHECK(t80[i + 1] < t80[i]);
		for (int i = 0; i < 5; i++)
			CHECK_NEAR(t80[i] - t80[1], tlf[i] - tlf[1], 4.0);
	}
}

static void test_lf_pitch_is_not_rounded(void)
{
	/* 361.475 Hz is a period of 30.5 samples: rounding periods to whole samples would be 1.7 % off. */
	int n = source_only(1, 361.475, 1.0, 100, 0, 0, 200, buf);
	CHECK_NEAR(m_f0(buf + SKIP, n - SKIP, SR, 200, 450), 361.475, 0.005 * 361.475);
}

static void test_noise_is_white(void)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AF] = 60;
	f[KP_AB] = 60;
	int n = t_synth_steady(g, f, 200, buf, 80000);
	static const double edge[] = { 30, 100, 500, 1000, 2000, 3000, 4000, 4961 };
	double dens[7], mean = 0;
	for (int b = 0; b < 7; b++) {
		double frac = 0;
		for (int s = 0; s < 4; s++) /* four 2048-sample stretches */
			frac += m_band_fraction(buf + SKIP + s * 2048, n - SKIP - s * 2048, SR, edge[b], edge[b + 1]);
		dens[b] = 10 * log10(frac / (edge[b + 1] - edge[b]));
		mean += dens[b] / 7;
	}
	for (int b = 0; b < 7; b++)
		CHECK_NEAR(dens[b], mean, 3.0);
}

/* AF-only frication through AB, one AF value per frame. Runs with the same seed draw the same noise, so the
 * sample-by-sample ratio of two runs is the ratio of their AF gains. */
static void af_run(const double *af, int nframes, short *out)
{
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	klatt_h h;
	t_default_globals(g);
	t_default_frame(f);
	g[KG_SW] = 1;
	f[KP_AB] = 60;
	KlattCreate(g, &h);
	int n = 0;
	for (int k = 0; k < nframes; k++) {
		f[KP_AF] = af[k];
		n += KlattSynthFrame(h, f, out + n, 80000 - n);
	}
	KlattDestroy(h);
}

/* The largest error of a/b against want(i) over samples [from, from + len) where |b| is large enough for the
 * rounding to 16 bits not to matter. */
static double ratio_error(const short *a, const short *b, int from, int len, double lo, double hi)
{
	double worst = 0;
	for (int i = 0; i < len; i++) {
		if (abs(b[from + i]) < 1000)
			continue;
		double want = lo + (hi - lo) * (i + 1.0) / len;
		worst = fmax(worst, fabs((double)a[from + i] / b[from + i] - want));
	}
	return worst;
}

static void test_af_ramps_and_jumps(void)
{
	double step[12], flat[12];
	for (int k = 0; k < 12; k++) {
		step[k] = k < 10 ? 60 : 70;
		flat[k] = 70;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	double lo = pow(10, -10 / 20.0);
	CHECK(ratio_error(buf, ref, 496, 55, lo, lo) < 0.01);  /* frame 9, samples 496-550: 60 dB */
	CHECK(ratio_error(buf, ref, 551, 55, lo, 1.0) < 0.01); /* frame 10, samples 551-605: the ramp */

	for (int k = 0; k < 12; k++) { /* a burst: 0 -> 60 dB is more than 50 dB, so it jumps */
		step[k] = k < 10 ? 0 : 60;
		flat[k] = 60;
	}
	af_run(step, 12, buf);
	af_run(flat, 12, ref);
	CHECK(ratio_error(buf, ref, 551, 55, 1.0, 1.0) < 0.01);
}

void suite_synth_source(void)
{
	test_lf_harmonics_follow_the_formula();
	test_lf_level();
	test_rd_tilts_the_1980_source();
	test_lf_pitch_is_not_rounded();
	test_noise_is_white();
	test_af_ramps_and_jumps();
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: 12 failures out of 854 checks: `test_synth_source.c:69` three times and `:72` (the harmonics do not
follow the LF formulas: SRC is not read yet), and `:117` eight times (Rd does not tilt the 1980 source).

- [ ] **Step 3: Replace the synthesizer**

Replace the whole of `klatt/klatt.c` with:

```c
/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995), with the 1.1 extension of an LF voicing source driven by Rd
 * (Fant 1997; Perrotin et al. 2021).
 * See docs/klatt-dll.md for the signal path and calibration. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "klatt.h"
#include "klatt_internal.h"
#include "resonator.h"

/* Calibration constants, measured as described in docs/klatt-dll.md. */
#ifndef K_OUT
#define K_OUT   4.125e5 /* neutral vowel, AV = 60, G0 = 47: peak about 16000 */
#endif
#ifndef K_NOISE
#define K_NOISE 2.070e-4 /* AH = 60 alone: same RMS as AV = 60 alone */
#endif
#ifndef K_PAR
#define K_PAR   6.0 /* SW = 1, A1-A4 = 60: same RMS as the cascade vowel */
#endif
#ifndef K_LF
#define K_LF    5.941e-3 /* SRC = 1, Rd = 1: same RMS as the 1980 source's vowel */
#endif

#define K_BYPASS 0.45 /* a formant resonator/antiresonator at or above K_BYPASS*SR is bypassed for the frame */
#define AF_JUMP_DB 50.0 /* an AF rise larger than this jumps at the frame start instead of ramping */

/* One period's LF filter settings: the glottal-formant poles (y = x - a1*y1 - a2*y2), the tilt pole
 * (z = (1-p)*y + p*z1) and the open quotient. */
typedef struct { double a1, a2, p, oq; } lf_shape;

struct klatt_synth {
	double g[KLATT_NUM_GLOBAL]; /* clamped globals */
	double sr, spf, fmax;       /* sample rate, samples per frame, highest allowed frequency */
	int nfc, parallel_only, lf;
	double out_gain;
	double frame;               /* frames synthesized since the last reset */
	uint32_t rng;
	int overload;

	/* voicing source; phase is the glottal phase in periods, wrapped to [0,1) while voiced, and pinned
	 * at 1.0 while unvoiced so voicing starts with a pulse at once; av/avs/rd latch at each pulse */
	double phase;
	double av, avs, rd, av_next, avs_next, rd_next, f0_next;
	double oq;                  /* open quotient of the current period: the LF value, or 0.5 */
	reson rgp, rgs;
	antireson rgz;
	lf_shape lfs;
	double gy1, gy2, tz1;       /* LF glottal-formant and tilt filter state */
	double lf_amp;              /* LF pulse size (lf_norm) */
	double tp80, ty80;          /* 1980 source: Rd's darkening low-pass pole and state (pole 0 = off) */
	double v_prev;              /* previous voicing sample, for the radiation first difference */

	double ah0, ah1, af0, af1;  /* linear noise gains ramped across the frame from *0 to *1 */
	double af_db;               /* AF of the previous frame, for the jump rule */

	reson rnp;                  /* cascade nasal pole */
	antireson rnz;              /* cascade nasal zero */
	reson rc[6];                /* cascade formants F1..F6 */
	int bypass_np, bypass_nz, bypass_r[6]; /* set when the resonator's frequency is >= K_BYPASS*sr */

	reson rp[6];                /* parallel formants F1..F6 */
	reson rnp_par;              /* parallel nasal formant */
	double a[6], an, ab;        /* linear parallel gains of the current frame */
	double par_prev;            /* previous parallel voicing input, for its first difference */
};

static double db_to_lin(double db)
{
	return db <= 0.0 ? 0.0 : pow(10.0, (db - 80.0) / 20.0);
}

/* White, approximately Gaussian noise: the sum of 16 uniform numbers (paper p. 977), centred on zero. */
static double noise(struct klatt_synth *s)
{
	double sum = 0.0;
	for (int i = 0; i < 16; i++) {
		s->rng = s->rng * 1664525u + 1013904223u;
		sum += (double)(s->rng >> 8) / 16777216.0;
	}
	return sum - 8.0;
}

static void clear_reson(reson *r) { r->y1 = r->y2 = 0.0; }
static void clear_anti(antireson *r) { r->x1 = r->x2 = 0.0; }

/* The LF settings for one period at f0 with shape rd (Fant 1997, Eq. A1: Rd -> Ra, Rk, Rg; Perrotin et al.
 * 2021, Eqs. C2-C5: glottal formant Fg, Bg and tilt cutoff Fa). Returns 0 when the values are degenerate. */
static int lf_setup(double sr, double f0, double rd, lf_shape *o)
{
	double t0 = 1.0 / f0;
	double ra = (-1.0 + 4.8 * rd) / 100.0, rk = (22.4 + 11.8 * rd) / 100.0;
	double den = 0.44 * rd - 4.0 * ra * (0.5 + 1.2 * rk);
	if (fabs(den) < 1e-6)
		return 0;
	double rg = rk * (0.5 + 1.2 * rk) / den;
	double oq = (1.0 + rk) / (2.0 * rg), am = 1.0 / (1.0 + rk), ta = ra * t0;
	double tn = tan(KLATT_PI * (1.0 - am));
	if (!isfinite(oq) || !(oq > 0.0) || !(ta > 0.0) || !isfinite(tn) || fabs(tn) < 1e-6)
		return 0;
	double fg = 1.0 / (2.0 * oq * t0), bg = 1.0 / (oq * t0 * tn), fa = 1.0 / (2.0 * KLATT_PI * ta);
	double r = exp(-KLATT_PI * bg / sr);
	o->a1 = -2.0 * r * cos(2.0 * KLATT_PI * fg / sr);
	o->a2 = r * r;
	o->p = exp(-2.0 * KLATT_PI * fa / sr);
	o->oq = oq < 1.0 ? oq : 1.0;
	return 1;
}

/* Magnitude of the LF flow filters (glottal formant times tilt) at f. */
static double lf_gain(const lf_shape *o, double f, double sr)
{
	double w = 2.0 * KLATT_PI * f / sr;
	double re = 1.0 + o->a1 * cos(w) + o->a2 * cos(2.0 * w);
	double im = -(o->a1 * sin(w) + o->a2 * sin(2.0 * w));
	double tr = 1.0 - o->p * cos(w), ti = o->p * sin(w);
	return (1.0 - o->p) / sqrt((re * re + im * im) * (tr * tr + ti * ti));
}

/* The LF source's level at min(3000 Hz, 0.45*SR) relative to its level at f0, in dB (0 if degenerate). */
static double lf_tilt_db(double sr, double f0, double rd)
{
	lf_shape o;
	if (!lf_setup(sr, f0, rd, &o))
		return 0.0;
	return 20.0 * log10(lf_gain(&o, fmin(3000.0, 0.45 * sr), sr) / lf_gain(&o, f0, sr));
}

/* The pulse size that gives a neutral vowel the same power for every Rd: a pulse train of size A at f0 through
 * H has power 2*A^2*(f0/sr)^2 * sum over harmonics of |H(k*f0)|^2, with H the radiated LF source (flow filters
 * times the first difference) through the default cascade (F1-F5 = 500/1500/2500/3500/3750 Hz, B1-B5 =
 * 50/70/110/250/200 Hz). Choosing that power to be f0/sr (one unit of energy per period, as a unit impulse
 * through a flat path) makes the level depend on F0 the way the 1980 source's does. */
static double lf_norm(const lf_shape *o, double sr, double f0)
{
	static const double nf[5] = { 500.0, 1500.0, 2500.0, 3500.0, 3750.0 };
	static const double nb[5] = { 50.0, 70.0, 110.0, 250.0, 200.0 };
	reson tract[5];
	for (int i = 0; i < 5; i++)
		reson_set(&tract[i], nf[i], nb[i], sr);
	double sum = 0.0;
	for (int k = 1; k * f0 < sr / 2.0; k++) {
		double f = k * f0, d = 2.0 * sin(KLATT_PI * f / sr), h = lf_gain(o, f, sr) * d;
		for (int i = 0; i < 5; i++)
			if (nf[i] < K_BYPASS * sr)
				h *= reson_gain(&tract[i], f, sr);
		sum += h * h;
	}
	return sum > 0.0 ? sqrt(sr / (2.0 * f0 * sum)) : 0.0;
}

/* The pole p of a unity-DC-gain low-pass (1-p)/(1 - p/z) whose level at fh relative to f0 is q_db (< 0) dB:
 * the root in [0, 1) of (1-q)p^2 - 2(c0 - q*ch)p + (1-q) = 0, with q = 10^(q_db/10), c = cos(2*pi*f/sr). */
static double tilt_pole(double q_db, double sr, double f0, double fh)
{
	double q = pow(10.0, q_db / 10.0);
	double c0 = cos(2.0 * KLATT_PI * f0 / sr), ch = cos(2.0 * KLATT_PI * fh / sr);
	double b = c0 - q * ch, disc = b * b - (1.0 - q) * (1.0 - q);
	if (!(q < 1.0))
		return 0.0;
	if (disc < 0.0) /* more than a one-pole low-pass can give: its steepest */
		return 0.999;
	double p = (b - sqrt(disc)) / (1.0 - q);
	return p > 0.0 && p < 1.0 ? p : 0.0;
}

/* RGP's level at fh relative to its level at f0, in dB, for bandwidth bw. */
static double rgp_tilt_db(double fgp, double bw, double sr, double f0, double fh)
{
	reson r;
	reson_set(&r, fgp, bw, sr);
	return 20.0 * log10(reson_gain(&r, fh, sr) / reson_gain(&r, f0, sr));
}

KLATT_API int KLATT_CALL KlattCreate(const double *globals, klatt_h *out)
{
	if (!globals || !out)
		return KLATT_ERR_ARG;
	struct klatt_synth *s = calloc(1, sizeof *s);
	if (!s)
		return KLATT_ERR_MEMORY;
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++)
		s->g[i] = klatt_clamp(KLATT_GLOBAL, i, globals[i]);
	s->sr = s->g[KG_SR];
	s->spf = s->sr * s->g[KG_FRAME_MS] / 1000.0;
	s->fmax = s->sr / 2.0 - 1.0;
	s->nfc = (int)lround(s->g[KG_NFC]);
	s->parallel_only = s->g[KG_SW] >= 0.5;
	s->lf = s->g[KG_SRC] >= 0.5;
	s->out_gain = K_OUT * pow(10.0, s->g[KG_G0] / 20.0);

	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	antireson_set(&s->rgz, fmin(s->g[KG_FGZ], s->fmax), s->g[KG_BGZ], s->sr);
	reson_set(&s->rgs, 0.0, s->g[KG_BGS], s->sr);
	reson_set(&s->rnp, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rnp_par, s->g[KG_FNP], s->g[KG_BNP], s->sr);
	reson_set(&s->rc[4], fmin(s->g[KG_F5], s->fmax), s->g[KG_B5], s->sr);
	reson_set(&s->rc[5], fmin(s->g[KG_F6], s->fmax), s->g[KG_B6], s->sr);
	s->rp[4] = s->rc[4];
	s->rp[5] = s->rc[5];
	s->bypass_np = s->g[KG_FNP] >= K_BYPASS * s->sr;
	s->bypass_r[4] = s->g[KG_F5] >= K_BYPASS * s->sr;
	s->bypass_r[5] = s->g[KG_F6] >= K_BYPASS * s->sr;
	KlattReset(s, 1);
	*out = s;
	return KLATT_OK;
}

KLATT_API void KLATT_CALL KlattReset(klatt_h s, unsigned seed)
{
	if (!s)
		return;
	s->frame = 0.0;
	s->rng = seed;
	s->overload = 0;
	s->phase = 1.0;
	s->av = s->avs = s->av_next = s->avs_next = s->f0_next = 0.0;
	s->rd = s->rd_next = 1.0;
	s->oq = 0.5;
	s->gy1 = s->gy2 = s->tz1 = s->lf_amp = s->v_prev = s->tp80 = s->ty80 = 0.0;
	s->lfs = (lf_shape){ 0.0, 0.0, 0.0, 0.5 };
	s->ah0 = s->ah1 = s->af0 = s->af1 = s->af_db = 0.0;
	s->par_prev = 0.0;
	reson_set(&s->rgp, s->g[KG_FGP], s->g[KG_BGP], s->sr);
	clear_reson(&s->rgp);
	clear_reson(&s->rgs);
	clear_anti(&s->rgz);
	clear_reson(&s->rnp);
	clear_anti(&s->rnz);
	clear_reson(&s->rnp_par);
	for (int k = 0; k < 6; k++) {
		clear_reson(&s->rc[k]);
		clear_reson(&s->rp[k]);
	}
}

KLATT_API void KLATT_CALL KlattDestroy(klatt_h s)
{
	free(s);
}

KLATT_API int KLATT_CALL KlattMaxFrameSamples(klatt_h s)
{
	return s ? (int)ceil(s->spf) : KLATT_ERR_ARG;
}

KLATT_API int KLATT_CALL KlattOverloadCount(klatt_h s)
{
	return s ? s->overload : KLATT_ERR_ARG;
}

/* Loads one frame's parameters into the filters and gains. */
static void load_frame(struct klatt_synth *s, const double *in)
{
	double p[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++)
		p[i] = klatt_clamp(KLATT_FRAME, i, in[i]);

	static const int fi[4] = { KP_F1, KP_F2, KP_F3, KP_F4 };
	double bw[4] = { p[KP_B1], p[KP_B2], p[KP_B3], s->g[KG_B4] };
	for (int k = 0; k < 4; k++) {
		s->bypass_r[k] = p[fi[k]] >= K_BYPASS * s->sr;
		reson_set(&s->rc[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
		reson_set(&s->rp[k], fmin(p[fi[k]], s->fmax), bw[k], s->sr);
	}
	s->bypass_nz = p[KP_FNZ] >= K_BYPASS * s->sr;
	antireson_set(&s->rnz, fmin(p[KP_FNZ], s->fmax), s->g[KG_BNZ], s->sr);

	s->av_next = db_to_lin(p[KP_AV]);
	s->avs_next = db_to_lin(p[KP_AVS]);
	s->rd_next = p[KP_RD];
	s->f0_next = p[KP_F0];

	/* AF and AH ramp linearly across the frame (paper p. 978), except that an AF rise of more than
	 * AF_JUMP_DB jumps at once, for plosive bursts. */
	s->ah0 = s->ah1;
	s->af0 = s->af1;
	s->ah1 = K_NOISE * db_to_lin(p[KP_AH]);
	s->af1 = K_NOISE * db_to_lin(p[KP_AF]);
	if (p[KP_AF] - s->af_db > AF_JUMP_DB)
		s->af0 = s->af1;
	s->af_db = p[KP_AF];

	for (int k = 0; k < 6; k++)
		s->a[k] = K_PAR * db_to_lin(p[KP_A1 + k]);
	s->an = K_PAR * db_to_lin(p[KP_AN]);
	s->ab = K_PAR * db_to_lin(p[KP_AB]);
}

/* Starts a glottal period at f0: latches the per-pulse values and sets the source filters for the period.
 * Returns the size of the pulse to issue (0 when the LF values are degenerate). */
static double start_period(struct klatt_synth *s, double f0)
{
	s->av = s->av_next;
	s->avs = s->avs_next;
	s->rd = s->rd_next;
	double amp = 1.0;
	if (s->lf) {
		if (lf_setup(s->sr, f0, s->rd, &s->lfs)) {
			s->lf_amp = lf_norm(&s->lfs, s->sr, f0);
			s->oq = s->lfs.oq;
		} else {
			amp = 0.0;
		}
	} else {
		/* Rd tilts the 1980 source by as much as the LF source's level at fh relative to F0 changes from
		 * Rd = 1 to this Rd. Brighter (Rd < 1): RGP widens, to the bandwidth (up to 0.45*SR, found by
		 * bisection) that gives that change. Darker (Rd > 1): RGP at its default BGP is already as steep
		 * as it gets, so a one-pole low-pass on the voicing gives the change instead. At Rd = 1 the change
		 * is exactly 0: RGP keeps BGP and the low-pass is off, so the output is unchanged. */
		double fh = fmin(3000.0, K_BYPASS * s->sr), fgp = s->g[KG_FGP], bw = s->g[KG_BGP];
		double d = lf_tilt_db(s->sr, f0, s->rd) - lf_tilt_db(s->sr, f0, 1.0);
		s->tp80 = 0.0;
		if (d > 0.0) {
			double want = rgp_tilt_db(fgp, bw, s->sr, f0, fh) + d;
			double lo = log(bw), hi = log(K_BYPASS * s->sr);
			for (int i = 0; i < 40; i++) {
				double mid = 0.5 * (lo + hi);
				if (rgp_tilt_db(fgp, exp(mid), s->sr, f0, fh) < want)
					lo = mid; /* a wider RGP is flatter */
				else
					hi = mid;
			}
			bw = exp(0.5 * (lo + hi));
		} else if (d < 0.0) {
			s->tp80 = tilt_pole(d, s->sr, f0, fh);
		}
		reson_set(&s->rgp, fgp, bw, s->sr);
		s->oq = 0.5;
	}
	return amp;
}

/* One output sample; frac is how far through the frame this sample is, in (0, 1]. */
static double sample(struct klatt_synth *s, double frac)
{
	/* Voicing: an impulse each time the glottal phase wraps past 1, F0 read fresh every sample so a
	 * change takes effect within the current period instead of waiting for it to finish. */
	double pulse = 0.0;
	int voiced = s->f0_next > 0.0;
	if (voiced) {
		s->phase += s->f0_next / s->sr;
		if (s->phase >= 1.0) {
			s->phase -= 1.0;
			pulse = start_period(s, s->f0_next);
		}
	} else {
		s->phase = 1.0;
		s->av = s->av_next;
		s->avs = s->avs_next;
	}

	/* The voicing source as glottal flow, then its first difference (lip radiation, moved here from the
	 * output so the noise can enter the tract white; see docs/klatt-dll.md). */
	double v;
	if (s->lf) {
		double y = pulse * s->lf_amp - s->lfs.a1 * s->gy1 - s->lfs.a2 * s->gy2;
		s->gy2 = s->gy1;
		s->gy1 = y;
		double z = (1.0 - s->lfs.p) * y + s->lfs.p * s->tz1;
		s->tz1 = z;
		v = K_LF * (s->av * z + s->avs * reson_step(&s->rgs, z));
	} else {
		double gp = reson_step(&s->rgp, pulse);
		double tv = (1.0 - s->tp80) * antireson_step(&s->rgz, gp) + s->tp80 * s->ty80; /* Rd's darkening */
		s->ty80 = tv;
		v = s->av * tv + s->avs * reson_step(&s->rgs, gp);
	}
	double voice = v - s->v_prev;
	s->v_prev = v;

	/* Noise, halved in the closed phase of each voiced period (from the open quotient on). */
	double n = noise(s);
	if (voiced && s->av > 0.0 && s->phase >= s->oq)
		n *= 0.5;
	double asp = (s->ah0 + (s->ah1 - s->ah0) * frac) * n;
	double fric = (s->af0 + (s->af1 - s->af0) * frac) * n;

	/* Cascade branch: voicing and aspiration (unless SW = 1). A resonator/antiresonator whose frequency is
	 * at or above K_BYPASS*sr is bypassed: its input passes through unchanged. */
	double casc = 0.0, par_voice = 0.0;
	if (s->parallel_only) {
		par_voice = voice + asp;
	} else {
		double x = voice + asp;
		if (!s->bypass_np) x = reson_step(&s->rnp, x);
		if (!s->bypass_nz) x = antireson_step(&s->rnz, x);
		for (int k = s->nfc - 1; k >= 0; k--)
			if (!s->bypass_r[k]) x = reson_step(&s->rc[k], x);
		casc = x;
	}

	/* Parallel branch: frication, plus voicing when SW = 1. F1 gets undifferenced voicing only;
	 * the others get the first difference of the voicing plus frication. Signs alternate. A bypassed
	 * resonator contributes nothing. */
	double dv = par_voice - s->par_prev;
	s->par_prev = par_voice;
	double src = dv + fric;
	double t0 = s->bypass_r[0] ? 0.0 : s->a[0] * reson_step(&s->rp[0], par_voice);
	double tn = s->bypass_np ? 0.0 : s->an * reson_step(&s->rnp_par, dv);
	double t1 = s->bypass_r[1] ? 0.0 : s->a[1] * reson_step(&s->rp[1], src);
	double t2 = s->bypass_r[2] ? 0.0 : s->a[2] * reson_step(&s->rp[2], src);
	double t3 = s->bypass_r[3] ? 0.0 : s->a[3] * reson_step(&s->rp[3], src);
	double t4 = s->bypass_r[4] ? 0.0 : s->a[4] * reson_step(&s->rp[4], src);
	double t5 = s->bypass_r[5] ? 0.0 : s->a[5] * reson_step(&s->rp[5], src);
	double par = t0 + tn - t1 + t2 - t3 + t4 - t5 + s->ab * src;

	return (casc + par) * s->out_gain;
}

KLATT_API int KLATT_CALL KlattSynthFrame(klatt_h s, const double *frame, short *out, int max)
{
	if (!s || !frame || !out)
		return KLATT_ERR_ARG;
	int n = (int)(floor((s->frame + 1.0) * s->spf + 1e-9) - floor(s->frame * s->spf + 1e-9));
	if (max < n)
		return KLATT_ERR_BUFFER;
	load_frame(s, frame);
	for (int i = 0; i < n; i++) {
		double v = floor(sample(s, (i + 1.0) / n) + 0.5);
		if (v > 32767.0) { v = 32767.0; s->overload++; }
		if (v < -32768.0) { v = -32768.0; s->overload++; }
		out[i] = (short)v;
	}
	s->frame += 1.0;
	return n;
}
```

- [ ] **Step 4: Update the calibration tool and check the constants**

Replace the whole of `tests/calibrate.c` with:

```c
/* calibrate.c - measures the unscaled levels that set K_OUT, K_NOISE, K_PAR and K_LF in klatt.c.
 * Built against the synthesizer source directly (not the DLL) so it can read the pre-quantization signal. */
#define KLATT_BUILD
#define K_OUT 1.0
#define K_NOISE 1.0
#define K_PAR 1.0
#define K_LF 1.0
#include "../klatt/params.c"
#include "../klatt/klatt.c"
#include <stdio.h>

static void measure(const double *frame, int sw, int src, double *peak, double *rms)
{
	double g[KLATT_NUM_GLOBAL];
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SW] = sw;
	g[KG_SRC] = src;
	klatt_h h;
	KlattCreate(g, &h);
	load_frame(h, frame);
	load_frame(h, frame); /* twice, so the AF/AH ramps start at their targets */
	int n = (int)h->sr; /* one second; skip the first 0.1 s */
	double pk = 0, sq = 0;
	int cnt = 0;
	for (int i = 0; i < n; i++) {
		double v = sample(h, 1.0);
		if (i < n / 10) continue;
		if (fabs(v) > pk) pk = fabs(v);
		sq += v * v;
		cnt++;
	}
	*peak = pk;
	*rms = sqrt(sq / cnt);
	KlattDestroy(h);
}

int main(void)
{
	double f[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		f[i] = pi.def;
	}
	double pv, rv, pa, ra, pp, rp, pl, rl;
	f[KP_AV] = 60;
	measure(f, 0, 0, &pv, &rv);            /* cascade vowel, AV = 60 */
	measure(f, 0, 1, &pl, &rl);            /* the same vowel from the LF source, Rd = 1 */
	f[KP_AV] = 0; f[KP_AH] = 60;
	measure(f, 0, 0, &pa, &ra);            /* aspiration only, AH = 60 */
	f[KP_AH] = 0; f[KP_AV] = 60;
	f[KP_A1] = f[KP_A2] = f[KP_A3] = f[KP_A4] = 60;
	measure(f, 1, 0, &pp, &rp);            /* all-parallel vowel, AV = A1..A4 = 60 */
	printf("cascade vowel: peak %.6g rms %.6g\n", pv, rv);
	printf("LF vowel:      peak %.6g rms %.6g\n", pl, rl);
	printf("aspiration:    peak %.6g rms %.6g\n", pa, ra);
	printf("parallel:      peak %.6g rms %.6g\n", pp, rp);
	printf("K_OUT   = %.4g  (cascade vowel peak -> 16000)\n", 16000.0 / pv);
	printf("K_NOISE = %.4g  (aspiration rms -> cascade vowel rms)\n", rv / ra);
	printf("K_PAR   = %.4g  (parallel vowel rms -> cascade vowel rms)\n", rv / rp);
	printf("K_LF    = %.4g  (LF vowel rms -> cascade vowel rms)\n", rv / rl);
	return 0;
}
```

Run: `MAKE calibrate`
Expected (the last four lines):

```
K_OUT   = 4.125e+05  (cascade vowel peak -> 16000)
K_NOISE = 0.000207  (aspiration rms -> cascade vowel rms)
K_PAR   = 5.995  (parallel vowel rms -> cascade vowel rms)
K_LF    = 0.005941  (LF vowel rms -> cascade vowel rms)
```

`K_LF` = 5.941e-3 is already in the new `klatt.c`.

- [ ] **Step 5: Run the tests on both architectures**

Run: `MAKE test`
Expected: `854 checks, 0 failures` twice; `xarch: 16537 samples, max difference 0 LSB: ok`.

- [ ] **Step 6: Commit**

```bash
git add klatt/klatt.c tests/calibrate.c tests/test_synth_source.c
git commit -m "Add the LF voicing source (SRC = 1) and Rd

Rd shapes the LF source (Fant 1997; Perrotin et al. 2021) and tilts the
1980 source by the same amount: wider RGP to brighten, a one-pole low-pass
to darken, unchanged at Rd = 1. LF pulses are sized so a neutral vowel has
the same power for every Rd; K_LF matches the 1980 source's level.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Flutter and diplophonia

**Files:**
- Modify: `klatt/klatt.c`, `tests/test_synth_source.c`, `tests/test_xarch.c` (replaced whole)

**Interfaces:**
- Consumes: Task 3's `start_period` and `sample`, `KG_FL`, `KP_DI`.
- Produces: the finished DLL 1.1 synthesizer.

- [ ] **Step 1: Write the failing tests**

In `tests/test_synth_source.c`, replace:

```c

static void test_noise_is_white(void)
```

with:

```c

/* Finds pulses as the largest |sample| of each cluster above thresh (clusters are at least 20 samples apart).
 * The source must be short-lived (a wide RGP). Returns the count. */
static int find_pulses(const short *x, int n, double thresh, int *pos, double *amp, int max)
{
	int np = 0;
	for (int i = 1; i < n - 1; i++) {
		double a = fabs((double)x[i]);
		if (a < thresh)
			continue;
		if (np && i - pos[np - 1] < 20) {
			if (a > amp[np - 1]) {
				pos[np - 1] = i;
				amp[np - 1] = a;
			}
		} else if (np < max) {
			pos[np] = i;
			amp[np++] = a;
		}
	}
	return np;
}

/* Flutter's F0 over four periods at a time (the phase accumulator makes that exact to a sample) against the
 * formula's average over the same stretch. */
static void test_flutter(void)
{
	static int pos[400];
	static double amp[400];
	int n = source_only(0, 100, 1.0, 2000, 0, 100, 400, buf); /* 2 s */
	double pk = m_peak(buf, n);
	int np = find_pulses(buf, n, 0.3 * pk, pos, amp, 400);
	CHECK(np > 150);
	double lo = 1e9, hi = -1e9, worst = 0;
	for (int i = 1; i + 4 < np; i += 4) {
		double got = 4 * SR / (pos[i + 4] - pos[i]), want = 0;
		for (int j = pos[i]; j < pos[i + 4]; j++) {
			double t = j / SR;
			want += 100 + 2 * (sin(2 * PI * 12.7 * t) + sin(2 * PI * 7.1 * t) + sin(2 * PI * 4.7 * t));
		}
		want /= pos[i + 4] - pos[i];
		lo = fmin(lo, want);
		hi = fmax(hi, want);
		worst = fmax(worst, fabs(got - want));
	}
	CHECK(hi - lo > 4);              /* flutter moves F0 by several Hz */
	CHECK(worst < 0.10 * (hi - lo)); /* and the pulses follow the formula */
}

static void test_diplophonia(void)
{
	static int pos[64];
	static double amp[64];
	int n = source_only(0, 100, 1.0, 2000, 50, 0, 200, buf);
	int np = find_pulses(buf, n, 0.2 * m_peak(buf, n), pos, amp, 64);
	double t0 = SR / 100, delay = 0.5 * (1 - 0.5) * t0; /* DI = 50 %, open quotient 0.5 */
	CHECK(np > 20);
	int ok_ratio = 1, ok_gap = 1;
	for (int i = 2; i + 1 < np; i += 2) { /* pulses 1, 3, ... (0-based) are the delayed ones */
		double r = amp[i + 1] / amp[i];
		if (fabs(r - 0.5) > 0.05)
			ok_ratio = 0;
		if (fabs(pos[i + 1] - pos[i] - (t0 + delay)) > 1.5)
			ok_gap = 0;
	}
	CHECK(ok_ratio);
	CHECK(ok_gap);
}

static void test_noise_is_white(void)
```

In `tests/test_synth_source.c`, replace:

```c
	test_lf_pitch_is_not_rounded();
	test_noise_is_white();
```

with:

```c
	test_lf_pitch_is_not_rounded();
	test_flutter();
	test_diplophonia();
	test_noise_is_white();
```

Replace the whole of `tests/test_xarch.c` with:

```c
/* Writes xarch.raw (in the working directory) for the 32/64-bit comparison done by tools/xarch_cmp.c. */
#include <stdio.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

/* 300 frames with a slow pitch glide, a moving F2 and an Rd glide, appended to buf at n; returns the new n. */
static int glide(const double *g, double *f, short *buf, int n, int max)
{
	klatt_h h;
	CHECK(KlattCreate(g, &h) == KLATT_OK);
	for (int k = 0; k < 300; k++) {
		f[KP_F0] = 90 + k * 0.2;
		f[KP_F2] = 1100 + k * 3;
		f[KP_RD] = 0.5 + k * (2.0 / 300);
		n += KlattSynthFrame(h, f, buf + n, max - n);
	}
	KlattDestroy(h);
	return n;
}

void suite_xarch(void)
{
	static short buf[40000];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
	t_default_globals(g);
	t_default_frame(f);
	f[KP_AV] = 60;
	f[KP_AH] = 40;
	f[KP_AF] = 50;
	f[KP_A3] = 50;
	f[KP_A4] = 50;
	f[KP_FNZ] = 330;
	int n = glide(g, f, buf, 0, 40000);    /* the 1980 source, Rd tilting it */
	g[KG_SRC] = 1;
	g[KG_FL] = 50;
	f[KP_DI] = 30;
	n = glide(g, f, buf, n, 40000);        /* the LF source with flutter and diplophonia */
	FILE *fp = fopen("xarch.raw", "wb");
	CHECK(fp != NULL);
	if (fp) {
		CHECK(fwrite(buf, sizeof(short), n, fp) == (size_t)n);
		fclose(fp);
	}
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: 3 failures out of 861 checks: `test_synth_source.c:175` (F0 does not follow the flutter formula), `:194`
and `:195` (no alternating pulse sizes or delays).

- [ ] **Step 3: Add flutter and diplophonia to the synthesizer**

In `klatt/klatt.c`, replace:

```c
/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995), with the 1.1 extension of an LF voicing source driven by Rd
 * (Fant 1997; Perrotin et al. 2021).
 * See docs/klatt-dll.md for the signal path and calibration. */
```

with:

```c
/* klatt.c - Klatt (1980) cascade/parallel formant synthesizer, implemented from the paper's equations and
 * block diagram (D.H. Klatt, JASA 67, 971-995), with the 1.1 extensions: an LF voicing source driven by Rd
 * (Fant 1997; Perrotin et al. 2021), flutter and diplophonia (Klatt & Klatt 1990, JASA 87, 820-857).
 * See docs/klatt-dll.md for the signal path and calibration. */
```

In `klatt/klatt.c`, replace:

```c
	double frame;               /* frames synthesized since the last reset */
	uint32_t rng;
```

with:

```c
	double frame;               /* frames synthesized since the last reset */
	double nsamp;               /* samples synthesized since the last reset, for flutter */
	uint32_t rng;
```

In `klatt/klatt.c`, replace:

```c
	/* voicing source; phase is the glottal phase in periods, wrapped to [0,1) while voiced, and pinned
	 * at 1.0 while unvoiced so voicing starts with a pulse at once; av/avs/rd latch at each pulse */
	double phase;
	double av, avs, rd, av_next, avs_next, rd_next, f0_next;
	double oq;                  /* open quotient of the current period: the LF value, or 0.5 */
	reson rgp, rgs;
```

with:

```c
	/* voicing source; phase is the glottal phase in periods, wrapped to [0,1) while voiced, and pinned
	 * at 1.0 while unvoiced so voicing starts with a pulse at once; av/avs/rd/di latch at each pulse */
	double phase;
	double av, avs, rd, di, av_next, avs_next, rd_next, di_next, f0_next;
	double oq;                  /* open quotient of the current period: the LF value, or 0.5 */
	int pulses;                 /* pulses since voicing started, for diplophonia's alternation */
	double pending_at, pending_amp; /* a delayed (diplophonic) pulse: the phase it fires at, or -1 */
	reson rgp, rgs;
```

In `klatt/klatt.c`, replace:

```c
		return;
	s->frame = 0.0;
	s->rng = seed;
```

with:

```c
		return;
	s->frame = s->nsamp = 0.0;
	s->rng = seed;
```

In `klatt/klatt.c`, replace:

```c
	s->rd = s->rd_next = 1.0;
	s->oq = 0.5;
	s->gy1 = s->gy2 = s->tz1 = s->lf_amp = s->v_prev = s->tp80 = s->ty80 = 0.0;
```

with:

```c
	s->rd = s->rd_next = 1.0;
	s->di = s->di_next = 0.0;
	s->oq = 0.5;
	s->pulses = 0;
	s->pending_at = -1.0;
	s->pending_amp = 0.0;
	s->gy1 = s->gy2 = s->tz1 = s->lf_amp = s->v_prev = s->tp80 = s->ty80 = 0.0;
```

In `klatt/klatt.c`, replace:

```c
	s->rd_next = p[KP_RD];
	s->f0_next = p[KP_F0];
```

with:

```c
	s->rd_next = p[KP_RD];
	s->di_next = p[KP_DI];
	s->f0_next = p[KP_F0];
```

In `klatt/klatt.c`, replace:

```c
/* Starts a glottal period at f0: latches the per-pulse values and sets the source filters for the period.
 * Returns the size of the pulse to issue (0 when the LF values are degenerate). */
static double start_period(struct klatt_synth *s, double f0)
```

with:

```c
/* Starts a glottal period at f0: latches the per-pulse values and sets the source filters for the period.
 * Returns the size of the pulse to issue now; a diplophonic pulse is instead scheduled for later. */
static double start_period(struct klatt_synth *s, double f0)
```

In `klatt/klatt.c`, replace:

```c
	s->rd = s->rd_next;
	double amp = 1.0;
```

with:

```c
	s->rd = s->rd_next;
	s->di = s->di_next;
	double amp = 1.0;
```

In `klatt/klatt.c`, replace:

```c
	}
	return amp;
```

with:

```c
	}
	/* Diplophonia (Klatt & Klatt 1990, p. 840): every second pulse is delayed by DI% of the closed phase
	 * and attenuated by DI%, so at 100 % it closes as the next pulse opens (and vanishes). */
	s->pulses++;
	if (s->di > 0.0 && s->pulses % 2 == 0) {
		s->pending_at = s->di / 100.0 * (1.0 - s->oq);
		s->pending_amp = amp * (1.0 - s->di / 100.0);
		return 0.0;
	}
	return amp;
```

In `klatt/klatt.c`, replace:

```c
	/* Voicing: an impulse each time the glottal phase wraps past 1, F0 read fresh every sample so a
	 * change takes effect within the current period instead of waiting for it to finish. */
	double pulse = 0.0;
	int voiced = s->f0_next > 0.0;
	if (voiced) {
		s->phase += s->f0_next / s->sr;
		if (s->phase >= 1.0) {
			s->phase -= 1.0;
			pulse = start_period(s, s->f0_next);
		}
```

with:

```c
	/* Voicing: an impulse each time the glottal phase wraps past 1, F0 read fresh every sample so a
	 * change takes effect within the current period instead of waiting for it to finish. Flutter
	 * (Klatt & Klatt 1990, Eq. 1) wobbles F0 by three slow sinusoids. */
	double f0 = s->f0_next;
	if (f0 > 0.0 && s->g[KG_FL] > 0.0) {
		double t = s->nsamp / s->sr;
		f0 += s->g[KG_FL] / 50.0 * f0 / 100.0 *
		      (sin(2.0 * KLATT_PI * 12.7 * t) + sin(2.0 * KLATT_PI * 7.1 * t) + sin(2.0 * KLATT_PI * 4.7 * t));
		f0 = klatt_clamp(KLATT_FRAME, KP_F0, f0);
	}
	s->nsamp += 1.0;

	double pulse = 0.0;
	int voiced = s->f0_next > 0.0;
	if (voiced) {
		s->phase += f0 / s->sr;
		if (s->phase >= 1.0) {
			s->phase -= 1.0;
			if (s->pending_at >= 0.0) { /* a delayed pulse that the new period overtook fires now */
				pulse += s->pending_amp;
				s->pending_at = -1.0;
			}
			pulse += start_period(s, f0 > 0.0 ? f0 : s->f0_next);
		}
		if (s->pending_at >= 0.0 && s->phase >= s->pending_at) {
			pulse += s->pending_amp;
			s->pending_at = -1.0;
		}
```

In `klatt/klatt.c`, replace:

```c
		s->avs = s->avs_next;
	}
```

with:

```c
		s->avs = s->avs_next;
		s->pulses = 0;
		s->pending_at = -1.0;
	}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `861 checks, 0 failures` twice; `xarch: 33074 samples, max difference 0 LSB: ok` (the comparison now also
covers the LF source, flutter and diplophonia).

- [ ] **Step 5: Commit**

```bash
git add klatt/klatt.c tests/test_synth_source.c tests/test_xarch.c
git commit -m "Add flutter (FL) and diplophonia (DI)

Both follow Klatt & Klatt (1990): FL wobbles F0 by three slow sinusoids,
DI delays and weakens every second pulse. The 32/64-bit comparison now also
runs the LF source, flutter, diplophonia and an Rd glide.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Editor support

**Files:**
- Modify: `kpe/model.c`, `kpe/model.h`, `kpe/view.c`, `kpe/main.c`, `kpe/strips.c`, `kpe/dialogs.c`, `kpe/kpe.rc`,
  `kpe/resource.h`, `tests/test_model.c`, `tests/test_view.c`

**Interfaces:**
- Consumes: `KP_RD`, `KP_DI`, `KG_SRC` (Task 1).
- Produces: `double doc_param_step(int param)` in `kpe/model.h` (0.01 for Rd, 1 otherwise), used by `strips.c`
  and by plan 5.

- [ ] **Step 1: Write the failing tests**

In `tests/test_model.c`, replace:

```c

static void test_insert_delete_copy(void)
```

with:

```c

static void test_paint_rounds_rd_to_hundredths(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	CHECK_NEAR(doc_param_step(KP_RD), 0.01, 0);
	CHECK_NEAR(doc_param_step(KP_F1), 1, 0);
	doc_paint(&d, KP_RD, 10, 1.234, 30, 1.234); /* frames 2..6 */
	CHECK(doc_get(&d, 2, KP_RD) == 1.23); /* exactly the double 1.23, so the CSV says 1.23 */
	CHECK(doc_get(&d, 6, KP_RD) == 1.23);
	doc_paint(&d, KP_RD, 50, 2.3456, 50, 2.3456); /* frame 10 */
	CHECK(doc_get(&d, 10, KP_RD) == 2.35);
	CHECK(doc_get(&d, 7, KP_RD) == 1.0); /* untouched: the default */
	doc_free(&d);
}

static void test_insert_delete_copy(void)
```

In `tests/test_model.c`, replace:

```c
	g[KG_SR] = 999999; /* clamped to 20000 */
	CHECK(doc_set_globals(&d, g) == 0);
	CHECK_NEAR(d.globals[KG_G0], 60, 0);
	CHECK_NEAR(d.globals[KG_SR], 20000, 0);
	CHECK(d.nframes == 20);
```

with:

```c
	g[KG_SR] = 999999; /* clamped to 20000 */
	g[KG_SRC] = 0.7;   /* a whole number: rounded to 1 */
	CHECK(doc_set_globals(&d, g) == 0);
	CHECK_NEAR(d.globals[KG_G0], 60, 0);
	CHECK_NEAR(d.globals[KG_SR], 20000, 0);
	CHECK_NEAR(d.globals[KG_SRC], 1, 0);
	CHECK(d.nframes == 20);
```

In `tests/test_model.c`, replace:

```c
	test_paint_interpolates_and_rounds();
	test_insert_delete_copy();
```

with:

```c
	test_paint_interpolates_and_rounds();
	test_paint_rounds_rd_to_hundredths();
	test_insert_delete_copy();
```

In `tests/test_view.c`, replace:

```c
	int n = view_layout(vis, 0, 300, 50, 4, s);
	CHECK(n == 18); /* 17 parameter strips + the formant strip */
	CHECK(s[5].param == FORMANT_STRIP && s[5].height == 100);
}
```

with:

```c
	int n = view_layout(vis, 0, 300, 50, 4, s);
	CHECK(n == 20); /* 19 parameter strips + the formant strip */
	CHECK(s[5].param == KP_RD && s[6].param == KP_DI); /* the voice strips follow the source strips */
	CHECK(s[7].param == FORMANT_STRIP && s[7].height == 100);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_model.c:181:20: error: implicit declaration of function
'doc_param_step'`.

- [ ] **Step 3: Paint step and SRC rounding in the model**

In `kpe/model.h`, replace:

```c
double doc_get(const kpe_doc *d, int frame, int param);

```

with:

```c
double doc_get(const kpe_doc *d, int frame, int param);
/* The step painted values are rounded to: 0.01 for Rd, 1 (whole units) for every other frame parameter. */
double doc_param_step(int param);

```

In `kpe/model.h`, replace:

```c
/* Sets the frames between t0_ms and t1_ms (either order) by linear interpolation from v0 to v1, rounded to
 * whole units. Frame k sits at k * FRAME_MS. Used for paint strokes and the line tool. */
void doc_paint(kpe_doc *d, int param, double t0_ms, double v0, double t1_ms, double v1);
```

with:

```c
/* Sets the frames between t0_ms and t1_ms (either order) by linear interpolation from v0 to v1, rounded to
 * doc_param_step. Frame k sits at k * FRAME_MS. Used for paint strokes and the line tool. */
void doc_paint(kpe_doc *d, int param, double t0_ms, double v0, double t1_ms, double v1);
```

In `kpe/model.h`, replace:

```c
int doc_copy_frames(const kpe_doc *d, int first, int count, double *out); /* no undo step */
/* Replaces the globals (clamped, and SR/NFC/SW rounded to the nearest integer since they are whole numbers).
 * If FRAME_MS changes, the frames are resampled by linear interpolation so the duration stays the same.
```

with:

```c
int doc_copy_frames(const kpe_doc *d, int first, int count, double *out); /* no undo step */
/* Replaces the globals (clamped, and SR/NFC/SW/SRC rounded to the nearest integer since they are whole numbers).
 * If FRAME_MS changes, the frames are resampled by linear interpolation so the duration stays the same.
```

In `kpe/model.c`, replace:

```c

/* Clamps a global to its range; SR, NFC and SW are whole numbers, so they are then rounded to the nearest
 * integer (half away from zero). */
```

with:

```c

/* Clamps a global to its range; SR, NFC, SW and SRC are whole numbers, so they are then rounded to the nearest
 * integer (half away from zero). */
```

In `kpe/model.c`, replace:

```c
	v = clamp_param(KLATT_GLOBAL, index, v);
	if (index == KG_SR || index == KG_NFC || index == KG_SW)
		v = round(v);
	return v;
}
```

with:

```c
	v = clamp_param(KLATT_GLOBAL, index, v);
	if (index == KG_SR || index == KG_NFC || index == KG_SW || index == KG_SRC)
		v = round(v);
	return v;
}

double doc_param_step(int param)
{
	return param == KP_RD ? 0.01 : 1.0;
}

/* Rounds v to the parameter's step (half away from zero). Scaling by the step's inverse, a whole number, and
 * dividing back gives the double nearest the decimal value (1.23, not 1.2300000000000002). */
static double round_to_step(int param, double v)
{
	double per_unit = round(1.0 / doc_param_step(param));
	return round(v * per_unit) / per_unit;
}
```

In `kpe/model.c`, replace:

```c
		double v = t1_ms == t0_ms ? v1 : v0 + (v1 - v0) * (t - t0_ms) / (t1_ms - t0_ms);
		set_cell(d, k, param, round(v));
	}
```

with:

```c
		double v = t1_ms == t0_ms ? v1 : v0 + (v1 - v0) * (t - t0_ms) / (t1_ms - t0_ms);
		set_cell(d, k, param, round_to_step(param, v));
	}
```

- [ ] **Step 4: Rd and DI strips after AVS**

In `kpe/view.c`, replace:

```c

static const int before[] = { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS };
static const int after[] = { KP_B1, KP_B2, KP_B3, KP_FNZ, KP_AN, KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB };
```

with:

```c

static const int before[] = { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, KP_RD, KP_DI };
static const int after[] = { KP_B1, KP_B2, KP_B3, KP_FNZ, KP_AN, KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB };
```

- [ ] **Step 5: The left panel: Voice group, scrolling, SRC**

The two new rows would push the globals text off a default-size window (S(760) high), so the panel scrolls.

In `kpe/main.c`, replace:

```c
{
	static const int shown[] = { KG_SR, KG_FRAME_MS, KG_NFC, KG_SW, KG_G0, KG_F5, KG_F6 };
	wchar_t t[512];
```

with:

```c
{
	static const int shown[] = { KG_SR, KG_FRAME_MS, KG_SRC, KG_NFC, KG_SW, KG_G0, KG_F5, KG_F6 };
	wchar_t t[512];
```

In `kpe/main.c`, replace:

```c
	{ "Source", { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, -1 } },
	{ "Formants (on spectrogram)", { KP_F1, KP_F2, KP_F3, KP_F4, -1 } },
```

with:

```c
	{ "Source", { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, -1 } },
	{ "Voice", { KP_RD, KP_DI, -1 } },
	{ "Formants (on spectrogram)", { KP_F1, KP_F2, KP_F3, KP_F4, -1 } },
```

In `kpe/main.c`, replace:

```c

static LRESULT CALLBACK panel_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= IDC_CHECK_BASE &&
```

with:

```c

static int panel_content_h, panel_pos; /* the panel's content height and vertical scroll position, in pixels */

/* Scrolls the panel's children so that content row pos is at the top (clamped), and updates the scrollbar,
 * which Windows hides while everything fits. */
static void panel_scroll_to(HWND h, int pos)
{
	RECT rc = { 0 };
	GetClientRect(h, &rc);
	if (pos > panel_content_h - rc.bottom)
		pos = panel_content_h - rc.bottom;
	if (pos < 0)
		pos = 0;
	if (pos != panel_pos) {
		ScrollWindowEx(h, 0, panel_pos - pos, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
		panel_pos = pos;
	}
	SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, panel_content_h - 1, (UINT)rc.bottom, panel_pos, 0 };
	SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void panel_vscroll(HWND h, int code)
{
	RECT rc = { 0 };
	GetClientRect(h, &rc);
	int pos = panel_pos;
	switch (code) {
	case SB_LINEUP: pos -= S(19); break;
	case SB_LINEDOWN: pos += S(19); break;
	case SB_PAGEUP: pos -= rc.bottom; break;
	case SB_PAGEDOWN: pos += rc.bottom; break;
	case SB_TOP: pos = 0; break;
	case SB_BOTTOM: pos = panel_content_h; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: {
		SCROLLINFO si = { sizeof si, SIF_TRACKPOS, 0, 0, 0, 0, 0 };
		GetScrollInfo(h, SB_VERT, &si);
		pos = si.nTrackPos;
		break;
	}
	}
	panel_scroll_to(h, pos);
}

static LRESULT CALLBACK panel_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		panel_scroll_to(h, panel_pos);
		return 0;
	case WM_VSCROLL:
		panel_vscroll(h, LOWORD(wp));
		return 0;
	case WM_MOUSEWHEEL:
		panel_scroll_to(h, panel_pos - GET_WHEEL_DELTA_WPARAM(wp) * 3 * S(19) / WHEEL_DELTA);
		return 0;
	}
	if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= IDC_CHECK_BASE &&
```

In `kpe/main.c`, replace:

```c
{
	app.panel = CreateWindowExW(0, L"KpePanel", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
	                            app.main, NULL, app.inst, NULL);
	int y = S(6), x = S(8), w = S(150);
```

with:

```c
{
	app.panel = CreateWindowExW(0, L"KpePanel", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0,
	                            0, app.main, NULL, app.inst, NULL);
	int y = S(6), x = S(8), w = S(150);
```

In `kpe/main.c`, replace:

```c
	globals_text = child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, y + S(4), w, S(18) * 9, app.panel, 2);
}
```

with:

```c
	globals_text = child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, y + S(4), w, S(18) * 9, app.panel, 2);
	panel_content_h = y + S(4) + S(18) * 9 + S(6);
}
```

In `kpe/main.c`, replace:

```c
	GetWindowRect(app.status, &sr);
	int sh = sr.bottom - sr.top, pw = S(170);
	int parts[3] = { rc.right - S(560), rc.right - S(340), -1 };
```

with:

```c
	GetWindowRect(app.status, &sr);
	int sh = sr.bottom - sr.top, pw = S(170) + GetSystemMetrics(SM_CXVSCROLL); /* room for the panel's scrollbar */
	int parts[3] = { rc.right - S(560), rc.right - S(340), -1 };
```

- [ ] **Step 6: Strips: colour, axis title, pointer value**

In `kpe/strips.c`, replace:

```c
	case KP_AVS: return RGB(160, 80, 160);
	case KP_F1: case KP_F2: case KP_F3: case KP_F4: return RGB(216, 90, 48);
```

with:

```c
	case KP_AVS: return RGB(160, 80, 160);
	case KP_RD: case KP_DI: return RGB(190, 120, 20);
	case KP_F1: case KP_F2: case KP_F3: case KP_F4: return RGB(216, 90, 48);
```

In `kpe/strips.c`, replace:

```c
		KlattParamInfo(KLATT_FRAME, s->param, &pi);
	swprintf(t, 64, L"%hs (%hs)", pi.name, pi.unit);
	HFONT of = SelectObject(dc, vfont);
```

with:

```c
		KlattParamInfo(KLATT_FRAME, s->param, &pi);
	if (strcmp(pi.unit, "-") == 0)
		swprintf(t, 64, L"%hs", pi.name); /* unitless (Rd) */
	else
		swprintf(t, 64, L"%hs (%hs)", pi.name, pi.unit);
	HFONT of = SelectObject(dc, vfont);
```

In `kpe/strips.c`, replace:

```c
			KlattParamInfo(KLATT_FRAME, s->param, &pi);
			len += swprintf(t + len, 256 - len, L" \x00B7 %hs %g %hs \x00B7 pointer %g", pi.name,
			                doc_get(&app.doc, k, s->param), pi.unit, round(vm_v(y, lo, hi, s->top, s->height)));
		}
```

with:

```c
			KlattParamInfo(KLATT_FRAME, s->param, &pi);
			double step = doc_param_step(s->param);
			len += swprintf(t + len, 256 - len, L" \x00B7 %hs %g %hs \x00B7 pointer %g", pi.name,
			                doc_get(&app.doc, k, s->param), pi.unit,
			                round(vm_v(y, lo, hi, s->top, s->height) / step) * step);
		}
```

- [ ] **Step 7: Optimize tolerances for Rd and %**

In `kpe/resource.h`, replace:

```c
#define IDC_SUMMARY       2109
#define IDC_LIST          2201
```

with:

```c
#define IDC_SUMMARY       2109
#define IDC_TOL_RD        2110
#define IDC_TOL_PCT       2111
#define IDC_LIST          2201
```

In `kpe/kpe.rc`, replace:

```
	COMBOBOX IDC_GRID, 168, 58, 50, 60, CBS_DROPDOWNLIST | WS_TABSTOP
	GROUPBOX "Tolerance", -1, 105, 76, 110, 62
	LTEXT "Formants, bandwidths (Hz):", -1, 111, 89, 70, 16
```

with:

```
	COMBOBOX IDC_GRID, 168, 58, 50, 60, CBS_DROPDOWNLIST | WS_TABSTOP
	GROUPBOX "Tolerance", -1, 105, 76, 110, 94
	LTEXT "Formants, bandwidths (Hz):", -1, 111, 89, 70, 16
```

In `kpe/kpe.rc`, replace:

```
	EDITTEXT IDC_TOL_DB, 180, 120, 30, 12
	PUSHBUTTON "Preview", IDC_PREVIEW, 105, 146, 50, 14
	EDITTEXT IDC_SUMMARY, 222, 7, 111, 211, ES_MULTILINE | ES_READONLY | WS_VSCROLL
```

with:

```
	EDITTEXT IDC_TOL_DB, 180, 120, 30, 12
	LTEXT "Rd:", -1, 111, 137, 60, 8
	EDITTEXT IDC_TOL_RD, 180, 136, 30, 12
	LTEXT "DI (%):", -1, 111, 153, 60, 8
	EDITTEXT IDC_TOL_PCT, 180, 152, 30, 12
	PUSHBUTTON "Preview", IDC_PREVIEW, 105, 178, 50, 14
	EDITTEXT IDC_SUMMARY, 222, 7, 111, 211, ES_MULTILINE | ES_READONLY | WS_VSCROLL
```

In `kpe/dialogs.c`, replace:

```c

static double tolerance_for(int p, double hz, double f0, double db)
{
	if (p == KP_F0)
		return f0;
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	return strcmp(pi.unit, "dB") == 0 ? db : hz;
}
```

with:

```c

/* The tolerance fields, in the order of tol_ids: Hz (formants and bandwidths), F0, dB, Rd and %. */
enum { TOL_HZ, TOL_F0, TOL_DB, TOL_RD, TOL_PCT, NTOL };
static const int tol_ids[NTOL] = { IDC_TOL_HZ, IDC_TOL_F0, IDC_TOL_DB, IDC_TOL_RD, IDC_TOL_PCT };

static double tolerance_for(int p, const double *tol)
{
	if (p == KP_F0)
		return tol[TOL_F0];
	if (p == KP_RD)
		return tol[TOL_RD];
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	if (strcmp(pi.unit, "dB") == 0)
		return tol[TOL_DB];
	return strcmp(pi.unit, "%") == 0 ? tol[TOL_PCT] : tol[TOL_HZ];
}
```

In `kpe/dialogs.c`, replace:

```c
{
	double hz, f0, db;
	if (!get_number(dlg, IDC_TOL_HZ, &hz) || hz < 0)
		return reject(dlg, IDC_TOL_HZ, L"Enter a tolerance of 0 or more.");
	if (!get_number(dlg, IDC_TOL_F0, &f0) || f0 < 0)
		return reject(dlg, IDC_TOL_F0, L"Enter a tolerance of 0 or more.");
	if (!get_number(dlg, IDC_TOL_DB, &db) || db < 0)
		return reject(dlg, IDC_TOL_DB, L"Enter a tolerance of 0 or more.");
	if (!discard_preview())
```

with:

```c
{
	double tol[NTOL];
	for (int i = 0; i < NTOL; i++)
		if (!get_number(dlg, tol_ids[i], &tol[i]) || tol[i] < 0)
			return reject(dlg, tol_ids[i], L"Enter a tolerance of 0 or more.");
	if (!discard_preview())
```

In `kpe/dialogs.c`, replace:

```c
		double err;
		int kept = opt_simplify(v, count, first, grid, tolerance_for(p, hz, f0, db), &err);
		for (int i = 0; i < count; i++)
```

with:

```c
		double err;
		int kept = opt_simplify(v, count, first, grid, tolerance_for(p, tol), &err);
		for (int i = 0; i < count; i++)
```

In `kpe/dialogs.c`, replace:

```c
		SendMessageW(grid, CB_SETCURSEL, 0, 0);
		set_number(dlg, IDC_TOL_HZ, 20);
		set_number(dlg, IDC_TOL_F0, 2);
		set_number(dlg, IDC_TOL_DB, 1);
		previewed = 0;
```

with:

```c
		SendMessageW(grid, CB_SETCURSEL, 0, 0);
		static const double tol_default[NTOL] = { 20, 2, 1, 0.05, 2 };
		for (int i = 0; i < NTOL; i++)
			set_number(dlg, tol_ids[i], tol_default[i]);
		previewed = 0;
```

- [ ] **Step 8: Run the tests on both architectures**

Run: `MAKE test`
Expected: `870 checks, 0 failures` twice; `xarch: 33074 samples, max difference 0 LSB: ok`; `build/32/kpe.exe`
built without warnings.

- [ ] **Step 9: Run the UI scripts and the Python sample**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui/check_main.ps1`
Expected: ends with `saved ui_after_cancel`. Then look at `build/ui/ui_optimize.png` (the Tolerance box lists
Rd 0.05 and DI (%) 2 below Amplitudes, with Preview under it) and `build/ui/ui_after_cancel.png` (a "Voice" group
with Rd and DI after Source, and a scrollbar on the left panel).

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui/check_edit.ps1`
Expected: ends with `message box after play: 0`.

Run: `python samples/python/klatt.py build/64/klatt64.dll build/glide.wav`
Expected: `wrote 5512 samples to build/glide.wav` (the sample reads the parameter counts from the DLL).

- [ ] **Step 10: Commit**

```bash
git add kpe/model.c kpe/model.h kpe/view.c kpe/main.c kpe/strips.c kpe/dialogs.c kpe/kpe.rc kpe/resource.h tests/test_model.c tests/test_view.c
git commit -m "Editor support for Rd, DI, SRC and FL

Rd is painted in steps of 0.01; Rd and DI get a Voice group and strips after
AVS; the left panel scrolls when it is taller than the window; Optimize has
tolerances for Rd and %; SRC is a whole number and shows in the panel.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Documentation

**Files:**
- Modify: `docs/klatt-dll.md`, `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, `docs/manual-checklist.md`,
  `README.md`

- [ ] **Step 1: The DLL guide**

In `docs/klatt-dll.md`, replace:

```markdown
D.H. Klatt (1980), "Software for a cascade/parallel formant synthesizer", JASA 67, 971-995. They are built from
the same source and produce the same samples.

```

with:

```markdown
D.H. Klatt (1980), "Software for a cascade/parallel formant synthesizer", JASA 67, 971-995. They are built from
the same source and produce the same samples. Version 1.1 adds a selectable Liljencrants-Fant (LF) voicing source
shaped by Rd, Rd's tilt on the 1980 source, flutter (FL) and diplophonia (DI), and brings the noise source in line
with the paper; see "Signal path" below.

```

In `docs/klatt-dll.md`, replace:

```markdown
|---|---|
| `KlattVersion()` | `KLATT_VERSION` (10000 = 1.0.0) |
| `KlattParamCount(kind)` | 21 for `KLATT_FRAME`, 18 for `KLATT_GLOBAL` |
| `KlattParamInfo(kind, i, &info)` | name, unit, description, min, max, default |
```

with:

```markdown
|---|---|
| `KlattVersion()` | `KLATT_VERSION` (10100 = 1.1.0) |
| `KlattParamCount(kind)` | 23 for `KLATT_FRAME`, 20 for `KLATT_GLOBAL` |
| `KlattParamInfo(kind, i, &info)` | name, unit, description, min, max, default |
```

In `docs/klatt-dll.md`, replace:

```markdown
| `KlattDestroy(h)` | frees the handle |

```

with:

```markdown
| `KlattDestroy(h)` | frees the handle |

Version 1.1 appended Rd and DI to the frame parameters and SRC and FL to the globals; no earlier index changed. A
program built against the 1.0 header passes arrays two values short and must be rebuilt.

```

In `docs/klatt-dll.md`, replace:

```markdown

- **Voicing:** an impulse each time the glottal phase (which advances by F0/SR per sample) wraps past 1, through
  the glottal resonator RGP (FGP, BGP). That goes through the glottal antiresonator RGZ (FGZ, BGZ) scaled by AV,
  and through RGS (FGP, BGS) scaled by AVS. F0 is read every frame, so a change takes effect within the current
  period; AV and AVS are latched at each pulse.
- **Noise:** a sum of four uniform random numbers, low-pass filtered (`y = x + 0.9·y[n-1]`), halved in the second
  half of each voiced period. AH scales it into aspiration, AF into frication.
- **Cascade** (SW = 0): voicing + aspiration through the nasal pole (FNP, BNP), nasal zero (FNZ, BNZ) and
```

with:

```markdown

- **Pulses:** an impulse each time the glottal phase (which advances by F0/SR per sample) wraps past 1. F0 is read
  every sample, so a change takes effect within the current period; AV, AVS, Rd and DI are latched at each pulse.
  **FL** (flutter, a global) adds (FL/50)·(F0/100)·[sin 2π·12.7t + sin 2π·7.1t + sin 2π·4.7t] Hz to F0, t in
  seconds since create or reset (Klatt & Klatt 1990, Eq. 1). **DI** (diplophonia) delays every second pulse by
  DI% of the closed phase and weakens it by DI%, so at 100 % it vanishes (Klatt & Klatt 1990, p. 840).
- **Voicing, SRC = 0 (Klatt 1980, the default):** the pulses go through the glottal resonator RGP (FGP, BGP), then
  the glottal antiresonator RGZ (FGZ, BGZ) scaled by AV, and RGS (0 Hz, BGS) scaled by AVS. **Rd** tilts this
  source by as much as it tilts the LF source (level near 3 kHz relative to F0, from Rd = 1): below 1 RGP widens,
  above 1 a one-pole low-pass darkens the AV voicing, and at 1 nothing changes.
- **Voicing, SRC = 1 (LF):** at each pulse Rd sets Ra, Rk and Rg (Fant 1997) and from them a two-pole glottal
  formant and a one-pole tilt low-pass (Perrotin et al. 2021). The pulse is sized so that a neutral vowel gets the
  same power for every Rd, and `K_LF` matches it to the 1980 source's level. AV scales it, and RGS gives AVS.
- **Radiation:** the voicing is computed as glottal flow and its first difference is taken here, before the
  branches, instead of at the output.
- **Noise:** the sum of 16 uniform random numbers (paper p. 977), white, halved in the closed phase of each voiced
  period (from the open quotient on: 0.5 for SRC = 0, the LF value for SRC = 1). AH scales it into aspiration, AF
  into frication; both ramp linearly across each frame, except that AF jumps when it rises by more than 50 dB.
- **Cascade** (SW = 0): voicing + aspiration through the nasal pole (FNP, BNP), nasal zero (FNZ, BNZ) and
```

In `docs/klatt-dll.md`, replace:

```markdown
  Signs alternate: +R1 -R2 +R3 -R4 +R5 -R6.
- **Output:** first difference (lip radiation), gain `K_OUT·10^(G0/20)`, rounded and saturated to 16 bits.
- **Amplitudes:** 0 dB is off; otherwise the linear gain is `10^((dB-80)/20)`.
```

with:

```markdown
  Signs alternate: +R1 -R2 +R3 -R4 +R5 -R6.
- **Output:** gain `K_OUT·10^(G0/20)`, rounded and saturated to 16 bits.
- **Amplitudes:** 0 dB is off; otherwise the linear gain is `10^((dB-80)/20)`.
```

In `docs/klatt-dll.md`, replace:

```markdown
| `K_OUT` | 4.125e5 | neutral vowel, AV = 60, G0 = 47: peak about 16000 |
| `K_NOISE` | 4.071e-4 | AH = 60 alone has the RMS of AV = 60 alone |
| `K_PAR` | 6.0 | SW = 1 with A1-A4 = 60 has the RMS of the cascade vowel |

```

with:

```markdown
| `K_OUT` | 4.125e5 | neutral vowel, AV = 60, G0 = 47: peak about 16000 |
| `K_NOISE` | 2.070e-4 | AH = 60 alone has the RMS of AV = 60 alone |
| `K_PAR` | 6.0 | SW = 1 with A1-A4 = 60 has the RMS of the cascade vowel |
| `K_LF` | 5.941e-3 | SRC = 1 with Rd = 1 has the RMS of the cascade vowel |

```

- [ ] **Step 2: The base spec (the Part A items of the new spec's section 5)**

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown

`C:\Users\abart\Desktop\KPE\kpe_open`, its own git repository.

```

with:

```markdown

`C:\Users\abart\Desktop\KPE\kpe_open`, its own git repository.

```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown

- **Voicing source:** an impulse train at F0, started pitch-synchronously, through the glottal resonator RGP
```

with:

```markdown

Version 1.1 of the DLL changed this section: see `2026-09-29-voice-source-and-wav-import-design.md`, section 3, for
the selectable LF source (SRC, Rd), Rd's tilt on the 1980 source, flutter (FL), diplophonia (DI), the 16-sum white
noise with AF/AH ramps, and the radiation difference taken on the voicing instead of the output. The bullets below
describe the 1980 source as it still is.

- **Voicing source:** an impulse train at F0, started pitch-synchronously, through the glottal resonator RGP
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown

Per-frame parameters (21) and globals (18), following Table I of the paper. The paper's SR and NWS become `SR`
and `FRAME_MS`.

```

with:

```markdown

Per-frame parameters (23) and globals (20): Table I of the paper, plus Rd and DI (frame) and SRC and FL (global)
added in DLL 1.1. The paper's SR and NWS become `SR` and `FRAME_MS`.

```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
| global | NFC | number of cascade formants | - | 4 | 6 | 5 |

The frame parameter order is AV AF AH AVS F0 F1 F2 F3 F4 FNZ AN A1 A2 A3 A4 A5 A6 AB B1 B2 B3 (indices 0-20).
The global order is as listed (indices 0-17).

```

with:

```markdown
| global | NFC | number of cascade formants | - | 4 | 6 | 5 |
| frame | Rd | voice quality: pressed to breathy (step 0.01) | - | 0.3 | 2.7 | 1 |
| frame | DI | diplophonia | % | 0 | 100 | 0 |
| global | SRC | voicing source: 0 = Klatt 1980, 1 = LF | - | 0 | 1 | 0 |
| global | FL | flutter | % | 0 | 100 | 0 |

The frame parameter order is AV AF AH AVS F0 F1 F2 F3 F4 FNZ AN A1 A2 A3 A4 A5 A6 AB B1 B2 B3 Rd DI (indices
0-22). The global order is as listed, SW to NFC, then SRC and FL (indices 0-19).

```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
int         KlattVersion(void);
int         KlattParamCount(int kind);                          /* 21 or 18 */
int         KlattParamInfo(int kind, int index, klatt_param_info *out);
```

with:

```markdown
int         KlattVersion(void);
int         KlattParamCount(int kind);                          /* 23 or 20 (1.0: 21 or 18) */
int         KlattParamInfo(int kind, int index, klatt_param_info *out);
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
- **Menu bar:** File, Edit, Sound, Parameters, View, Help.
- **Left panel:** a checkbox per parameter, grouped (Source: F0, AV, AF, AH, AVS; Cascade: F1-F4 on the
  spectrogram, B1-B3; Parallel: A1-A6, AB; Nasal: FNZ, AN), then the globals as read-only text.
- **Strips:** one strip per ticked parameter, stacked, each with its own Y axis showing units and automatically
```

with:

```markdown
- **Menu bar:** File, Edit, Sound, Parameters, View, Help.
- **Left panel:** a checkbox per parameter, grouped (Source: F0, AV, AF, AH, AVS; Voice: Rd, DI; Cascade: F1-F4
  on the spectrogram, B1-B3; Parallel: A1-A6, AB; Nasal: FNZ, AN), then the globals as read-only text. The panel
  scrolls vertically when it is taller than the window.
- **Strips:** one strip per ticked parameter, stacked, each with its own Y axis showing units and automatically
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
  grid (every frame, 5 ms or 10 ms; grids that are not a multiple of FRAME_MS are disabled), and a tolerance per
  unit: 20 Hz for formants and bandwidths, 2 Hz for F0, 1 dB for amplitudes.
- **Algorithm:** Douglas-Peucker restricted to grid times. It keeps the fewest breakpoints for which linear
```

with:

```markdown
  grid (every frame, 5 ms or 10 ms; grids that are not a multiple of FRAME_MS are disabled), and a tolerance per
  unit: 20 Hz for formants and bandwidths, 2 Hz for F0, 1 dB for amplitudes, 0.05 for Rd, 2 % for DI.
- **Algorithm:** Douglas-Peucker restricted to grid times. It keeps the fewest breakpoints for which linear
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
- **Saving** writes both files, each to a temporary file first and then renamed over the target. The frame file
  always has all 21 columns in table order.
- **Prompts:** unsaved changes prompt before New, Open and Exit.
```

with:

```markdown
- **Saving** writes both files, each to a temporary file first and then renamed over the target. The frame file
  always has all 23 columns in table order.
- **Prompts:** unsaved changes prompt before New, Open and Exit.
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
- **Cross-architecture:** 32- and 64-bit output for the same input agree within 1 LSB.
- **Editor logic** (`model.c`, `csvio.c`, `optimize.c`): CSV round trip (load, save, load gives identical data);
```

with:

```markdown
- **Cross-architecture:** 32- and 64-bit output for the same input agree within 1 LSB.
- **Voice source (DLL 1.1):** the LF source's harmonics against its formulas, its level across Rd, Rd's tilt on the
  1980 source, flutter, diplophonia, white noise and the AF/AH ramps (`tests/test_synth_source.c`).
- **Editor logic** (`model.c`, `csvio.c`, `optimize.c`): CSV round trip (load, save, load gives identical data);
```

- [ ] **Step 3: The manual checklist and the README**

In `docs/manual-checklist.md`, replace:

```markdown
- [ ] Ctrl+wheel zooms around the pointer; the wheel and the scrollbar pan; Ctrl+0 fits the whole file.

```

with:

```markdown
- [ ] Ctrl+wheel zooms around the pointer; the wheel and the scrollbar pan; Ctrl+0 fits the whole file.
- [ ] Ticking Rd and DI (group "Voice") adds their strips right after AVS; the Rd axis shows 1 and 2 and the
      status bar shows the pointer's Rd with two decimals.
- [ ] At the default window size the left panel shows a scrollbar; the wheel over the panel and the scrollbar
      reach the last global line (F6). Enlarging the window until everything fits hides the scrollbar.

```

In `docs/manual-checklist.md`, replace:

```markdown
      a neutral "uh".
- [ ] Once the listening checks are approved (and after any intended change to the synthesizer), run
```

with:

```markdown
      a neutral "uh".
- [ ] Listening, voice source: with Globals SRC = 1, `ba.csv` still sounds like "ba" at about the same loudness;
      painting Rd from 0.5 to 2.5 moves the voice from pressed to breathy in both SRC = 0 and SRC = 1; FL = 50 adds
      a slow natural wobble; DI = 50 over the last 200 ms gives a rough, creaky ending.
- [ ] Once the listening checks are approved (and after any intended change to the synthesizer), run
```

In `docs/manual-checklist.md`, replace:

```markdown
- [ ] Optimize > Preview > Cancel leaves the title without * and Redo disabled.
- [ ] Help > Parameter reference lists all 21 frame parameters and 18 globals.

```

with:

```markdown
- [ ] Optimize > Preview > Cancel leaves the title without * and Redo disabled.
- [ ] Parameters > Optimize... has Rd and DI (%) tolerances (0.05 and 2).
- [ ] Help > Parameter reference lists all 23 frame parameters and 20 globals.

```

In `README.md`, replace:

```markdown
An open-source, 32-bit Windows editor for Klatt (1980) synthesis parameters, and a Klatt 1980 formant
synthesizer DLL (32- and 64-bit) that other programs can use.

```

with:

```markdown
An open-source, 32-bit Windows editor for Klatt (1980) synthesis parameters, and a Klatt 1980 formant
synthesizer DLL (32- and 64-bit) that other programs can use, with an optional LF voicing source and the flutter
and diplophonia controls of Klatt & Klatt (1990).

```

In `README.md`, replace:

```markdown
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
- Design: `docs/superpowers/specs/2026-09-28-kpe-open-design.md`.

```

with:

```markdown
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
- Design: `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, and for the voice source and WAV import
  `docs/superpowers/specs/2026-09-29-voice-source-and-wav-import-design.md`.

```

- [ ] **Step 4: Check nothing else changed**

Run: `MAKE test`
Expected: as after Task 5 (`870 checks, 0 failures` twice, `0 LSB`).

- [ ] **Step 5: Commit**

```bash
git add docs/klatt-dll.md docs/superpowers/specs/2026-09-28-kpe-open-design.md docs/manual-checklist.md README.md
git commit -m "Document DLL 1.1: LF source, Rd, flutter, diplophonia, noise

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Listening approval and the recording (needs the user)

This task is done by the controller with the user; no implementer is dispatched.

- [ ] **Step 1: Ask the user to listen**

Ask the user to open `samples/ba.csv` in `build/32/kpe.exe` and press Space, then to go through the "Listening,
voice source" item of `docs/manual-checklist.md` (SRC = 1, painting Rd, FL = 50, DI = 50). Stop here until the
user answers.

- [ ] **Step 2: After approval, record and commit**

Run: `MAKE golden`
Then: `MAKE test`
Expected: `872 checks, 0 failures` twice (the golden suite compares again).

```bash
git add tests/golden/ba.raw
git commit -m "Approve the golden recording of samples/ba.csv with DLL 1.1

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

If the user does not approve, record what they heard as a finding and stop; do not record the file.
