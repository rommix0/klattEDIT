/* strips.c - the plot window: stacked parameter strips, the spectrogram strip with formant tracks, the shared
 * time axis, and painting, line drawing and selection with the mouse. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>
#include "app.h"
#include "view.h"

enum { DRAG_NONE, DRAG_PAINT, DRAG_LINE, DRAG_SELECT };

static struct {
	int mode, param;
	double press_t, press_v, prev_t, prev_v, cur_t, cur_v;
	int press_x, press_frame;
} drag;

static struct {
	BITMAPINFO *bmi;       /* 8-bit greyscale DIB of the spectrogram up to app.spec_top_hz */
	unsigned char *bits;
	int w, h;
} dib;

static int ML(void) { return S(70); }  /* left margin: value labels and axis titles */
static int MR(void) { return S(40); }  /* right margin: formant labels */
static int MT(void) { return S(6); }
static int MB(void) { return S(44); }  /* time axis and selection ruler */

static int plot_width(void)
{
	RECT rc;
	GetClientRect(app.plot, &rc);
	int w = rc.right - ML() - MR();
	return w > 1 ? w : 1;
}

static time_map tmap(void)
{
	time_map m = { app.t0_ms, app.ms_per_px, ML() };
	return m;
}

static double frame_ms(void)
{
	return doc_frame_ms(&app.doc);
}

static int frame_at(double x)
{
	time_map m = tmap();
	int k = (int)lround(tm_t(&m, x) / frame_ms());
	return k < 0 ? 0 : k >= app.doc.nframes ? app.doc.nframes - 1 : k;
}

static int layout(strip_rect *s)
{
	RECT rc;
	GetClientRect(app.plot, &rc);
	return view_layout(app.visible, MT(), rc.bottom - MT() - MB(), S(64), S(8), s);
}

static COLORREF track_color(int p)
{
	switch (p) {
	case KP_F0: case KP_AV: return RGB(40, 110, 200);
	case KP_AF: return RGB(20, 140, 100);
	case KP_AH: return RGB(120, 100, 210);
	case KP_AVS: return RGB(160, 80, 160);
	case KP_F1: case KP_F2: case KP_F3: case KP_F4: return RGB(216, 90, 48);
	default: return RGB(90, 90, 90);
	}
}

/* ---- scrolling and zoom ---- */

void plot_sync_scroll(void)
{
	double dur = doc_duration_ms(&app.doc), page = plot_width() * app.ms_per_px;
	if (app.t0_ms > dur - page)
		app.t0_ms = dur - page;
	if (app.t0_ms < 0)
		app.t0_ms = 0;
	SCROLLINFO si = { .cbSize = sizeof si, .fMask = SIF_ALL | SIF_DISABLENOSCROLL, .nMax = (int)ceil(dur),
	                   .nPage = (UINT)page, .nPos = (int)app.t0_ms };
	SetScrollInfo(app.plot, SB_HORZ, &si, TRUE);
}

void plot_fit(void)
{
	app.t0_ms = 0;
	app.ms_per_px = doc_duration_ms(&app.doc) / plot_width();
	plot_sync_scroll();
	InvalidateRect(app.plot, NULL, FALSE);
}

/* Zooms by factor (< 1 zooms in) keeping the time at pixel x in place. */
static void zoom_at(double factor, double x)
{
	time_map m = tmap();
	double t = tm_t(&m, x), mpp = app.ms_per_px * factor;
	double lo = frame_ms() / S(40), hi = doc_duration_ms(&app.doc) / plot_width(); /* a frame is 40 px at most */
	if (mpp < lo)
		mpp = lo;
	if (mpp > hi)
		mpp = hi;
	app.t0_ms = t - (x - ML()) * mpp;
	app.ms_per_px = mpp;
	plot_sync_scroll();
	InvalidateRect(app.plot, NULL, FALSE);
}

void plot_zoom(double factor)
{
	zoom_at(factor, ML() + plot_width() / 2.0);
}

/* ---- spectrogram bitmap ---- */

