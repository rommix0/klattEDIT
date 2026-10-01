/* dialogs.c - File > New and Parameters > Globals (one dialog), File > Import WAV's range and options (with its
 * waveform strip), Parameters > Optimize, Help > Parameter reference, and a small number prompt. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <commctrl.h>
#include "app.h"
#include "optimize.h"
#include "resource.h"
#include "waverange.h"

/* Reads a number from a dialog field; 1 on success. */
static int get_number(HWND dlg, int id, double *v)
{
	wchar_t t[64], *end;
	GetDlgItemTextW(dlg, id, t, 64);
	*v = wcstod(t, &end);
	while (*end == L' ')
		end++;
	return end != t && !*end && isfinite(*v);
}

static void set_number(HWND dlg, int id, double v)
{
	wchar_t t[64];
	swprintf(t, 64, L"%g", v);
	SetDlgItemTextW(dlg, id, t);
}

/* Shows msg and puts the focus on the field; returns 0 for use in validation. */
static int reject(HWND dlg, int id, const wchar_t *msg)
{
	MessageBoxW(dlg, msg, L"Klatt Parameter Editor", MB_OK | MB_ICONWARNING);
	HWND f = GetDlgItem(dlg, id);
	SetFocus(f);
	SendMessageW(f, EM_SETSEL, 0, -1);
	return 0;
}

/* ---- New / Globals ---- */

typedef struct {
	double *globals, *duration_ms;
} globals_args;

/* Creates one row per global inside the IDC_FIELDS frame, in table order: the name, its description (so SW
 * and SRC cannot be mistaken for each other), the range with its unit, and the edit field, left to right.
 * Without the duration row (Globals rather than New) the fields move up into its place. */
static void create_fields(HWND dlg, const double *g, int with_duration)
{
	RECT area, dur;
	GetWindowRect(GetDlgItem(dlg, IDC_FIELDS), &area);
	MapWindowPoints(NULL, dlg, (POINT *)&area, 2);
	if (!with_duration) {
		GetWindowRect(GetDlgItem(dlg, IDC_DURATION), &dur);
		MapWindowPoints(NULL, dlg, (POINT *)&dur, 2);
		area.top = dur.top;
	}
	RECT du = { 0, 0, 4, 13 }; /* a row is 13 dialog units high; 4 du is the gap between columns */
	MapDialogRect(dlg, &du);
	int row_h = du.bottom, pad = du.right, row_w = area.right - area.left;
	int name_w = row_w * 12 / 100, desc_w = row_w * 50 / 100, range_w = row_w * 20 / 100;
	int name_x = area.left, desc_x = name_x + name_w, range_x = desc_x + desc_w, edit_x = range_x + range_w;
	HFONT font = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		int y = area.top + i * row_h;
		wchar_t name[32], desc[128], range[48];
		swprintf(name, 32, L"%hs", pi.name);
		swprintf(desc, 128, L"%hs", pi.description);
		if (strcmp(pi.unit, "-") == 0)
			swprintf(range, 48, L"%g-%g", pi.min, pi.max);
		else
			swprintf(range, 48, L"%g-%g %hs", pi.min, pi.max, pi.unit);
		HWND sn = CreateWindowExW(0, L"STATIC", name, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, name_x, y + 2,
		                          name_w - pad, row_h - 2, dlg, NULL, app.inst, NULL);
		HWND sd = CreateWindowExW(0, L"STATIC", desc, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, desc_x, y + 2,
		                          desc_w - pad, row_h - 2, dlg, NULL, app.inst, NULL);
		HWND sr = CreateWindowExW(0, L"STATIC", range, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, range_x, y + 2,
		                          range_w - pad, row_h - 2, dlg, NULL, app.inst, NULL);
		HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		                         edit_x, y, area.right - edit_x, row_h - 2, dlg,
		                         (HMENU)(INT_PTR)(IDC_FIELD_BASE + i), app.inst, NULL);
		SendMessageW(sn, WM_SETFONT, (WPARAM)font, 0);
		SendMessageW(sd, WM_SETFONT, (WPARAM)font, 0);
		SendMessageW(sr, WM_SETFONT, (WPARAM)font, 0);
		SendMessageW(e, WM_SETFONT, (WPARAM)font, 0);
		set_number(dlg, IDC_FIELD_BASE + i, g[i]);
	}
}

