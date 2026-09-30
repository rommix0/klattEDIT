/* main.c - the editor's main window: menus, panel, status bar, file commands, and keeping every view in step
 * with the document. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include "app.h"
#include "csvio.h"
#include "resource.h"
#include "wavout.h"

kpe_app app;

static HWND globals_text;
static HACCEL accel;

int S(int px)
{
	return MulDiv(px, app.dpi, 96);
}

const char *param_name(int kind, int p)
{
	klatt_param_info pi;
	return KlattParamInfo(kind, p, &pi) == KLATT_OK ? pi.name : "?";
}

void app_error(const char *utf8)
{
	wchar_t w[1024];
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 1024);
	MessageBoxW(app.main, w, L"Klatt Parameter Editor", MB_OK | MB_ICONWARNING);
}

void app_status(const wchar_t *text)
{
	SendMessageW(app.status, SB_SETTEXTW, 0, (LPARAM)text);
}

static void default_globals(double *g)
{
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
}

static void update_title(void)
{
	wchar_t t[MAX_PATH + 64];
	const wchar_t *name = app.path;
	for (const wchar_t *p = app.path; *p; p++)
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	swprintf(t, sizeof t / sizeof t[0], L"%ls%ls - Klatt Parameter Editor", app.path[0] ? name : L"Untitled",
	         app.doc.dirty ? L"*" : L"");
	SetWindowTextW(app.main, t);
}

/* The panel lists the globals that matter most while editing; Parameters > Globals shows them all. */
static void update_globals_text(void)
{
	static const int shown[] = { KG_SR, KG_FRAME_MS, KG_NFC, KG_SW, KG_G0, KG_F5, KG_F6 };
	wchar_t t[512];
	int len = swprintf(t, 512, L"Globals\r\n");
	for (unsigned i = 0; i < sizeof shown / sizeof shown[0]; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, shown[i], &pi);
		len += swprintf(t + len, 512 - len, L"%hs  %g %hs\r\n", pi.name, app.doc.globals[shown[i]],
		                strcmp(pi.unit, "-") ? pi.unit : "");
	}
	SetWindowTextW(globals_text, t);
}

static void update_status_info(void)
{
	wchar_t t[160];
	swprintf(t, 160, L"%g Hz \x00B7 %g ms frames \x00B7 %d frames (%g ms)", app.doc.globals[KG_SR],
	         doc_frame_ms(&app.doc), app.doc.nframes, doc_duration_ms(&app.doc));
	SendMessageW(app.status, SB_SETTEXTW, 2, (LPARAM)t);
	if (app.snd.overload)
		swprintf(t, 160, L"Overload: %d samples clipped", app.snd.overload);
	else
		t[0] = 0;
	SendMessageW(app.status, SB_SETTEXTW, 1, (LPARAM)t);
}

/* Recomputes the spectrogram of the current sound. The column step grows with the duration so very long
 * documents do not need minutes and gigabytes to resynthesize on every edit; see SPECTRO_MAX_COLUMNS. */
static void respectro(void)
{
	spectro_free(&app.spec);
	free(app.levels);
	app.levels = NULL;
	double step_ms = fmax(SPECTRO_MIN_STEP_MS, doc_duration_ms(&app.doc) / SPECTRO_MAX_COLUMNS);
	if (app.snd.pcm && spectro_compute(app.snd.pcm, app.snd.n, app.snd.sr,
	                                   app.narrowband ? SPECTRO_NARROW_MS : SPECTRO_WIDE_MS, step_ms,
	                                   &app.spec) == 0) {
		app.levels = malloc((size_t)app.spec.ncols * app.spec.nbins);
		if (app.levels)
			spectro_levels(&app.spec, 50, app.levels);
		free(app.spec.db); /* nothing reads the power matrix past spectro_levels; keep only the metadata */
		app.spec.db = NULL;
	} else if (app.snd.pcm) {
		app_status(L"Spectrogram unavailable (out of memory)");
	}
	plot_new_spectrogram();
}

/* Keeps the formant strip's top frequency inside the range cmd_freq_range offers for the current SR: SR can
 * change (Globals, Open/New, undo/redo of any of those) and leave a stale, now out-of-range value. */