void plot_new_spectrogram(void)
{
	free(dib.bmi);
	free(dib.bits);
	memset(&dib, 0, sizeof dib);
	if (!app.levels || app.spec.ncols <= 0)
		return;
	int rows = (int)ceil(app.spec_top_hz / app.spec.bin_hz) + 1;
	if (rows > app.spec.nbins)
		rows = app.spec.nbins;
	int stride = (app.spec.ncols + 3) & ~3;
	dib.bmi = calloc(1, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
	dib.bits = malloc((size_t)stride * rows);
	if (!dib.bmi || !dib.bits) {
		free(dib.bmi);
		free(dib.bits);
		memset(&dib, 0, sizeof dib);
		return;
	}
	BITMAPINFOHEADER *bh = &dib.bmi->bmiHeader;
	bh->biSize = sizeof *bh;
	bh->biWidth = app.spec.ncols;
	bh->biHeight = rows; /* bottom-up: row 0 is 0 Hz */
	bh->biPlanes = 1;
	bh->biBitCount = 8;
	bh->biCompression = BI_RGB;
	bh->biClrUsed = 256;
	for (int i = 0; i < 256; i++) { /* darkness i: 0 = white, 255 = black */
		BYTE v = (BYTE)(255 - i);
		dib.bmi->bmiColors[i] = (RGBQUAD){ v, v, v, 0 };
	}
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < app.spec.ncols; c++)
			dib.bits[(size_t)r * stride + c] = app.levels[(size_t)c * app.spec.nbins + r];
	dib.w = app.spec.ncols;
	dib.h = rows;
}

/* ---- painting ---- */

static void text_at(HDC dc, int x, int y, UINT align, const wchar_t *t)
{
	SetTextAlign(dc, align);
	TextOutW(dc, x, y, t, (int)wcslen(t));
}

static void line(HDC dc, int x0, int y0, int x1, int y1)
{
	MoveToEx(dc, x0, y0, NULL);
	LineTo(dc, x1, y1);
}

static void draw_track(HDC dc, const strip_rect *s, double lo, double hi, const double *vals, int first, int count,
                       int stride)
{
	time_map m = tmap();
	double fm = frame_ms();
	int k0 = (int)floor(app.t0_ms / fm) - 1, k1 = (int)ceil((app.t0_ms + plot_width() * app.ms_per_px) / fm) + 1;
	if (k0 < first)
		k0 = first;
	if (k1 > first + count - 1)
		k1 = first + count - 1;
	if (k1 < k0)
		return;
	POINT *pts = malloc(sizeof(POINT) * (k1 - k0 + 1));
	if (!pts)
		return;
	for (int k = k0; k <= k1; k++) {
		pts[k - k0].x = (LONG)lround(tm_x(&m, k * fm));
		pts[k - k0].y = (LONG)lround(vm_y(vals[(size_t)(k - first) * stride], lo, hi, s->top, s->height));
	}
	Polyline(dc, pts, k1 - k0 + 1);
	free(pts);
}

