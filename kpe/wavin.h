/* wavin.h - reads RIFF/WAVE files (PCM 8/16/24/32-bit, IEEE float 32-bit, plain or WAVE_FORMAT_EXTENSIBLE)
 * into mono samples. */
#ifndef KPE_WAVIN_H
#define KPE_WAVIN_H

#include <stddef.h>
#include <wchar.h>

typedef struct {
	double *x;     /* n samples in [-1, 1), every channel averaged into one */
	long n;
	int sr;
	int channels;  /* as stored in the file */
	int bits;      /* bits per sample as stored in the file */
	int is_float;  /* IEEE float samples */
} wav_data;

/* Reads path into *out. max_seconds > 0 refuses a longer file before any samples are read (0: no limit).
 * Returns 0 (msg set to ""), or -1 with msg set to the reason (UTF-8, one sentence naming the format when it is
 * unsupported) and *out zeroed. */
int wav_read(const wchar_t *path, double max_seconds, wav_data *out, char *msg, size_t msglen);
void wav_data_free(wav_data *w);

#endif
