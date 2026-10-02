/* view.c - see view.h. */
#include <math.h>
#include "view.h"

void view_setup_default(strip_setup *su)
{
	static const int order[] = { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, KP_RD, KP_DI, FORMANT_STRIP, KP_B1, KP_B2,
		KP_B3, KP_FNZ, KP_AN, KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB };
	su->count = (int)(sizeof order / sizeof order[0]);
	for (int i = 0; i < su->count; i++)
		su->order[i] = order[i];
	for (int p = 0; p < NP; p++)
		su->height[p] = VIEW_STRIP_H;
}

static int scaled(int px, double scale)
{
	return (int)lround(px * scale);
}

static int min_height(int param, double scale)
{
	return scaled(param == FORMANT_STRIP ? VIEW_FORMANT_MIN : VIEW_MIN_H, scale);
}

int view_layout(const strip_setup *su, const int *visible, int top, int height, double scale, int gap,
                strip_rect *out)
{
	int n = 0, want = 0;
	for (int i = 0; i < su->count; i++) {
		int p = su->order[i];
		if (p != FORMANT_STRIP && !visible[p])
			continue;
		out[n] = (strip_rect){ p, 0, p == FORMANT_STRIP ? 0 : scaled(su->height[p], scale) };
		want += out[n++].height;
	}
	int room = height - (n - 1) * gap - min_height(FORMANT_STRIP, scale); /* for the parameter strips */
	int used = 0;
	for (int i = 0; i < n; i++) {
		if (out[i].param == FORMANT_STRIP)
			continue;
		if (want > room) {
			int h = (int)floor(out[i].height * (double)room / want), lo = min_height(out[i].param, scale);
			out[i].height = h < lo ? lo : h;
		}
		used += out[i].height;
	}
	int y = top;
	for (int i = 0; i < n; i++) {
		if (out[i].param == FORMANT_STRIP) {
			int fh = height - (n - 1) * gap - used, lo = min_height(FORMANT_STRIP, scale);
			out[i].height = fh < lo ? lo : fh;
		}
		out[i].top = y;
		y += out[i].height + gap;
	}
	return n;
}

int view_gap_at(const strip_rect *s, int n, int y)
{
	for (int i = 0; i + 1 < n; i++)
		if (y >= s[i].top + s[i].height && y < s[i + 1].top)
			return i;
	return -1;
}

void view_resize(strip_setup *su, const strip_rect *s, int n, int i, int dy, double scale)
{
	if (i < 0 || i + 1 >= n)
		return;
	const strip_rect *a = &s[i], *b = &s[i + 1];
	int lo = min_height(a->param, scale) - a->height, hi = b->height - min_height(b->param, scale);
	if (dy > hi)
		dy = hi;
	if (dy < lo)
		dy = lo;
	if (lo > hi) /* both already below their minimums */
		dy = 0;
	if (a->param != FORMANT_STRIP)
		su->height[a->param] = (int)lround((a->height + dy) / scale);
	if (b->param != FORMANT_STRIP)
		su->height[b->param] = (int)lround((b->height - dy) / scale);
}

int view_drop_at(const strip_rect *s, int n, int y)
{
	int k = 0;
	while (k < n && s[k].top + s[k].height / 2 <= y)
		k++;
	return k;
}

void view_move(strip_setup *su, int id, int before)
{
	if (id == before)
		return;
	int from = -1;
	for (int i = 0; i < su->count; i++)
		if (su->order[i] == id)
			from = i;
	if (from < 0)
		return;
	for (int i = from; i + 1 < su->count; i++)
		su->order[i] = su->order[i + 1];
	int to = su->count - 1;
	for (int i = 0; i < su->count - 1; i++)
		if (su->order[i] == before) {
			to = i;
			break;
		}
	for (int i = su->count - 1; i > to; i--)
		su->order[i] = su->order[i - 1];
	su->order[to] = id;
}

double view_nice_step(double range, double pixels, double min_px)
{
	if (range <= 0 || pixels <= 0)
		return 1;
	double raw = range * min_px / pixels; /* the smallest step that keeps ticks min_px apart */
	double p = pow(10, floor(log10(raw)));
	for (int i = 0; i < 3; i++) {
		static const double m[3] = { 1, 2, 5 };
		if (m[i] * p >= raw - 1e-9 * p)
			return m[i] * p;
	}
	return 10 * p;
}

int view_nearest_formant(const kpe_doc *d, const int *visible, int frame, double hz)
{
	static const int f[4] = { KP_F1, KP_F2, KP_F3, KP_F4 };
	int best = -1;
	double bd = 0;
	if (frame < 0)
		frame = 0;
	if (frame >= d->nframes)
		frame = d->nframes - 1;
	for (int i = 0; i < 4; i++) {
		if (!visible[f[i]])
			continue;
		double dist = fabs(doc_get(d, frame, f[i]) - hz);
		if (best < 0 || dist < bd) {
			best = f[i];
			bd = dist;
		}
	}
	return best;
}

void view_range(int param, double top_hz, double *lo, double *hi)
{
	if (param == FORMANT_STRIP) {
		*lo = 0;
		*hi = top_hz;
		return;
	}
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, param, &pi);
	*lo = pi.min;
	*hi = pi.max;
}

double tm_x(const time_map *m, double t_ms)
{
	return m->x0 + (t_ms - m->t0_ms) / m->ms_per_px;
}

double tm_t(const time_map *m, double x)
{
	return m->t0_ms + (x - m->x0) * m->ms_per_px;
}

double vm_y(double v, double lo, double hi, int top, int height)
{
	return top + height - 1 - (v - lo) / (hi - lo) * (height - 1);
}

double vm_v(double y, double lo, double hi, int top, int height)
{
	double v = lo + (top + height - 1 - y) / (height - 1) * (hi - lo);
	return v < lo ? lo : v > hi ? hi : v;
}
