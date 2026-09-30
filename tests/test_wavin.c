/* WAV reader tests. The WAV files are written into the working directory (build/32 or build/64). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "test.h"
#include "wavin.h"

#define TAG_PCM 1
#define TAG_FLOAT 3
#define TAG_EXT 0xFFFE

static unsigned char *p32(unsigned char *p, uint32_t v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
	return p + 4;
}

static unsigned char *p16(unsigned char *p, uint16_t v)
{
	p[0] = v & 0xFF;
	p[1] = v >> 8;
	return p + 2;
}

/* A WAV file's description; sub is the sub-format tag for WAVE_FORMAT_EXTENSIBLE (0: not extensible). */
typedef struct {
	int tag, channels, sr, bits, sub;
	const unsigned char *data;
	int nbytes;
} wav_spec;

/* Builds the file image in buf; returns its length. */
static int build(const wav_spec *s, unsigned char *buf)
{
	unsigned char *p = buf;
	int ext = s->tag == TAG_EXT;
	int fmt_len = ext ? 40 : 16;
	int block = s->channels * s->bits / 8;
	memcpy(p, "RIFF", 4);
	p = p32(p + 4, 4 + 8 + fmt_len + 8 + s->nbytes + (s->nbytes & 1));
	memcpy(p, "WAVEfmt ", 8);
	p = p32(p + 8, fmt_len);
	p = p16(p, s->tag);
	p = p16(p, s->channels);
	p = p32(p, s->sr);
	p = p32(p, s->sr * block);
	p = p16(p, block);
	p = p16(p, s->bits);
	if (ext) {
		static const unsigned char guid_tail[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
			0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
		p = p16(p, 22);           /* cbSize */
		p = p16(p, s->bits);      /* valid bits */
		p = p32(p, 0);            /* channel mask */
		p = p16(p, s->sub);       /* sub-format GUID: tag, then the fixed tail */
		memcpy(p, guid_tail, 14);
		p += 14;
	}
	memcpy(p, "data", 4);
	p = p32(p + 4, s->nbytes);
	memcpy(p, s->data, s->nbytes);
	p += s->nbytes;
	if (s->nbytes & 1)
		*p++ = 0;
	return (int)(p - buf);
}

static void write_bytes(const wchar_t *path, const unsigned char *b, int n)
{
	FILE *f = _wfopen(path, L"wb");
	fwrite(b, 1, n, f);
	fclose(f);
}

static unsigned char buf[4096];

/* Writes s to w.wav and reads it back. */
static int read_spec(const wav_spec *s, double max_seconds, wav_data *w, char *msg)
{
	write_bytes(L"w.wav", buf, build(s, buf));
	return wav_read(L"w.wav", max_seconds, w, msg, 256);
}

/* read_spec, expecting success; a failure is counted and the caller returns. */
static int read_ok(const wav_spec *s, double max_seconds, wav_data *w, char *msg)
{
	int rc = read_spec(s, max_seconds, w, msg);
	CHECK(rc == 0);
	return rc == 0;
}

/* Reads bytes as a file and expects a refusal whose message contains want. */
static void expect_refusal(const unsigned char *b, int n, const char *want)
{
	wav_data w;
	char msg[256] = "";
	write_bytes(L"bad.wav", b, n);
	int rc = wav_read(L"bad.wav", 0, &w, msg, sizeof msg);
	CHECK(rc == -1);
	CHECK(w.x == NULL && w.n == 0);
	if (!strstr(msg, want))
		printf("  message \"%s\" lacks \"%s\"\n", msg, want);
	CHECK(strstr(msg, want) != NULL);
}

static void test_pcm16_mono(void)
{
	unsigned char d[8];
	p16(p16(p16(p16(d, 0), 16384), (uint16_t)-32768), 32767);
	wav_spec s = { TAG_PCM, 1, 11025, 16, 0, d, 8 };
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 4 && w.sr == 11025 && w.channels == 1 && w.bits == 16 && !w.is_float);
	CHECK_NEAR(w.x[0], 0, 0);
	CHECK_NEAR(w.x[1], 0.5, 0);
	CHECK_NEAR(w.x[2], -1, 0);
	CHECK_NEAR(w.x[3], 32767.0 / 32768, 0);
	CHECK(strcmp(msg, "") == 0);
	wav_data_free(&w);
	CHECK(w.x == NULL && w.n == 0);
}

static void test_pcm8_unsigned(void)
{
	unsigned char d[3] = { 128, 0, 255 };
	wav_spec s = { TAG_PCM, 1, 8000, 8, 0, d, 3 }; /* odd data length: a pad byte follows */
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 3 && w.sr == 8000 && w.bits == 8);
	CHECK_NEAR(w.x[0], 0, 0);
	CHECK_NEAR(w.x[1], -1, 0);
	CHECK_NEAR(w.x[2], 127.0 / 128, 0);
	wav_data_free(&w);
}

