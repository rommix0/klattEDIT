/* Golden-file regression: every sample document is synthesized and written to golden_<name>.raw in the working
 * directory. Once the user has listened and approved, `make golden` copies those files to tests/golden/; from
 * then on this suite fails if the synthesis of a sample changes by more than 1 LSB. */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "test.h"
#include "csvio.h"
#include "synthrun.h"

static const wchar_t *samples[] = { L"ba" };

/* Reads a raw 16-bit file; returns the sample count, or -1 if it does not exist. */
static long read_raw(const wchar_t *path, short **out)
{
	FILE *f = _wfopen(path, L"rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f) / (long)sizeof(short);
	fseek(f, 0, SEEK_SET);
	*out = malloc(sizeof(short) * (n > 0 ? n : 1));
	n = (long)fread(*out, sizeof(short), n, f);
	fclose(f);
	return n;
}

void suite_golden(void)
{
	for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
		wchar_t path[260];
		kpe_doc d;
		csv_report r;
		synth_result s;
		swprintf(path, 260, L"../../samples/%ls.csv", samples[i]); /* the runner works in build/32 or build/64 */
		CHECK(csv_load(path, &d, &r) == 0);
		CHECK(synth_doc(&d, &s) == 0);
		swprintf(path, 260, L"golden_%ls.raw", samples[i]);
		FILE *f = _wfopen(path, L"wb");
		CHECK(f != NULL);
		if (f) {
			fwrite(s.pcm, sizeof(short), s.n, f);
			fclose(f);
		}
		short *g;
		swprintf(path, 260, L"../../tests/golden/%ls.raw", samples[i]);
		long n = read_raw(path, &g);
		if (n < 0) {
			printf("  %ls: no approved golden file yet (see make golden)\n", samples[i]);
		} else {
			int worst = 0;
			CHECK(n == s.n);
			for (long k = 0; k < n && k < s.n; k++) {
				int diff = abs(g[k] - s.pcm[k]);
				if (diff > worst)
					worst = diff;
			}
			CHECK(worst <= 1);
			free(g);
		}
		synth_result_free(&s);
		doc_free(&d);
	}
}