static void clamp_spec_top_hz(void)
{
	double lo = 1000, hi = app.doc.globals[KG_SR] / 2;
	if (app.spec_top_hz > hi)
		app.spec_top_hz = hi;
	if (app.spec_top_hz < lo)
		app.spec_top_hz = lo;
}

void app_changed(int what)
{
	(void)what; /* every change re-synthesizes: at 11025 Hz a whole document takes milliseconds */
	clamp_spec_top_hz();
	synth_result_free(&app.snd);
	int r = synth_doc(&app.doc, &app.snd);
	if (r < 0)
		app_error(KlattErrorText(r));
	respectro();
	if (app.cursor >= app.doc.nframes)
		app.cursor = app.doc.nframes - 1;
	if (app.sel_a >= app.doc.nframes)
		app.sel_a = -1;
	if (app.sel_b >= app.doc.nframes)
		app.sel_b = app.doc.nframes - 1;
	update_globals_text();
	update_title();
	update_status_info();
	table_refresh();
	plot_sync_scroll();
	InvalidateRect(app.plot, NULL, FALSE);
}

void app_set_cursor(int frame, int from_table)
{
	app.cursor = frame;
	if (!from_table)
		table_select(frame);
	InvalidateRect(app.plot, NULL, FALSE);
}

/* Stores path in app.path, resolved to an absolute path so a later Save still finds the file after the
 * working directory changes (a relative path from argv[1], for instance). L"" (no document saved yet) is kept
 * as is. */
static void set_path(const wchar_t *path)
{
	if (!path[0]) {
		app.path[0] = 0;
		return;
	}
	if (!GetFullPathNameW(path, MAX_PATH, app.path, NULL)) {
		wcsncpy(app.path, path, MAX_PATH - 1);
		app.path[MAX_PATH - 1] = 0;
	}
}

static void replace_doc(kpe_doc *d, const wchar_t *path)
{
	audio_stop();
	doc_free(&app.doc);
	app.doc = *d;
	set_path(path);
	app.sel_a = -1;
	app_set_cursor(0, 0);
	app_changed(CHG_ALL);
	plot_fit();
}

/* ---- files ---- */

static int ask_path(int save, const wchar_t *filter, const wchar_t *ext, wchar_t *path)
{
	OPENFILENAMEW of = { .lStructSize = sizeof of };
	of.hwndOwner = app.main;
	of.lpstrFilter = filter;
	of.lpstrFile = path;
	of.nMaxFile = MAX_PATH;
	of.lpstrDefExt = ext;
	of.Flags = (save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST) | OFN_NOCHANGEDIR;
	return save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
}

static const wchar_t csv_filter[] = L"Klatt parameter files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";

/* Saves to path, showing any error or (on success) success-path warning; does not touch app.path. */
static int save_to(const wchar_t *path, csv_report *r)
{
	if (csv_save(path, &app.doc, r) < 0) {
		app_error(r->message);
		return 0;
	}
	return 1;
}

static int save_as(void)
{
	wchar_t path[MAX_PATH];
	wcsncpy(path, app.path, MAX_PATH);
	if (!ask_path(1, csv_filter, L"csv", path))
		return 0;
	size_t len = wcslen(path);
	if (len >= 12 && _wcsicmp(path + len - 12, L".globals.csv") == 0) {
		app_error("Save to the frame file (name.csv), not its globals file (name.globals.csv).");
		return 0;
	}
	csv_report r;
	if (!save_to(path, &r))
		return 0;
	set_path(path); /* only after the save succeeds, so a failed Save As cannot repoint later Saves */
	update_title();
	if (r.message[0])
		app_error(r.message);
	return 1;
}

static int save(void)
{
	if (!app.path[0])
		return save_as();
	csv_report r;
	if (!save_to(app.path, &r))
		return 0;
	update_title();
	if (r.message[0])
		app_error(r.message);
	return 1;
}

/* Before discarding the document: 1 to go ahead, 0 to cancel. */
static int confirm_discard(void)
{
	if (!app.doc.dirty)
		return 1;
	int r = MessageBoxW(app.main, L"Save changes to this document?", L"Klatt Parameter Editor",
	                    MB_YESNOCANCEL | MB_ICONQUESTION);
	if (r == IDYES)
		return save();
	return r == IDNO;
}