static void draw_strip(HDC dc, const strip_rect *s, HFONT vfont)
{
	int x0 = ML(), x1 = ML() + plot_width();
	double lo, hi;
	view_range(s->param, app.spec_top_hz, &lo, &hi);
	HRGN clip = CreateRectRgn(x0, s->top, x1 + 1, s->top + s->height);

	if (s->param == FORMANT_STRIP && dib.bits) {
		double step = app.spec.step_ms, dur = doc_duration_ms(&app.doc);
		double t_end = fmin(app.t0_ms + plot_width() * app.ms_per_px, dur);
		int src_x = (int)floor(app.t0_ms / step), src_w = (int)ceil((t_end - app.t0_ms) / step);
		if (src_x + src_w > dib.w)
			src_w = dib.w - src_x;
		double top_row_hz = (dib.h - 1) * app.spec.bin_hz; /* the DIB may end just above spec_top_hz */
		int dest_h = (int)lround(s->height * top_row_hz / app.spec_top_hz);
		SelectClipRgn(dc, clip);
		SetStretchBltMode(dc, HALFTONE);
		if (src_w > 0)
			StretchDIBits(dc, x0, s->top + s->height - dest_h, (int)lround((src_w * step) / app.ms_per_px), dest_h,
			              src_x, 0, src_w, dib.h, dib.bits, dib.bmi, DIB_RGB_COLORS, SRCCOPY);
		SelectClipRgn(dc, NULL);
	}

	/* value grid and labels */
	HPEN grid = CreatePen(PS_SOLID, 1, RGB(222, 222, 222));
	HPEN old = SelectObject(dc, grid);
	double step = view_nice_step(hi - lo, s->height, S(22));
	wchar_t t[64];
	SetTextColor(dc, RGB(90, 90, 90));
	for (double v = ceil(lo / step) * step; v <= hi + 1e-9; v += step) {
		int y = (int)lround(vm_y(v, lo, hi, s->top, s->height));
		if (s->param != FORMANT_STRIP)
			line(dc, x0, y, x1, y);
		line(dc, x0 - S(4), y, x0, y);
		swprintf(t, 64, L"%g", v);
		text_at(dc, x0 - S(6), y - S(7), TA_RIGHT | TA_TOP, t);
	}
	SelectObject(dc, old);
	DeleteObject(grid);

	/* axis title, rotated */
	klatt_param_info pi = { "Frequency", "Hz", "", 0, 0, 0 };
	if (s->param != FORMANT_STRIP)
		KlattParamInfo(KLATT_FRAME, s->param, &pi);
	swprintf(t, 64, L"%hs (%hs)", pi.name, pi.unit);
	HFONT of = SelectObject(dc, vfont);
	SetTextColor(dc, RGB(60, 60, 60));
	text_at(dc, S(4), s->top + s->height / 2, TA_CENTER | TA_TOP, t);
	SelectObject(dc, of);

	/* tracks */
	SelectClipRgn(dc, clip);
	if (s->param == FORMANT_STRIP) {
		for (int p = KP_F1; p <= KP_F4; p++) {
			if (!app.visible[p])
				continue;
			if (app.ghost[p]) {
				HPEN gp = CreatePen(PS_SOLID, 1, RGB(170, 170, 170));
				old = SelectObject(dc, gp);
				draw_track(dc, s, lo, hi, app.ghost[p], app.ghost_first, app.ghost_count, 1);
				SelectObject(dc, old);
				DeleteObject(gp);
			}
			HPEN pen = CreatePen(PS_SOLID, S(2), track_color(p));
			old = SelectObject(dc, pen);
			draw_track(dc, s, lo, hi, app.doc.frames + p, 0, app.doc.nframes, NP);
			SelectObject(dc, old);
			DeleteObject(pen);
		}
		/* F5 and F6 are globals: dashed constant lines */
		HPEN dash = CreatePen(PS_DOT, 1, RGB(216, 90, 48));
		old = SelectObject(dc, dash);
		SetBkMode(dc, TRANSPARENT);
		for (int i = 0; i < 2; i++) {
			double f = app.doc.globals[i ? KG_F6 : KG_F5];
			if (f <= hi) {
				int y = (int)lround(vm_y(f, lo, hi, s->top, s->height));
				line(dc, x0, y, x1, y);
			}
		}
		SelectObject(dc, old);
		DeleteObject(dash);
	} else {
		if (app.ghost[s->param]) {
			HPEN gp = CreatePen(PS_SOLID, 1, RGB(170, 170, 170));
			old = SelectObject(dc, gp);
			draw_track(dc, s, lo, hi, app.ghost[s->param], app.ghost_first, app.ghost_count, 1);
			SelectObject(dc, old);
			DeleteObject(gp);
		}
		HPEN pen = CreatePen(PS_SOLID, S(2), track_color(s->param));
		old = SelectObject(dc, pen);
		draw_track(dc, s, lo, hi, app.doc.frames + s->param, 0, app.doc.nframes, NP);
		SelectObject(dc, old);
		DeleteObject(pen);
	}
	SelectClipRgn(dc, NULL);
	DeleteObject(clip);

	/* formant labels in the right margin */
	if (s->param == FORMANT_STRIP) {
		int k = frame_at(x1);
		for (int i = 0; i < 6; i++) { /* F1-F4 from the frame at the right edge, F5 and F6 from the globals */
			double f = i < 4 ? doc_get(&app.doc, k, KP_F1 + i) : app.doc.globals[i == 4 ? KG_F5 : KG_F6];
			const char *name = i < 4 ? param_name(KLATT_FRAME, KP_F1 + i) : i == 4 ? "F5" : "F6";
			if ((i < 4 && !app.visible[KP_F1 + i]) || f > hi)
				continue;
			SetTextColor(dc, track_color(KP_F1));
			swprintf(t, 64, L"%hs", name);
			text_at(dc, x1 + S(4), (int)lround(vm_y(f, lo, hi, s->top, s->height)) - S(7), TA_LEFT | TA_TOP, t);
		}
	}

	/* frame */
	HBRUSH border = CreateSolidBrush(RGB(150, 150, 150));
	RECT r = { x0, s->top, x1 + 1, s->top + s->height };
	FrameRect(dc, &r, border);
	DeleteObject(border);
}

