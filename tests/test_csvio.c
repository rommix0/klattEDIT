/* CSV tests write their files into the working directory (build/32 or build/64). */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
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

static void test_globals_rounded_to_whole_numbers(void)
{
	kpe_doc d;
	csv_report r;
	write_text(L"round.csv", "time_ms,AV\n0,60\n");
	write_text(L"round.globals.csv", "SR,NFC,SW\n11025.6,4.5,0.4\n");
	CHECK(csv_load(L"round.csv", &d, &r) == 0);
	CHECK_NEAR(d.globals[KG_SR], 11026, 0);
	CHECK_NEAR(d.globals[KG_NFC], 5, 0);
	CHECK_NEAR(d.globals[KG_SW], 0, 0);
	doc_free(&d);
}

static void test_time_shift_warning(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("time_ms,AV\n20,60\n30,60\n40,60\n", &d, &r) == 0);
	CHECK(strcmp(r.message, "times start at 20 ms; the document starts at 0 ms") == 0);
	doc_free(&d);
}

static void test_frame_ms_clamped_from_spacing_warning(void)
{
	kpe_doc d;
	csv_report r;
	/* time_ms steps by 25 ms, over FRAME_MS's max of 20; no globals file, so FRAME_MS is derived and clamped. */
	CHECK(load_text("time_ms,AV\n0,60\n25,60\n50,60\n", &d, &r) == 0);
	CHECK_NEAR(doc_frame_ms(&d), 20, 0);
	CHECK(strcmp(r.message, "time_ms steps by 25 ms; FRAME_MS clamped to 20 ms") == 0);
	doc_free(&d);
}

static void test_empty_header_cell_ignored(void)
{
	kpe_doc d;
	csv_report r;
	CHECK(load_text("time_ms,F1,\n0,500,\n5,600,\n", &d, &r) == 0);
	CHECK(r.unknown_columns == 0);
	CHECK(strcmp(r.message, "") == 0);
	CHECK_NEAR(doc_get(&d, 1, KP_F1), 600, 0);
	doc_free(&d);
}

static void test_too_many_values_reported(void)
{
	kpe_doc d;
	csv_report r;
	char text[2000] = "time_ms,F1\n0,500\n5";
	for (int i = 0; i < 70; i++)
		strcat(text, ",1");
	strcat(text, "\n");
	CHECK(load_text(text, &d, &r) == -1);
	CHECK(strcmp(r.message, "t.csv line 3: more than 64 values") == 0);
}

static void test_globals_too_many_values_reported(void)
{
	kpe_doc d;
	csv_report r;
	write_text(L"gtoo.csv", "time_ms,AV\n0,60\n10,60\n");
	char text[2000] = "SR\n11025";
	for (int i = 0; i < 70; i++)
		strcat(text, ",1");
	strcat(text, "\n");
	write_text(L"gtoo.globals.csv", text);
	CHECK(csv_load(L"gtoo.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "gtoo.globals.csv line 2: more than 64 values") == 0);
}

static void test_long_nonascii_name_in_message(void)
{
	kpe_doc d;
	csv_report r;
	/* A long, non-ASCII display name; message must name it correctly, not truncate or garble it. */
	wchar_t path[100] = L"";
	for (int i = 0; i < 40; i++)
		wcscat(path, L"é"); /* 'e' with acute accent, 40 times */
	wcscat(path, L".csv");
	CHECK(csv_load(path, &d, &r) == -1);
	char expect[300] = "cannot read ";
	size_t used = strlen(expect);
	int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, expect + used, (int)(sizeof expect - used), NULL, NULL);
	CHECK(n > 0);
	CHECK(strncmp(r.message, expect, strlen(expect)) == 0);
}

static void set_readonly(const wchar_t *path, int ro)
{
	DWORD attr = GetFileAttributesW(path);
	SetFileAttributesW(path, ro ? (attr | FILE_ATTRIBUTE_READONLY) : (attr & ~FILE_ATTRIBUTE_READONLY));
}

static void test_readonly_frame_file_rejected(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 10) == 0);
	CHECK(csv_save(L"ro.csv", &d, &r) == 0);
	set_readonly(L"ro.csv", 1);
	doc_set(&d, 0, KP_AV, 60);
	CHECK(csv_save(L"ro.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "ro.csv is read-only") == 0);
	CHECK(!exists(L"ro.csv.tmp") && !exists(L"ro.globals.csv.tmp") && !exists(L"ro.csv.kpe-backup"));
	set_readonly(L"ro.csv", 0); /* cleanup */
	CHECK(csv_load(L"ro.csv", &e, &r) == 0);
	CHECK_NEAR(doc_get(&e, 0, KP_AV), 0, 0); /* unchanged */
	doc_free(&e);
	doc_free(&d);
}

static void test_readonly_globals_file_rejected(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 10) == 0);
	CHECK(csv_save(L"rog.csv", &d, &r) == 0);
	set_readonly(L"rog.globals.csv", 1);
	doc_set(&d, 0, KP_AV, 60);
	CHECK(csv_save(L"rog.csv", &d, &r) == -1);
	CHECK(strcmp(r.message, "rog.globals.csv is read-only") == 0);
	CHECK(!exists(L"rog.csv.tmp") && !exists(L"rog.globals.csv.tmp") && !exists(L"rog.csv.kpe-backup"));
	set_readonly(L"rog.globals.csv", 0); /* cleanup */
	CHECK(csv_load(L"rog.csv", &e, &r) == 0);
	CHECK_NEAR(doc_get(&e, 0, KP_AV), 0, 0); /* unchanged */
	doc_free(&e);
	doc_free(&d);
}