static void open_path(const wchar_t *path)
{
	size_t len = wcslen(path);
	if (len >= 12 && _wcsicmp(path + len - 12, L".globals.csv") == 0) {
		app_error("Open the frame file (name.csv), not its globals file (name.globals.csv).");
		return;
	}
	kpe_doc d;
	csv_report r;
	if (csv_load(path, &d, &r) < 0) {
		app_error(r.message);
		return;
	}
	replace_doc(&d, path);
	char note[700] = "";
	if (r.message[0])
		snprintf(note, sizeof note, "%s\n", r.message);
	if (r.clamped)
		snprintf(note + strlen(note), sizeof note - strlen(note),
		         "%d values were outside their parameter's range and were clamped.\n", r.clamped);
	if (note[0])
		app_error(note);
}

static void cmd_new(void)
{
	if (!confirm_discard())
		return;
	double g[NG], dur = 800;
	default_globals(g);
	if (!dlg_globals(app.main, g, &dur))
		return;
	kpe_doc d;
	if (doc_new(&d, g, dur) < 0) {
		app_error("Out of memory.");
		return;
	}
	replace_doc(&d, L"");
}

static void cmd_open(void)
{
	wchar_t path[MAX_PATH] = L"";
	if (confirm_discard() && ask_path(0, csv_filter, L"csv", path))
		open_path(path);
}

static void cmd_export_wav(void)
{
	wchar_t path[MAX_PATH] = L"";
	if (!app.snd.pcm || !ask_path(1, L"WAV files (*.wav)\0*.wav\0", L"wav", path))
		return;
	if (wav_write(path, app.snd.pcm, app.snd.n, app.snd.sr) < 0)
		app_error("The WAV file could not be written.");
}

/* ---- edit commands ---- */

static int has_selection(void)
{
	return app.sel_a >= 0 && app.sel_b >= app.sel_a;
}

/* Returns 1 on success, 0 if it ran out of memory (app.clip is left untouched). */
static int copy_selection(void)
{
	int count = app.sel_b - app.sel_a + 1;
	double *c = malloc(sizeof(double) * NP * count);
	if (!c)
		return 0;
	doc_copy_frames(&app.doc, app.sel_a, count, c);
	free(app.clip);
	app.clip = c;
	app.nclip = count;
	return 1;
}

static void cmd_delete(void)
{
	if (!has_selection())
		return;
	int at = app.sel_a, count = app.sel_b - app.sel_a + 1;
	if (count >= app.doc.nframes) {
		app_error("At least one frame must remain.");
		return;
	}
	if (doc_delete_frames(&app.doc, at, count) < 0) {
		app_error("Out of memory.");
		return;
	}
	app.sel_a = -1;
	app_set_cursor(at < app.doc.nframes ? at : app.doc.nframes - 1, 0);
	app_changed(CHG_FRAMES);
}

static int insert_point(void)
{
	if (has_selection())
		return app.sel_a;
	return app.cursor >= 0 ? app.cursor : app.doc.nframes;
}

static void cmd_paste(void)
{
	if (!app.clip)
		return;
	int at = insert_point();
	if (doc_insert_frames(&app.doc, at, app.nclip, app.clip) == 0) {
		app.sel_a = at;
		app.sel_b = at + app.nclip - 1;
		app_changed(CHG_FRAMES);
	} else {
		app_error("Out of memory.");
	}
}

static void cmd_insert(void)
{
	static double ms = 50;
	if (!dlg_asknum(app.main, L"Insert frames", L"Duration to insert (ms):", &ms, doc_frame_ms(&app.doc), 60000))
		return;
	int count = (int)(ms / doc_frame_ms(&app.doc) + 0.5);
	int at = insert_point();
	if (doc_insert_frames(&app.doc, at, count, NULL) == 0) {
		app.sel_a = at;
		app.sel_b = at + count - 1;
		app_changed(CHG_FRAMES);
	} else {
		app_error("Out of memory.");
	}
}

