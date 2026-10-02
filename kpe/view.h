/* view.h - plot geometry: strip layout, time and value mapping, axis ticks, formant picking. No Win32. */
#ifndef KPE_VIEW_H
#define KPE_VIEW_H

#include "model.h"

#define FORMANT_STRIP (-1) /* the param of the spectrogram strip */

typedef struct {
	int param;       /* a KP_* index, or FORMANT_STRIP */
	int top, height; /* pixels */
} strip_rect;

#define VIEW_STRIP_H 64       /* a parameter strip's default height, in 96-DPI pixels */
#define VIEW_MIN_H 24         /* a parameter strip's smallest height */
#define VIEW_FORMANT_MIN 128  /* the formant strip's smallest height */
#define VIEW_END NP           /* view_move: after the last strip */

/* The user's arrangement of the strips: their order (every parameter strip but F1-F4, which are drawn on the
 * formant strip, and the formant strip), and each parameter strip's height in 96-DPI pixels. The formant strip
 * takes the height the others leave. */
typedef struct {
	int order[NP];
	int count;
	int height[NP];
} strip_setup;

/* F0 AV AF AH AVS RD DI, the formant strip, then B1 B2 B3 FNZ AN A1-A6 AB, each VIEW_STRIP_H high. */
void view_setup_default(strip_setup *su);

/* Lays out the strips of su whose visible[p] is set (the formant strip always) from top down in height pixels,
 * gap pixels apart; scale is screen pixels per 96-DPI pixel. If the parameter strips leave the formant strip less
 * than its minimum, they shrink in proportion, but not below their own minimum. Returns the number of strips
 * written to out (at most NP). */
int view_layout(const strip_setup *su, const int *visible, int top, int height, double scale, int gap,
                strip_rect *out);

/* The index i of the gap between s[i] and s[i + 1] that contains row y, or -1. */
int view_gap_at(const strip_rect *s, int n, int y);

/* Moves the gap between s[i] and s[i + 1] (a layout of su) dy pixels down: the strip above grows and the one
 * below shrinks by the same amount, within their minimums. Stores the new heights of the parameter strips. */
void view_resize(strip_setup *su, const strip_rect *s, int n, int i, int dy, double scale);

/* Where a strip dropped at row y lands: before s[k] for the returned k, or after the last strip for k == n. */
int view_drop_at(const strip_rect *s, int n, int y);

/* Moves strip id (a parameter or FORMANT_STRIP) to just before strip before, or to the end for VIEW_END. */
void view_move(strip_setup *su, int id, int before);

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
