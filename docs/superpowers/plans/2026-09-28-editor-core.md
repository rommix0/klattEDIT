# Editor core: implementation plan (plan 2 of 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the parts of the Klatt Parameter Editor that contain no user interface, each tested without a
GUI:
- the document model with edits and undo;
- the CSV file pair;
- track optimization;
- whole-document synthesis, WAV output and the spectrogram.

**Architecture:** Portable C files in `kpe/`, compiled into the existing test runner for both architectures. The
Win32 GUI (plan 3) only calls these functions. `kpe_doc` holds the globals and a frames × parameters matrix.
Every change goes through functions that record undo steps: value edits as cell changes, and structural edits
as frame splices. `csvio.c` uses only kernel32 file functions (`MoveFileExW`); nothing here touches windows,
GDI or sound.

**Tech Stack:** C (gnu11), winlibs GCC 14.2, GNU make, the Klatt DLL from plan 1.

**Spec:** `docs/superpowers/specs/2026-09-28-kpe-open-design.md` (sections 5.2, 5.3, 5.4, 5.6, 5.7, 6, 8).
**Prerequisite:** plan 1 (`docs/superpowers/plans/2026-09-28-klatt-dll.md`) completed; `MAKE test` passes.

**Status of the code below:** compiled and run before the plan was written. The full suite passed on both
architectures (582 checks, 0 failures) with no compiler warnings.

## Global Constraints

- Everything in plan 1's Global Constraints applies: Git Bash, `MAKE` = `/c/winlibs/mingw32/bin/make.exe`,
  `-std=gnu11 -O2 -Wall -Wextra` with zero warnings, tabs and K&R braces, and the commit trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Files in `kpe/` covered by this plan must not include `<windows.h>`, except `csvio.c`, which may use kernel32
  file functions only. The GUI files of plan 3 (`main.c`, `strips.c`, `table.c`, `dialogs.c`, `audio.c`) are
  excluded from the test build by name.
- File paths are `wchar_t` (UTF-16, as Windows uses). Messages for the user are UTF-8 `char` strings.
- Frame parameter values are doubles in physical units, clamped to the DLL's parameter limits
  (`KlattParamInfo`).
- The CSV format is exactly as in spec section 5.7. Error messages name the file, line and column, as in the
  tests below.

## File map

| File | Responsibility |
|---|---|
| `kpe/model.h`, `kpe/model.c` | `kpe_doc`: globals, frames, value edits, transactions, structural edits, undo/redo |
| `kpe/csvio.h`, `kpe/csvio.c` | load and save `name.csv` + `name.globals.csv` |
| `kpe/optimize.h`, `kpe/optimize.c` | Douglas-Peucker on a grid for one track |
| `kpe/synthrun.h`, `kpe/synthrun.c` | synthesize a whole document through the DLL |
| `kpe/wavout.h`, `kpe/wavout.c` | write 16-bit mono WAV |
| `kpe/spectro.h`, `kpe/spectro.c` | FFT spectrogram and grey levels |
| `tests/test_model.c`, `test_csvio.c`, `test_optimize.c`, `test_audio.c` | one suite each |

---

### Task 1: Document model with undo

**Files:**
- Create: `kpe/model.h`, `kpe/model.c`, `tests/test_model.c`
- Modify: `Makefile` (compile `kpe/` core files into the test runner), `tests/run_tests.c`

**Interfaces:**
- Consumes: `klatt.h` (`KlattParamInfo`, `KLATT_NUM_FRAME`, `KLATT_NUM_GLOBAL`, `KP_*`, `KG_*`);
  `t_default_globals` from `tests/measure.h`.
- Produces: everything declared in `kpe/model.h` below. Plan 3 relies on `kpe_doc` fields `globals`, `nframes`,
  `frames`, `dirty`; `NP`/`NG`; `doc_begin`/`doc_end` around a paint stroke; `doc_paint`; `doc_set`;
  `doc_insert_frames`/`doc_delete_frames`/`doc_copy_frames`; `doc_set_globals`; `doc_undo`/`doc_redo` and their
  `can_`/`label` queries.

- [ ] **Step 1: Compile the core files into the test runner**

In the `Makefile`, after the `TEST_SRC := ...` line, add:

```make
GUI_SRC   := $(addprefix kpe/,main.c strips.c table.c dialogs.c audio.c)
CORE_SRC  := $(filter-out $(GUI_SRC),$(wildcard kpe/*.c))
```

and replace the `$(TESTS)` rule with:

```make
$(TESTS): $(TEST_SRC) $(CORE_SRC) $(wildcard tests/*.h kpe/*.h) $(DLL) | $(OUT)
	$(CC) $(CFLAGS) -Itests -Ikpe -o $@ $(TEST_SRC) $(CORE_SRC) $(IMPLIB) -static
```

- [ ] **Step 2: Write the failing model tests**

`tests/test_model.c`:

```c
#include <string.h>
#include "test.h"
#include "measure.h"
#include "model.h"

static void new_doc(kpe_doc *d, double ms)
{
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(d, g, ms) == 0);
}

static void test_new_document(void)
{
	kpe_doc d;
	new_doc(&d, 800);
	CHECK(d.nframes == 160);
	CHECK_NEAR(doc_duration_ms(&d), 800, 1e-9);
	CHECK_NEAR(doc_get(&d, 0, KP_AV), 0, 0);
	CHECK_NEAR(doc_get(&d, 159, KP_F1), 500, 0);
	CHECK_NEAR(doc_get(&d, 80, KP_F0), 100, 0);
	CHECK(!d.dirty);
	CHECK(!doc_can_undo(&d) && !doc_can_redo(&d));
	CHECK(strcmp(doc_undo_label(&d), "") == 0);
	doc_free(&d);
	new_doc(&d, 1); /* rounds to at least one frame */
	CHECK(d.nframes == 1);
	doc_free(&d);
}

static void test_set_clamps_and_undoes(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	doc_set(&d, 3, KP_F1, 99999);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 1300, 0);
	CHECK(d.dirty);
	CHECK(doc_can_undo(&d));
	doc_undo(&d);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 500, 0);
	CHECK(doc_can_redo(&d));
	doc_redo(&d);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 1300, 0);
	doc_set(&d, 99, KP_F1, 700); /* out of range frame: ignored, no step */
	doc_undo(&d);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 500, 0);
	CHECK(!doc_can_undo(&d));
	doc_free(&d);
}

static void test_transaction_is_one_step(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	doc_begin(&d, "Paint");
	doc_set(&d, 1, KP_F2, 1600);
	doc_set(&d, 2, KP_F2, 1700);
	doc_set(&d, 1, KP_F2, 1800); /* the same cell twice: undo still restores the first old value */
	CHECK(!doc_can_undo(&d));    /* nothing to undo while the transaction is open */
	doc_end(&d);
	CHECK(strcmp(doc_undo_label(&d), "Paint") == 0);
	CHECK_NEAR(doc_get(&d, 1, KP_F2), 1800, 0);
	doc_undo(&d);
	CHECK_NEAR(doc_get(&d, 1, KP_F2), 1500, 0);
	CHECK_NEAR(doc_get(&d, 2, KP_F2), 1500, 0);
	CHECK(!doc_can_undo(&d));
	doc_free(&d);
}

static void test_no_op_transaction_leaves_no_step(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	doc_begin(&d, "Paint");
	doc_set(&d, 1, KP_F2, 1600);
	doc_set(&d, 1, KP_F2, 1500); /* back to where it was */
	doc_end(&d);
	CHECK(!doc_can_undo(&d));
	CHECK(!d.dirty);
	doc_free(&d);
}

static void test_new_edit_clears_redo(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	doc_set(&d, 0, KP_AV, 60);
	doc_undo(&d);
	CHECK(doc_can_redo(&d));
	doc_set(&d, 0, KP_AV, 50);
	CHECK(!doc_can_redo(&d));
	doc_free(&d);
}

static void test_paint_interpolates_and_rounds(void)
{
	kpe_doc d;
	new_doc(&d, 100); /* 20 frames at 0, 5, ... 95 ms */
	doc_paint(&d, KP_AV, 10, 0, 30, 61); /* frames 2..6 */
	CHECK_NEAR(doc_get(&d, 2, KP_AV), 0, 0);
	CHECK_NEAR(doc_get(&d, 3, KP_AV), 15, 0); /* 15.25 rounded */
	CHECK_NEAR(doc_get(&d, 4, KP_AV), 31, 0); /* 30.5 rounded away from zero */
	CHECK_NEAR(doc_get(&d, 6, KP_AV), 61, 0);
	CHECK_NEAR(doc_get(&d, 7, KP_AV), 0, 0);
	CHECK(strcmp(doc_undo_label(&d), "Paint") == 0);
	doc_paint(&d, KP_F1, 52, 800, 52, 800); /* a single point: the nearest frame (10) */
	CHECK_NEAR(doc_get(&d, 10, KP_F1), 800, 0);
	CHECK_NEAR(doc_get(&d, 11, KP_F1), 500, 0);
	doc_paint(&d, KP_F1, 30, 900, 10, 700); /* right to left works too */
	CHECK_NEAR(doc_get(&d, 2, KP_F1), 700, 0);
	CHECK_NEAR(doc_get(&d, 6, KP_F1), 900, 0);
	doc_paint(&d, KP_F1, -50, 600, 500, 600); /* clipped to the document */
	CHECK_NEAR(doc_get(&d, 0, KP_F1), 600, 0);
	CHECK_NEAR(doc_get(&d, 19, KP_F1), 600, 0);
	doc_free(&d);
}

static void test_insert_delete_copy(void)
{
	kpe_doc d;
	double buf[3 * NP];
	new_doc(&d, 50); /* 10 frames */
	for (int k = 0; k < 10; k++)
		doc_set(&d, k, KP_F0, 100 + k);
	CHECK(doc_insert_frames(&d, 4, 2, NULL) == 0); /* copies of frame 4 */
	CHECK(d.nframes == 12);
	CHECK_NEAR(doc_get(&d, 4, KP_F0), 104, 0);
	CHECK_NEAR(doc_get(&d, 5, KP_F0), 104, 0);
	CHECK_NEAR(doc_get(&d, 6, KP_F0), 104, 0);
	CHECK_NEAR(doc_get(&d, 7, KP_F0), 105, 0);
	CHECK(strcmp(doc_undo_label(&d), "Insert frames") == 0);
	doc_undo(&d);
	CHECK(d.nframes == 10);
	CHECK_NEAR(doc_get(&d, 5, KP_F0), 105, 0);
	doc_redo(&d);
	CHECK(d.nframes == 12);

	CHECK(doc_copy_frames(&d, 0, 3, buf) == 0);
	CHECK(doc_delete_frames(&d, 0, 3) == 0);
	CHECK(d.nframes == 9);
	CHECK_NEAR(doc_get(&d, 0, KP_F0), 103, 0);
	CHECK(doc_insert_frames(&d, 9, 3, buf) == 0); /* paste at the end */
	CHECK(d.nframes == 12);
	CHECK_NEAR(doc_get(&d, 11, KP_F0), 102, 0);
	doc_undo(&d);
	doc_undo(&d);
	CHECK(d.nframes == 12);
	CHECK_NEAR(doc_get(&d, 0, KP_F0), 100, 0);

	CHECK(doc_delete_frames(&d, 0, 12) == -1); /* one frame must remain */
	CHECK(doc_delete_frames(&d, 10, 5) == -1);
	CHECK(doc_insert_frames(&d, 13, 1, NULL) == -1);
	CHECK(doc_copy_frames(&d, 11, 2, buf) == -1);
	doc_free(&d);
}

static void test_globals_without_resample(void)
{
	kpe_doc d;
	double g[NG];
	new_doc(&d, 100);
	memcpy(g, d.globals, sizeof g);
	g[KG_G0] = 60;
	g[KG_SR] = 999999; /* clamped to 20000 */
	CHECK(doc_set_globals(&d, g) == 0);
	CHECK_NEAR(d.globals[KG_G0], 60, 0);
	CHECK_NEAR(d.globals[KG_SR], 20000, 0);
	CHECK(d.nframes == 20);
	doc_undo(&d);
	CHECK_NEAR(d.globals[KG_G0], 47, 0);
	CHECK_NEAR(d.globals[KG_SR], 11025, 0);
	CHECK(doc_set_globals(&d, d.globals) == 0); /* unchanged: no step */
	CHECK(!doc_can_undo(&d));
	doc_free(&d);
}

static void test_frame_ms_change_resamples(void)
{
	kpe_doc d;
	double g[NG];
	new_doc(&d, 100); /* 20 frames of 5 ms */
	for (int k = 0; k < 20; k++)
		doc_set(&d, k, KP_F0, 100 + 10 * k); /* a straight ramp in time */
	memcpy(g, d.globals, sizeof g);
	g[KG_FRAME_MS] = 10;
	CHECK(doc_set_globals(&d, g) == 1);
	CHECK(d.nframes == 10);
	CHECK_NEAR(doc_duration_ms(&d), 100, 1e-9);
	CHECK_NEAR(doc_get(&d, 3, KP_F0), 160, 1e-9); /* 30 ms was frame 6 */
	g[KG_FRAME_MS] = 4;
	CHECK(doc_set_globals(&d, g) == 1);
	CHECK(d.nframes == 25);
	CHECK_NEAR(doc_get(&d, 5, KP_F0), 140, 1e-9); /* 20 ms was 10 ms frame 2 */
	doc_undo(&d);
	doc_undo(&d);
	CHECK(d.nframes == 20);
	CHECK_NEAR(d.globals[KG_FRAME_MS], 5, 0);
	CHECK_NEAR(doc_get(&d, 7, KP_F0), 170, 0);
	doc_free(&d);
}

void suite_model(void)
{
	test_new_document();
	test_set_clamps_and_undoes();
	test_transaction_is_one_step();
	test_no_op_transaction_leaves_no_step();
	test_new_edit_clears_redo();
	test_paint_interpolates_and_rounds();
	test_insert_delete_copy();
	test_globals_without_resample();
	test_frame_ms_change_resamples();
}
```