static int read_fields(HWND dlg, globals_args *a)
{
	double g[NG], dur = 0;
	wchar_t msg[160];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		if (!get_number(dlg, IDC_FIELD_BASE + i, &g[i]) || g[i] < pi.min || g[i] > pi.max) {
			swprintf(msg, 160, L"%hs must be a number from %g to %g.", pi.name, pi.min, pi.max);
			return reject(dlg, IDC_FIELD_BASE + i, msg);
		}
	}
	if (a->duration_ms) {
		if (!get_number(dlg, IDC_DURATION, &dur) || dur < g[KG_FRAME_MS] || dur > 600000) {
			swprintf(msg, 160, L"The duration must be from %g ms (one frame) to 600000 ms.", g[KG_FRAME_MS]);
			return reject(dlg, IDC_DURATION, msg);
		}
		*a->duration_ms = dur;
	}
	memcpy(a->globals, g, sizeof g);
	return 1;
}

static INT_PTR CALLBACK globals_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	globals_args *a = (globals_args *)GetWindowLongPtrW(dlg, DWLP_USER);
	switch (msg) {
	case WM_INITDIALOG:
		a = (globals_args *)lp;
		SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)a);
		create_fields(dlg, a->globals, a->duration_ms != NULL);
		if (a->duration_ms) {
			SetWindowTextW(dlg, L"New document");
			set_number(dlg, IDC_DURATION, *a->duration_ms);
			SetDlgItemTextW(dlg, IDC_NOTE, L"The new document starts silent (AV, AF, AH and AVS at 0), with the "
			                               L"other parameters at their defaults.");
		} else {
			ShowWindow(GetDlgItem(dlg, IDC_DURATION_LBL), SW_HIDE);
			ShowWindow(GetDlgItem(dlg, IDC_DURATION), SW_HIDE);
			SetDlgItemTextW(dlg, IDC_NOTE, L"Changing FRAME_MS resamples the frames so that the duration stays "
			                               L"about the same.");
		}
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK && read_fields(dlg, a))
			EndDialog(dlg, 1);
		else if (LOWORD(wp) == IDCANCEL)
			EndDialog(dlg, 0);
		return TRUE;
	}
	return FALSE;
}

int dlg_globals(HWND owner, double *globals, double *duration_ms)
{
	globals_args a = { globals, duration_ms };
	return DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_GLOBALS), owner, globals_proc, (LPARAM)&a) == 1;
}

/* ---- number prompt ---- */

typedef struct {
	const wchar_t *title, *label;
	double *value, lo, hi;
} ask_args;

static INT_PTR CALLBACK ask_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	ask_args *a = (ask_args *)GetWindowLongPtrW(dlg, DWLP_USER);
	switch (msg) {
	case WM_INITDIALOG:
		a = (ask_args *)lp;
		SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)a);
		SetWindowTextW(dlg, a->title);
		SetDlgItemTextW(dlg, IDC_ASK_LABEL, a->label);
		set_number(dlg, IDC_ASK_VALUE, *a->value);
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK) {
			double v;
			wchar_t m[128];
			if (!get_number(dlg, IDC_ASK_VALUE, &v) || v < a->lo || v > a->hi) {
				swprintf(m, 128, L"Enter a number from %g to %g.", a->lo, a->hi);
				reject(dlg, IDC_ASK_VALUE, m);
				return TRUE;
			}
			*a->value = v;
			EndDialog(dlg, 1);
		} else if (LOWORD(wp) == IDCANCEL) {
			EndDialog(dlg, 0);
		}
		return TRUE;
	}
	return FALSE;
}

int dlg_asknum(HWND owner, const wchar_t *title, const wchar_t *label, double *value, double lo, double hi)
{
	ask_args a = { title, label, value, lo, hi };
	return DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_ASKNUM), owner, ask_proc, (LPARAM)&a) == 1;
}

/* ---- Parameter reference ---- */

