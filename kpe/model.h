/* model.h - the editor's document: globals plus a frames x parameters matrix, edit operations and undo.
 * No Win32 code; the GUI and the tests both drive it through these functions. */
#ifndef KPE_MODEL_H
#define KPE_MODEL_H

#include "klatt.h"

#define NP KLATT_NUM_FRAME  /* parameters per frame */
#define NG KLATT_NUM_GLOBAL /* global parameters */

#define DOC_MAX_DURATION_MS (10.0 * 60 * 1000) /* 10 minutes: doc_new rejects a longer duration */

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
 * Returns -1 (leaving *d untouched) when out of memory, or when duration_ms is not finite, not positive, or
 * greater than DOC_MAX_DURATION_MS.
 * doc_init_from: takes ownership of frames (malloc'd, nframes * NP). Returns 0, or -1 when out of memory. */
int doc_new(kpe_doc *d, const double *globals, double duration_ms);
int doc_init_from(kpe_doc *d, const double *globals, int nframes, double *frames);
void doc_free(kpe_doc *d);

double doc_frame_ms(const kpe_doc *d);
double doc_duration_ms(const kpe_doc *d);
double doc_get(const kpe_doc *d, int frame, int param);

/* Value edits happen inside a transaction; each transaction becomes one undo step (none if nothing changed). */
void doc_begin(kpe_doc *d, const char *label);
/* Returns 1 if the transaction recorded an undo step, 0 if it was empty and dropped, -1 if there was no open
 * transaction. */
int doc_end(kpe_doc *d);
/* Sets one value, clamped to the parameter's range. Outside a transaction it is its own undo step. */
void doc_set(kpe_doc *d, int frame, int param, double v);
/* Sets the frames between t0_ms and t1_ms (either order) by linear interpolation from v0 to v1, rounded to
 * whole units. Frame k sits at k * FRAME_MS. Used for paint strokes and the line tool. */
void doc_paint(kpe_doc *d, int param, double t0_ms, double v0, double t1_ms, double v1);

/* Structural edits; each is one undo step. Return 0, or -1 on bad arguments (including a transaction still
 * open) or out of memory. */
int doc_insert_frames(kpe_doc *d, int at, int count, const double *data); /* data NULL: copies of frame at (or the last) */
int doc_delete_frames(kpe_doc *d, int first, int count);                  /* at least one frame always remains */
int doc_copy_frames(const kpe_doc *d, int first, int count, double *out); /* no undo step */
/* Replaces the globals (clamped, and SR/NFC/SW rounded to the nearest integer since they are whole numbers).
 * If FRAME_MS changes, the frames are resampled by linear interpolation so the duration stays the same.
 * Returns 1 if resampled, 0 if not, -1 on bad arguments (a transaction still open) or out of memory. */
int doc_set_globals(kpe_doc *d, const double *globals);

int doc_can_undo(const kpe_doc *d);
int doc_can_redo(const kpe_doc *d);
const char *doc_undo_label(const kpe_doc *d); /* "" when there is nothing to undo */
const char *doc_redo_label(const kpe_doc *d);
/* Return 0 when a step was applied, -1 on out of memory (the step stays on its original stack, the document is
 * unchanged), 1 when there was nothing to undo/redo. */
int doc_undo(kpe_doc *d);
int doc_redo(kpe_doc *d);

#ifdef KPE_TESTING
/* Test-only fault injection: the nth malloc performed by model.c from this call onward returns NULL, as if out
 * of memory; nth <= 0 disables it. Self-resetting: once it fires, injection is off again. Compiled only when
 * KPE_TESTING is defined (the test build defines it). */
void doc_test_fail_alloc(int nth);
#endif

#endif