In `tests/run_tests.c`, add `void suite_model(void);` before `void suite_xarch(void);`, and
`{ "model", suite_model },` before `{ "xarch", suite_xarch },` (tab-indented like the others). The xarch suite
stays last.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: FAIL to compile: `model.h: No such file or directory`.

- [ ] **Step 4: Write the model**

`kpe/model.h`:

```c
/* model.h - the editor's document: globals plus a frames x parameters matrix, edit operations and undo.
 * No Win32 code; the GUI and the tests both drive it through these functions. */
#ifndef KPE_MODEL_H
#define KPE_MODEL_H

#include "klatt.h"

#define NP KLATT_NUM_FRAME  /* parameters per frame */
#define NG KLATT_NUM_GLOBAL /* global parameters */

typedef struct undo_step undo_step;

typedef struct {
	double globals[NG];
	int nframes;
	double *frames;       /* nframes * NP values, frame-major: frames[k * NP + p] */
	int dirty;            /* changed since the last save or load */

	undo_step **undo;     /* undo[0..nundo-1], newest last */
	int nundo, capundo;
	undo_step **redo;     /* redo[0..nredo-1], next redo last */
	int nredo, capredo;
	undo_step *open;      /* the step being built between doc_begin and doc_end, or NULL */
	unsigned char *touched; /* per cell: already recorded in the open step */
} kpe_doc;

/* Lifecycle. doc_new: duration rounded to whole frames (at least 1); frames at their defaults, amplitudes 0.
 * doc_init_from: takes ownership of frames (malloc'd, nframes * NP). Both return 0, or -1 when out of memory. */
int doc_new(kpe_doc *d, const double *globals, double duration_ms);
int doc_init_from(kpe_doc *d, const double *globals, int nframes, double *frames);
void doc_free(kpe_doc *d);

double doc_frame_ms(const kpe_doc *d);
double doc_duration_ms(const kpe_doc *d);
double doc_get(const kpe_doc *d, int frame, int param);

/* Value edits happen inside a transaction; each transaction becomes one undo step (none if nothing changed). */
void doc_begin(kpe_doc *d, const char *label);
void doc_end(kpe_doc *d);
/* Sets one value, clamped to the parameter's range. Outside a transaction it is its own undo step. */
void doc_set(kpe_doc *d, int frame, int param, double v);
/* Sets the frames between t0_ms and t1_ms (either order) by linear interpolation from v0 to v1, rounded to
 * whole units. Frame k sits at k * FRAME_MS. Used for paint strokes and the line tool. */
void doc_paint(kpe_doc *d, int param, double t0_ms, double v0, double t1_ms, double v1);

/* Structural edits; each is one undo step. Return 0, or -1 on bad arguments or out of memory. */
int doc_insert_frames(kpe_doc *d, int at, int count, const double *data); /* data NULL: copies of frame at (or the last) */
int doc_delete_frames(kpe_doc *d, int first, int count);                  /* at least one frame always remains */
int doc_copy_frames(const kpe_doc *d, int first, int count, double *out); /* no undo step */
/* Replaces the globals (clamped). If FRAME_MS changes, the frames are resampled by linear interpolation so the
 * duration stays the same. Returns 1 if resampled, 0 if not, -1 when out of memory. */
int doc_set_globals(kpe_doc *d, const double *globals);

int doc_can_undo(const kpe_doc *d);
int doc_can_redo(const kpe_doc *d);
const char *doc_undo_label(const kpe_doc *d); /* "" when there is nothing to undo */
const char *doc_redo_label(const kpe_doc *d);
void doc_undo(kpe_doc *d);
void doc_redo(kpe_doc *d);

#endif
```

`kpe/model.c`:

```c
/* model.c - document, edit operations and undo. See model.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

typedef struct {
	int index;       /* frame * NP + param */
	double old, now;
} cell_change;

struct undo_step {
	char label[40];
	cell_change *cells;
	int ncells, capcells;
	int splice;                  /* frames [at, at + old_count) were replaced by new_count frames */
	int at, old_count, new_count;
	double *old_data, *new_data;
	int globals;                 /* the globals changed from old_g to new_g */
	double old_g[NG], new_g[NG];
};

static double clamp_param(int kind, int index, double v)
{
	klatt_param_info pi;
	KlattParamInfo(kind, index, &pi);
	if (!(v >= pi.min))
		return pi.min;
	return v > pi.max ? pi.max : v;
}

static void step_free(undo_step *s)
{
	if (!s)
		return;
	free(s->cells);
	free(s->old_data);
	free(s->new_data);
	free(s);
}

static undo_step *step_new(const char *label)
{
	undo_step *s = calloc(1, sizeof *s);
	if (s) {
		strncpy(s->label, label ? label : "", sizeof s->label - 1);
	}
	return s;
}

static int push(undo_step ***arr, int *n, int *cap, undo_step *s)
{
	if (*n == *cap) {
		int nc = *cap ? *cap * 2 : 16;
		undo_step **p = realloc(*arr, nc * sizeof *p);
		if (!p)
			return -1;
		*arr = p;
		*cap = nc;
	}
	(*arr)[(*n)++] = s;
	return 0;
}

static void clear_redo(kpe_doc *d)
{
	for (int i = 0; i < d->nredo; i++)
		step_free(d->redo[i]);
	d->nredo = 0;
}

/* Records a finished step: pushes it on the undo stack and forgets the redo history. */
static void record(kpe_doc *d, undo_step *s)
{
	if (push(&d->undo, &d->nundo, &d->capundo, s) < 0) {
		step_free(s); /* out of memory: the edit stays, it just cannot be undone */
		return;
	}
	clear_redo(d);
	d->dirty = 1;
}

int doc_init_from(kpe_doc *d, const double *globals, int nframes, double *frames)
{
	memset(d, 0, sizeof *d);
	for (int i = 0; i < NG; i++)
		d->globals[i] = clamp_param(KLATT_GLOBAL, i, globals[i]);
	d->nframes = nframes;
	d->frames = frames;
	return 0;
}

int doc_new(kpe_doc *d, const double *globals, double duration_ms)
{
	double g[NG];
	for (int i = 0; i < NG; i++)
		g[i] = clamp_param(KLATT_GLOBAL, i, globals[i]);
	int n = (int)lround(duration_ms / g[KG_FRAME_MS]);
	if (n < 1)
		n = 1;
	double *f = malloc(sizeof(double) * NP * n);
	if (!f)
		return -1;
	for (int p = 0; p < NP; p++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		for (int k = 0; k < n; k++)
			f[k * NP + p] = pi.def;
	}
	for (int k = 0; k < n; k++)
		f[k * NP + KP_AV] = f[k * NP + KP_AF] = f[k * NP + KP_AH] = f[k * NP + KP_AVS] = 0;
	return doc_init_from(d, g, n, f);
}

void doc_free(kpe_doc *d)
{
	for (int i = 0; i < d->nundo; i++)
		step_free(d->undo[i]);
	clear_redo(d);
	step_free(d->open);
	free(d->undo);
	free(d->redo);
	free(d->touched);
	free(d->frames);
	memset(d, 0, sizeof *d);
}

double doc_frame_ms(const kpe_doc *d)
{
	return d->globals[KG_FRAME_MS];
}

double doc_duration_ms(const kpe_doc *d)
{
	return d->nframes * d->globals[KG_FRAME_MS];
}

double doc_get(const kpe_doc *d, int frame, int param)
{
	return d->frames[frame * NP + param];
}

void doc_begin(kpe_doc *d, const char *label)
{
	if (d->open)
		return;
	d->open = step_new(label);
	d->touched = calloc((size_t)d->nframes * NP, 1);
	if (!d->open || !d->touched) {
		step_free(d->open);
		free(d->touched);
		d->open = NULL;
		d->touched = NULL;
	}
}

void doc_end(kpe_doc *d)
{
	undo_step *s = d->open;
	if (!s)
		return;
	d->open = NULL;
	free(d->touched);
	d->touched = NULL;
	int n = 0; /* drop cells that ended where they started */
	for (int i = 0; i < s->ncells; i++)
		if (s->cells[i].old != s->cells[i].now)
			s->cells[n++] = s->cells[i];
	s->ncells = n;
	if (n == 0)
		step_free(s);
	else
		record(d, s);
}

/* Sets a cell inside the open transaction, remembering its first old value. */
static void set_cell(kpe_doc *d, int frame, int param, double v)
{
	if (frame < 0 || frame >= d->nframes || param < 0 || param >= NP)
		return;
	int idx = frame * NP + param;
	v = clamp_param(KLATT_FRAME, param, v);
	undo_step *s = d->open;
	if (s && d->touched) {
		if (!d->touched[idx]) {
			if (s->ncells == s->capcells) {
				int nc = s->capcells ? s->capcells * 2 : 64;
				cell_change *c = realloc(s->cells, nc * sizeof *c);
				if (!c)
					return;
				s->cells = c;
				s->capcells = nc;
			}
			s->cells[s->ncells++] = (cell_change){ idx, d->frames[idx], v };
			d->touched[idx] = 1;
		} else {
			for (int i = s->ncells - 1; i >= 0; i--)
				if (s->cells[i].index == idx) {
					s->cells[i].now = v;
					break;
				}
		}
	}
	d->frames[idx] = v;
}

void doc_set(kpe_doc *d, int frame, int param, double v)
{
	int own = !d->open;
	if (own)
		doc_begin(d, "Edit");
	set_cell(d, frame, param, v);
	if (own)
		doc_end(d);
}

void doc_paint(kpe_doc *d, int param, double t0_ms, double v0, double t1_ms, double v1)
{
	double fm = doc_frame_ms(d);
	double tlo = fmin(t0_ms, t1_ms), thi = fmax(t0_ms, t1_ms);
	int k0 = (int)lround(tlo / fm), k1 = (int)lround(thi / fm);
	if (k0 < 0)
		k0 = 0;
	if (k1 > d->nframes - 1)
		k1 = d->nframes - 1;
	int own = !d->open;
	if (own)
		doc_begin(d, "Paint");
	for (int k = k0; k <= k1; k++) {
		double t = fmin(fmax(k * fm, tlo), thi);
		double v = t1_ms == t0_ms ? v1 : v0 + (v1 - v0) * (t - t0_ms) / (t1_ms - t0_ms);
		set_cell(d, k, param, round(v));
	}
	if (own)
		doc_end(d);
}

/* Replaces frames [at, at + old_count) with new_count frames from data. Returns 0 or -1. */
static int splice(kpe_doc *d, int at, int old_count, int new_count, const double *data)
{
	int n = d->nframes - old_count + new_count;
	double *f = malloc(sizeof(double) * NP * (n > 0 ? n : 1));
	if (!f)
		return -1;
	memcpy(f, d->frames, sizeof(double) * NP * at);
	memcpy(f + NP * at, data, sizeof(double) * NP * new_count);
	memcpy(f + NP * (at + new_count), d->frames + NP * (at + old_count),
	       sizeof(double) * NP * (d->nframes - at - old_count));
	free(d->frames);
	d->frames = f;
	d->nframes = n;
	return 0;
}

/* Builds and applies a splice step; takes ownership of new_data. */
static int splice_step(kpe_doc *d, const char *label, int at, int old_count, int new_count, double *new_data,
                       const double *new_globals)
{
	undo_step *s = step_new(label);
	double *old = malloc(sizeof(double) * NP * (old_count > 0 ? old_count : 1));
	if (!s || !old) {
		step_free(s);
		free(old);
		free(new_data);
		return -1;
	}
	memcpy(old, d->frames + NP * at, sizeof(double) * NP * old_count);
	s->splice = 1;
	s->at = at;
	s->old_count = old_count;
	s->new_count = new_count;
	s->old_data = old;
	s->new_data = new_data;
	if (splice(d, at, old_count, new_count, new_data) < 0) {
		step_free(s);
		return -1;
	}
	if (new_globals) {
		s->globals = 1;
		memcpy(s->old_g, d->globals, sizeof s->old_g);
		memcpy(s->new_g, new_globals, sizeof s->new_g);
		memcpy(d->globals, new_globals, sizeof d->globals);
	}
	record(d, s);
	return 0;
}

int doc_insert_frames(kpe_doc *d, int at, int count, const double *data)
{
	if (d->open || at < 0 || at > d->nframes || count <= 0)
		return -1;
	double *nd = malloc(sizeof(double) * NP * count);
	if (!nd)
		return -1;
	if (data) {
		memcpy(nd, data, sizeof(double) * NP * count);
		for (int i = 0; i < NP * count; i++)
			nd[i] = clamp_param(KLATT_FRAME, i % NP, nd[i]);
	} else {
		int src = at < d->nframes ? at : d->nframes - 1;
		for (int k = 0; k < count; k++)
			memcpy(nd + NP * k, d->frames + NP * src, sizeof(double) * NP);
	}
	return splice_step(d, "Insert frames", at, 0, count, nd, NULL);
}

int doc_delete_frames(kpe_doc *d, int first, int count)
{
	if (d->open || first < 0 || count <= 0 || first + count > d->nframes || count >= d->nframes)
		return -1;
	return splice_step(d, "Delete frames", first, count, 0, NULL, NULL);
}

int doc_copy_frames(const kpe_doc *d, int first, int count, double *out)
{
	if (first < 0 || count <= 0 || first + count > d->nframes || !out)
		return -1;
	memcpy(out, d->frames + NP * first, sizeof(double) * NP * count);
	return 0;
}

int doc_set_globals(kpe_doc *d, const double *globals)
{
	if (d->open)
		return -1;
	double g[NG];
	for (int i = 0; i < NG; i++)
		g[i] = clamp_param(KLATT_GLOBAL, i, globals[i]);
	if (memcmp(g, d->globals, sizeof g) == 0)
		return 0;
	double old_ms = d->globals[KG_FRAME_MS], new_ms = g[KG_FRAME_MS];
	if (old_ms == new_ms) {
		undo_step *s = step_new("Globals");
		if (!s)
			return -1;
		s->globals = 1;
		memcpy(s->old_g, d->globals, sizeof s->old_g);
		memcpy(s->new_g, g, sizeof s->new_g);
		memcpy(d->globals, g, sizeof d->globals);
		record(d, s);
		return 0;
	}
	int n = (int)lround(d->nframes * old_ms / new_ms);
	if (n < 1)
		n = 1;
	double *nd = malloc(sizeof(double) * NP * n);
	if (!nd)
		return -1;
	for (int k = 0; k < n; k++) {
		double pos = k * new_ms / old_ms;
		int i = (int)floor(pos);
		double frac = pos - i;
		for (int p = 0; p < NP; p++) {
			double v;
			if (i >= d->nframes - 1)
				v = d->frames[(d->nframes - 1) * NP + p];
			else
				v = d->frames[i * NP + p] * (1 - frac) + d->frames[(i + 1) * NP + p] * frac;
			nd[k * NP + p] = v;
		}
	}
	return splice_step(d, "Globals", 0, d->nframes, n, nd, g) < 0 ? -1 : 1;
}

int doc_can_undo(const kpe_doc *d)
{
	return d->nundo > 0 && !d->open;
}

int doc_can_redo(const kpe_doc *d)
{
	return d->nredo > 0 && !d->open;
}

const char *doc_undo_label(const kpe_doc *d)
{
	return d->nundo ? d->undo[d->nundo - 1]->label : "";
}

const char *doc_redo_label(const kpe_doc *d)
{
	return d->nredo ? d->redo[d->nredo - 1]->label : "";
}

/* Applies a step backwards (undo) or forwards (redo). */
static void apply(kpe_doc *d, undo_step *s, int forward)
{
	if (s->splice) {
		if (forward)
			splice(d, s->at, s->old_count, s->new_count, s->new_data);
		else
			splice(d, s->at, s->new_count, s->old_count, s->old_data);
	}
	if (s->globals)
		memcpy(d->globals, forward ? s->new_g : s->old_g, sizeof d->globals);
	if (forward)
		for (int i = 0; i < s->ncells; i++)
			d->frames[s->cells[i].index] = s->cells[i].now;
	else
		for (int i = s->ncells - 1; i >= 0; i--)
			d->frames[s->cells[i].index] = s->cells[i].old;
	d->dirty = 1;
}

void doc_undo(kpe_doc *d)
{
	if (!doc_can_undo(d))
		return;
	undo_step *s = d->undo[--d->nundo];
	apply(d, s, 0);
	if (push(&d->redo, &d->nredo, &d->capredo, s) < 0)
		step_free(s);
}

void doc_redo(kpe_doc *d)
{
	if (!doc_can_redo(d))
		return;
	undo_step *s = d->redo[--d->nredo];
	apply(d, s, 1);
	if (push(&d->undo, &d->nundo, &d->capundo, s) < 0)
		step_free(s);
}
```