static INT_PTR CALLBACK paramref_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	(void)lp;
	if (msg == WM_INITDIALOG) {
		HWND l = GetDlgItem(dlg, IDC_LIST);
		ListView_SetExtendedListViewStyle(l, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
		static const wchar_t *cols[] = { L"Name", L"Kind", L"Unit", L"Min", L"Max", L"Default", L"Description" };
		static const int widths[] = { 60, 50, 40, 50, 50, 55, 250 };
		for (int c = 0; c < 7; c++) {
			LVCOLUMNW col = { .mask = LVCF_TEXT | LVCF_WIDTH, .cx = S(widths[c]), .pszText = (LPWSTR)cols[c] };
			ListView_InsertColumn(l, c, &col);
		}
		int row = 0;
		for (int kind = KLATT_FRAME; kind <= KLATT_GLOBAL; kind++)
			for (int i = 0; i < KlattParamCount(kind); i++, row++) {
				klatt_param_info pi;
				KlattParamInfo(kind, i, &pi);
				wchar_t t[128];
				swprintf(t, 128, L"%hs", pi.name);
				LVITEMW it = { .mask = LVIF_TEXT, .iItem = row, .pszText = t };
				ListView_InsertItem(l, &it);
				ListView_SetItemText(l, row, 1, kind == KLATT_FRAME ? L"frame" : L"global");
				swprintf(t, 128, L"%hs", pi.unit);
				ListView_SetItemText(l, row, 2, t);
				swprintf(t, 128, L"%g", pi.min);
				ListView_SetItemText(l, row, 3, t);
				swprintf(t, 128, L"%g", pi.max);
				ListView_SetItemText(l, row, 4, t);
				swprintf(t, 128, L"%g", pi.def);
				ListView_SetItemText(l, row, 5, t);
				swprintf(t, 128, L"%hs", pi.description);
				ListView_SetItemText(l, row, 6, t);
			}
		return TRUE;
	}
	if (msg == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
		EndDialog(dlg, 0);
		return TRUE;
	}
	return FALSE;
}

void dlg_paramref(HWND owner)
{
	DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_PARAMREF), owner, paramref_proc, 0);
}

/* ---- Optimize ---- */

static int previewed;   /* an Optimize step from Preview is on the undo stack */
static int saved_dirty; /* app.doc.dirty before the first Preview of this dialog session */

static void clear_ghosts(void)
{
	for (int p = 0; p < NP; p++) {
		free(app.ghost[p]);
		app.ghost[p] = NULL;
	}
}

/* Undoes a previewed Optimize step, if any, and drops it from the redo stack so it cannot come back through
 * Redo. Returns 1 on success (or if there was nothing to undo), 0 on out of memory, in which case previewed
 * stays set and the caller must leave the preview applied. */
static int discard_preview(void)
{
	if (!previewed)
		return 1;
	if (doc_undo(&app.doc) < 0) {
		app_error("Out of memory.");
		return 0;
	}
	doc_redo_discard(&app.doc);
	previewed = 0;
	return 1;
}

/* The tolerance fields, in the order of the OPT_TOL_* classes (optimize.h). */
#define NTOL OPT_NTOL
static const int tol_ids[NTOL] = { IDC_TOL_HZ, IDC_TOL_F0, IDC_TOL_DB, IDC_TOL_RD, IDC_TOL_PCT };

static double tolerance_for(int p, const double *tol)
{
	return tol[opt_tol_class(p)];
}

#define SMOOTH_MAX_MS 500
static double smooth_ms[NP]; /* each track's smoothing width, remembered for the session */
static int smooth_set;       /* smooth_ms holds the defaults or later choices */
static int filling;          /* the dialog is setting the Smoothing field itself: not a user edit */

/* A track list row: the track's name and its smoothing width ("F2 · 15 ms"). */
static void set_row_text(HWND tracks, int p)
{
	wchar_t t[48];
	swprintf(t, 48, L"%hs \x00B7 %g ms", param_name(KLATT_FRAME, p), smooth_ms[p]);
	ListView_SetItemText(tracks, p, 0, t);
}

/* The Smoothing field shows the first selected track's width; it is disabled while no track is selected. */
static void show_smoothing(HWND dlg)
{
	int p = ListView_GetNextItem(GetDlgItem(dlg, IDC_TRACKS), -1, LVNI_SELECTED);
	filling = 1;
	if (p >= 0)
		set_number(dlg, IDC_SMOOTH, smooth_ms[p]);
	else
		SetDlgItemTextW(dlg, IDC_SMOOTH, L"");
	filling = 0;
	EnableWindow(GetDlgItem(dlg, IDC_SMOOTH), p >= 0);
}