static void paint(HDC dc, RECT rc)
{
	FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
	HFONT of = SelectObject(dc, app.font);
	SetBkMode(dc, TRANSPARENT);
	LOGFONTW lf;
	GetObjectW(app.font, sizeof lf, &lf);
	lf.lfEscapement = lf.lfOrientation = 900;
	HFONT vfont = CreateFontIndirectW(&lf);

	time_map m = tmap();
	int x0 = ML(), x1 = ML() + plot_width(), ybot = rc.bottom - MB();
	double fm = frame_ms();

	/* selection, behind everything */
	if (app.sel_a >= 0) {
		RECT sr = { (LONG)tm_x(&m, app.sel_a * fm), MT(), (LONG)tm_x(&m, (app.sel_b + 1) * fm), ybot };
		if (sr.left < x0) sr.left = x0;
		if (sr.right > x1) sr.right = x1;
		HBRUSH b = CreateSolidBrush(RGB(214, 230, 250));
		FillRect(dc, &sr, b);
		DeleteObject(b);
	}

	/* vertical time grid */
	double visible_ms = plot_width() * app.ms_per_px, tstep = view_nice_step(visible_ms, plot_width(), S(70));
	strip_rect strips[NP];
	int n = layout(strips);
	HPEN grid = CreatePen(PS_SOLID, 1, RGB(228, 228, 228));
	HPEN old = SelectObject(dc, grid);
	for (double t = ceil(app.t0_ms / tstep) * tstep; t <= app.t0_ms + visible_ms; t += tstep) {
		int x = (int)lround(tm_x(&m, t));
		for (int i = 0; i < n; i++)
			if (strips[i].param != FORMANT_STRIP)
				line(dc, x, strips[i].top, x, strips[i].top + strips[i].height);
	}
	SelectObject(dc, old);
	DeleteObject(grid);

	for (int i = 0; i < n; i++)
		draw_strip(dc, &strips[i], vfont);

	/* time axis */
	RECT ruler = { x0, ybot + S(2), x1 + 1, ybot + S(22) };
	FillRect(dc, &ruler, GetSysColorBrush(COLOR_BTNFACE));
	SetTextColor(dc, RGB(90, 90, 90));
	wchar_t t[32];
	for (double tt = ceil(app.t0_ms / tstep) * tstep; tt <= app.t0_ms + visible_ms + 1e-9; tt += tstep) {
		int x = (int)lround(tm_x(&m, tt));
		line(dc, x, ybot + S(2), x, ybot + S(7));
		swprintf(t, 32, L"%g", tt);
		text_at(dc, x, ybot + S(7), TA_CENTER | TA_TOP, t);
	}
	text_at(dc, (x0 + x1) / 2, ybot + S(24), TA_CENTER | TA_TOP, L"Time (ms)");

	/* cursor, playhead, line-tool preview */
	if (app.cursor >= 0) {
		HPEN cp = CreatePen(PS_DOT, 1, RGB(24, 95, 165));
		old = SelectObject(dc, cp);
		int x = (int)lround(tm_x(&m, app.cursor * fm));
		if (x >= x0 && x <= x1)
			line(dc, x, MT(), x, ybot);
		SelectObject(dc, old);
		DeleteObject(cp);
	}
	if (app.play_ms >= 0) {
		HPEN pp = CreatePen(PS_SOLID, S(2), RGB(200, 40, 40));
		old = SelectObject(dc, pp);
		int x = (int)lround(tm_x(&m, app.play_ms));
		if (x >= x0 && x <= x1)
			line(dc, x, MT(), x, ybot);
		SelectObject(dc, old);
		DeleteObject(pp);
	}
	if (drag.mode == DRAG_LINE) {
		for (int i = 0; i < n; i++) {
			int sp = strips[i].param;
			int match = sp == drag.param || (sp == FORMANT_STRIP && drag.param >= KP_F1 && drag.param <= KP_F4);
			if (!match)
				continue;
			double lo, hi;
			view_range(sp, app.spec_top_hz, &lo, &hi);
			HPEN lp = CreatePen(PS_DASH, 1, track_color(drag.param));
			old = SelectObject(dc, lp);
			line(dc, (int)tm_x(&m, drag.press_t), (int)vm_y(drag.press_v, lo, hi, strips[i].top, strips[i].height),
			     (int)tm_x(&m, drag.cur_t), (int)vm_y(drag.cur_v, lo, hi, strips[i].top, strips[i].height));
			SelectObject(dc, old);
			DeleteObject(lp);
		}
	}
	SelectObject(dc, of);
	DeleteObject(vfont);
}