static void test_globals_file_without_frame_ms(void)
{
	kpe_doc d;
	csv_report r;
	write_text(L"nofm.csv", "time_ms,AV\n0,60\n10,60\n20,60\n");
	write_text(L"nofm.globals.csv", "SR,G0\n16000,55\n");
	CHECK(csv_load(L"nofm.csv", &d, &r) == 0);
	CHECK(!r.globals_missing);
	CHECK_NEAR(doc_frame_ms(&d), 10, 0); /* derived from time_ms spacing: the file has no FRAME_MS column */
	CHECK_NEAR(d.globals[KG_SR], 16000, 0);
	doc_free(&d);
}

static void test_nonascii_path_round_trip(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 20) == 0);
	doc_set(&d, 0, KP_F1, 900);
	const wchar_t *path = L"café-あ.csv"; /* non-ASCII path */
	CHECK(csv_save(path, &d, &r) == 0);
	CHECK(csv_load(path, &e, &r) == 0);
	CHECK_NEAR(doc_get(&e, 0, KP_F1), 900, 0);
	doc_free(&e);
	doc_free(&d);
}

static void test_fractional_frame_ms_round_trip(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	g[KG_FRAME_MS] = 6.4;
	CHECK(doc_new(&d, g, 64) == 0); /* 10 frames */
	doc_set(&d, 2, KP_F2, 1234);
	CHECK(csv_save(L"frac.csv", &d, &r) == 0);
	CHECK(csv_load(L"frac.csv", &e, &r) == 0);
	CHECK(strcmp(r.message, "") == 0);
	CHECK_NEAR(doc_frame_ms(&e), 6.4, 1e-9);
	CHECK_NEAR(doc_get(&e, 2, KP_F2), 1234, 0);
	doc_free(&e);
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

static void test_save_atomicity_across_files(void)
{
	kpe_doc d, e;
	csv_report r;
	double g[NG];
	t_default_globals(g);
	CHECK(doc_new(&d, g, 5) == 0);
	CHECK(csv_save(L"atom.csv", &d, &r) == 0);

	/* Open the globals file exclusively to block a save. */
	HANDLE h = CreateFileW(L"atom.globals.csv", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	CHECK(h != INVALID_HANDLE_VALUE);

	/* Try to save with the globals file locked. */
	doc_set(&d, 0, KP_F1, 800);
	CHECK(csv_save(L"atom.csv", &d, &r) == -1);
	CHECK(strncmp(r.message, "cannot replace atom.globals.csv", 31) == 0);
	CHECK(d.dirty); /* still dirty after failed save */

	/* Close the lock and verify the frame file was NOT overwritten. */
	CloseHandle(h);
	CHECK(csv_load(L"atom.csv", &e, &r) == 0);
	CHECK_NEAR(doc_get(&e, 0, KP_F1), 500, 0); /* original default, not 800 */
	doc_free(&e);

	/* Verify no stray temp or backup files. */
	CHECK(!exists(L"atom.csv.tmp"));
	CHECK(!exists(L"atom.csv.kpe-backup"));
	CHECK(!exists(L"atom.globals.csv.tmp"));

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
	test_globals_rounded_to_whole_numbers();
	test_time_shift_warning();
	test_frame_ms_clamped_from_spacing_warning();
	test_empty_header_cell_ignored();
	test_too_many_values_reported();
	test_globals_too_many_values_reported();
	test_long_nonascii_name_in_message();
	test_readonly_frame_file_rejected();
	test_readonly_globals_file_rejected();
	test_globals_file_without_frame_ms();
	test_nonascii_path_round_trip();
	test_fractional_frame_ms_round_trip();
	test_globals_file();
	test_rejections();
	test_failed_save_keeps_old_files();
	test_save_atomicity_across_files();
}