/* A valid width typed into the Smoothing field applies to every selected track at once. */
static void smoothing_edited(HWND dlg)
{
	double w;
	if (filling || !get_number(dlg, IDC_SMOOTH, &w) || w < 0 || w > SMOOTH_MAX_MS)
		return;
	HWND tracks = GetDlgItem(dlg, IDC_TRACKS);
	for (int p = -1; (p = ListView_GetNextItem(tracks, p, LVNI_SELECTED)) >= 0;) {
		smooth_ms[p] = w;
		set_row_text(tracks, p);
	}
}

/* Undoes an earlier preview, then smooths and simplifies the checked tracks as one undo step. Returns 1, or 0 if
 * a field is invalid or discarding the earlier preview ran out of memory (the dialog stays open either way). */
static int run_optimize(HWND dlg)
{
	double tol[NTOL], w;
	for (int i = 0; i < NTOL; i++)
		if (!get_number(dlg, tol_ids[i], &tol[i]) || tol[i] < 0)
			return reject(dlg, tol_ids[i], L"Enter a tolerance of 0 or more.");
	if (IsWindowEnabled(GetDlgItem(dlg, IDC_SMOOTH)) &&
	    (!get_number(dlg, IDC_SMOOTH, &w) || w < 0 || w > SMOOTH_MAX_MS))
		return reject(dlg, IDC_SMOOTH, L"Enter a smoothing width from 0 (off) to 500 ms.");
	if (!discard_preview())
		return 0;
	clear_ghosts();
	HWND tracks = GetDlgItem(dlg, IDC_TRACKS), grid_box = GetDlgItem(dlg, IDC_GRID);
	int grid = (int)SendMessageW(grid_box, CB_GETITEMDATA, SendMessageW(grid_box, CB_GETCURSEL, 0, 0), 0);
	int first = 0, count = app.doc.nframes;
	if (IsDlgButtonChecked(dlg, IDC_RANGE_SEL) == BST_CHECKED && app.sel_a >= 0) {
		first = app.sel_a;
		count = app.sel_b - app.sel_a + 1;
	}
	double *v = malloc(sizeof(double) * count);
	wchar_t summary[4096] = L"";
	int len = 0;
	if (!v)
		return 0;
	doc_begin(&app.doc, "Optimize");
	for (int p = 0; p < NP; p++) {
		if (!ListView_GetCheckState(tracks, p))
			continue;
		app.ghost[p] = malloc(sizeof(double) * count);
		for (int i = 0; i < count; i++)
			v[i] = doc_get(&app.doc, first + i, p);
		if (app.ghost[p])
			memcpy(app.ghost[p], v, sizeof(double) * count);
		double err = 0, smoothed = smooth_ms[p];
		if (opt_smooth(v, count, doc_frame_ms(&app.doc), smoothed, opt_smooth_runs(p)) < 0)
			smoothed = 0; /* out of memory: simplified unsmoothed */
		int kept = opt_simplify(v, count, first, grid, tolerance_for(p, tol), NULL);
		for (int i = 0; i < count; i++) {
			err = fmax(err, fabs(v[i] - doc_get(&app.doc, first + i, p))); /* against the original values */
			doc_set(&app.doc, first + i, p, v[i]);
		}
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		wchar_t how[32] = L"";
		if (smoothed > 0)
			swprintf(how, 32, L"smoothed %g ms, ", smoothed);
		len += swprintf(summary + len, 4096 - len, L"%hs: %ls%d -> %d breakpoints, max error %.3g %hs\r\n", pi.name,
		                how, count, kept, err, pi.unit);
	}
	free(v);
	app.ghost_first = first;
	app.ghost_count = count;
	previewed = doc_end(&app.doc) == 1;
	SetDlgItemTextW(dlg, IDC_SUMMARY, len ? summary : L"No tracks are ticked.");
	app_changed(CHG_VALUES);
	return 1;
}

