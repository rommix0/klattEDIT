#include <math.h>
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

static void test_new_document_rejects_bad_duration(void)
{
	kpe_doc d;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 0) == -1);
	CHECK(doc_new(&d, g, -5) == -1);
	CHECK(doc_new(&d, g, NAN) == -1);
	CHECK(doc_new(&d, g, INFINITY) == -1);
	CHECK(doc_new(&d, g, DOC_MAX_DURATION_MS + 1) == -1);
	CHECK(doc_new(&d, g, DOC_MAX_DURATION_MS) == 0); /* right at the limit: accepted */
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
	CHECK(doc_undo(&d) == 0);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 500, 0);
	CHECK(doc_can_redo(&d));
	CHECK(doc_redo(&d) == 0);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 1300, 0);
	doc_set(&d, 99, KP_F1, 700); /* out of range frame: ignored, no step */
	CHECK(doc_undo(&d) == 0);
	CHECK_NEAR(doc_get(&d, 3, KP_F1), 500, 0);
	CHECK(!doc_can_undo(&d));
	doc_free(&d);
}

static void test_undo_redo_report_nothing_to_do(void)
{
	kpe_doc d;
	new_doc(&d, 50);
	CHECK(doc_undo(&d) == 1); /* nothing to undo */
	CHECK(doc_redo(&d) == 1); /* nothing to redo */
	doc_set(&d, 0, KP_AV, 60);
	CHECK(doc_redo(&d) == 1); /* still nothing to redo: the edit cleared it */
	CHECK(doc_undo(&d) == 0);
	CHECK(doc_undo(&d) == 1); /* nothing left to undo */
	doc_free(&d);
}

static void test_doc_end_return_value(void)
{
	kpe_doc d;
	new_doc(&d, 50);
	CHECK(doc_end(&d) == -1); /* no open transaction */
	doc_begin(&d, "Edit");
	doc_set(&d, 0, KP_AV, 60);
	CHECK(doc_end(&d) == 1); /* recorded a step */
	doc_begin(&d, "Edit");
	doc_set(&d, 0, KP_AV, 60); /* already 60: no-op */
	CHECK(doc_end(&d) == 0); /* empty, dropped */
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
	CHECK(doc_end(&d) == 1);
	CHECK(strcmp(doc_undo_label(&d), "Paint") == 0);
	CHECK_NEAR(doc_get(&d, 1, KP_F2), 1800, 0);
	CHECK(doc_undo(&d) == 0);
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
	CHECK(doc_end(&d) == 0);
	CHECK(!doc_can_undo(&d));
	CHECK(!d.dirty);
	doc_free(&d);
}

static void test_structural_edit_rejected_during_transaction(void)
{
	kpe_doc d;
	double buf[NP];
	new_doc(&d, 50);
	doc_begin(&d, "Paint");
	CHECK(doc_insert_frames(&d, 0, 1, NULL) == -1);
	CHECK(doc_delete_frames(&d, 0, 1) == -1);
	CHECK(doc_set_globals(&d, d.globals) == -1);
	CHECK(doc_copy_frames(&d, 0, 1, buf) == 0); /* read-only: not a structural edit, never blocked */
	doc_set(&d, 0, KP_AV, 60);
	CHECK(doc_end(&d) == 1);
	/* Once the transaction is closed, the same calls work again. */
	CHECK(doc_insert_frames(&d, 0, 1, NULL) == 0);
	doc_free(&d);
}