How undo works, for the reviewer:
- **Value edits** (`doc_set`, `doc_paint`) run inside a transaction. The first time a cell is touched, its old
  value is recorded. Touching it again only updates its new value. `doc_end` drops cells that ended where they
  started and pushes the step, unless it is empty.
- **Structural edits** (insert, delete, and globals with a FRAME_MS change) are one *splice* step: they store the
  replaced frames and the new frames.
- A new step clears the redo stack.

- [ ] **Step 5: Run the tests on both architectures**

Run: `MAKE test`
Expected: every suite `ok` including `model`, `N checks, 0 failures` for 32 and 64 (N = 433 with plan 1's
suites), and `max difference 0 LSB: ok`.

- [ ] **Step 6: Commit**

```bash
git add Makefile kpe/model.h kpe/model.c tests/test_model.c tests/run_tests.c
git commit -m "Add the editor document model with undo

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: CSV file pair

**Files:**
- Create: `kpe/csvio.h`, `kpe/csvio.c`, `tests/test_csvio.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Consumes: `doc_init_from`, `doc_frame_ms`, `doc_get`, `NP`, `NG` (Task 1); `KlattParamIndex`, `KlattParamInfo`.
- Produces: `csv_report`, `csv_globals_path`, `csv_load`, `csv_save` as declared in `kpe/csvio.h` below.

- [ ] **Step 1: Write the failing CSV tests**

`tests/test_csvio.c`:

```c
/* CSV tests write their files into the working directory (build/32 or build/64). */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "test.h"
#include "measure.h"
#include "csvio.h"

static void write_text(const wchar_t *path, const char *text)
{
	FILE *f = _wfopen(path, L"wb");
	fputs(text, f);
	fclose(f);
}

static int exists(const wchar_t *path)
{
	FILE *f = _wfopen(path, L"rb");
	if (f)
		fclose(f);
	return f != NULL;
}

/* Loads text as t.csv (with no globals file) and returns csv_load's result. */
static int load_text(const char *text, kpe_doc *d, csv_report *r)
{
	_wremove(L"t.globals.csv");
	write_text(L"t.csv", text);
	return csv_load(L"t.csv", d, r);
}

static void test_globals_path(void)
{
	wchar_t out[64];
	csv_globals_path(L"C:\\a\\vowel.csv", out, 64);
	CHECK(wcscmp(out, L"C:\\a\\vowel.globals.csv") == 0);
	csv_globals_path(L"x.CSV", out, 64);
	CHECK(wcscmp(out, L"x.globals.csv") == 0);
	csv_globals_path(L"noext", out, 64);
	CHECK(wcscmp(out, L"noext.globals.csv") == 0);
}

static void test_round_trip(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	g[KG_G0] = 52.5;
	CHECK(doc_new(&d, g, 50) == 0);
	doc_set(&d, 3, KP_F1, 612.25);
	doc_set(&d, 4, KP_AV, 0.1);   /* needs 17 digits to round-trip exactly */
	doc_set(&d, 5, KP_F0, 123.456);
	CHECK(csv_save(L"rt.csv", &d, &r) == 0);
	CHECK(!d.dirty);
	CHECK(exists(L"rt.globals.csv"));
	CHECK(!exists(L"rt.csv.tmp"));
	CHECK(csv_load(L"rt.csv", &e, &r) == 0);
	CHECK(strcmp(r.message, "") == 0);
	CHECK(!r.globals_missing && r.clamped == 0 && r.unknown_columns == 0);
	CHECK(e.nframes == d.nframes);
	CHECK(memcmp(e.globals, d.globals, sizeof d.globals) == 0);
	CHECK(memcmp(e.frames, d.frames, sizeof(double) * NP * d.nframes) == 0);
	CHECK(!e.dirty && !doc_can_undo(&e));
	doc_free(&d);
	doc_free(&e);
}

static void test_header_line_format(void)
{
	kpe_doc d;
	csv_report r;
	double g[NG];
	char line[512] = "";
	t_default_globals(g);
	CHECK(doc_new(&d, g, 10) == 0);
	CHECK(csv_save(L"fmt.csv", &d, &r) == 0);
	FILE *f = _wfopen(L"fmt.csv", L"rb");
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strcmp(line, "time_ms,AV,AF,AH,AVS,F0,F1,F2,F3,F4,FNZ,AN,A1,A2,A3,A4,A5,A6,AB,B1,B2,B3\r\n") == 0);
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strcmp(line, "0,0,0,0,0,100,500,1500,2500,3500,250,0,0,0,0,0,0,0,0,50,70,110\r\n") == 0);
	CHECK(fgets(line, sizeof line, f) != NULL);
	CHECK(strncmp(line, "5,", 2) == 0);
	fclose(f);
	doc_free(&d);
}

static void test_subset_any_order_defaults(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("\xEF\xBB\xBF" "F2, time_ms ,av\r\n1200,0,60\r\n1300,10,61\r\n1400,20,62\r\n\r\n", &d, &r) == 0);
	CHECK(d.nframes == 3);
	CHECK(r.globals_missing);
	CHECK_NEAR(doc_frame_ms(&d), 10, 0); /* from the time_ms spacing */
	CHECK_NEAR(doc_get(&d, 1, KP_F2), 1300, 0);
	CHECK_NEAR(doc_get(&d, 2, KP_AV), 62, 0);
	CHECK_NEAR(doc_get(&d, 2, KP_F1), 500, 0); /* missing column: default */
	CHECK_NEAR(d.globals[KG_SR], 11025, 0);
	doc_free(&d);
}

static void test_unknown_columns_and_clamping(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("time_ms,F1,Zap,note\n0,99999,1,2\n5,100,1,2\n", &d, &r) == 0);
	CHECK(r.unknown_columns == 2);
	CHECK(strcmp(r.message, "skipped unknown columns: Zap, note") == 0);
	CHECK(r.clamped == 2);
	CHECK_NEAR(doc_get(&d, 0, KP_F1), 1300, 0);
	CHECK_NEAR(doc_get(&d, 1, KP_F1), 150, 0);
	doc_free(&d);
}

static void test_single_row_uses_default_frame_ms(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("time_ms,AV\n0,60\n", &d, &r) == 0);
	CHECK(d.nframes == 1);
	CHECK_NEAR(doc_frame_ms(&d), 5, 0);
	doc_free(&d);
}

static void test_globals_file(void)
{
	kpe_doc d;
	csv_report r;
	write_text(L"g.csv", "time_ms,AV\n0,60\n10,60\n20,60\n");
	write_text(L"g.globals.csv", "SR,FRAME_MS,G0\n16000,10,55\n");
	CHECK(csv_load(L"g.csv", &d, &r) == 0);
	CHECK(!r.globals_missing);
	CHECK_NEAR(d.globals[KG_SR], 16000, 0);
	CHECK_NEAR(d.globals[KG_G0], 55, 0);
	CHECK_NEAR(d.globals[KG_NFC], 5, 0); /* not in the file: default */
	doc_free(&d);
	write_text(L"g.globals.csv", "FRAME_MS\n5\n");
	CHECK(csv_load(L"g.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "FRAME_MS is 5 ms in g.globals.csv but time_ms steps by 10 ms in g.csv") == 0);
	write_text(L"g.globals.csv", "SR\nabc\n");
	CHECK(csv_load(L"g.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "g.globals.csv line 2, column SR: \"abc\" is not a number") == 0);
	write_text(L"g.globals.csv", "SR\n11025\n8000\n");
	CHECK(csv_load(L"g.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "g.globals.csv line 3: expected a single row of values") == 0);
}

static void test_rejections(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv is empty") == 0);
	CHECK(load_text("time_ms,AV\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv has a header but no frames") == 0);
	CHECK(load_text("AV,F1\n60,500\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 1: no time_ms column") == 0);
	CHECK(load_text("time_ms,F2\n0,1500\n5,abc\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 3, column F2: \"abc\" is not a number") == 0);
	CHECK(load_text("time_ms,F2\n0,1500\n5,\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 3, column F2: \"\" is not a number") == 0);
	CHECK(load_text("time_ms,F2\n0,1500\n5\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 3: expected 2 values, found 1") == 0);
	CHECK(load_text("time_ms,F2\n0,1\n5,1\n5,1\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 4: time_ms 5 does not increase") == 0);
	CHECK(load_text("time_ms,F2\n0,1\n5,1\n10.02,1\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 4: time_ms steps by 5.02 ms here but by 5 ms before") == 0);
	CHECK(load_text("time_ms,F2,f2\n0,1,1\n", &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 1: column f2 appears twice") == 0);
	CHECK(csv_load(L"does-not-exist.csv", &d, &r) == -1);
	CHECK(strncmp(r.message, "cannot read does-not-exist.csv: ", 32) == 0);
}

static void test_failed_save_keeps_old_files(void)
{
	kpe_doc d;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 10) == 0);
	CHECK(csv_save(L"keep.csv", &d, &r) == 0);
	/* A directory where the globals temp file should go makes the second write fail. */
	_wmkdir(L"keep.globals.csv.tmp");
	doc_set(&d, 0, KP_AV, 60);
	CHECK(csv_save(L"keep.csv", &d, &r) == -1);
	CHECK(strncmp(r.message, "cannot write keep.globals.csv", 29) == 0);
	CHECK(d.dirty);
	CHECK(!exists(L"keep.csv.tmp"));
	_wrmdir(L"keep.globals.csv.tmp");
	kpe_doc e;
	CHECK(csv_load(L"keep.csv", &e, &r) == 0);
	CHECK_NEAR(doc_get(&e, 0, KP_AV), 0, 0); /* the old contents */
	doc_free(&e);
	doc_free(&d);
}

void suite_csvio(void)
{
	test_globals_path();
	test_round_trip();
	test_header_line_format();
	test_subset_any_order_defaults();
	test_unknown_columns_and_clamping();
	test_single_row_uses_default_frame_ms();
	test_globals_file();
	test_rejections();
	test_failed_save_keeps_old_files();
}
```

