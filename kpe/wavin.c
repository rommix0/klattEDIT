/* wavin.c - see wavin.h. */
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wavin.h"

#define TAG_PCM 0x0001
#define TAG_FLOAT 0x0003
#define TAG_EXTENSIBLE 0xFFFE

#define READ_BLOCK 65536 /* bytes of sample data converted per fread */
#define MAX_SR 1000000   /* highest sample rate read (keeps it an int) */

static uint32_t get32(const unsigned char *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned get16(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static int fail(char *msg, size_t msglen, const char *fmt, ...)
{
	if (msglen) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(msg, msglen, fmt, ap);
		va_end(ap);
	}
	return -1;
}

/* A few well-known tags, so the refusal names the format. */
static const char *tag_name(unsigned tag)
{
	switch (tag) {
	case 0x0002: return "Microsoft ADPCM";
	case 0x0006: return "A-law";
	case 0x0007: return "mu-law";
	case 0x0011: return "IMA ADPCM";
	case 0x0031: return "GSM 6.10";
	case 0x0050: return "MPEG";
	case 0x0055: return "MPEG layer 3";
	case 0x00FF: return "AAC";
	default: return "unknown";
	}
}

/* One sample of the given layout as a double in [-1, 1). */
static double sample(const unsigned char *p, int bits, int is_float)
{
	if (is_float) {
		float f;
		uint32_t u = get32(p);
		memcpy(&f, &u, 4);
		double v = f;
		if (!(v >= -1)) /* also catches NaN */
			return v > 0 ? nextafter(1.0, 0.0) : -1;
		return v < 1 ? v : nextafter(1.0, 0.0);
	}
	switch (bits) {
	case 8: return (p[0] - 128) / 128.0;
	case 16: return (int16_t)get16(p) / 32768.0;
	case 24: return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0;
	default: return (int32_t)get32(p) / 2147483648.0;
	}
}

int wav_read(const wchar_t *path, double max_seconds, wav_data *out, char *msg, size_t msglen)
{
	memset(out, 0, sizeof *out);
	if (msglen)
		msg[0] = 0;
	FILE *f = _wfopen(path, L"rb");
	if (!f)
		return fail(msg, msglen, "Cannot open the file.");
	_fseeki64(f, 0, SEEK_END);
	long long size = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);

	unsigned char h[40];
	if (size < 12 || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
		fclose(f);
		return fail(msg, msglen, "Not a WAV file (no RIFF/WAVE header).");
	}
	int have_fmt = 0, have_data = 0, truncated = 0;
	unsigned tag = 0, channels = 0, bits = 0, sub = 0;
	uint32_t sr = 0;
	long long pos = 12, data_pos = 0, data_len = 0;
	while (pos + 8 <= size) {
		_fseeki64(f, pos, SEEK_SET);
		if (fread(h, 1, 8, f) != 8)
			break;
		uint32_t len = get32(h + 4);
		if (!memcmp(h, "fmt ", 4)) {
			size_t want = len < 40 ? len : 40;
			if (pos + 8 + len > size || fread(h, 1, want, f) != want) {
				truncated = 1;
				break;
			}
			if (len < 16) {
				fclose(f);
				return fail(msg, msglen, "The WAV file's fmt chunk is too short.");
			}
			tag = get16(h);
			channels = get16(h + 2);
			sr = get32(h + 4);
			bits = get16(h + 14);
			if (tag == TAG_EXTENSIBLE) {
				static const unsigned char tail[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
					0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
				if (want < 40 || get16(h + 16) < 22) {
					fclose(f);
					return fail(msg, msglen, "Unsupported WAV format: WAVE_FORMAT_EXTENSIBLE without its "
						"sub-format.");
				}
				sub = memcmp(h + 26, tail, 14) ? 0xFFFFFFFFu : get16(h + 24);
			}
			have_fmt = 1;
		} else if (!memcmp(h, "data", 4)) {
			data_pos = pos + 8;
			data_len = len;
			have_data = 1;
			if (data_pos + data_len > size) {
				truncated = 1;
				break;
			}
		}
		pos += 8 + (long long)len + (len & 1);
	}
	/* another chunk running past the end matters only if it hid the fmt or data chunk */
	if (pos > size + 1 && (!have_fmt || !have_data))
		truncated = 1;
	if (truncated) {
		fclose(f);
		return fail(msg, msglen, "The WAV file is truncated.");
	}
	if (!have_fmt) {
		fclose(f);
		return fail(msg, msglen, "The WAV file has no fmt chunk.");
	}
	unsigned fmt = tag == TAG_EXTENSIBLE ? sub : tag;
	if (tag == TAG_EXTENSIBLE && fmt != TAG_PCM && fmt != TAG_FLOAT) {
		fclose(f);
		if (sub == 0xFFFFFFFFu)
			return fail(msg, msglen, "Unsupported WAV format: WAVE_FORMAT_EXTENSIBLE with a non-standard "
				"sub-format.");
		return fail(msg, msglen, "Unsupported WAV format: WAVE_FORMAT_EXTENSIBLE with sub-format 0x%04X (%s).",
			sub, tag_name(sub));
	}
	if (fmt != TAG_PCM && fmt != TAG_FLOAT) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: format tag 0x%04X (%s); only PCM and IEEE float can be "
			"read.", tag, tag_name(tag));
	}
	if (fmt == TAG_PCM && bits != 8 && bits != 16 && bits != 24 && bits != 32) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: %u-bit PCM (8, 16, 24 or 32 bits can be read).", bits);
	}
	if (fmt == TAG_FLOAT && bits != 32) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: %u-bit float (32-bit float can be read).", bits);
	}
	if (channels == 0) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: %u channels.", channels);
	}
	if (sr < 1 || sr > MAX_SR) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: sample rate %lu Hz (1 to %d Hz can be read).",
			(unsigned long)sr, MAX_SR);
	}
	if (!have_data) {
		fclose(f);
		return fail(msg, msglen, "The WAV file has no data chunk.");
	}
	int bytes = bits / 8;
	long long block = (long long)bytes * channels;
	long long frames = data_len / block;
	if (max_seconds > 0 && (double)frames / sr > max_seconds) {
		fclose(f);
		return fail(msg, msglen, "The recording is too long (%.1f s; at most %.0f s can be imported).",
			(double)frames / sr, max_seconds);
	}
	if ((unsigned long long)frames > SIZE_MAX / sizeof(double) || frames > LONG_MAX) {
		fclose(f);
		return fail(msg, msglen, "The recording is too large (%lld samples) to be read.", frames);
	}
	double *x = malloc(sizeof(double) * (size_t)(frames > 0 ? frames : 1));
	size_t chunk = (size_t)(READ_BLOCK / block > 0 ? READ_BLOCK / block : 1) * (size_t)block;
	unsigned char *raw = malloc(chunk);
	if (!x || !raw) {
		free(x);
		free(raw);
		fclose(f);
		return fail(msg, msglen, "Out of memory.");
	}
	_fseeki64(f, data_pos, SEEK_SET);
	long long done = 0;
	while (done < frames) {
		long long want = frames - done;
		if (want * block > (long long)chunk)
			want = (long long)chunk / block;
		if (fread(raw, (size_t)block, (size_t)want, f) != (size_t)want) {
			free(x);
			free(raw);
			fclose(f);
			return fail(msg, msglen, "The WAV file is truncated.");
		}
		for (long long i = 0; i < want; i++) {
			const unsigned char *p = raw + i * block;
			double s = 0;
			for (unsigned c = 0; c < channels; c++)
				s += sample(p + c * bytes, (int)bits, fmt == TAG_FLOAT);
			x[done + i] = s / channels;
		}
		done += want;
	}
	free(raw);
	fclose(f);
	out->x = x;
	out->n = (long)frames;
	out->sr = (int)sr;
	out->channels = (int)channels;
	out->bits = (int)bits;
	out->is_float = fmt == TAG_FLOAT;
	return 0;
}

void wav_data_free(wav_data *w)
{
	free(w->x);
	memset(w, 0, sizeof *w);
}