static void test_pcm24_and_pcm32(void)
{
	unsigned char d[12] = { 0x00, 0x00, 0x40, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	wav_spec s = { TAG_PCM, 1, 16000, 24, 0, d, 12 };
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 4 && w.bits == 24);
	CHECK_NEAR(w.x[0], 0.5, 0);
	CHECK_NEAR(w.x[1], -1, 0);
	CHECK_NEAR(w.x[2], -1.0 / 8388608, 0);
	CHECK_NEAR(w.x[3], 0, 0);
	wav_data_free(&w);

	unsigned char e[8];
	p32(p32(e, 0xC0000000u), 0x7FFFFFFFu);
	wav_spec s32 = { TAG_PCM, 1, 16000, 32, 0, e, 8 };
	if (!read_ok(&s32, 0, &w, msg))
		return;
	CHECK(w.n == 2 && w.bits == 32);
	CHECK_NEAR(w.x[0], -0.5, 0);
	CHECK_NEAR(w.x[1], 2147483647.0 / 2147483648.0, 0);
	wav_data_free(&w);
}

static void test_float32_clamped(void)
{
	float v[4] = { 0.25f, -0.75f, 3.0f, -2.0f };
	unsigned char d[16];
	for (int i = 0; i < 4; i++) {
		uint32_t u;
		memcpy(&u, &v[i], 4);
		p32(d + 4 * i, u);
	}
	wav_spec s = { TAG_FLOAT, 1, 44100, 32, 0, d, 16 };
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 4 && w.sr == 44100 && w.is_float);
	CHECK_NEAR(w.x[0], 0.25, 0);
	CHECK_NEAR(w.x[1], -0.75, 0);
	CHECK(w.x[2] < 1 && w.x[2] > 0.999); /* out-of-range floats are clamped into [-1, 1) */
	CHECK_NEAR(w.x[3], -1, 0);
	wav_data_free(&w);
}

static void test_channels_averaged(void)
{
	unsigned char d[12];
	/* stereo 16-bit: (16384, 0) (-32768, -16384) (8192, 8192) */
	p16(p16(p16(p16(p16(p16(d, 16384), 0), (uint16_t)-32768), (uint16_t)-16384), 8192), 8192);
	wav_spec s = { TAG_PCM, 2, 22050, 16, 0, d, 12 };
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 3 && w.channels == 2);
	CHECK_NEAR(w.x[0], 0.25, 0);
	CHECK_NEAR(w.x[1], -0.75, 0);
	CHECK_NEAR(w.x[2], 0.25, 0);
	wav_data_free(&w);

	unsigned char t[6]; /* three channels of 8-bit: 128 + 64, 128 - 64, 128 -> mean 0 */
	t[0] = 192; t[1] = 64; t[2] = 128; t[3] = 255; t[4] = 255; t[5] = 255;
	wav_spec s3 = { TAG_PCM, 3, 8000, 8, 0, t, 6 };
	if (!read_ok(&s3, 0, &w, msg))
		return;
	CHECK(w.n == 2 && w.channels == 3);
	CHECK_NEAR(w.x[0], 0, 0);
	CHECK_NEAR(w.x[1], 127.0 / 128, 1e-15);
	wav_data_free(&w);
}

static void test_extensible(void)
{
	unsigned char d[4];
	p16(p16(d, 16384), (uint16_t)-16384);
	wav_spec s = { TAG_EXT, 1, 48000, 16, TAG_PCM, d, 4 };
	wav_data w;
	char msg[256];
	if (!read_ok(&s, 0, &w, msg))
		return;
	CHECK(w.n == 2 && w.sr == 48000 && !w.is_float);
	CHECK_NEAR(w.x[0], 0.5, 0);
	CHECK_NEAR(w.x[1], -0.5, 0);
	wav_data_free(&w);

	float v = -0.125f;
	uint32_t u;
	memcpy(&u, &v, 4);
	unsigned char e[8];
	p32(p32(e, u), u);
	wav_spec sf = { TAG_EXT, 2, 48000, 32, TAG_FLOAT, e, 8 };
	if (!read_ok(&sf, 0, &w, msg))
		return;
	CHECK(w.n == 1 && w.is_float && w.channels == 2);
	CHECK_NEAR(w.x[0], -0.125, 0);
	wav_data_free(&w);
}

