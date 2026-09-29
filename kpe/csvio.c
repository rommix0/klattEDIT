/* csvio.c - loading and saving the CSV pair. See csvio.h. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "csvio.h"

#define MAX_COLS 64

/* ---- small helpers ---- */

static void report(csv_report *r, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r->message, sizeof r->message, fmt, ap);
	va_end(ap);
}

/* The file name part of path, as UTF-8, for messages. */
static void base_utf8(const wchar_t *path, char *out, size_t n)
{
	const wchar_t *b = path;
	for (const wchar_t *p = path; *p; p++)
		if (*p == L'\\' || *p == L'/')
			b = p + 1;
	if (!WideCharToMultiByte(CP_UTF8, 0, b, -1, out, (int)n, NULL, NULL))
		snprintf(out, n, "?");
}

void csv_globals_path(const wchar_t *path, wchar_t *out, size_t n)
{
	size_t len = wcslen(path);
	if (len >= 4 && _wcsicmp(path + len - 4, L".csv") == 0)
		len -= 4;
	_snwprintf(out, n, L"%.*ls.globals.csv", (int)len, path);
	out[n - 1] = 0;
}

/* Reads a whole file; returns a NUL-terminated buffer without a UTF-8 BOM, or NULL (errno set). */
static char *read_file(const wchar_t *path)
{
	FILE *f = _wfopen(path, L"rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(len + 1);
	if (!buf || fread(buf, 1, len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		errno = EIO;
		return NULL;
	}
	fclose(f);
	buf[len] = 0;
	if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
		memmove(buf, buf + 3, len - 2);
	return buf;
}

/* Splits one line in place into trimmed cells. Returns the number of cells, or -1 if more than max. */
static int split(char *line, char **cells, int max)
{
	int n = 0;
	char *p = line;
	for (;;) {
		if (n == max)
			return -1;
		char *end = strchr(p, ',');
		if (end)
			*end = 0;
		while (*p == ' ' || *p == '\t')
			p++;
		char *q = p + strlen(p);
		while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'))
			*--q = 0;
		cells[n++] = p;
		if (!end)
			return n;
		p = end + 1;
	}
}

/* Moves *text to the next line; returns the current line (NUL-terminated) or NULL at the end. */
static char *next_line(char **text)
{
	char *s = *text;
	if (!s || !*s)
		return NULL;
	char *nl = strchr(s, '\n');
	if (nl) {
		*nl = 0;
		*text = nl + 1;
	} else {
		*text = s + strlen(s);
	}
	size_t len = strlen(s);
	if (len && s[len - 1] == '\r')
		s[len - 1] = 0;
	return s;
}

static int blank(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return *s == 0;
}

static int parse_number(const char *s, double *v)
{
	char *end;
	if (!*s)
		return 0;
	errno = 0;
	*v = strtod(s, &end);
	return *end == 0 && errno != ERANGE && isfinite(*v);
}

static int eq_nocase(const char *a, const char *b)
{
	while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b))
		a++, b++;
	return !*a && !*b;
}

/* Maps header cells to parameter indices of kind (-1: skip). Adds unknown names to warn. Returns 0, or -1
 * with an error in r for a duplicate column. time_col receives the time_ms column (or -1) when not NULL. */
static int map_header(char **cells, int n, int kind, int *map, int *time_col, const char *file, char *warn,
                      size_t warnlen, csv_report *r)
{
	if (time_col)
		*time_col = -1;
	for (int c = 0; c < n; c++) {
		map[c] = -1;
		if (time_col && eq_nocase(cells[c], "time_ms")) {
			if (*time_col >= 0) {
				report(r, "%s line 1: column time_ms appears twice", file);
				return -1;
			}
			*time_col = c;
			continue;
		}
		int idx = KlattParamIndex(kind, cells[c]);
		if (idx < 0) {
			r->unknown_columns++;
			size_t used = strlen(warn);
			snprintf(warn + used, warnlen - used, "%s%s", used ? ", " : "", cells[c]);
			continue;
		}
		for (int e = 0; e < c; e++)
			if (map[e] == idx) {
				report(r, "%s line 1: column %s appears twice", file, cells[c]);
				return -1;
			}
		map[c] = idx;
	}
	return 0;
}

