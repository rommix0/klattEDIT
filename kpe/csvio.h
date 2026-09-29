/* csvio.h - the native file format: name.csv (time_ms + frame parameters, one row per frame) and
 * name.globals.csv (a header row of global names, one row of values). */
#ifndef KPE_CSVIO_H
#define KPE_CSVIO_H

#include <stddef.h>
#include <wchar.h>
#include "model.h"

typedef struct {
	int clamped;          /* values moved into their parameter's range while loading */
	int unknown_columns;  /* columns skipped because their names are not parameters */
	int globals_missing;  /* name.globals.csv did not exist; defaults were used */
	char message[512];    /* UTF-8. On failure: the error. On success: warnings, or "" */
} csv_report;

/* x.csv -> x.globals.csv (any other name gets ".globals.csv" appended). */
void csv_globals_path(const wchar_t *path, wchar_t *out, size_t n);

/* Loads the pair into *d (a fresh document, not dirty). Returns 0, or -1 with r->message set; on failure *d
 * is left untouched. */
int csv_load(const wchar_t *path, kpe_doc *d, csv_report *r);

/* Writes both files, each through a temporary file renamed over the target. Returns 0 (and clears d->dirty),
 * or -1 with r->message set; on failure the existing files are unchanged. */
int csv_save(const wchar_t *path, kpe_doc *d, csv_report *r);

#endif