static INT_PTR CALLBACK optimize_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_INITDIALOG: {
		HWND tracks = GetDlgItem(dlg, IDC_TRACKS);
		ListView_SetExtendedListViewStyle(tracks, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
		RECT rc;
		GetClientRect(tracks, &rc);
		LVCOLUMNW col = { .mask = LVCF_WIDTH, .cx = rc.right - GetSystemMetrics(SM_CXVSCROLL) };
		ListView_InsertColumn(tracks, 0, &col);
		if (!smooth_set) {
			for (int p = 0; p < NP; p++)
				smooth_ms[p] = opt_smooth_default(p);
			smooth_set = 1;
		}
		for (int p = 0; p < NP; p++) {
			LVITEMW it = { .mask = LVIF_TEXT, .iItem = p, .pszText = L"" };
			ListView_InsertItem(tracks, &it);
			set_row_text(tracks, p);
			ListView_SetCheckState(tracks, p, app.visible[p]);
		}
		show_smoothing(dlg);
		int sel = app.sel_a >= 0;
		EnableWindow(GetDlgItem(dlg, IDC_RANGE_SEL), sel);
		CheckRadioButton(dlg, IDC_RANGE_SEL, IDC_RANGE_ALL, sel ? IDC_RANGE_SEL : IDC_RANGE_ALL);
		HWND grid = GetDlgItem(dlg, IDC_GRID);
		double fm = doc_frame_ms(&app.doc);
		static const struct { const wchar_t *name; double ms; } grids[] = {
			{ L"Every frame", 0 }, { L"5 ms", 5 }, { L"10 ms", 10 } };
		for (int i = 0; i < 3; i++) {
			double frames = grids[i].ms ? grids[i].ms / fm : 1;
			if (fabs(frames - round(frames)) > 1e-9 || frames < 1)
				continue; /* not a whole number of frames */
			int at = (int)SendMessageW(grid, CB_ADDSTRING, 0, (LPARAM)grids[i].name);
			SendMessageW(grid, CB_SETITEMDATA, at, (LPARAM)lround(frames));
		}
		SendMessageW(grid, CB_SETCURSEL, 0, 0);
		for (int i = 0; i < NTOL; i++)
			set_number(dlg, tol_ids[i], opt_tol_default[i]);
		previewed = 0;
		saved_dirty = app.doc.dirty;
		return TRUE;
	}
	case WM_NOTIFY: {
		NMLISTVIEW *n = (NMLISTVIEW *)lp;
		if (n->hdr.idFrom == IDC_TRACKS && n->hdr.code == LVN_ITEMCHANGED && (n->uChanged & LVIF_STATE) &&
		    ((n->uOldState ^ n->uNewState) & LVIS_SELECTED))
			show_smoothing(dlg);
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_SMOOTH:
			if (HIWORD(wp) == EN_CHANGE)
				smoothing_edited(dlg);
			return TRUE;
		case IDC_PREVIEW:
			run_optimize(dlg);
			return TRUE;
		case IDOK: /* re-run, in case a setting changed after Preview */
			if (run_optimize(dlg)) {
				clear_ghosts();
				app_changed(CHG_VALUES);
				EndDialog(dlg, 1);
			}
			return TRUE;
		case IDCANCEL:
			if (discard_preview()) {
				app.doc.dirty = saved_dirty;
				clear_ghosts();
				app_changed(CHG_VALUES);
				EndDialog(dlg, 0);
			}
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void dlg_optimize(HWND owner)
{
	DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_OPTIMIZE), owner, optimize_proc, 0);
}

/* ---- Import WAV ---- */

static const struct { const wchar_t *name; int q; } qualities[] = {
	{ L"Fast", IMPORT_FAST }, { L"Normal", IMPORT_NORMAL }, { L"Best", IMPORT_BEST } };

#define WAVE_CHANGED 1 /* the waveform strip's notification (WM_COMMAND): its range changed */

/* The waveform strip (class KpeWave) of the dialog: the whole recording as a min/max envelope per pixel column, the
 * selected samples a..b shaded. Dragging selects a range, a double-click all of it. One dialog at a time. */
static struct {
	const wav_data *wav;
	long a, b;          /* the selected samples */
	long press, a0, b0; /* while dragging: where it started, and the range before */
	int dragging;
	double *lo, *hi;    /* the envelope, for width columns */
	int width;
} wave;

static void wave_notify(HWND h)
{
	InvalidateRect(h, NULL, FALSE);
	SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), WAVE_CHANGED), (LPARAM)h);
}