static double clamp_count(int kind, int idx, double v, int *clamped)
{
	klatt_param_info pi;
	KlattParamInfo(kind, idx, &pi);
	if (v < pi.min) { (*clamped)++; return pi.min; }
	if (v > pi.max) { (*clamped)++; return pi.max; }
	return v;
}

/* ---- loading ---- */

/* Parses name.globals.csv into g (defaults for missing columns). have_frame_ms reports whether the file set
 * FRAME_MS. Returns 1 if loaded, 0 if the file does not exist, -1 on an error (r set). */
static int load_globals(const wchar_t *gpath, double *g, int *have_frame_ms, char *warn, size_t warnlen,
                        csv_report *r)
{
	char file[260], *cells[MAX_COLS], *vals[MAX_COLS];
	int map[MAX_COLS];
	base_utf8(gpath, file, sizeof file);
	*have_frame_ms = 0;
	char *text = read_file(gpath);
	if (!text) {
		if (errno == ENOENT)
			return 0;
		report(r, "cannot read %s: %s", file, strerror(errno));
		return -1;
	}
	char *cur = text, *line;
	int lineno = 0, ret = -1, nh = 0, rows = 0;
	while ((line = next_line(&cur))) {
		lineno++;
		if (blank(line))
			continue;
		if (!nh) {
			nh = split(line, cells, MAX_COLS);
			if (nh < 0) {
				report(r, "%s line %d: more than %d columns", file, lineno, MAX_COLS);
				goto done;
			}
			if (map_header(cells, nh, KLATT_GLOBAL, map, NULL, file, warn, warnlen, r) < 0)
				goto done;
			continue;
		}
		if (++rows > 1) {
			report(r, "%s line %d: expected a single row of values", file, lineno);
			goto done;
		}
		int nv = split(line, vals, MAX_COLS);
		if (nv != nh) {
			report(r, "%s line %d: expected %d values, found %d", file, lineno, nh, nv);
			goto done;
		}
		for (int c = 0; c < nh; c++) {
			double v;
			if (map[c] < 0)
				continue;
			if (!parse_number(vals[c], &v)) {
				report(r, "%s line %d, column %s: \"%s\" is not a number", file, lineno, cells[c], vals[c]);
				goto done;
			}
			g[map[c]] = clamp_count(KLATT_GLOBAL, map[c], v, &r->clamped);
			if (map[c] == KG_FRAME_MS)
				*have_frame_ms = 1;
		}
	}
	if (rows == 0) {
		report(r, "%s: no values", file);
		goto done;
	}
	ret = 1;
done:
	free(text);
	return ret;
}

