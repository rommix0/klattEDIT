#include <string.h>
#include "test.h"
#include "measure.h"
#include "view.h"

static void test_layout_order_and_sizes(void)
{
	int vis[NP] = { 0 };
	strip_rect s[NP];
	strip_setup su;
	view_setup_default(&su);
	for (int p = 0; p < NP; p++)
		su.height[p] = 60;
	vis[KP_F0] = vis[KP_AV] = vis[KP_AH] = vis[KP_B1] = vis[KP_AB] = 1;
	vis[KP_F1] = vis[KP_F2] = 1; /* formants: no strips of their own */
	int n = view_layout(&su, vis, 10, 600, 1, 8, s);
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
	strip_setup su;
	view_setup_default(&su);
	for (int p = 0; p < NP; p++)
		vis[p] = 1;
	int n = view_layout(&su, vis, 0, 300, 1, 4, s);
	CHECK(n == 20); /* 19 parameter strips + the formant strip */
	CHECK(s[5].param == KP_RD && s[6].param == KP_DI); /* the voice strips follow the source strips */
	CHECK(s[7].param == FORMANT_STRIP && s[7].height == VIEW_FORMANT_MIN);
	CHECK(s[0].height == VIEW_MIN_H && s[19].height == VIEW_MIN_H); /* shrunk as far as they go */
}

static void test_setup_default(void)
{
	strip_setup su;
	view_setup_default(&su);
	CHECK(su.count == NP - 3); /* every parameter but F1-F4, and the formant strip */
	CHECK(su.order[0] == KP_F0 && su.order[7] == FORMANT_STRIP && su.order[su.count - 1] == KP_AB);
	int bad = 0;
	for (int i = 0; i < su.count; i++)
		bad += su.order[i] >= KP_F1 && su.order[i] <= KP_F4;
	CHECK(bad == 0);
	CHECK(su.height[KP_F0] == VIEW_STRIP_H && su.height[KP_AB] == VIEW_STRIP_H);
}

/* Custom heights are scaled to the screen; hidden strips keep theirs. When the strips do not fit above the
 * formant strip's minimum they shrink in proportion, but not below their own minimum. */
static void test_layout_heights(void)
{
	int vis[NP] = { 0 };
	strip_rect s[NP];
	strip_setup su;
	view_setup_default(&su);
	su.height[KP_AV] = 100;
	su.height[KP_AF] = 40; /* hidden */
	vis[KP_F0] = vis[KP_AV] = 1;
	int n = view_layout(&su, vis, 0, 1000, 1.5, 12, s);
	CHECK(n == 3);
	CHECK(s[0].height == 96 && s[1].height == 150 && s[1].top == 108);
	CHECK(s[2].param == FORMANT_STRIP && s[2].top == 270 && s[2].height == 730);
	CHECK(su.height[KP_AF] == 40);

	/* 344 px: 200 left above the formant strip's minimum for strips that want 64 + 300 */
	su.height[KP_AV] = 300;
	n = view_layout(&su, vis, 0, 344, 1, 8, s);
	CHECK(s[0].height + s[1].height + s[2].height == 344 - 2 * 8);
	CHECK(s[2].height >= VIEW_FORMANT_MIN && s[2].height <= VIEW_FORMANT_MIN + 2);
	CHECK(s[1].height > 4 * s[0].height); /* in proportion */
}

static void test_gap_at(void)
{
	strip_rect s[3] = { { KP_F0, 10, 60 }, { FORMANT_STRIP, 78, 200 }, { KP_B1, 286, 60 } };
	CHECK(view_gap_at(s, 3, 69) == -1);  /* inside the first strip */
	CHECK(view_gap_at(s, 3, 70) == 0);   /* the gap rows 70..77 */
	CHECK(view_gap_at(s, 3, 77) == 0);
	CHECK(view_gap_at(s, 3, 78) == -1);
	CHECK(view_gap_at(s, 3, 280) == 1);
	CHECK(view_gap_at(s, 3, 346) == -1); /* below the last strip: no gap */
	CHECK(view_gap_at(s, 3, 5) == -1);
}

/* Dragging a gap trades height between its two neighbours, within their minimums; the formant strip takes the
 * rest, so only parameter strips store a height. */