static void wave_paint(HWND h)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(h, &ps);
	RECT rc;
	GetClientRect(h, &rc);
	if (wave.width != rc.right) {
		free(wave.lo);
		free(wave.hi);
		wave.lo = malloc(sizeof(double) * (rc.right > 0 ? rc.right : 1));
		wave.hi = malloc(sizeof(double) * (rc.right > 0 ? rc.right : 1));
		wave.width = wave.lo && wave.hi ? rc.right : 0;
		if (wave.width)
			range_envelope(wave.wav->x, wave.wav->n, wave.width, wave.lo, wave.hi);
	}
	FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
	RECT sel = { (LONG)floor(range_pixel_of(wave.wav->n, rc.right, wave.a)), 0,
		(LONG)ceil(range_pixel_of(wave.wav->n, rc.right, wave.b)), rc.bottom };
	HBRUSH shade = CreateSolidBrush(RGB(204, 228, 247));
	FillRect(dc, &sel, shade);
	DeleteObject(shade);
	HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT)), old = SelectObject(dc, pen);
	double mid = rc.bottom / 2.0;
	for (int c = 0; c < wave.width; c++) { /* each column from its least to its greatest sample */
		MoveToEx(dc, c, (int)lround(mid - wave.hi[c] * mid), NULL);
		LineTo(dc, c, (int)lround(mid - wave.lo[c] * mid) + 1);
	}
	SelectObject(dc, old);
	DeleteObject(pen);
	EndPaint(h, &ps);
}

