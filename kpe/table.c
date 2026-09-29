/* table.c - View > Frame table: every frame's values in a virtual list view, editable in place. */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>
#include <commctrl.h>
#include "app.h"

#define COL_FIRST_PARAM 2 /* columns: frame, time_ms, then the parameters */

static HWND list, editor;
static int edit_row, edit_col, last_col = COL_FIRST_PARAM + KP_F0, syncing;

static void end_edit(int commit)
{
	if (!editor)
		return;
	HWND e = editor;
	editor = NULL; /* before DestroyWindow, which sends WM_KILLFOCUS */
	if (commit) {
		wchar_t t[64], *end;
		GetWindowTextW(e, t, 64);
		double v = wcstod(t, &end);
		while (*end == L' ')
			end++;
		if (end != t && !*end) {
			char label[40];
			snprintf(label, sizeof label, "Edit %s", param_name(KLATT_FRAME, edit_col - COL_FIRST_PARAM));
			doc_begin(&app.doc, label);
			doc_set(&app.doc, edit_row, edit_col - COL_FIRST_PARAM, v);
			doc_end(&app.doc);
			app_changed(CHG_VALUES);
		} else {
			MessageBeep(MB_ICONWARNING);
		}
	}
	DestroyWindow(e);
	SetFocus(list);
}

static LRESULT CALLBACK editor_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
	(void)id;
	(void)ref;
	if (msg == WM_GETDLGCODE)
		return DLGC_WANTALLKEYS;
	if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
		end_edit(wp == VK_RETURN);
		return 0;
	}
	if (msg == WM_KILLFOCUS) {
		end_edit(1);
		return 0;
	}
	return DefSubclassProc(h, msg, wp, lp);
}

static void begin_edit(int row, int col)
{
	if (row < 0 || row >= app.doc.nframes || col < COL_FIRST_PARAM || col >= COL_FIRST_PARAM + NP)
		return;
	end_edit(1);
	RECT r;
	ListView_EnsureVisible(list, row, FALSE);
	ListView_GetSubItemRect(list, row, col, LVIR_BOUNDS, &r);
	wchar_t t[64];
	swprintf(t, 64, L"%g", doc_get(&app.doc, row, col - COL_FIRST_PARAM));
	editor = CreateWindowExW(0, L"EDIT", t, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT,
	                         r.left, r.top, r.right - r.left, r.bottom - r.top, list, NULL, app.inst, NULL);
	SendMessageW(editor, WM_SETFONT, (WPARAM)app.font, 0);
	SetWindowSubclass(editor, editor_proc, 1, 0);
	SendMessageW(editor, EM_SETSEL, 0, -1);
	SetFocus(editor);
	edit_row = row;
	edit_col = col;
}

static void add_column(int i, const wchar_t *name, int width)
{
	LVCOLUMNW c = { .mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT, .fmt = LVCFMT_RIGHT, .cx = width, .pszText = (LPWSTR)name,
	                .iSubItem = i };
	ListView_InsertColumn(list, i, &c);
}

static LRESULT notify(NMHDR *nh)
{
	if (nh->code == LVN_GETDISPINFOW) {
		LVITEMW *it = &((NMLVDISPINFOW *)nh)->item;
		if (!(it->mask & LVIF_TEXT) || it->iItem >= app.doc.nframes)
			return 0;
		if (it->iSubItem == 0)
			swprintf(it->pszText, it->cchTextMax, L"%d", it->iItem);
		else if (it->iSubItem == 1)
			swprintf(it->pszText, it->cchTextMax, L"%g", it->iItem * doc_frame_ms(&app.doc));
		else
			swprintf(it->pszText, it->cchTextMax, L"%g", doc_get(&app.doc, it->iItem, it->iSubItem - COL_FIRST_PARAM));
		return 0;
	}
	if (nh->code == LVN_ITEMCHANGED) {
		NMLISTVIEW *lv = (NMLISTVIEW *)nh;
		if (!syncing && lv->iItem >= 0 && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
			app_set_cursor(lv->iItem, 1);
		return 0;
	}
	if (nh->code == NM_CLICK || nh->code == NM_DBLCLK) {
		NMITEMACTIVATE *ia = (NMITEMACTIVATE *)nh;
		LVHITTESTINFO hi = { .pt = ia->ptAction };
		if (ListView_SubItemHitTest(list, &hi) >= 0 && hi.iSubItem >= COL_FIRST_PARAM) {
			last_col = hi.iSubItem;
			if (nh->code == NM_DBLCLK)
				begin_edit(hi.iItem, hi.iSubItem);
		}
		return 0;
	}
	if (nh->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)nh)->wVKey == VK_F2) {
		begin_edit(ListView_GetNextItem(list, -1, LVNI_SELECTED), last_col);
		return 0;
	}
	return 0;
}

static LRESULT CALLBACK table_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_SIZE:
		MoveWindow(list, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
		return 0;
	case WM_NOTIFY:
		return notify((NMHDR *)lp);
	case WM_CLOSE:
		end_edit(1);
		ShowWindow(h, SW_HIDE);
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

void table_show(void)
{
	if (!app.table) {
		WNDCLASSEXW wc = { .cbSize = sizeof wc };
		wc.hInstance = app.inst;
		wc.lpfnWndProc = table_proc;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = L"KpeTable";
		RegisterClassExW(&wc);
		app.table = CreateWindowExW(WS_EX_TOOLWINDOW, L"KpeTable", L"Frame table", WS_OVERLAPPEDWINDOW,
		                            CW_USEDEFAULT, CW_USEDEFAULT, S(900), S(420), app.main, NULL, app.inst, NULL);
		list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA |
		                       LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, app.table, NULL, app.inst, NULL);
		SendMessageW(list, WM_SETFONT, (WPARAM)app.font, 0);
		ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
		add_column(0, L"Frame", S(50));
		add_column(1, L"time_ms", S(60));
		for (int p = 0; p < NP; p++) {
			wchar_t t[16];
			swprintf(t, 16, L"%hs", param_name(KLATT_FRAME, p));
			add_column(COL_FIRST_PARAM + p, t, S(48));
		}
		RECT rc;
		GetClientRect(app.table, &rc);
		MoveWindow(list, 0, 0, rc.right, rc.bottom, TRUE);
		table_refresh();
		table_select(app.cursor);
	}
	ShowWindow(app.table, SW_SHOW);
	SetForegroundWindow(app.table);
}

void table_refresh(void)
{
	if (!list)
		return;
	end_edit(0);
	ListView_SetItemCountEx(list, app.doc.nframes, LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	InvalidateRect(list, NULL, FALSE);
}

void table_select(int frame)
{
	if (!list || frame < 0 || frame >= app.doc.nframes)
		return;
	syncing = 1;
	ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_SetItemState(list, frame, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_EnsureVisible(list, frame, FALSE);
	syncing = 0;
}