int csv_load(const wchar_t *path, kpe_doc *d, csv_report *r)
{
	char file[260], warn[400] = "", *cells[MAX_COLS], *vals[MAX_COLS];
	int map[MAX_COLS], time_col;
	double g[NG], defaults[NP];
	memset(r, 0, sizeof *r);
	base_utf8(path, file, sizeof file);
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	for (int i = 0; i < NP; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, i, &pi);
		defaults[i] = pi.def;
	}

	char *text = read_file(path);
	if (!text) {
		report(r, "cannot read %s: %s", file, strerror(errno));
		return -1;
	}
	int ret = -1, nh = 0, nframes = 0, cap = 0, lineno = 0;
	double *frames = NULL, t_prev = 0, step = 0;
	char *cur = text, *line;
	while ((line = next_line(&cur))) {
		lineno++;
		if (blank(line))
			continue;
		if (!nh) {
			nh = split(line, cells, MAX_COLS);
			if (nh < 0) {
				report(r, "%s line %d: more than %d columns", file, lineno, MAX_COLS);
				goto done;
			}
			if (map_header(cells, nh, KLATT_FRAME, map, &time_col, file, warn, sizeof warn, r) < 0)
				goto done;
			if (time_col < 0) {
				report(r, "%s line %d: no time_ms column", file, lineno);
				goto done;
			}
			continue;
		}
		int nv = split(line, vals, MAX_COLS);
		if (nv != nh) {
			report(r, "%s line %d: expected %d values, found %d", file, lineno, nh, nv);
			goto done;
		}
		if (nframes == cap) {
			cap = cap ? cap * 2 : 256;
			double *nf = realloc(frames, sizeof(double) * NP * cap);
			if (!nf) {
				report(r, "%s: out of memory", file);
				goto done;
			}
			frames = nf;
		}
		double *fr = frames + NP * nframes;
		memcpy(fr, defaults, sizeof defaults);
		for (int c = 0; c < nh; c++) {
			double v;
			if (c != time_col && map[c] < 0)
				continue;
			if (!parse_number(vals[c], &v)) {
				report(r, "%s line %d, column %s: \"%s\" is not a number", file, lineno, cells[c], vals[c]);
				goto done;
			}
			if (c == time_col) {
				if (nframes == 0) {
					/* the first time may be anything; frames are re-timed from 0 on save */
				} else if (v <= t_prev) {
					report(r, "%s line %d: time_ms %g does not increase", file, lineno, v);
					goto done;
				} else if (nframes == 1) {
					step = v - t_prev;
				} else if (fabs((v - t_prev) - step) > 0.01) {
					report(r, "%s line %d: time_ms steps by %g ms here but by %g ms before", file, lineno,
					       v - t_prev, step);
					goto done;
				}
				t_prev = v;
			} else {
				fr[map[c]] = clamp_count(KLATT_FRAME, map[c], v, &r->clamped);
			}
		}
		nframes++;
	}
	if (!nh) {
		report(r, "%s is empty", file);
		goto done;
	}
	if (nframes == 0) {
		report(r, "%s has a header but no frames", file);
		goto done;
	}

	wchar_t gpath[MAX_PATH * 2];
	int have_ms = 0;
	csv_globals_path(path, gpath, sizeof gpath / sizeof gpath[0]);
	int gl = load_globals(gpath, g, &have_ms, warn, sizeof warn, r);
	if (gl < 0)
		goto done;
	r->globals_missing = gl == 0;
	if (nframes >= 2) {
		if (!have_ms) {
			g[KG_FRAME_MS] = clamp_count(KLATT_GLOBAL, KG_FRAME_MS, step, &r->clamped);
		} else if (fabs(g[KG_FRAME_MS] - step) > 0.01) {
			char gfile[260];
			base_utf8(gpath, gfile, sizeof gfile);
			report(r, "FRAME_MS is %g ms in %s but time_ms steps by %g ms in %s", g[KG_FRAME_MS], gfile, step, file);
			goto done;
		}
	}
	doc_init_from(d, g, nframes, frames);
	frames = NULL;
	if (r->unknown_columns)
		report(r, "skipped unknown columns: %s", warn);
	else
		r->message[0] = 0;
	ret = 0;
done:
	free(frames);
	free(text);
	return ret;
}

/* ---- saving ---- */

/* Shortest of %.15g, %.16g, %.17g that reads back as exactly v. */
static void fmt_num(char *buf, size_t n, double v)
{
	for (int prec = 15; prec <= 17; prec++) {
		snprintf(buf, n, "%.*g", prec, v);
		if (strtod(buf, NULL) == v)
			return;
	}
}

/* Writes text produced by emit() to tmp. Returns 0 or -1 with r set (and tmp removed). */
static int write_tmp(const wchar_t *path, const wchar_t *tmp, int (*emit)(FILE *, const kpe_doc *),
                     const kpe_doc *d, csv_report *r)
{
	char file[260];
	base_utf8(path, file, sizeof file);
	FILE *f = _wfopen(tmp, L"wb");
	if (!f) {
		report(r, "cannot write %s: %s", file, strerror(errno));
		return -1;
	}
	int ok = emit(f, d) == 0;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		report(r, "cannot write %s: %s", file, strerror(errno));
		_wremove(tmp);
		return -1;
	}
	return 0;
}