static LRESULT CALLBACK wave_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	RECT rc;
	GetClientRect(h, &rc);
	long at = range_sample_at(wave.wav ? wave.wav->n : 0, rc.right, (short)LOWORD(lp));
	switch (msg) {
	case WM_PAINT:
		if (!wave.wav)
			break;
		wave_paint(h);
		return 0;
	case WM_LBUTTONDOWN:
		SetCapture(h);
		wave.dragging = 1;
		wave.press = at;
		wave.a0 = wave.a;
		wave.b0 = wave.b;
		return 0;
	case WM_MOUSEMOVE:
		if (wave.dragging) {
			range_order(wave.press, at, wave.wav->sr, wave.wav->n, &wave.a, &wave.b);
			wave_notify(h);
		}
		return 0;
	case WM_LBUTTONUP:
		if (wave.dragging) {
			ReleaseCapture();
			wave.dragging = 0;
			if (wave.a == wave.b) { /* a click, not a drag: the range stays */
				wave.a = wave.a0;
				wave.b = wave.b0;
			}
			wave_notify(h);
		}
		return 0;
	case WM_CAPTURECHANGED: /* lost mid-drag (Alt+Tab): the drag is over */
		wave.dragging = 0;
		return 0;
	case WM_LBUTTONDBLCLK:
		wave.a = 0;
		wave.b = wave.wav->n;
		wave_notify(h);
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

typedef struct {
	import_options *o;
	const wav_data *wav;
	long *first, *end;
} import_args;

static int import_filling; /* the dialog is writing the Start and End fields itself */

static void show_range(HWND dlg)
{
	wchar_t t[32];
	import_filling = 1;
	swprintf(t, 32, L"%.3f", (double)wave.a / wave.wav->sr);
	SetDlgItemTextW(dlg, IDC_RANGE_START, t);
	swprintf(t, 32, L"%.3f", (double)wave.b / wave.wav->sr);
	SetDlgItemTextW(dlg, IDC_RANGE_END, t);
	import_filling = 0;
}

/* Typed Start and End (seconds) move the strip's range when both are numbers and Start comes first. */
static void range_typed(HWND dlg)
{
	double s, e;
	if (import_filling || !get_number(dlg, IDC_RANGE_START, &s) || !get_number(dlg, IDC_RANGE_END, &e))
		return;
	long a = range_from_seconds(s, wave.wav->sr, wave.wav->n), b = range_from_seconds(e, wave.wav->sr, wave.wav->n);
	if (a < b) {
		wave.a = a;
		wave.b = b;
		InvalidateRect(GetDlgItem(dlg, IDC_WAVE), NULL, FALSE);
	}
}

/* Plays the selected range at the recording's own rate. */
static void play_range(void)
{
	long n = wave.b - wave.a;
	short *pcm = malloc(sizeof(short) * (n > 0 ? n : 1));
	if (!pcm)
		return;
	for (long i = 0; i < n; i++) {
		double v = floor(wave.wav->x[wave.a + i] * 32768 + 0.5);
		pcm[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
	if (n > 0 && audio_play(pcm, (int)n, wave.wav->sr, 0, (int)n) < 0)
		app_error("The sound device could not be opened.");
	free(pcm); /* audio_play keeps its own copy */
}

static INT_PTR CALLBACK import_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	static import_args *a;
	switch (msg) {
	case WM_INITDIALOG: {
		a = (import_args *)lp;
		HWND voice = GetDlgItem(dlg, IDC_VOICE);
		for (int v = 0; v < VOICE_COUNT; v++) {
			wchar_t t[64];
			swprintf(t, 64, L"%hs", import_voice(v)->name);
			SendMessageW(voice, CB_ADDSTRING, 0, (LPARAM)t);
		}
		SendMessageW(voice, CB_SETCURSEL, a->o->voice, 0);
		HWND quality = GetDlgItem(dlg, IDC_QUALITY);
		for (int i = 0; i < 3; i++) {
			int at = (int)SendMessageW(quality, CB_ADDSTRING, 0, (LPARAM)qualities[i].name);
			SendMessageW(quality, CB_SETITEMDATA, at, qualities[i].q);
			if (qualities[i].q == a->o->quality)
				SendMessageW(quality, CB_SETCURSEL, at, 0);
		}
		set_number(dlg, IDC_TARGET_SR, a->o->sr);
		CheckDlgButton(dlg, IDC_SIMPLIFY, a->o->simplify ? BST_CHECKED : BST_UNCHECKED);
		show_range(dlg);
		return TRUE;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_WAVE:
			if (HIWORD(wp) == WAVE_CHANGED)
				show_range(dlg);
			return TRUE;
		case IDC_RANGE_START:
		case IDC_RANGE_END:
			if (HIWORD(wp) == EN_CHANGE)
				range_typed(dlg);
			return TRUE;
		case IDC_RANGE_PLAY:
			play_range();
			return TRUE;
		case IDOK: {
			double sr, s, e;
			klatt_param_info pi;
			KlattParamInfo(KLATT_GLOBAL, KG_SR, &pi);
			wchar_t m[96];
			if (!get_number(dlg, IDC_RANGE_START, &s) || s < 0) {
				reject(dlg, IDC_RANGE_START, L"Enter the start of the range in seconds.");
				return TRUE;
			}
			if (!get_number(dlg, IDC_RANGE_END, &e) || e <= s) {
				reject(dlg, IDC_RANGE_END, L"Enter the end of the range in seconds, after its start.");
				return TRUE;
			}
			long first = range_from_seconds(s, a->wav->sr, a->wav->n), end = range_from_seconds(e, a->wav->sr, a->wav->n);
			if (!range_long_enough(first, end, a->wav->sr)) {
				swprintf(m, 96, L"The range must be at least %g ms long.", RANGE_MIN_MS);
				reject(dlg, IDC_RANGE_END, m);
				return TRUE;
			}
			if (!get_number(dlg, IDC_TARGET_SR, &sr) || sr < pi.min || sr > pi.max) {
				swprintf(m, 96, L"Enter a sample rate from %g to %g Hz.", pi.min, pi.max);
				reject(dlg, IDC_TARGET_SR, m);
				return TRUE;
			}
			a->o->voice = (int)SendMessageW(GetDlgItem(dlg, IDC_VOICE), CB_GETCURSEL, 0, 0);
			a->o->sr = (int)lround(sr);
			HWND quality = GetDlgItem(dlg, IDC_QUALITY);
			a->o->quality = (int)SendMessageW(quality, CB_GETITEMDATA, SendMessageW(quality, CB_GETCURSEL, 0, 0), 0);
			a->o->simplify = IsDlgButtonChecked(dlg, IDC_SIMPLIFY) == BST_CHECKED;
			*a->first = first;
			*a->end = end;
			audio_stop();
			EndDialog(dlg, 1);
			return TRUE;
		}
		case IDCANCEL:
			audio_stop();
			EndDialog(dlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

int dlg_import(HWND owner, import_options *o, const wav_data *wav, long *first, long *end)
{
	static int registered;
	if (!registered) {
		WNDCLASSEXW wc = { .cbSize = sizeof wc, .style = CS_DBLCLKS, .lpfnWndProc = wave_proc, .hInstance = app.inst,
			.hCursor = LoadCursor(NULL, IDC_IBEAM), .lpszClassName = L"KpeWave" };
		registered = RegisterClassExW(&wc) != 0;
	}
	import_args a = { o, wav, first, end };
	wave.wav = wav;
	wave.a = 0;
	wave.b = wav->n;
	wave.dragging = 0;
	int ok = (int)DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_IMPORT), owner, import_proc, (LPARAM)&a) == 1;
	free(wave.lo);
	free(wave.hi);
	memset(&wave, 0, sizeof wave);
	return ok;
}