static void test_new_edit_clears_redo(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	doc_set(&d, 0, KP_AV, 60);
	CHECK(doc_undo(&d) == 0);
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
	CHECK(doc_undo(&d) == 0);
	CHECK(d.nframes == 10);
	CHECK_NEAR(doc_get(&d, 5, KP_F0), 105, 0);
	CHECK(doc_redo(&d) == 0);
	CHECK(d.nframes == 12);

	CHECK(doc_copy_frames(&d, 0, 3, buf) == 0);
	CHECK(doc_delete_frames(&d, 0, 3) == 0);
	CHECK(d.nframes == 9);
	CHECK_NEAR(doc_get(&d, 0, KP_F0), 103, 0);
	CHECK(doc_insert_frames(&d, 9, 3, buf) == 0); /* paste at the end */
	CHECK(d.nframes == 12);
	CHECK_NEAR(doc_get(&d, 11, KP_F0), 102, 0);
	CHECK(doc_undo(&d) == 0);
	CHECK(doc_undo(&d) == 0);
	CHECK(d.nframes == 12);
	CHECK_NEAR(doc_get(&d, 0, KP_F0), 100, 0);

	CHECK(doc_delete_frames(&d, 0, 12) == -1); /* one frame must remain */
	CHECK(doc_delete_frames(&d, 10, 5) == -1);
	CHECK(doc_insert_frames(&d, 13, 1, NULL) == -1);
	CHECK(doc_copy_frames(&d, 11, 2, buf) == -1);
	doc_free(&d);
}

static void test_undo_oom_leaves_document_unchanged(void)
{
	kpe_doc d;
	new_doc(&d, 50); /* 10 frames */
	for (int k = 0; k < 10; k++)
		doc_set(&d, k, KP_F0, 100 + k);
	CHECK(doc_delete_frames(&d, 3, 2) == 0); /* removes frames 3,4 (values 103, 104) */
	CHECK(d.nframes == 8);
	CHECK_NEAR(doc_get(&d, 3, KP_F0), 105, 0); /* what used to be frame 5 */

	doc_test_fail_alloc(1); /* fail the very next malloc: the undo's replacement frame buffer */
	CHECK(doc_undo(&d) == -1);
	CHECK(d.nframes == 8); /* document unchanged */
	CHECK_NEAR(doc_get(&d, 3, KP_F0), 105, 0);
	CHECK(doc_can_undo(&d)); /* the step stayed on the undo stack */

	CHECK(doc_undo(&d) == 0); /* a later undo (no injected failure) succeeds */
	CHECK(d.nframes == 10);
	CHECK_NEAR(doc_get(&d, 3, KP_F0), 103, 0); /* restored exactly */
	CHECK_NEAR(doc_get(&d, 4, KP_F0), 104, 0);
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
	CHECK(doc_undo(&d) == 0);
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
	CHECK(doc_undo(&d) == 0);
	CHECK(doc_undo(&d) == 0);
	CHECK(d.nframes == 20);
	CHECK_NEAR(d.globals[KG_FRAME_MS], 5, 0);
	CHECK_NEAR(doc_get(&d, 7, KP_F0), 170, 0);
	doc_free(&d);
}

static void test_redo_after_frame_ms_resample(void)
{
	kpe_doc d;
	double g[NG];
	new_doc(&d, 100); /* 20 frames of 5 ms */
	for (int k = 0; k < 20; k++)
		doc_set(&d, k, KP_F0, 100 + 10 * k);
	memcpy(g, d.globals, sizeof g);
	g[KG_FRAME_MS] = 10;
	CHECK(doc_set_globals(&d, g) == 1);
	CHECK(d.nframes == 10);
	CHECK(doc_undo(&d) == 0);
	CHECK(d.nframes == 20);
	CHECK_NEAR(d.globals[KG_FRAME_MS], 5, 0);
	CHECK(doc_redo(&d) == 0);
	CHECK(d.nframes == 10);
	CHECK_NEAR(d.globals[KG_FRAME_MS], 10, 0);
	CHECK_NEAR(doc_get(&d, 3, KP_F0), 160, 1e-9);
	doc_free(&d);
}

void suite_model(void)
{
	test_new_document();
	test_new_document_rejects_bad_duration();
	test_set_clamps_and_undoes();
	test_undo_redo_report_nothing_to_do();
	test_doc_end_return_value();
	test_transaction_is_one_step();
	test_no_op_transaction_leaves_no_step();
	test_structural_edit_rejected_during_transaction();
	test_new_edit_clears_redo();
	test_paint_interpolates_and_rounds();
	test_insert_delete_copy();
	test_undo_oom_leaves_document_unchanged();
	test_globals_without_resample();
	test_frame_ms_change_resamples();
	test_redo_after_frame_ms_resample();
}