static void test_resize(void)
{
	int vis[NP] = { 0 };
	strip_rect s[NP];
	strip_setup su;
	view_setup_default(&su);
	vis[KP_F0] = vis[KP_AV] = vis[KP_B1] = 1;
	int n = view_layout(&su, vis, 0, 1000, 2, 16, s); /* F0 AV formant B1, at twice the size */
	CHECK(n == 4 && s[0].height == 128 && s[1].height == 128);

	view_resize(&su, s, n, 0, 40, 2); /* F0 grows by 40 px, AV shrinks by 40 */
	CHECK(su.height[KP_F0] == 84 && su.height[KP_AV] == 44);
	n = view_layout(&su, vis, 0, 1000, 2, 16, s);
	CHECK(s[0].height == 168 && s[1].height == 88 && s[2].height == 1000 - 168 - 88 - 128 - 3 * 16);

	view_resize(&su, s, n, 0, 1000, 2); /* AV stops at its minimum */
	CHECK(su.height[KP_AV] == VIEW_MIN_H && su.height[KP_F0] == 84 + 44 - VIEW_MIN_H);

	view_setup_default(&su);
	n = view_layout(&su, vis, 0, 1000, 2, 16, s);
	int fh = s[2].height;
	view_resize(&su, s, n, 1, 50, 2); /* AV grows into the formant strip */
	CHECK(su.height[KP_AV] == 64 + 25);
	n = view_layout(&su, vis, 0, 1000, 2, 16, s);
	CHECK(s[2].height == fh - 50);
	view_resize(&su, s, n, 1, 10000, 2); /* down to the formant strip's minimum */
	n = view_layout(&su, vis, 0, 1000, 2, 16, s);
	CHECK(s[2].height == 2 * VIEW_FORMANT_MIN);
	view_resize(&su, s, n, 2, -10000, 2); /* the formant strip above B1: B1 grows, the formant strip is at its minimum */
	CHECK(su.height[KP_B1] == VIEW_STRIP_H);
	view_resize(&su, s, n, 2, 10000, 2); /* B1 shrinks to its minimum */
	CHECK(su.height[KP_B1] == VIEW_MIN_H);
}

static void test_drop_and_move(void)
{
	strip_rect s[3] = { { KP_F0, 0, 60 }, { FORMANT_STRIP, 68, 200 }, { KP_B1, 276, 60 } };
	CHECK(view_drop_at(s, 3, 0) == 0);
	CHECK(view_drop_at(s, 3, 29) == 0);
	CHECK(view_drop_at(s, 3, 30) == 1); /* below the first strip's middle */
	CHECK(view_drop_at(s, 3, 200) == 2);
	CHECK(view_drop_at(s, 3, 400) == 3);

	strip_setup su;
	view_setup_default(&su);
	view_move(&su, KP_B1, KP_F0); /* to the top */
	CHECK(su.order[0] == KP_B1 && su.order[1] == KP_F0 && su.order[8] == FORMANT_STRIP && su.order[9] == KP_B2);
	view_move(&su, FORMANT_STRIP, VIEW_END); /* to the bottom */
	CHECK(su.order[su.count - 1] == FORMANT_STRIP && su.order[su.count - 2] == KP_AB);
	view_move(&su, KP_F0, KP_F0); /* onto itself: nothing changes */
	CHECK(su.order[1] == KP_F0);
	view_move(&su, KP_F0, KP_AV); /* before the next strip: nothing changes */
	CHECK(su.order[1] == KP_F0 && su.order[2] == KP_AV);
	view_move(&su, KP_F0, KP_AF); /* one down */
	CHECK(su.order[1] == KP_AV && su.order[2] == KP_F0 && su.order[3] == KP_AF);
	CHECK(su.count == NP - 3);

	int vis[NP] = { 0 };
	strip_rect l[NP];
	vis[KP_F0] = vis[KP_B1] = 1;
	int n = view_layout(&su, vis, 0, 600, 1, 8, l);
	CHECK(n == 3 && l[0].param == KP_B1 && l[1].param == KP_F0 && l[2].param == FORMANT_STRIP);
	CHECK(l[2].height == 600 - 2 * 64 - 2 * 8);
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
	test_setup_default();
	test_layout_heights();
	test_gap_at();
	test_resize();
	test_drop_and_move();
	test_nice_step();
	test_nearest_formant();
	test_mappings();
}