In `tests/run_tests.c`, add `void suite_csvio(void);` and `{ "csvio", suite_csvio },` after the `model` lines.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: FAIL to compile: `csvio.h: No such file or directory`.

- [ ] **Step 3: Write the CSV module**

`kpe/csvio.h`:

```c
/* csvio.h - the native file format: name.csv (time_ms + frame parameters, one row per frame) and
 * name.globals.csv (a header row of global names, one row of values). */
#ifndef KPE_CSVIO_H
#define KPE_CSVIO_H

#include <stddef.h>
#include <wchar.h>
#include "model.h"

typedef struct {
	int clamped;          /* values moved into their parameter's range while loading */
	int unknown_columns;  /* columns skipped because their names are not parameters */
	int globals_missing;  /* name.globals.csv did not exist; defaults were used */
	char message[512];    /* UTF-8. On failure: the error. On success: warnings, or "" */
} csv_report;

/* x.csv -> x.globals.csv (any other name gets ".globals.csv" appended). */
void csv_globals_path(const wchar_t *path, wchar_t *out, size_t n);

/* Loads the pair into *d (a fresh document, not dirty). Returns 0, or -1 with r->message set; on failure *d
 * is left untouched. */
int csv_load(const wchar_t *path, kpe_doc *d, csv_report *r);

/* Writes both files, each through a temporary file renamed over the target. Returns 0 (and clears d->dirty),
 * or -1 with r->message set; on failure the existing files are unchanged. */
int csv_save(const wchar_t *path, kpe_doc *d, csv_report *r);

#endif
```

`kpe/csvio.c`:

```c
/* csvio.c - loading and saving the CSV pair. See csvio.h. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "csvio.h"

#define MAX_COLS 64

/* ---- small helpers ---- */

static void report(csv_report *r, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r->message, sizeof r->message, fmt, ap);
	va_end(ap);
}

/* The file name part of path, as UTF-8, for messages. */
static void base_utf8(const wchar_t *path, char *out, size_t n)
{
	const wchar_t *b = path;
	for (const wchar_t *p = path; *p; p++)
		if (*p == L'\\' || *p == L'/')
			b = p + 1;
	if (!WideCharToMultiByte(CP_UTF8, 0, b, -1, out, (int)n, NULL, NULL))
		snprintf(out, n, "?");
}

void csv_globals_path(const wchar_t *path, wchar_t *out, size_t n)
{
	size_t len = wcslen(path);
	if (len >= 4 && _wcsicmp(path + len - 4, L".csv") == 0)
		len -= 4;
	_snwprintf(out, n, L"%.*ls.globals.csv", (int)len, path);
	out[n - 1] = 0;
}

/* Reads a whole file; returns a NUL-terminated buffer without a UTF-8 BOM, or NULL (errno set). */
static char *read_file(const wchar_t *path)
{
	FILE *f = _wfopen(path, L"rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(len + 1);
	if (!buf || fread(buf, 1, len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		errno = EIO;
		return NULL;
	}
	fclose(f);
	buf[len] = 0;
	if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
		memmove(buf, buf + 3, len - 2);
	return buf;
}

/* Splits one line in place into trimmed cells. Returns the number of cells, or -1 if more than max. */
static int split(char *line, char **cells, int max)
{
	int n = 0;
	char *p = line;
	for (;;) {
		if (n == max)
			return -1;
		char *end = strchr(p, ',');
		if (end)
			*end = 0;
		while (*p == ' ' || *p == '\t')
			p++;
		char *q = p + strlen(p);
		while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'))
			*--q = 0;
		cells[n++] = p;
		if (!end)
			return n;
		p = end + 1;
	}
}

/* Moves *text to the next line; returns the current line (NUL-terminated) or NULL at the end. */
static char *next_line(char **text)
{
	char *s = *text;
	if (!s || !*s)
		return NULL;
	char *nl = strchr(s, '\n');
	if (nl) {
		*nl = 0;
		*text = nl + 1;
	} else {
		*text = s + strlen(s);
	}
	size_t len = strlen(s);
	if (len && s[len - 1] == '\r')
		s[len - 1] = 0;
	return s;
}

static int blank(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return *s == 0;
}

static int parse_number(const char *s, double *v)
{
	char *end;
	if (!*s)
		return 0;
	errno = 0;
	*v = strtod(s, &end);
	return *end == 0 && errno != ERANGE && isfinite(*v);
}

static int eq_nocase(const char *a, const char *b)
{
	while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b))
		a++, b++;
	return !*a && !*b;
}

/* Maps header cells to parameter indices of kind (-1: skip). Adds unknown names to warn. Returns 0, or -1
 * with an error in r for a duplicate column. time_col receives the time_ms column (or -1) when not NULL. */
static int map_header(char **cells, int n, int kind, int *map, int *time_col, const char *file, char *warn,
                      size_t warnlen, csv_report *r)
{
	if (time_col)
		*time_col = -1;
	for (int c = 0; c < n; c++) {
		map[c] = -1;
		if (time_col && eq_nocase(cells[c], "time_ms")) {
			if (*time_col >= 0) {
				report(r, "%s line 1: column time_ms appears twice", file);
				return -1;
			}
			*time_col = c;
			continue;
		}
		int idx = KlattParamIndex(kind, cells[c]);
		if (idx < 0) {
			r->unknown_columns++;
			size_t used = strlen(warn);
			snprintf(warn + used, warnlen - used, "%s%s", used ? ", " : "", cells[c]);
			continue;
		}
		for (int e = 0; e < c; e++)
			if (map[e] == idx) {
				report(r, "%s line 1: column %s appears twice", file, cells[c]);
				return -1;
			}
		map[c] = idx;
	}
	return 0;
}

static double clamp_count(int kind, int idx, double v, int *clamped)
{
	klatt_param_info pi;
	KlattParamInfo(kind, idx, &pi);
	if (v < pi.min) { (*clamped)++; return pi.min; }
	if (v > pi.max) { (*clamped)++; return pi.max; }
	return v;
}

/* ---- loading ---- */

/* Parses name.globals.csv into g (defaults for missing columns). have_frame_ms reports whether the file set
 * FRAME_MS. Returns 1 if loaded, 0 if the file does not exist, -1 on an error (r set). */
static int load_globals(const wchar_t *gpath, double *g, int *have_frame_ms, char *warn, size_t warnlen,
                        csv_report *r)
{
	char file[260], *cells[MAX_COLS], *vals[MAX_COLS];
	int map[MAX_COLS];
	base_utf8(gpath, file, sizeof file);
	*have_frame_ms = 0;
	char *text = read_file(gpath);
	if (!text) {
		if (errno == ENOENT)
			return 0;
		report(r, "cannot read %s: %s", file, strerror(errno));
		return -1;
	}
	char *cur = text, *line;
	int lineno = 0, ret = -1, nh = 0, rows = 0;
	while ((line = next_line(&cur))) {
		lineno++;
		if (blank(line))
			continue;
		if (!nh) {
			nh = split(line, cells, MAX_COLS);
			if (nh < 0) {
				report(r, "%s line %d: more than %d columns", file, lineno, MAX_COLS);
				goto done;
			}
			if (map_header(cells, nh, KLATT_GLOBAL, map, NULL, file, warn, warnlen, r) < 0)
				goto done;
			continue;
		}
		if (++rows > 1) {
			report(r, "%s line %d: expected a single row of values", file, lineno);
			goto done;
		}
		int nv = split(line, vals, MAX_COLS);
		if (nv != nh) {
			report(r, "%s line %d: expected %d values, found %d", file, lineno, nh, nv);
			goto done;
		}
		for (int c = 0; c < nh; c++) {
			double v;
			if (map[c] < 0)
				continue;
			if (!parse_number(vals[c], &v)) {
				report(r, "%s line %d, column %s: \"%s\" is not a number", file, lineno, cells[c], vals[c]);
				goto done;
			}
			g[map[c]] = clamp_count(KLATT_GLOBAL, map[c], v, &r->clamped);
			if (map[c] == KG_FRAME_MS)
				*have_frame_ms = 1;
		}
	}
	if (rows == 0) {
		report(r, "%s: no values", file);
		goto done;
	}
	ret = 1;
done:
	free(text);
	return ret;
}

int csv_load(const wchar_t *path, kpe_doc *d, csv_report *r)
{
	char file[260], warn[400] = "", *cells[MAX_COLS], *vals[MAX_COLS];
	int map[MAX_COLS], time_col;
	double g[NG], defaults[NP];
	memset(r, 0, sizeof *r);
	base_utf8(path, file, sizeof file);
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	for (int i = 0; i < NP; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		defaults[i] = pi.def;
	}

	char *text = read_file(path);
	if (!text) {
		report(r, "cannot read %s: %s", file, strerror(errno));
		return -1;
	}
	int ret = -1, nh = 0, nframes = 0, cap = 0, lineno = 0;
	double *frames = NULL, t_prev = 0, step = 0;
	char *cur = text, *line;
	while ((line = next_line(&cur))) {
		lineno++;
		if (blank(line))
			continue;
		if (!nh) {
			nh = split(line, cells, MAX_COLS);
			if (nh < 0) {
				report(r, "%s line %d: more than %d columns", file, lineno, MAX_COLS);
				goto done;
			}
			if (map_header(cells, nh, KLATT_FRAME, map, &time_col, file, warn, sizeof warn, r) < 0)
				goto done;
			if (time_col < 0) {
				report(r, "%s line %d: no time_ms column", file, lineno);
				goto done;
			}
			continue;
		}
		int nv = split(line, vals, MAX_COLS);
		if (nv != nh) {
			report(r, "%s line %d: expected %d values, found %d", file, lineno, nh, nv);
			goto done;
		}
		if (nframes == cap) {
			cap = cap ? cap * 2 : 256;
			double *nf = realloc(frames, sizeof(double) * NP * cap);
			if (!nf) {
				report(r, "%s: out of memory", file);
				goto done;
			}
			frames = nf;
		}
		double *fr = frames + NP * nframes;
		memcpy(fr, defaults, sizeof defaults);
		for (int c = 0; c < nh; c++) {
			double v;
			if (c != time_col && map[c] < 0)
				continue;
			if (!parse_number(vals[c], &v)) {
				report(r, "%s line %d, column %s: \"%s\" is not a number", file, lineno, cells[c], vals[c]);
				goto done;
			}
			if (c == time_col) {
				if (nframes == 0) {
					/* the first time may be anything; frames are re-timed from 0 on save */
				} else if (v <= t_prev) {
					report(r, "%s line %d: time_ms %g does not increase", file, lineno, v);
					goto done;
				} else if (nframes == 1) {
					step = v - t_prev;
				} else if (fabs((v - t_prev) - step) > 0.01) {
					report(r, "%s line %d: time_ms steps by %g ms here but by %g ms before", file, lineno,
					       v - t_prev, step);
					goto done;
				}
				t_prev = v;
			} else {
				fr[map[c]] = clamp_count(KLATT_FRAME, map[c], v, &r->clamped);
			}
		}
		nframes++;
	}
	if (!nh) {
		report(r, "%s is empty", file);
		goto done;
	}
	if (nframes == 0) {
		report(r, "%s has a header but no frames", file);
		goto done;
	}

	wchar_t gpath[MAX_PATH * 2];
	int have_ms = 0;
	csv_globals_path(path, gpath, sizeof gpath / sizeof gpath[0]);
	int gl = load_globals(gpath, g, &have_ms, warn, sizeof warn, r);
	if (gl < 0)
		goto done;
	r->globals_missing = gl == 0;
	if (nframes >= 2) {
		if (!have_ms) {
			g[KG_FRAME_MS] = clamp_count(KLATT_GLOBAL, KG_FRAME_MS, step, &r->clamped);
		} else if (fabs(g[KG_FRAME_MS] - step) > 0.01) {
			char gfile[260];
			base_utf8(gpath, gfile, sizeof gfile);
			report(r, "FRAME_MS is %g ms in %s but time_ms steps by %g ms in %s", g[KG_FRAME_MS], gfile, step, file);
			goto done;
		}
	}
	doc_init_from(d, g, nframes, frames);
	frames = NULL;
	if (r->unknown_columns)
		report(r, "skipped unknown columns: %s", warn);
	else
		r->message[0] = 0;
	ret = 0;
done:
	free(frames);
	free(text);
	return ret;
}

/* ---- saving ---- */

/* Shortest of %.15g, %.16g, %.17g that reads back as exactly v. */
static void fmt_num(char *buf, size_t n, double v)
{
	for (int prec = 15; prec <= 17; prec++) {
		snprintf(buf, n, "%.*g", prec, v);
		if (strtod(buf, NULL) == v)
			return;
	}
}

/* Writes text produced by emit() to tmp. Returns 0 or -1 with r set (and tmp removed). */
static int write_tmp(const wchar_t *path, const wchar_t *tmp, int (*emit)(FILE *, const kpe_doc *),
                     const kpe_doc *d, csv_report *r)
{
	char file[260];
	base_utf8(path, file, sizeof file);
	FILE *f = _wfopen(tmp, L"wb");
	if (!f) {
		report(r, "cannot write %s: %s", file, strerror(errno));
		return -1;
	}
	int ok = emit(f, d) == 0;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		report(r, "cannot write %s: %s", file, strerror(errno));
		_wremove(tmp);
		return -1;
	}
	return 0;
}

static int emit_frames(FILE *f, const kpe_doc *d)
{
	char num[40];
	fputs("time_ms", f);
	for (int p = 0; p < NP; p++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		fprintf(f, ",%s", pi.name);
	}
	fputs("\r\n", f);
	for (int k = 0; k < d->nframes; k++) {
		fmt_num(num, sizeof num, k * doc_frame_ms(d));
		fputs(num, f);
		for (int p = 0; p < NP; p++) {
			fmt_num(num, sizeof num, doc_get(d, k, p));
			fprintf(f, ",%s", num);
		}
		fputs("\r\n", f);
	}
	return ferror(f) ? -1 : 0;
}

static int emit_globals(FILE *f, const kpe_doc *d)
{
	char num[40];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		fprintf(f, "%s%s", i ? "," : "", pi.name);
	}
	fputs("\r\n", f);
	for (int i = 0; i < NG; i++) {
		fmt_num(num, sizeof num, d->globals[i]);
		fprintf(f, "%s%s", i ? "," : "", num);
	}
	fputs("\r\n", f);
	return ferror(f) ? -1 : 0;
}

int csv_save(const wchar_t *path, kpe_doc *d, csv_report *r)
{
	wchar_t gpath[MAX_PATH * 2], tmp[MAX_PATH * 2 + 8], gtmp[MAX_PATH * 2 + 8];
	char file[260];
	memset(r, 0, sizeof *r);
	csv_globals_path(path, gpath, sizeof gpath / sizeof gpath[0]);
	_snwprintf(tmp, sizeof tmp / sizeof tmp[0], L"%ls.tmp", path);
	_snwprintf(gtmp, sizeof gtmp / sizeof gtmp[0], L"%ls.tmp", gpath);
	tmp[sizeof tmp / sizeof tmp[0] - 1] = gtmp[sizeof gtmp / sizeof gtmp[0] - 1] = 0;
	/* Both temporary files first, so a write error leaves both targets untouched. */
	if (write_tmp(path, tmp, emit_frames, d, r) < 0)
		return -1;
	if (write_tmp(gpath, gtmp, emit_globals, d, r) < 0) {
		_wremove(tmp);
		return -1;
	}
	if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		base_utf8(path, file, sizeof file);
		report(r, "cannot replace %s (Windows error %lu)", file, GetLastError());
		_wremove(tmp);
		_wremove(gtmp);
		return -1;
	}
	if (!MoveFileExW(gtmp, gpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		base_utf8(gpath, file, sizeof file);
		report(r, "cannot replace %s (Windows error %lu)", file, GetLastError());
		_wremove(gtmp);
		return -1;
	}
	d->dirty = 0;
	return 0;
}
```