/* ---- mouse ---- */

static const strip_rect *strip_at(int y, strip_rect *strips, int n)
{
	for (int i = 0; i < n; i++)
		if (y >= strips[i].top && y < strips[i].top + strips[i].height)
			return &strips[i];
	return NULL;
}

/* Time and value of a point in a strip; values outside the strip are clamped to its range. */
static void point_value(const strip_rect *s, int x, int y, double *t, double *v)
{
	time_map m = tmap();
	double lo, hi;
	view_range(s->param, app.spec_top_hz, &lo, &hi);
	*t = tm_t(&m, x);
	*v = vm_v(y, lo, hi, s->top, s->height);
}

static void hover(int x, int y)
{
	strip_rect strips[NP];
	int n = layout(strips);
	RECT rc;
	GetClientRect(app.plot, &rc);
	if (x < ML() || x > ML() + plot_width()) {
		app_status(L"");
		return;
	}
	int k = frame_at(x);
	wchar_t t[256];
	int len = swprintf(t, 256, L"t = %g ms \x00B7 frame %d", k * frame_ms(), k);
	const strip_rect *s = strip_at(y, strips, n);
	if (s) {
		double lo, hi;
		view_range(s->param, app.spec_top_hz, &lo, &hi);
		if (s->param == FORMANT_STRIP) {
			for (int p = KP_F1; p <= KP_F4; p++)
				if (app.visible[p])
					len += swprintf(t + len, 256 - len, L"  %hs %g", param_name(KLATT_FRAME, p), doc_get(&app.doc, k, p));
			len += swprintf(t + len, 256 - len, L" \x00B7 pointer %g Hz", round(vm_v(y, lo, hi, s->top, s->height)));
		} else {
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, s->param, &pi);
			len += swprintf(t + len, 256 - len, L" \x00B7 %hs %g %hs \x00B7 pointer %g", pi.name,
			                doc_get(&app.doc, k, s->param), pi.unit, round(vm_v(y, lo, hi, s->top, s->height)));
		}
	}
	app_status(t);
}

static void mouse_down(HWND h, int x, int y, WPARAM keys)
{
	SetFocus(h);
	RECT rc;
	GetClientRect(h, &rc);
	strip_rect strips[NP];
	int n = layout(strips);
	if (x < ML() || x > ML() + plot_width())
		return;
	if (y >= rc.bottom - MB()) {
		drag.mode = DRAG_SELECT;
		drag.press_x = x;
		drag.press_frame = frame_at(x);
		SetCapture(h);
		return;
	}
	const strip_rect *s = strip_at(y, strips, n);
	if (!s)
		return;
	int p = s->param;
	double t, v;
	point_value(s, x, y, &t, &v);
	if (p == FORMANT_STRIP) {
		p = view_nearest_formant(&app.doc, app.visible, frame_at(x), v);
		if (p < 0)
			return;
	}
	drag.param = p;
	drag.press_t = drag.prev_t = drag.cur_t = t;
	drag.press_v = drag.prev_v = drag.cur_v = v;
	SetCapture(h);
	if (keys & MK_SHIFT) {
		drag.mode = DRAG_LINE;
	} else {
		char label[40];
		snprintf(label, sizeof label, "Paint %s", param_name(KLATT_FRAME, p));
		drag.mode = DRAG_PAINT;
		doc_begin(&app.doc, label);
		doc_paint(&app.doc, p, t, v, t, v);
	}
	InvalidateRect(h, NULL, FALSE);
}

