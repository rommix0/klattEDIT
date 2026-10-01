/* model.c - document, edit operations and undo. See model.h. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

#ifdef KPE_TESTING
/* Test-only allocation-failure injection; see doc_test_fail_alloc in model.h. */
static int fail_alloc_nth = 0;

void doc_test_fail_alloc(int nth)
{
	fail_alloc_nth = nth;
}

static void *test_malloc(size_t n)
{
	if (fail_alloc_nth > 0 && --fail_alloc_nth == 0)
		return NULL;
	return malloc(n);
}
#define malloc(n) test_malloc(n)
#endif

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

/* Clamps a global to its range; SR, NFC, SW and SRC are whole numbers, so they are then rounded to the nearest
 * integer (half away from zero). */
static double clamp_global(int index, double v)
{
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
		d->globals[i] = clamp_global(i, globals[i]);
	d->nframes = nframes;
	d->frames = frames;
	return 0;
}

int doc_new(kpe_doc *d, const double *globals, double duration_ms)
{
	if (!isfinite(duration_ms) || duration_ms <= 0 || duration_ms > DOC_MAX_DURATION_MS)
		return -1;
	double g[NG];
	for (int i = 0; i < NG; i++)
		g[i] = clamp_global(i, globals[i]);
	int n = (int)lround(duration_ms / g[KG_FRAME_MS]);
	if (n < 1)
		n = 1;
	if ((size_t)n > (SIZE_MAX / sizeof(double)) / NP) /* guard against overflow below */
		return -1;
	double *f = malloc(sizeof(double) * (size_t)NP * (size_t)n);
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

int doc_end(kpe_doc *d)
{
	undo_step *s = d->open;
	if (!s)
		return -1;
	d->open = NULL;
	free(d->touched);
	d->touched = NULL;
	int n = 0; /* drop cells that ended where they started */
	for (int i = 0; i < s->ncells; i++)
		if (s->cells[i].old != s->cells[i].now)
			s->cells[n++] = s->cells[i];
	s->ncells = n;
	if (n == 0) {
		step_free(s);
		return 0;
	}
	record(d, s);
	return 1;
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
		set_cell(d, k, param, round_to_step(param, v));
	}
	if (own)
		doc_end(d);
}

/* Builds the frame buffer that a splice replacing [at, at + old_count) with new_count frames from data would
 * produce, without touching the document. Returns a malloc'd buffer (and sets *out_n), or NULL out of memory. */
static double *splice_build(const kpe_doc *d, int at, int old_count, int new_count, const double *data, int *out_n)
{
	int n = d->nframes - old_count + new_count;
	double *f = malloc(sizeof(double) * NP * (n > 0 ? n : 1));
	if (!f)
		return NULL;
	memcpy(f, d->frames, sizeof(double) * NP * at);
	memcpy(f + NP * at, data, sizeof(double) * NP * new_count);
	memcpy(f + NP * (at + new_count), d->frames + NP * (at + old_count),
	       sizeof(double) * NP * (d->nframes - at - old_count));
	*out_n = n;
	return f;
}

/* Replaces frames [at, at + old_count) with new_count frames from data. Returns 0 or -1. */
static int splice(kpe_doc *d, int at, int old_count, int new_count, const double *data)
{
	int n;
	double *f = splice_build(d, at, old_count, new_count, data, &n);
	if (!f)
		return -1;
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

/* One splice of the whole document down to the kept frames: a single undo step, which a deletion before and one
 * after the range would not be. */
int doc_crop(kpe_doc *d, int first, int count)
{
	if (d->open || first < 0 || count <= 0 || first + count > d->nframes || count >= d->nframes)
		return -1;
	double *kept = malloc(sizeof(double) * NP * count);
	if (!kept)
		return -1;
	memcpy(kept, d->frames + NP * first, sizeof(double) * NP * count);
	return splice_step(d, "Crop", 0, d->nframes, count, kept, NULL);
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
		g[i] = clamp_global(i, globals[i]);
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

/* Applies a step backwards (undo) or forwards (redo). Every allocation it needs (the splice's replacement
 * frame buffer) is made before any document state changes, so a failure leaves the document exactly as it
 * was. Returns 0, or -1 out of memory. */
static int apply(kpe_doc *d, undo_step *s, int forward)
{
	double *f = NULL;
	int n = 0;
	if (s->splice) {
		int at = s->at;
		int old_count = forward ? s->old_count : s->new_count;
		int new_count = forward ? s->new_count : s->old_count;
		const double *data = forward ? s->new_data : s->old_data;
		f = splice_build(d, at, old_count, new_count, data, &n);
		if (!f)
			return -1;
	}
	/* Nothing past this point can fail: commit. */
	if (s->splice) {
		free(d->frames);
		d->frames = f;
		d->nframes = n;
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
	return 0;
}

int doc_undo(kpe_doc *d)
{
	if (!doc_can_undo(d))
		return 1;
	undo_step *s = d->undo[d->nundo - 1];
	if (apply(d, s, 0) < 0)
		return -1; /* step stays on the undo stack; document unchanged */
	d->nundo--;
	if (push(&d->redo, &d->nredo, &d->capredo, s) < 0)
		step_free(s);
	return 0;
}

int doc_redo(kpe_doc *d)
{
	if (!doc_can_redo(d))
		return 1;
	undo_step *s = d->redo[d->nredo - 1];
	if (apply(d, s, 1) < 0)
		return -1; /* step stays on the redo stack; document unchanged */
	d->nredo--;
	if (push(&d->undo, &d->nundo, &d->capundo, s) < 0)
		step_free(s);
	return 0;
}

int doc_redo_discard(kpe_doc *d)
{
	if (d->nredo == 0)
		return 1;
	d->nredo--;
	step_free(d->redo[d->nredo]);
	return 0;
}