Behaviour worth knowing:
- Column names are matched case-insensitively, and cells are trimmed of spaces and tabs.
- A UTF-8 BOM and blank lines are ignored.
- The first `time_ms` may be any value. Frames are re-timed from 0 on save.
- Saving writes the numbers with the fewest digits (15 to 17) that read back exactly, so a load-save-load round
  trip is lossless.
- Both temporary files are written before either target is replaced.

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: every suite `ok`, `N checks, 0 failures` for 32 and 64 (N = 512).

- [ ] **Step 5: Commit**

```bash
git add kpe/csvio.h kpe/csvio.c tests/test_csvio.c tests/run_tests.c
git commit -m "Add CSV loading and saving for documents

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Track optimization

**Files:**
- Create: `kpe/optimize.h`, `kpe/optimize.c`, `tests/test_optimize.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `int opt_simplify(double *v, int count, int first_frame, int grid, double tol, double *max_err)`.
  Plan 3's Optimize dialog runs it on a copy of each chosen track and applies the result inside one transaction:
  `doc_begin(d, "Optimize")`, then `doc_set` for each frame, then `doc_end(d)`.

- [ ] **Step 1: Write the failing optimizer tests**

`tests/test_optimize.c`:

```c
#include <math.h>
#include <string.h>
#include "test.h"
#include "optimize.h"

static void test_straight_line_keeps_ends(void)
{
	double v[50], err;
	for (int i = 0; i < 50; i++)
		v[i] = 100 + 3 * i;
	CHECK(opt_simplify(v, 50, 0, 1, 1, &err) == 2);
	CHECK_NEAR(err, 0, 1e-9);
	CHECK_NEAR(v[25], 175, 1e-9);
}

static void test_small_wiggles_removed(void)
{
	double v[40], err;
	for (int i = 0; i < 40; i++)
		v[i] = 1500 + ((i % 3) - 1) * 8; /* +-8 Hz around a flat line */
	CHECK(opt_simplify(v, 40, 0, 1, 20, &err) == 2);
	CHECK(err <= 20);
}

static void test_corner_kept(void)
{
	double v[21], orig[21], err;
	for (int i = 0; i <= 20; i++)
		v[i] = orig[i] = i <= 10 ? 500 + 20 * i : 700 - 30 * (i - 10); /* up then down */
	CHECK(opt_simplify(v, 21, 0, 1, 5, &err) == 3);
	CHECK_NEAR(v[10], 700, 0);
	CHECK(memcmp(v, orig, sizeof v) == 0); /* piecewise linear already: nothing changes */
}

static void test_tolerance_respected(void)
{
	double v[200], orig[200], err;
	for (int i = 0; i < 200; i++)
		v[i] = orig[i] = 1500 + 400 * sin(i * 0.05) + 30 * sin(i * 0.9);
	int kept = opt_simplify(v, 200, 0, 1, 20, &err);
	CHECK(kept > 2 && kept < 200);
	CHECK(err <= 20);
	double worst = 0;
	for (int i = 0; i < 200; i++)
		worst = fmax(worst, fabs(v[i] - orig[i]));
	CHECK_NEAR(worst, err, 1e-9);
}

static void test_grid_alignment(void)
{
	double v[31], orig[31];
	for (int i = 0; i < 31; i++)
		v[i] = orig[i] = 100 * sin(i * 0.4);
	/* The range starts at absolute frame 3 with a grid of 2 frames (10 ms at 5 ms frames), so breakpoints may
	 * only be at odd i, plus the ends. */
	opt_simplify(v, 31, 3, 2, 1, NULL);
	CHECK_NEAR(v[0], orig[0], 0);
	CHECK_NEAR(v[30], orig[30], 0);
	/* An off-grid frame is never a breakpoint, so it lies on the line through its two neighbours. */
	for (int i = 2; i < 30; i += 2)
		CHECK_NEAR(v[i], (v[i - 1] + v[i + 1]) / 2, 1e-9);
}

static void test_step_survives(void)
{
	double v[40], err;
	for (int i = 0; i < 40; i++)
		v[i] = i < 20 ? 0 : 60; /* a voicing onset */
	int kept = opt_simplify(v, 40, 0, 1, 1, &err);
	CHECK(kept == 4);
	CHECK_NEAR(v[19], 0, 0);
	CHECK_NEAR(v[20], 60, 0);
	CHECK_NEAR(err, 0, 1e-9);
}

static void test_edge_cases(void)
{
	double one[1] = { 5 }, err = -1;
	CHECK(opt_simplify(one, 1, 0, 1, 1, &err) == 1);
	CHECK(err == 0);
	CHECK(opt_simplify(one, 0, 0, 1, 1, NULL) == 0);
	double two[2] = { 1, 9 };
	CHECK(opt_simplify(two, 2, 0, 4, 1, NULL) == 2);
}

void suite_optimize(void)
{
	test_straight_line_keeps_ends();
	test_small_wiggles_removed();
	test_corner_kept();
	test_tolerance_respected();
	test_grid_alignment();
	test_step_survives();
	test_edge_cases();
}
```

