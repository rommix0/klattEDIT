/* view.h - plot geometry: strip layout, time and value mapping, axis ticks, formant picking. No Win32. */
#ifndef KPE_VIEW_H
#define KPE_VIEW_H

#include "model.h"

#define FORMANT_STRIP (-1) /* the param of the spectrogram strip */

typedef struct {
	int param;       /* a KP_* index, or FORMANT_STRIP */
	int top, height; /* pixels */
} strip_rect;

/* Strip order: F0 AV AF AH AVS, the formant strip (always present), then B1 B2 B3 FNZ AN A1-A6 AB.
 * visible[p] selects the parameter strips; F1-F4 never get a strip of their own (they are drawn on the
 * formant strip). Parameter strips are unit pixels high with gap pixels between strips; the formant strip
 * takes the rest of height, but at least 2 * unit. Returns the number of strips written to out (at most NP). */
int view_layout(const int *visible, int top, int height, int unit, int gap, strip_rect *out);

/* A tick step of 1, 2 or 5 x 10^n such that ticks are at least min_px pixels apart when range spans pixels. */
double view_nice_step(double range, double pixels, double min_px);

/* The visible formant (KP_F1..KP_F4) whose value at frame is closest to hz, or -1 if none is visible. */
int view_nearest_formant(const kpe_doc *d, const int *visible, int frame, double hz);

/* The value range a strip shows: the parameter's limits, or 0..top_hz for the formant strip. */
void view_range(int param, double top_hz, double *lo, double *hi);

typedef struct {
	double t0_ms, ms_per_px; /* time at the left edge of the plot, and the zoom */
	int x0;                  /* pixel column of t0_ms */
} time_map;

double tm_x(const time_map *m, double t_ms);
double tm_t(const time_map *m, double x);
double vm_y(double v, double lo, double hi, int top, int height);
double vm_v(double y, double lo, double hi, int top, int height);

#endif
