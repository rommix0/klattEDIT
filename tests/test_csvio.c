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
