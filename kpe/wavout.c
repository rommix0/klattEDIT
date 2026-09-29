/* wavout.c - see wavout.h. */
#include <stdint.h>
#include <stdio.h>
#include "wavout.h"

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static void put16(unsigned char *p, uint16_t v)
{
	p[0] = v & 0xFF;
	p[1] = v >> 8;
}

int wav_write(const wchar_t *path, const short *pcm, int n, int sr)
{
	unsigned char h[44] = "RIFF____WAVEfmt ____________________data____";
	uint32_t bytes = (uint32_t)n * 2;
	put32(h + 4, 36 + bytes);
	put32(h + 16, 16);           /* fmt chunk size */
	put16(h + 20, 1);            /* PCM */
	put16(h + 22, 1);            /* mono */
	put32(h + 24, (uint32_t)sr);
	put32(h + 28, (uint32_t)sr * 2);
	put16(h + 32, 2);            /* block align */
	put16(h + 34, 16);           /* bits per sample */
	put32(h + 40, bytes);
	FILE *f = _wfopen(path, L"wb");
	if (!f)
		return -1;
	int ok = fwrite(h, 1, 44, f) == 44;
	for (int i = 0; ok && i < n; i++) { /* little-endian regardless of the host */
		unsigned char s[2];
		put16(s, (uint16_t)pcm[i]);
		ok = fwrite(s, 1, 2, f) == 2;
	}
	ok = (fclose(f) == 0) && ok;
	return ok ? 0 : -1;
}