static void cmd_globals(void)
{
	double g[NG];
	memcpy(g, app.doc.globals, sizeof g);
	if (!dlg_globals(app.main, g, NULL))
		return;
	int r = doc_set_globals(&app.doc, g);
	if (r < 0) {
		app_error("Out of memory.");
		return;
	}
	app_changed(CHG_ALL);
	if (r == 1) {
		char m[160];
		snprintf(m, sizeof m, "The frames were resampled to %g ms; the duration stays about the same.",
		         doc_frame_ms(&app.doc));
		app_error(m);
		plot_fit();
	}
}

static void play(int selection)
{
	if (audio_playing()) {
		audio_stop();
		app.play_ms = -1;
		InvalidateRect(app.plot, NULL, FALSE);
		return;
	}
	if (!app.snd.pcm)
		return;
	int first = 0, count = app.snd.n;
	if (selection) {
		if (!has_selection())
			return;
		first = (int)synth_frame_start(&app.doc, app.sel_a);
		count = (int)synth_frame_start(&app.doc, app.sel_b + 1) - first;
	}
	if (audio_play(app.snd.pcm, app.snd.n, app.snd.sr, first, count) < 0) {
		app_error("The sound device could not be opened.");
		return;
	}
	SetTimer(app.main, 1, 30, NULL);
}

static void cmd_freq_range(void)
{
	double top = app.spec_top_hz;
	if (dlg_asknum(app.main, L"Frequency range", L"Top of the formant strip (Hz):", &top, 1000,
	               app.doc.globals[KG_SR] / 2)) {
		app.spec_top_hz = top;
		plot_new_spectrogram();
		InvalidateRect(app.plot, NULL, FALSE);
	}
}

/* A paint drag holds a transaction open (doc_begin/doc_end around mouse move); commands that change the
 * document would otherwise interleave with it. */
static int blocked_while_open(int id)
{
	if (!app.doc.open)
		return 0;
	switch (id) {
	case IDM_DELETE: case IDM_INSERT: case IDM_PASTE: case IDM_CUT:
	case IDM_UNDO: case IDM_REDO: case IDM_GLOBALS: case IDM_OPTIMIZE:
	case IDM_NEW: case IDM_OPEN:
		return 1;
	}
	return 0;
}

static void command(int id)
{
	if (blocked_while_open(id))
		return;
	switch (id) {
	case IDM_NEW: cmd_new(); break;
	case IDM_OPEN: cmd_open(); break;
	case IDM_SAVE: save(); break;
	case IDM_SAVEAS: save_as(); break;
	case IDM_EXPORTWAV: cmd_export_wav(); break;
	case IDM_EXIT: SendMessageW(app.main, WM_CLOSE, 0, 0); break;
	case IDM_UNDO: if (doc_undo(&app.doc) < 0) app_error("Out of memory."); app_changed(CHG_ALL); break;
	case IDM_REDO: if (doc_redo(&app.doc) < 0) app_error("Out of memory."); app_changed(CHG_ALL); break;
	case IDM_CUT: if (has_selection()) { if (copy_selection()) cmd_delete(); else app_error("Out of memory."); } break;
	case IDM_COPY: if (has_selection()) copy_selection(); break;
	case IDM_PASTE: cmd_paste(); break;
	case IDM_DELETE: cmd_delete(); break;
	case IDM_INSERT: cmd_insert(); break;
	case IDM_SELECTALL:
		app.sel_a = 0;
		app.sel_b = app.doc.nframes - 1;
		app_set_cursor(0, 0);
		break;
	case IDM_SYNTH: app_changed(CHG_VALUES); break;
	case IDM_PLAY: play(0); break;
	case IDM_PLAYSEL: play(1); break;
	case IDM_GLOBALS: cmd_globals(); break;
	case IDM_OPTIMIZE: dlg_optimize(app.main); break;
	case IDM_TABLE: table_show(); break;
	case IDM_NARROW: app.narrowband = !app.narrowband; respectro(); InvalidateRect(app.plot, NULL, FALSE); break;
	case IDM_FREQRANGE: cmd_freq_range(); break;
	case IDM_ZOOMIN: plot_zoom(0.5); break;
	case IDM_ZOOMOUT: plot_zoom(2); break;
	case IDM_ZOOMFIT: plot_fit(); break;
	case IDM_PARAMREF: dlg_paramref(app.main); break;
	case IDM_ABOUT:
		MessageBoxW(app.main,
		            L"Klatt Parameter Editor (kpe_open) 1.0\n\n"
		            L"A clean-room editor inspired by the Klatt Parameter Editor of IPOX "
		            L"(Dirksen and Coleman, 1995). It contains no original IPOX code.\n\n"
		            L"Synthesizer: Klatt (1980), J. Acoust. Soc. Am. 67, 971-995.",
		            L"About", MB_OK | MB_ICONINFORMATION);
		break;
	}
}