static void mouse_move(HWND h, int x, int y)
{
	hover(x, y);
	if (drag.mode == DRAG_NONE)
		return;
	if (drag.mode == DRAG_SELECT) {
		int k = frame_at(x);
		if (abs(x - drag.press_x) >= S(3)) {
			app.sel_a = k < drag.press_frame ? k : drag.press_frame;
			app.sel_b = k < drag.press_frame ? drag.press_frame : k;
		}
		InvalidateRect(h, NULL, FALSE);
		return;
	}
	strip_rect strips[NP];
	int n = layout(strips);
	for (int i = 0; i < n; i++) {
		int sp = strips[i].param;
		if (sp == drag.param || (sp == FORMANT_STRIP && drag.param >= KP_F1 && drag.param <= KP_F4)) {
			point_value(&strips[i], x, y, &drag.cur_t, &drag.cur_v);
			break;
		}
	}
	if (drag.mode == DRAG_PAINT) {
		doc_paint(&app.doc, drag.param, drag.prev_t, drag.prev_v, drag.cur_t, drag.cur_v);
		drag.prev_t = drag.cur_t;
		drag.prev_v = drag.cur_v;
	}
	InvalidateRect(h, NULL, FALSE);
}

static void finish_drag(int x)
{
	int mode = drag.mode;
	drag.mode = DRAG_NONE;
	if (mode == DRAG_PAINT) {
		doc_end(&app.doc);
		app_changed(CHG_VALUES);
	} else if (mode == DRAG_LINE) {
		char label[40];
		snprintf(label, sizeof label, "Line %s", param_name(KLATT_FRAME, drag.param));
		doc_begin(&app.doc, label);
		doc_paint(&app.doc, drag.param, drag.press_t, drag.press_v, drag.cur_t, drag.cur_v);
		doc_end(&app.doc);
		app_changed(CHG_VALUES);
	} else if (mode == DRAG_SELECT) {
		if (abs(x - drag.press_x) < S(3)) {
			app.sel_a = -1;
			app_set_cursor(drag.press_frame, 0);
		} else {
			app_set_cursor(app.sel_a, 0);
		}
	}
}

static LRESULT CALLBACK plot_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT rc;
		GetClientRect(h, &rc);
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
		HBITMAP ob = SelectObject(mem, bmp);
		paint(mem, rc);
		BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
		SelectObject(mem, ob);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		plot_sync_scroll();
		InvalidateRect(h, NULL, FALSE);
		return 0;
	case WM_LBUTTONDOWN:
		mouse_down(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
		return 0;
	case WM_MOUSEMOVE:
		mouse_move(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		return 0;
	case WM_LBUTTONUP:
		if (drag.mode != DRAG_NONE) {
			finish_drag(GET_X_LPARAM(lp)); /* before ReleaseCapture, whose WM_CAPTURECHANGED then does nothing */
			ReleaseCapture();
		}
		return 0;
	case WM_CAPTURECHANGED:
		if (drag.mode == DRAG_PAINT || drag.mode == DRAG_LINE) /* capture lost mid-drag: keep what was painted */
			finish_drag(drag.press_x);
		return 0;
	case WM_MOUSEWHEEL: {
		POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ScreenToClient(h, &pt);
		int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
		if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
			zoom_at(pow(0.8, notches), pt.x);
		} else {
			app.t0_ms -= notches * plot_width() * app.ms_per_px * 0.1;
			plot_sync_scroll();
			InvalidateRect(h, NULL, FALSE);
		}
		return 0;
	}
	case WM_HSCROLL: {
		SCROLLINFO si = { .cbSize = sizeof si, .fMask = SIF_ALL };
		GetScrollInfo(h, SB_HORZ, &si);
		double page = plot_width() * app.ms_per_px;
		switch (LOWORD(wp)) {
		case SB_LINELEFT: app.t0_ms -= page * 0.1; break;
		case SB_LINERIGHT: app.t0_ms += page * 0.1; break;
		case SB_PAGELEFT: app.t0_ms -= page; break;
		case SB_PAGERIGHT: app.t0_ms += page; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: app.t0_ms = si.nTrackPos; break;
		}
		plot_sync_scroll();
		InvalidateRect(h, NULL, FALSE);
		return 0;
	}
	}
	return DefWindowProcW(h, msg, wp, lp);
}

ATOM plot_register(HINSTANCE inst)
{
	WNDCLASSEXW wc = { .cbSize = sizeof wc };
	wc.hInstance = inst;
	wc.lpfnWndProc = plot_proc;
	wc.hCursor = LoadCursor(NULL, IDC_CROSS);
	wc.lpszClassName = L"KpePlot";
	return RegisterClassExW(&wc);
}