static int emit_frames(FILE *f, const kpe_doc *d)
{
	char num[40];
	fputs("time_ms", f);
	for (int p = 0; p < NP; p++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		fprintf(f, ",%s", pi.name);
	}
	fputs("\r\n", f);
	for (int k = 0; k < d->nframes; k++) {
		fmt_num(num, sizeof num, k * doc_frame_ms(d));
		fputs(num, f);
		for (int p = 0; p < NP; p++) {
			fmt_num(num, sizeof num, doc_get(d, k, p));
			fprintf(f, ",%s", num);
		}
		fputs("\r\n", f);
	}
	return ferror(f) ? -1 : 0;
}

static int emit_globals(FILE *f, const kpe_doc *d)
{
	char num[40];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		fprintf(f, "%s%s", i ? "," : "", pi.name);
	}
	fputs("\r\n", f);
	for (int i = 0; i < NG; i++) {
		fmt_num(num, sizeof num, d->globals[i]);
		fprintf(f, "%s%s", i ? "," : "", num);
	}
	fputs("\r\n", f);
	return ferror(f) ? -1 : 0;
}

int csv_save(const wchar_t *path, kpe_doc *d, csv_report *r)
{
	wchar_t gpath[MAX_PATH * 2], tmp[MAX_PATH * 2 + 8], gtmp[MAX_PATH * 2 + 8], bak[MAX_PATH * 2 + 8];
	char file[260];
	int backup_made = 0;
	memset(r, 0, sizeof *r);
	csv_globals_path(path, gpath, sizeof gpath / sizeof gpath[0]);
	_snwprintf(tmp, sizeof tmp / sizeof tmp[0], L"%ls.tmp", path);
	_snwprintf(gtmp, sizeof gtmp / sizeof gtmp[0], L"%ls.tmp", gpath);
	_snwprintf(bak, sizeof bak / sizeof bak[0], L"%ls.bak", path);
	tmp[sizeof tmp / sizeof tmp[0] - 1] = gtmp[sizeof gtmp / sizeof gtmp[0] - 1] = 0;
	bak[sizeof bak / sizeof bak[0] - 1] = 0;
	/* Both temporary files first, so a write error leaves both targets untouched. */
	if (write_tmp(path, tmp, emit_frames, d, r) < 0)
		return -1;
	if (write_tmp(gpath, gtmp, emit_globals, d, r) < 0) {
		_wremove(tmp);
		return -1;
	}
	/* For atomicity across both files, back up the old frame file before replacing it. */
	FILE *existing = _wfopen(path, L"rb");
	if (existing) {
		fclose(existing);
		_wremove(bak); /* remove stale backup if present */
		if (!MoveFileExW(path, bak, MOVEFILE_REPLACE_EXISTING)) {
			base_utf8(path, file, sizeof file);
			report(r, "cannot back up %s (Windows error %lu)", file, GetLastError());
			_wremove(tmp);
			_wremove(gtmp);
			return -1;
		}
		backup_made = 1;
	}
	/* Move the temp frame file to the target. */
	if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		base_utf8(path, file, sizeof file);
		report(r, "cannot replace %s (Windows error %lu)", file, GetLastError());
		if (backup_made)
			MoveFileExW(bak, path, MOVEFILE_REPLACE_EXISTING);
		_wremove(tmp);
		_wremove(gtmp);
		return -1;
	}
	/* Move the temp globals file to the target. If this fails, restore the frame file backup. */
	if (!MoveFileExW(gtmp, gpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		base_utf8(gpath, file, sizeof file);
		report(r, "cannot replace %s (Windows error %lu)", file, GetLastError());
		if (backup_made)
			MoveFileExW(bak, path, MOVEFILE_REPLACE_EXISTING);
		_wremove(gtmp);
		return -1;
	}
	/* Success: clean up the backup. */
	if (backup_made)
		_wremove(bak);
	d->dirty = 0;
	return 0;
}