static void menu_label(HMENU m, int id, const wchar_t *verb, const char *label, const wchar_t *key)
{
	wchar_t t[96];
	if (label && label[0])
		swprintf(t, 96, L"%ls %hs\t%ls", verb, label, key);
	else
		swprintf(t, 96, L"%ls\t%ls", verb, key);
	ModifyMenuW(m, id, MF_BYCOMMAND | MF_STRING, id, t);
}

static void init_menu(HMENU m)
{
	int sel = has_selection();
	menu_label(m, IDM_UNDO, L"&Undo", doc_undo_label(&app.doc), L"Ctrl+Z");
	menu_label(m, IDM_REDO, L"&Redo", doc_redo_label(&app.doc), L"Ctrl+Y");
	EnableMenuItem(m, IDM_UNDO, doc_can_undo(&app.doc) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_REDO, doc_can_redo(&app.doc) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_CUT, sel ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_COPY, sel ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_DELETE, sel ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_PASTE, app.clip ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(m, IDM_PLAYSEL, sel ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(m, IDM_NARROW, app.narrowband ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(m, IDM_TABLE, app.table && IsWindowVisible(app.table) ? MF_CHECKED : MF_UNCHECKED);
}

/* ---- left panel ---- */

static const struct { const char *title; int params[7]; } groups[] = {
	{ "Source", { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, -1 } },
	{ "Formants (on spectrogram)", { KP_F1, KP_F2, KP_F3, KP_F4, -1 } },
	{ "Bandwidths", { KP_B1, KP_B2, KP_B3, -1 } },
	{ "Parallel", { KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB } },
	{ "Nasal", { KP_FNZ, KP_AN, -1 } },
};

static LRESULT CALLBACK panel_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= IDC_CHECK_BASE &&
	    LOWORD(wp) < IDC_CHECK_BASE + NP) {
		int p = LOWORD(wp) - IDC_CHECK_BASE;
		app.visible[p] = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
		InvalidateRect(app.plot, NULL, FALSE);
		return 0;
	}
	if (msg == WM_CTLCOLORSTATIC) { /* group titles in grey */
		if (GetDlgCtrlID((HWND)lp) == 1) {
			SetTextColor((HDC)wp, GetSysColor(COLOR_GRAYTEXT));
			SetBkMode((HDC)wp, TRANSPARENT);
			return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
		}
	}
	return DefWindowProcW(h, msg, wp, lp);
}

static HWND child(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int hgt, HWND parent, int id)
{
	HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, hgt, parent,
	                         (HMENU)(INT_PTR)id, app.inst, NULL);
	SendMessageW(c, WM_SETFONT, (WPARAM)app.font, 0);
	return c;
}

static void create_panel(void)
{
	app.panel = CreateWindowExW(0, L"KpePanel", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
	                            app.main, NULL, app.inst, NULL);
	int y = S(6), x = S(8), w = S(150);
	for (unsigned g = 0; g < sizeof groups / sizeof groups[0]; g++) {
		wchar_t t[64];
		MultiByteToWideChar(CP_UTF8, 0, groups[g].title, -1, t, 64);
		child(L"STATIC", t, 0, x, y, w, S(16), app.panel, 1);
		y += S(18);
		for (int i = 0; i < 7 && groups[g].params[i] >= 0; i++) {
			int p = groups[g].params[i];
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			swprintf(t, 64, L"%hs", pi.name);
			HWND c = child(L"BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, x + S(6), y, w - S(6), S(18), app.panel,
			               IDC_CHECK_BASE + p);
			SendMessageW(c, BM_SETCHECK, app.visible[p] ? BST_CHECKED : BST_UNCHECKED, 0);
			y += S(19);
		}
		y += S(4);
	}
	globals_text = child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, y + S(4), w, S(18) * 9, app.panel, 2);
}

/* ---- main window ---- */