static void test_chunks_skipped_and_order(void)
{
	/* a LIST chunk before fmt, an odd-sized chunk (with its pad byte) between fmt and data */
	unsigned char b[128], *p = b;
	memcpy(p, "RIFF", 4);
	p += 8;
	memcpy(p, "WAVE", 4);
	p += 4;
	memcpy(p, "LIST", 4);
	p = p32(p + 4, 4);
	memcpy(p, "INFO", 4);
	p += 4;
	memcpy(p, "fmt ", 4);
	p = p32(p + 4, 16);
	p = p16(p, TAG_PCM);
	p = p16(p, 1);
	p = p32(p, 11025);
	p = p32(p, 22050);
	p = p16(p, 2);
	p = p16(p, 16);
	memcpy(p, "junk", 4);
	p = p32(p + 4, 3);
	*p++ = 1; *p++ = 2; *p++ = 3; *p++ = 0; /* 3 bytes and the pad */
	memcpy(p, "data", 4);
	p = p32(p + 4, 4);
	p = p16(p16(p, 8192), 0);
	p32(b + 4, (uint32_t)(p - b - 8));
	write_bytes(L"chunks.wav", b, (int)(p - b));
	wav_data w;
	char msg[256];
	int rc = wav_read(L"chunks.wav", 0, &w, msg, sizeof msg);
	CHECK(rc == 0);
	CHECK(w.n == 2);
	if (rc == 0 && w.n == 2)
		CHECK_NEAR(w.x[0], 0.25, 0);
	wav_data_free(&w);
}

static void test_refusals(void)
{
	unsigned char d[4] = { 0, 0, 0, 0 };
	wav_spec s = { TAG_PCM, 1, 11025, 16, 0, d, 4 };
	int n = build(&s, buf);

	expect_refusal((const unsigned char *)"hello, this is not a wav file at all", 36, "Not a WAV file");
	unsigned char c[64];
	memcpy(c, buf, n);
	memcpy(c + 8, "AVI ", 4);
	expect_refusal(c, n, "Not a WAV file");
	expect_refusal(buf, 6, "Not a WAV file");  /* shorter than a RIFF header */
	expect_refusal(buf, 30, "truncated");      /* ends inside the fmt chunk */
	expect_refusal(buf, n - 2, "truncated");   /* data chunk shorter than declared */

	s.tag = 2; /* MS ADPCM */
	n = build(&s, buf);
	expect_refusal(buf, n, "ADPCM");
	s.tag = 0x55;
	n = build(&s, buf);
	expect_refusal(buf, n, "0x0055");
	s.tag = TAG_PCM;
	s.bits = 12;
	n = build(&s, buf);
	expect_refusal(buf, n, "12-bit");
	s.tag = TAG_FLOAT;
	s.bits = 64;
	unsigned char d8[8] = { 0 };
	s.data = d8;
	s.nbytes = 8;
	n = build(&s, buf);
	expect_refusal(buf, n, "64-bit float");
	s.tag = TAG_EXT;
	s.bits = 16;
	s.sub = 2;
	n = build(&s, buf);
	expect_refusal(buf, n, "sub-format");
	s.tag = TAG_PCM;
	s.sub = 0;
	s.channels = 0;
	n = build(&s, buf);
	expect_refusal(buf, n, "channels");

	/* a RIFF/WAVE file with a fmt chunk but no data chunk, and one with no fmt chunk */
	s.channels = 1;
	s.bits = 16;
	s.nbytes = 4;
	s.data = d;
	n = build(&s, buf);
	memcpy(c, buf, 36);
	p32(c + 4, 28);
	expect_refusal(c, 36, "no data chunk");
	memcpy(c, buf, 12);
	memcpy(c + 12, buf + 36, n - 36);
	p32(c + 4, n - 24 - 8);
	expect_refusal(c, n - 24, "no fmt chunk");

	wav_data w;
	char msg[256];
	CHECK(wav_read(L"does_not_exist.wav", 0, &w, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "Cannot open") != NULL);
}

static void test_too_long_refused_before_reading(void)
{
	static unsigned char d[2000];
	wav_spec s = { TAG_PCM, 1, 1000, 16, 0, d, 2000 }; /* 1000 samples at 1 kHz: 1 s */
	wav_data w;
	char msg[256];
	CHECK(read_spec(&s, 0.5, &w, msg) == -1);
	CHECK(strstr(msg, "too long") != NULL);
	CHECK(w.x == NULL);
	if (!read_ok(&s, 1.0, &w, msg))
		return; /* exactly at the limit: accepted */
	CHECK(w.n == 1000);
	wav_data_free(&w);
}

void suite_wavin(void)
{
	test_pcm16_mono();
	test_pcm8_unsigned();
	test_pcm24_and_pcm32();
	test_float32_clamped();
	test_channels_averaged();
	test_extensible();
	test_chunks_skipped_and_order();
	test_refusals();
	test_too_long_refused_before_reading();
}
