/* calibrate.c - measures the unscaled levels that set K_OUT, K_NOISE, K_PAR and K_LF in klatt.c.
 * Built against the synthesizer source directly (not the DLL) so it can read the pre-quantization signal. */
#define KLATT_BUILD
#define K_OUT 1.0
#define K_NOISE 1.0
#define K_PAR 1.0
#define K_LF 1.0
#include "../klatt/params.c"
#include "../klatt/klatt.c"
#include <stdio.h>

static void measure(const double *frame, int sw, int src, double *peak, double *rms)
{
	double g[KLATT_NUM_GLOBAL];
	for (int i = 0; i < KLATT_NUM_GLOBAL; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SW] = sw;
	g[KG_SRC] = src;
	klatt_h h;
	KlattCreate(g, &h);
	load_frame(h, frame);
	load_frame(h, frame); /* twice, so the AF/AH ramps start at their targets */
	int n = (int)h->sr; /* one second; skip the first 0.1 s */
	double pk = 0, sq = 0;
	int cnt = 0;
	for (int i = 0; i < n; i++) {
		double v = sample(h, 1.0);
		if (i < n / 10) continue;
		if (fabs(v) > pk) pk = fabs(v);
		sq += v * v;
		cnt++;
	}
	*peak = pk;
	*rms = sqrt(sq / cnt);
	KlattDestroy(h);
}

int main(void)
{
	double f[KLATT_NUM_FRAME];
	for (int i = 0; i < KLATT_NUM_FRAME; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		f[i] = pi.def;
	}
	double pv, rv, pa, ra, pp, rp, pl, rl;
	f[KP_AV] = 60;
	measure(f, 0, 0, &pv, &rv);            /* cascade vowel, AV = 60 */
	measure(f, 0, 1, &pl, &rl);            /* the same vowel from the LF source, Rd = 1 */
	f[KP_AV] = 0; f[KP_AH] = 60;
	measure(f, 0, 0, &pa, &ra);            /* aspiration only, AH = 60 */
	f[KP_AH] = 0; f[KP_AV] = 60;
	f[KP_A1] = f[KP_A2] = f[KP_A3] = f[KP_A4] = 60;
	measure(f, 1, 0, &pp, &rp);            /* all-parallel vowel, AV = A1..A4 = 60 */
	printf("cascade vowel: peak %.6g rms %.6g\n", pv, rv);
	printf("LF vowel:      peak %.6g rms %.6g\n", pl, rl);
	printf("aspiration:    peak %.6g rms %.6g\n", pa, ra);
	printf("parallel:      peak %.6g rms %.6g\n", pp, rp);
	printf("K_OUT   = %.4g  (cascade vowel peak -> 16000)\n", 16000.0 / pv);
	printf("K_NOISE = %.4g  (aspiration rms -> cascade vowel rms)\n", rv / ra);
	printf("K_PAR   = %.4g  (parallel vowel rms -> cascade vowel rms)\n", rv / rp);
	printf("K_LF    = %.4g  (LF vowel rms -> cascade vowel rms)\n", rv / rl);
	return 0;
}