static void layout(void)
{
	RECT rc = { 0 };
	GetClientRect(app.main, &rc);
	SendMessageW(app.status, WM_SIZE, 0, 0);
	RECT sr = { 0 };
	GetWindowRect(app.status, &sr);
	int sh = sr.bottom - sr.top, pw = S(170);
	int parts[3] = { rc.right - S(560), rc.right - S(340), -1 };
	SendMessageW(app.status, SB_SETPARTS, 3, (LPARAM)parts);
	MoveWindow(app.panel, 0, 0, pw, rc.bottom - sh, TRUE);
	MoveWindow(app.plot, pw, 0, rc.right - pw, rc.bottom - sh, TRUE);
}

static LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		layout();
		return 0;
	case WM_COMMAND:
		command(LOWORD(wp));
		return 0;
	case WM_INITMENUPOPUP:
		init_menu(GetMenu(h));
		return 0;
	case WM_TIMER:
		app.play_ms = audio_pos_ms();
		if (app.play_ms < 0)
			KillTimer(h, 1);
		InvalidateRect(app.plot, NULL, FALSE);
		return 0;
	case WM_APP_PLAYDONE:
		audio_finished(wp);
		if (!audio_playing()) {
			app.play_ms = -1;
			KillTimer(h, 1);
			InvalidateRect(app.plot, NULL, FALSE);
		}
		return 0;
	case WM_CLOSE:
		if (confirm_discard())
			DestroyWindow(h);
		return 0;
	case WM_QUERYENDSESSION:
		return confirm_discard();
	case WM_GETMINMAXINFO: {
		MINMAXINFO *mmi = (MINMAXINFO *)lp;
		mmi->ptMinTrackSize.x = S(640);
		mmi->ptMinTrackSize.y = S(480);
		return 0;
	}
	case WM_DESTROY:
		audio_stop();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

/* Accelerators do not apply while typing in an edit control. */
static int typing(HWND h)
{
	wchar_t cls[16];
	return GetClassNameW(h, cls, 16) && _wcsicmp(cls, L"Edit") == 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
	(void)prev;
	(void)cmdline;
	INITCOMMONCONTROLSEX icc = { .dwSize = sizeof icc, .dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);
	app.inst = inst;
	HDC dc = GetDC(NULL);
	app.dpi = GetDeviceCaps(dc, LOGPIXELSY);
	ReleaseDC(NULL, dc);
	NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
	app.font = CreateFontIndirectW(&ncm.lfMessageFont);

	app.spec_top_hz = 5000;
	app.play_ms = -1;
	app.sel_a = -1;
	static const int shown[] = { KP_F0, KP_AV, KP_AF, KP_AH, KP_F1, KP_F2, KP_F3, KP_F4 };
	for (unsigned i = 0; i < sizeof shown / sizeof shown[0]; i++)
		app.visible[shown[i]] = 1;
	double g[NG];
	default_globals(g);
	if (doc_new(&app.doc, g, 800) < 0)
		return 1;

	WNDCLASSEXW wc = { .cbSize = sizeof wc };
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpfnWndProc = main_proc;
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
	wc.lpszClassName = L"KpeMain";
	RegisterClassExW(&wc);
	wc.lpfnWndProc = panel_proc;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = L"KpePanel";
	RegisterClassExW(&wc);
	plot_register(inst);

	app.main = CreateWindowExW(0, L"KpeMain", L"Klatt Parameter Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
	                           CW_USEDEFAULT, CW_USEDEFAULT, S(1100), S(760), NULL, NULL, inst, NULL);
	app.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
	                             app.main, NULL, inst, NULL);
	create_panel();
	app.plot = CreateWindowExW(0, L"KpePlot", L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL, 0, 0, 0, 0, app.main,
	                           NULL, inst, NULL);
	accel = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL));
	layout();
	app_changed(CHG_ALL);
	plot_fit();
	ShowWindow(app.main, show);

	int argc;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv && argc > 1)
		open_path(argv[1]);
	LocalFree(argv);

	MSG m;
	while (GetMessageW(&m, NULL, 0, 0) > 0) {
		if (!typing(m.hwnd) && TranslateAcceleratorW(app.main, accel, &m))
			continue;
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	return 0;
}