In `tests/run_tests.c`, add `void suite_optimize(void);` and `{ "optimize", suite_optimize },` after the
`csvio` lines.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: FAIL to compile: `optimize.h: No such file or directory`.

- [ ] **Step 3: Write the optimizer**

`kpe/optimize.h`:

```c
/* optimize.h - track simplification for Parameters > Optimize. */
#ifndef KPE_OPTIMIZE_H
#define KPE_OPTIMIZE_H

/* Simplifies one track of count values in place. Breakpoints may only sit on frames whose absolute index
 * (first_frame + i) is a multiple of grid, plus the two ends. Douglas-Peucker: keep the fewest breakpoints for
 * which linear interpolation stays within tol of the original values, then refill the frames between kept
 * breakpoints by linear interpolation. Returns the number of breakpoints kept; *max_err (may be NULL) receives
 * the largest absolute change made. */
int opt_simplify(double *v, int count, int first_frame, int grid, double tol, double *max_err);

#endif
```

`kpe/optimize.c`:

```c
/* optimize.c - Douglas-Peucker simplification restricted to grid frames. See optimize.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "optimize.h"

/* Is frame i a breakpoint candidate? */
static int candidate(int i, int count, int first_frame, int grid)
{
	return i == 0 || i == count - 1 || (first_frame + i) % grid == 0;
}

/* Marks breakpoints in keep[] between kept ends a and b. */
static void split(const double *v, int a, int b, int count, int first_frame, int grid, double tol, char *keep)
{
	/* the frame that deviates most from the line a-b */
	int worst = -1;
	double worst_err = tol;
	for (int i = a + 1; i < b; i++) {
		double line = v[a] + (v[b] - v[a]) * (double)(i - a) / (b - a);
		double e = fabs(v[i] - line);
		if (e > worst_err) {
			worst_err = e;
			worst = i;
		}
	}
	if (worst < 0)
		return;
	/* the candidate nearest to it, strictly between a and b */
	int best = -1;
	for (int d = 0; d < b - a; d++) {
		if (worst - d > a && candidate(worst - d, count, first_frame, grid)) { best = worst - d; break; }
		if (worst + d < b && candidate(worst + d, count, first_frame, grid)) { best = worst + d; break; }
	}
	if (best < 0)
		return; /* no candidate between a and b: the grid is too coarse to do better */
	keep[best] = 1;
	split(v, a, best, count, first_frame, grid, tol, keep);
	split(v, best, b, count, first_frame, grid, tol, keep);
}

int opt_simplify(double *v, int count, int first_frame, int grid, double tol, double *max_err)
{
	if (max_err)
		*max_err = 0;
	if (count <= 0)
		return 0;
	if (count == 1)
		return 1;
	if (grid < 1)
		grid = 1;
	char *keep = calloc(count, 1);
	if (!keep)
		return count; /* out of memory: leave the track alone */
	keep[0] = keep[count - 1] = 1;
	split(v, 0, count - 1, count, first_frame, grid, tol, keep);
	int kept = 0, a = 0;
	double worst = 0;
	for (int b = 1; b < count; b++) {
		if (!keep[b])
			continue;
		for (int i = a + 1; i < b; i++) {
			double nv = v[a] + (v[b] - v[a]) * (double)(i - a) / (b - a);
			if (fabs(nv - v[i]) > worst)
				worst = fabs(nv - v[i]);
			v[i] = nv;
		}
		a = b;
	}
	for (int i = 0; i < count; i++)
		kept += keep[i];
	free(keep);
	if (max_err)
		*max_err = worst;
	return kept;
}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: every suite `ok`, `N checks, 0 failures` for 32 and 64 (N = 547).

- [ ] **Step 5: Commit**

```bash
git add kpe/optimize.h kpe/optimize.c tests/test_optimize.c tests/run_tests.c
git commit -m "Add grid-aligned track simplification

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Whole-document synthesis, WAV output and spectrogram

**Files:**
- Create: `kpe/synthrun.h`, `kpe/synthrun.c`, `kpe/wavout.h`, `kpe/wavout.c`, `kpe/spectro.h`, `kpe/spectro.c`,
  `tests/test_audio.c`
- Modify: `tests/run_tests.c`, `README.md`

**Interfaces:**
- Consumes: `kpe_doc`, `doc_new`, `doc_paint` (Task 1); the DLL API.
- Produces: `synth_result`, `synth_doc`, `synth_result_free`; `wav_write`; `kpe_spectro`, `SPECTRO_WIDE_MS`,
  `SPECTRO_NARROW_MS`, `spectro_compute`, `spectro_free`, `spectro_levels`, as declared below. Plan 3 calls
  `synth_doc` after every committed edit, computes the spectrogram with `step_ms = 2`, and paints
  `spectro_levels(..., 50, ...)` as darkness (silence stays white).

- [ ] **Step 1: Write the failing tests**

`tests/test_audio.c`:

```c
/* synthrun, wavout and spectro. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "spectro.h"
#include "synthrun.h"
#include "wavout.h"

static unsigned get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned get16(const unsigned char *p) { return p[0] | p[1] << 8; }

static void test_synth_doc(void)
{
	kpe_doc d;
	synth_result s;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 800) == 0);
	CHECK(synth_doc(&d, &s) == 0);
	CHECK(s.n == 8820 && s.sr == 11025 && s.overload == 0);
	CHECK(m_peak(s.pcm, s.n) == 0); /* a new document is silent */
	synth_result_free(&s);
	doc_paint(&d, KP_AV, 100, 60, 700, 60);
	CHECK(synth_doc(&d, &s) == 0);
	CHECK(m_peak(s.pcm, s.n) > 1000);
	CHECK(m_peak(s.pcm, 1000) == 0); /* still silent before 100 ms */
	synth_result t;
	CHECK(synth_doc(&d, &t) == 0);  /* deterministic */
	CHECK(t.n == s.n && memcmp(t.pcm, s.pcm, sizeof(short) * s.n) == 0);
	synth_result_free(&t);
	synth_result_free(&s);
	doc_free(&d);
}

static void test_wav_header(void)
{
	short pcm[5] = { 0, 1, -1, 32767, -32768 };
	unsigned char buf[64];
	CHECK(wav_write(L"t.wav", pcm, 5, 11025) == 0);
	FILE *f = _wfopen(L"t.wav", L"rb");
	size_t got = fread(buf, 1, sizeof buf, f);
	fclose(f);
	CHECK(got == 54);
	CHECK(memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVEfmt ", 8) == 0 && memcmp(buf + 36, "data", 4) == 0);
	CHECK(get32(buf + 4) == 46);
	CHECK(get32(buf + 16) == 16 && get16(buf + 20) == 1 && get16(buf + 22) == 1);
	CHECK(get32(buf + 24) == 11025 && get32(buf + 28) == 22050);
	CHECK(get16(buf + 32) == 2 && get16(buf + 34) == 16);
	CHECK(get32(buf + 40) == 10);
	CHECK(get16(buf + 44 + 6) == 32767 && get16(buf + 44 + 8) == 0x8000);
	CHECK(wav_write(L"no-such-dir\\t.wav", pcm, 5, 11025) == -1);
}

static void test_spectrogram_peak(void)
{
	static short pcm[11025];
	kpe_spectro s;
	for (int i = 0; i < 11025; i++)
		pcm[i] = (short)(10000 * sin(2 * 3.14159265358979 * 1000 * i / 11025.0));
	double windows[2] = { SPECTRO_WIDE_MS, SPECTRO_NARROW_MS };
	for (int w = 0; w < 2; w++) {
		CHECK(spectro_compute(pcm, 11025, 11025, windows[w], 2, &s) == 0);
		CHECK(s.ncols == 501); /* 0, 2, ... 1000 ms */
		int c = 250, best = 0;
		for (int b = 1; b < s.nbins; b++)
			if (s.db[c * s.nbins + b] > s.db[c * s.nbins + best])
				best = b;
		CHECK_NEAR(best * s.bin_hz, 1000, s.bin_hz);
		spectro_free(&s);
	}
	CHECK(spectro_compute(pcm, 11025, 11025, SPECTRO_WIDE_MS, 2, &s) == 0);
	CHECK_NEAR(s.bin_hz, 11025.0 / 256, 1e-9);
	spectro_free(&s);
	CHECK(spectro_compute(pcm, 11025, 11025, SPECTRO_NARROW_MS, 2, &s) == 0);
	CHECK_NEAR(s.bin_hz, 11025.0 / 1024, 1e-9);
	spectro_free(&s);
	CHECK(spectro_compute(pcm, 100, 11025, 0, 2, &s) == -1);
}

static void test_spectrogram_levels(void)
{
	static short pcm[2205];
	kpe_spectro s;
	for (int i = 1102; i < 2205; i++) /* silence, then a tone */
		pcm[i] = (short)(8000 * sin(i * 0.5));
	CHECK(spectro_compute(pcm, 2205, 11025, SPECTRO_WIDE_MS, 5, &s) == 0);
	unsigned char *lv = malloc((size_t)s.ncols * s.nbins);
	spectro_levels(&s, 70, lv);
	int top = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		if (lv[i] > top)
			top = lv[i];
	CHECK(top == 255);
	CHECK(lv[5 * s.nbins + 10] == 0); /* 25 ms: silence */
	free(lv);
	spectro_free(&s);

	static short quiet[2205]; /* all silence: white, not black */
	CHECK(spectro_compute(quiet, 2205, 11025, SPECTRO_WIDE_MS, 5, &s) == 0);
	lv = malloc((size_t)s.ncols * s.nbins);
	spectro_levels(&s, 50, lv);
	int any = 0;
	for (int i = 0; i < s.ncols * s.nbins; i++)
		any |= lv[i];
	CHECK(any == 0);
	free(lv);
	spectro_free(&s);
}

void suite_audio(void)
{
	test_synth_doc();
	test_wav_header();
	test_spectrogram_peak();
	test_spectrogram_levels();
}
```

