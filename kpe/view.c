/* view.c - see view.h. */
#include <math.h>
#include "view.h"

static const int before[] = { KP_F0, KP_AV, KP_AF, KP_AH, KP_AVS, KP_RD, KP_DI };
static const int after[] = { KP_B1, KP_B2, KP_B3, KP_FNZ, KP_AN, KP_A1, KP_A2, KP_A3, KP_A4, KP_A5, KP_A6, KP_AB };

int view_layout(const int *visible, int top, int height, int unit, int gap, strip_rect *out)
{
	int n = 0, small = 0;
	for (unsigned i = 0; i < sizeof before / sizeof before[0]; i++)
		small += visible[before[i]] != 0;
	for (unsigned i = 0; i < sizeof after / sizeof after[0]; i++)
		small += visible[after[i]] != 0;
	int fh = height - small * (unit + gap);
	if (fh < 2 * unit)
		fh = 2 * unit;
	int y = top;
	for (unsigned i = 0; i < sizeof before / sizeof before[0]; i++)
		if (visible[before[i]]) {
			out[n++] = (strip_rect){ before[i], y, unit };
			y += unit + gap;
		}
	out[n++] = (strip_rect){ FORMANT_STRIP, y, fh };
	y += fh + gap;
	for (unsigned i = 0; i < sizeof after / sizeof after[0]; i++)
		if (visible[after[i]]) {
			out[n++] = (strip_rect){ after[i], y, unit };
			y += unit + gap;
		}
	return n;
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
