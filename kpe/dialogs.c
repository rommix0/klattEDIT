/* dialogs.c - File > New and Parameters > Globals (one dialog), Parameters > Optimize, Help > Parameter
 * reference, and a small number prompt. */
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

/* Undoes an earlier preview, then optimizes the checked tracks as one undo step. Returns 1, or 0 if a field
 * is invalid or discarding the earlier preview ran out of memory (the dialog stays open either way). */
static int run_optimize(HWND dlg)
{
	double tol[NTOL];
	for (int i = 0; i < NTOL; i++)
		if (!get_number(dlg, tol_ids[i], &tol[i]) || tol[i] < 0)
			return reject(dlg, tol_ids[i], L"Enter a tolerance of 0 or more.");
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
		double err;
		int kept = opt_simplify(v, count, first, grid, tolerance_for(p, tol), &err);
		for (int i = 0; i < count; i++)
			doc_set(&app.doc, first + i, p, v[i]);
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		len += swprintf(summary + len, 4096 - len, L"%hs: %d -> %d breakpoints, max error %.3g %hs\r\n", pi.name,
		                count, kept, err, pi.unit);
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
	(void)lp;
	switch (msg) {
	case WM_INITDIALOG: {
		HWND tracks = GetDlgItem(dlg, IDC_TRACKS);
		ListView_SetExtendedListViewStyle(tracks, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
		RECT rc;
		GetClientRect(tracks, &rc);
		LVCOLUMNW col = { .mask = LVCF_WIDTH, .cx = rc.right - GetSystemMetrics(SM_CXVSCROLL) };
		ListView_InsertColumn(tracks, 0, &col);
		for (int p = 0; p < NP; p++) {
			wchar_t t[16];
			swprintf(t, 16, L"%hs", param_name(KLATT_FRAME, p));
			LVITEMW it = { .mask = LVIF_TEXT, .iItem = p, .pszText = t };
			ListView_InsertItem(tracks, &it);
			ListView_SetCheckState(tracks, p, app.visible[p]);
		}
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
		static const double tol_default[NTOL] = { 20, 2, 1, 0.05, 2 };
		for (int i = 0; i < NTOL; i++)
			set_number(dlg, tol_ids[i], tol_default[i]);
		previewed = 0;
		saved_dirty = app.doc.dirty;
		return TRUE;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
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
