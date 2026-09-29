/* wavout.h - writes 16-bit mono PCM WAV files. */
#ifndef KPE_WAVOUT_H
#define KPE_WAVOUT_H

#include <wchar.h>

/* Returns 0, or -1 if the file cannot be written (errno set). */
int wav_write(const wchar_t *path, const short *pcm, int n, int sr);

#endif
