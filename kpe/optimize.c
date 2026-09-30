/* optimize.c - Douglas-Peucker simplification restricted to grid frames. See optimize.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "klatt.h"
#include "optimize.h"

const double opt_tol_default[OPT_NTOL] = { 20, 2, 1, 0.05, 2 };

int opt_tol_class(int p)
{
	if (p == KP_F0)
		return OPT_TOL_F0;
	if (p == KP_RD)
		return OPT_TOL_RD;
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	if (strcmp(pi.unit, "dB") == 0)
		return OPT_TOL_DB;
	return strcmp(pi.unit, "%") == 0 ? OPT_TOL_PCT : OPT_TOL_HZ;
}

/* Is frame i a breakpoint candidate? */
static int candidate(int i, int count, int first_frame, int grid)
{
	return i == 0 || i == count - 1 || (first_frame + i) % grid == 0;
}

/* Marks breakpoints in keep[] between kept ends a and b.
 * Recurses into the smaller partition and iterates over the larger to keep O(log count) stack depth. */
static void split(const double *v, int a, int b, int count, int first_frame, int grid, double tol, char *keep)
{
	while (b - a > 1) {
		/* the frame that deviates most from the line a-b */
		int worst = -1;
		double worst_err = tol;
		for (int i = a + 1; i < b; i++) {
			double line = v[a] + (v[b] - v[a]) * (double)(i - a) / (b - a);
			double e = fabs(v[i] - line);
			if (e > worst_err) {
				worst_err = e;
				worst = i;
			}
		}
		if (worst < 0)
			return;
		/* the candidate nearest to it, strictly between a and b */
		int best = -1;
		for (int d = 0; d < b - a; d++) {
			if (worst - d > a && candidate(worst - d, count, first_frame, grid)) { best = worst - d; break; }
			if (worst + d < b && candidate(worst + d, count, first_frame, grid)) { best = worst + d; break; }
		}
		if (best < 0)
			return; /* no candidate between a and b: the grid is too coarse to do better */
		keep[best] = 1;
		/* Recurse into the smaller partition, iterate over the larger. */
		int left_size = best - a;
		int right_size = b - best;
		if (left_size < right_size) {
			split(v, a, best, count, first_frame, grid, tol, keep);
			a = best;
		} else {
			split(v, best, b, count, first_frame, grid, tol, keep);
			b = best;
		}
	}
}

int opt_simplify(double *v, int count, int first_frame, int grid, double tol, double *max_err)
{
	if (max_err)
		*max_err = 0;
	if (count <= 0)
		return 0;
	if (count == 1)
		return 1;
	if (grid < 1)
		grid = 1;
	char *keep = calloc(count, 1);
	if (!keep)
		return count; /* out of memory: leave the track alone */
	keep[0] = keep[count - 1] = 1;
	split(v, 0, count - 1, count, first_frame, grid, tol, keep);
	int kept = 0, a = 0;
	double worst = 0;
	for (int b = 1; b < count; b++) {
		if (!keep[b])
			continue;
		for (int i = a + 1; i < b; i++) {
			double nv = v[a] + (v[b] - v[a]) * (double)(i - a) / (b - a);
			if (fabs(nv - v[i]) > worst)
				worst = fabs(nv - v[i]);
			v[i] = nv;
		}
		a = b;
	}
	for (int i = 0; i < count; i++)
		kept += keep[i];
	free(keep);
	if (max_err)
		*max_err = worst;
	return kept;
}