In `tests/run_tests.c`, add `void suite_audio(void);` and `{ "audio", suite_audio },` after the `optimize`
lines.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: FAIL to compile: `spectro.h: No such file or directory`.

- [ ] **Step 3: Write whole-document synthesis**

`kpe/synthrun.h`:

```c
/* synthrun.h - synthesizes a whole document through the Klatt DLL. */
#ifndef KPE_SYNTHRUN_H
#define KPE_SYNTHRUN_H

#include "model.h"

typedef struct {
	short *pcm;    /* n samples at sr Hz */
	int n, sr;
	int overload;  /* samples clipped to 16 bits */
} synth_result;

/* Always uses noise seed 1, so the same document gives the same samples. Returns 0, or a negative
 * KLATT_ERR_* code (out->pcm is then NULL). */
int synth_doc(const kpe_doc *d, synth_result *out);
void synth_result_free(synth_result *r);

#endif
```

`kpe/synthrun.c`:

```c
/* synthrun.c - see synthrun.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "synthrun.h"

int synth_doc(const kpe_doc *d, synth_result *out)
{
	memset(out, 0, sizeof *out);
	klatt_h h;
	int r = KlattCreate(d->globals, &h);
	if (r < 0)
		return r;
	KlattReset(h, 1);
	int per = KlattMaxFrameSamples(h);
	short *pcm = malloc(sizeof(short) * ((size_t)per * d->nframes + 1));
	if (!pcm) {
		KlattDestroy(h);
		return KLATT_ERR_MEMORY;
	}
	int n = 0;
	for (int k = 0; k < d->nframes; k++) {
		r = KlattSynthFrame(h, d->frames + (size_t)k * NP, pcm + n, per);
		if (r < 0) {
			free(pcm);
			KlattDestroy(h);
			return r;
		}
		n += r;
	}
	out->pcm = pcm;
	out->n = n;
	out->sr = (int)lround(d->globals[KG_SR]);
	out->overload = KlattOverloadCount(h);
	KlattDestroy(h);
	return 0;
}

void synth_result_free(synth_result *r)
{
	free(r->pcm);
	memset(r, 0, sizeof *r);
}
```

- [ ] **Step 4: Write the WAV writer**

`kpe/wavout.h`:

```c
/* wavout.h - writes 16-bit mono PCM WAV files. */
#ifndef KPE_WAVOUT_H
#define KPE_WAVOUT_H

#include <wchar.h>

/* Returns 0, or -1 if the file cannot be written (errno set). */
int wav_write(const wchar_t *path, const short *pcm, int n, int sr);

#endif
```

`kpe/wavout.c`:

```c
/* wavout.c - see wavout.h. */
#include <stdint.h>
#include <stdio.h>
#include "wavout.h"

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static void put16(unsigned char *p, uint16_t v)
{
	p[0] = v & 0xFF;
	p[1] = v >> 8;
}

int wav_write(const wchar_t *path, const short *pcm, int n, int sr)
{
	unsigned char h[44] = "RIFF____WAVEfmt ____________________data____";
	uint32_t bytes = (uint32_t)n * 2;
	put32(h + 4, 36 + bytes);
	put32(h + 16, 16);           /* fmt chunk size */
	put16(h + 20, 1);            /* PCM */
	put16(h + 22, 1);            /* mono */
	put32(h + 24, (uint32_t)sr);
	put32(h + 28, (uint32_t)sr * 2);
	put16(h + 32, 2);            /* block align */
	put16(h + 34, 16);           /* bits per sample */
	put32(h + 40, bytes);
	FILE *f = _wfopen(path, L"wb");
	if (!f)
		return -1;
	int ok = fwrite(h, 1, 44, f) == 44;
	for (int i = 0; ok && i < n; i++) { /* little-endian regardless of the host */
		unsigned char s[2];
		put16(s, (uint16_t)pcm[i]);
		ok = fwrite(s, 1, 2, f) == 2;
	}
	ok = (fclose(f) == 0) && ok;
	return ok ? 0 : -1;
}
```

- [ ] **Step 5: Write the spectrogram**

`kpe/spectro.h`:

```c
/* spectro.h - spectrogram of the synthesized sound, for the formant strip. */
#ifndef KPE_SPECTRO_H
#define KPE_SPECTRO_H

#define SPECTRO_WIDE_MS 6.0    /* wideband window: resolves formants */
#define SPECTRO_NARROW_MS 30.0 /* narrowband window: resolves harmonics */

typedef struct {
	int ncols, nbins;   /* column c is centred at c * step_ms; bin b is at b * bin_hz */
	double step_ms, bin_hz;
	float *db;          /* power in dB, ncols * nbins, column by column: db[c * nbins + b] */
} kpe_spectro;

/* Pre-emphasis 0.94, Hann window of window_ms centred on each column (zeros beyond the ends), FFT of the
 * next power of two >= max(256, 2 * window). Returns 0, or -1 when out of memory or for bad arguments. */
int spectro_compute(const short *pcm, int n, double sr, double window_ms, double step_ms, kpe_spectro *out);
void spectro_free(kpe_spectro *s);

#define SPECTRO_FLOOR_DB 20.0 /* below the power of a 1-LSB signal: cells under it are silence */

/* Darkness per cell: 255 at the loudest cell, falling linearly to 0 at range_db below it. Cells below
 * SPECTRO_FLOOR_DB are 0, so silence stays white. */
void spectro_levels(const kpe_spectro *s, double range_db, unsigned char *out);

#endif
```

`kpe/spectro.c`:

```c
/* spectro.c - see spectro.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "spectro.h"

#define PI 3.14159265358979323846

/* In-place iterative radix-2 FFT; n is a power of two. */
static void fft(double *re, double *im, int n)
{
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			double t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		double ang = -2 * PI / len;
		for (int i = 0; i < n; i += len)
			for (int k = 0; k < len / 2; k++) {
				double wr = cos(ang * k), wi = sin(ang * k);
				double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
				double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
				re[i + k + len / 2] = re[i + k] - xr;
				im[i + k + len / 2] = im[i + k] - xi;
				re[i + k] += xr;
				im[i + k] += xi;
			}
	}
}

int spectro_compute(const short *pcm, int n, double sr, double window_ms, double step_ms, kpe_spectro *out)
{
	memset(out, 0, sizeof *out);
	if (n < 0 || sr <= 0 || window_ms <= 0 || step_ms <= 0)
		return -1;
	int win = (int)lround(window_ms * sr / 1000);
	if (win < 2)
		win = 2;
	int nfft = 256;
	while (nfft < 2 * win)
		nfft <<= 1;
	double dur_ms = n * 1000.0 / sr;
	int ncols = (int)floor(dur_ms / step_ms) + 1, nbins = nfft / 2 + 1;
	float *db = malloc(sizeof(float) * (size_t)ncols * nbins);
	double *re = malloc(sizeof(double) * nfft), *im = malloc(sizeof(double) * nfft);
	if (!db || !re || !im) {
		free(db);
		free(re);
		free(im);
		return -1;
	}
	for (int c = 0; c < ncols; c++) {
		int centre = (int)lround(c * step_ms * sr / 1000), start = centre - win / 2;
		memset(im, 0, sizeof(double) * nfft);
		memset(re, 0, sizeof(double) * nfft);
		for (int i = 0; i < win; i++) {
			int s = start + i;
			double x = s >= 0 && s < n ? pcm[s] : 0;
			double prev = s - 1 >= 0 && s - 1 < n ? pcm[s - 1] : 0;
			re[i] = (x - 0.94 * prev) * (0.5 - 0.5 * cos(2 * PI * i / (win - 1)));
		}
		fft(re, im, nfft);
		for (int b = 0; b < nbins; b++)
			db[(size_t)c * nbins + b] = (float)(10 * log10(re[b] * re[b] + im[b] * im[b] + 1e-12));
	}
	free(re);
	free(im);
	out->ncols = ncols;
	out->nbins = nbins;
	out->step_ms = step_ms;
	out->bin_hz = sr / nfft;
	out->db = db;
	return 0;
}

void spectro_free(kpe_spectro *s)
{
	free(s->db);
	memset(s, 0, sizeof *s);
}

void spectro_levels(const kpe_spectro *s, double range_db, unsigned char *out)
{
	size_t total = (size_t)s->ncols * s->nbins;
	float top = -1e30f;
	for (size_t i = 0; i < total; i++)
		if (s->db[i] > top)
			top = s->db[i];
	for (size_t i = 0; i < total; i++) {
		double v = s->db[i] < SPECTRO_FLOOR_DB ? 0 : (s->db[i] - (top - range_db)) / range_db;
		out[i] = (unsigned char)lround(255 * (v < 0 ? 0 : v > 1 ? 1 : v));
	}
}
```

- [ ] **Step 6: Run the tests on both architectures**

Run: `MAKE test`
Expected: every suite `ok`, `582 checks, 0 failures` for 32 and 64, and `max difference 0 LSB: ok`.

- [ ] **Step 7: Mention the editor core in the README**

In `README.md`, replace the line

```
- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
```

with

```
- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
- `kpe/`: the editor. Its core (document and undo, CSV files, optimization, synthesis, WAV, spectrogram) has no
  user interface and is covered by the tests; the Win32 GUI is built on top of it.
```

- [ ] **Step 8: Commit**

```bash
git add kpe tests/test_audio.c tests/run_tests.c README.md
git commit -m "Add document synthesis, WAV output and spectrogram

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
