#include <string.h>
#include "test.h"
#include "measure.h"
#include "view.h"

static void test_layout_order_and_sizes(void)
{
	int vis[NP] = { 0 };
	strip_rect s[NP];
	vis[KP_F0] = vis[KP_AV] = vis[KP_AH] = vis[KP_B1] = vis[KP_AB] = 1;
	vis[KP_F1] = vis[KP_F2] = 1; /* formants: no strips of their own */
	int n = view_layout(vis, 10, 600, 60, 8, s);
	CHECK(n == 6);
	CHECK(s[0].param == KP_F0 && s[0].top == 10 && s[0].height == 60);
	CHECK(s[1].param == KP_AV && s[1].top == 78);
	CHECK(s[2].param == KP_AH && s[2].top == 146);
	CHECK(s[3].param == FORMANT_STRIP && s[3].top == 214);
	CHECK(s[3].height == 600 - 5 * 68);
	CHECK(s[4].param == KP_B1 && s[4].top == 214 + s[3].height + 8);
	CHECK(s[5].param == KP_AB);
}

static void test_layout_formant_minimum(void)
{
	int vis[NP];
	strip_rect s[NP];
	for (int p = 0; p < NP; p++)
		vis[p] = 1;
	int n = view_layout(vis, 0, 300, 50, 4, s);
	CHECK(n == 20); /* 19 parameter strips + the formant strip */
	CHECK(s[5].param == KP_RD && s[6].param == KP_DI); /* the voice strips follow the source strips */
	CHECK(s[7].param == FORMANT_STRIP && s[7].height == 100);
}

static void test_nice_step(void)
{
	CHECK_NEAR(view_nice_step(800, 800, 60), 100, 1e-9);  /* 1 ms per pixel, 60 px apart */
	CHECK_NEAR(view_nice_step(800, 400, 60), 200, 1e-9);
	CHECK_NEAR(view_nice_step(5000, 250, 30), 1000, 1e-9);
	CHECK_NEAR(view_nice_step(80, 60, 20), 50, 1e-9);
	CHECK_NEAR(view_nice_step(100, 1000, 60), 10, 1e-9);  /* exactly 10: not 20 */
	CHECK_NEAR(view_nice_step(0, 100, 10), 1, 0);
}

static void test_nearest_formant(void)
{
	kpe_doc d;
	double g[NG];
	int vis[NP] = { 0 };
	t_default_globals(g);
	CHECK(doc_new(&d, g, 50) == 0); /* F1-F4 = 500, 1500, 2500, 3500 */
	vis[KP_F1] = vis[KP_F2] = vis[KP_F3] = vis[KP_F4] = 1;
	CHECK(view_nearest_formant(&d, vis, 3, 1400) == KP_F2);
	CHECK(view_nearest_formant(&d, vis, 3, 0) == KP_F1);
	CHECK(view_nearest_formant(&d, vis, 99, 3100) == KP_F4); /* frame clamped */
	vis[KP_F2] = 0;
	CHECK(view_nearest_formant(&d, vis, 3, 1400) == KP_F1);
	memset(vis, 0, sizeof vis);
	CHECK(view_nearest_formant(&d, vis, 3, 1400) == -1);
	doc_free(&d);
}

static void test_mappings(void)
{
	time_map m = { 100, 2, 50 };
	CHECK_NEAR(tm_x(&m, 100), 50, 1e-9);
	CHECK_NEAR(tm_x(&m, 300), 150, 1e-9);
	CHECK_NEAR(tm_t(&m, 150), 300, 1e-9);
	CHECK_NEAR(vm_y(0, 0, 80, 10, 81), 90, 1e-9);  /* bottom row */
	CHECK_NEAR(vm_y(80, 0, 80, 10, 81), 10, 1e-9); /* top row */
	CHECK_NEAR(vm_v(50, 0, 80, 10, 81), 40, 1e-9);
	CHECK_NEAR(vm_v(-5, 0, 80, 10, 81), 80, 0);    /* clamped */
	double lo, hi;
	view_range(KP_F1, 5000, &lo, &hi);
	CHECK(lo == 150 && hi == 1300);
	view_range(FORMANT_STRIP, 5000, &lo, &hi);
	CHECK(lo == 0 && hi == 5000);
}

void suite_view(void)
{
	test_layout_order_and_sizes();
	test_layout_formant_minimum();
	test_nice_step();
	test_nearest_formant();
	test_mappings();
}
