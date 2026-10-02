# WAV import: implementation plan (plan 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File > Import WAV: analyse a recording into a Klatt parameter document (F0, voicing, F1-F4, B1-B3, Rd,
AV, AH, AF and A2-A6) that resynthesizes it through DLL 1.1's LF source.

**Architecture:** A new analysis core in `kpe/`, plain C with no Win32 code, linked into the test runner and the
editor like `model.c`: a WAV reader, a resampler, Burg LPC with a polynomial root finder, YIN pitch with a Viterbi
pass, a Viterbi formant tracker, and the pipeline (`wavimport.c`) that classifies frames, fits Rd and calibrates the
levels by analysis-by-synthesis through the DLL. The editor adds the menu command and an options dialog. The DLL is
not changed.

**Tech Stack:** C (gnu11), winlibs GCC 14.2 (i686 and x86_64), GNU make run from Git Bash, Win32 for the editor,
PowerShell for the UI script.

**Spec:** `docs/superpowers/specs/2026-09-29-voice-source-and-wav-import-design.md`, section 4 (Part B) and the
Part B items of section 5. Plan 4 (DLL 1.1) is done; this plan builds on it.

**Status of the code below:** every task was built and run in order before the plan was written, each on the
previous task's result, starting from the commit that added this plan's spec changes. Expected check counts on both
architectures: Task 1: 977, Task 2: 1024, Task 3: 1151, Task 4: 1309, Task 5: 1421, Task 6: 1635, Task 7: 1635.
The 32- and 64-bit outputs were bit-identical after every task, and from Task 6 on that comparison includes two
complete imports. The build had zero warnings after every task. The thresholds in `wavimport.c` were tuned on the
synthetic corpus of `tests/test_wavimport.c` and on the user's recordings; their comments give the measured ranges.
`tools/ui/check_main.ps1`, `tools/ui/check_edit.ps1` and `tools/ui/check_import.ps1` passed after Task 7.

## Global Constraints

- Repository root: `C:\Users\abart\Desktop\KPE\kpe_open` (git, branch as set up by the controller).
- Shell: Git Bash. All commands run from the repository root. `MAKE` means `/c/winlibs/mingw32/bin/make.exe`.
- Flags: `-std=gnu11 -O2 -Wall -Wextra` (from the Makefile). The build must produce **zero warnings**.
- The Makefile picks up every `kpe/*.c` and `tests/*.c` by wildcard: new files need no Makefile change.
- The analysis core (`wavin`, `resample`, `lpc`, `pitch`, `track`, `wavimport`) has no Win32 code; it uses the DLL
  only to synthesize. `klatt/` is not changed.
- Clean room: Burg's method is written from the published recursion (Burg 1967; Andersen 1974), YIN from
  de Cheveigné & Kawahara (2002). fextr's `burg_lpc` (translated from Numerical Recipes) is never read or ported.
  Never read `C:\klsyn\src` or `C:\Users\abart\Desktop\prose2000_reborn`.
- The user's recordings in `C:\Users\abart\Desktop\fextr_sept2026\wavs` are for manual checks only and are never
  committed.
- A 60 s WAV imports in at most 5 s on the 32-bit build (the speed test prints the time).
- Code style: tabs, K&R braces, short comments only where the code does not explain itself.
- Commit messages end with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File map

| File | Change |
|---|---|
| `kpe/wavin.c`, `kpe/wavin.h` | WAV reader (Task 1) |
| `kpe/resample.c`, `kpe/resample.h` | Kaiser-windowed sinc resampler (Task 2) |
| `kpe/lpc.c`, `kpe/lpc.h` | pre-emphasis and window, Burg LPC, Aberth-Ehrlich roots, formant candidates (Task 3) |
| `kpe/pitch.c`, `kpe/pitch.h` | YIN per frame, Viterbi per voiced run, hysteresis, smoothing (Task 4; low-pass and speed-ups in Task 6) |
| `kpe/track.c`, `kpe/track.h` | Viterbi formant tracker F1-F4, B1-B3 smoothing (Task 5) |
| `kpe/wavimport.c`, `kpe/wavimport.h` | the import pipeline (Task 6) |
| `kpe/spectro.c`, `kpe/spectro.h` | the FFT made public for the pipeline (Task 6) |
| `kpe/optimize.c`, `kpe/optimize.h` | Optimize's default tolerances moved into the core (Task 6) |
| `kpe/dialogs.c` | uses those defaults (Task 6); the Import WAV dialog (Task 7) |
| `kpe/main.c`, `kpe/app.h`, `kpe/kpe.rc`, `kpe/resource.h` | File > Import WAV (Task 7) |
| `tests/test_wavin.c`, `test_resample.c`, `test_lpc.c`, `test_pitch.c`, `test_track.c`, `test_wavimport.c` | new suites |
| `tests/run_tests.c` | registers them |
| `tests/test_xarch.c` | the 32/64-bit comparison also runs two imports (Task 6) |
| `tools/ui/check_import.ps1` | UI script for the command (Task 7) |
| `docs/manual-checklist.md`, `README.md`, base spec | documentation (Task 7) |

---

### Task 1: WAV reader

**Files:**
- Create: `kpe/wavin.h`, `kpe/wavin.c`, `tests/test_wavin.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Produces (`kpe/wavin.h`): `wav_data` (`double *x; long n; int sr, channels, bits, is_float;`),
  `int wav_read(const wchar_t *path, double max_seconds, wav_data *out, char *msg, size_t msglen)` (0, or -1 with a
  one-sentence UTF-8 reason naming the format), `void wav_data_free(wav_data *w)`. Task 6 uses both.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_wavin.c`:

```c
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
```

In `tests/run_tests.c`, replace:

```c
void suite_xarch(void);

```

with:

```c
void suite_xarch(void);
void suite_wavin(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "xarch", suite_xarch },
};
```

with:

```c
	{ "xarch", suite_xarch },
	{ "wavin", suite_wavin },
};
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_wavin.c:7:10: fatal error: wavin.h: No such file or directory`.

- [ ] **Step 3: Write the reader**

Create `kpe/wavin.h`:

```c
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
```

Create `kpe/wavin.c`:

```c
/* wavin.c - see wavin.h. */
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
	if (channels == 0 || sr == 0) {
		fclose(f);
		return fail(msg, msglen, "Unsupported WAV format: %u channels at %lu Hz.", channels, (unsigned long)sr);
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
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `977 checks, 0 failures` twice, then `xarch: 33074 samples, max difference 0 LSB: ok`.

- [ ] **Step 5: Commit**

```bash
git add kpe/wavin.h kpe/wavin.c tests/test_wavin.c tests/run_tests.c
git commit -m "Add a WAV reader for the import

RIFF/WAVE with PCM 8/16/24/32-bit and IEEE float 32-bit, plain or
WAVE_FORMAT_EXTENSIBLE; channels averaged to mono; refusals name the format.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Resampler

**Files:**
- Create: `kpe/resample.h`, `kpe/resample.c`, `tests/test_resample.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Produces (`kpe/resample.h`): `long resample_length(long n, int sr_in, int sr_out)`,
  `int resample(const double *x, long n, int sr_in, int sr_out, double **out)` (0, or -1; the caller frees `*out`).
  Task 6 uses both.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_resample.c`:

```c
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "resample.h"

#define PI 3.14159265358979323846

static double *tone(long n, double sr, double f, double amp)
{
	double *x = malloc(sizeof(double) * n);
	for (long i = 0; i < n; i++)
		x[i] = amp * sin(2 * PI * f * i / sr);
	return x;
}

/* RMS over the middle of x, leaving out a tenth at each end (where the filter meets the zero padding). */
static double mid_rms(const double *x, long n)
{
	double s = 0;
	long a = n / 10, b = n - n / 10;
	for (long i = a; i < b; i++)
		s += x[i] * x[i];
	return sqrt(s / (b - a));
}

/* Power of the sinusoid at exactly f over the same middle part (Goertzel), as its RMS. */
static double mid_rms_at(const double *x, long n, double sr, double f)
{
	double re = 0, im = 0;
	long a = n / 10, b = n - n / 10;
	for (long i = a; i < b; i++) {
		re += x[i] * cos(2 * PI * f * i / sr);
		im -= x[i] * sin(2 * PI * f * i / sr);
	}
	return sqrt(2 * (re * re + im * im)) / (b - a);
}

static double db(double v, double ref)
{
	return 20 * log10(v / ref);
}

/* A tone of f Hz resampled from sr_in to sr_out: returns its level change in dB, and checks it stays at f. */
static double tone_gain_db(int sr_in, int sr_out, double f)
{
	long n = sr_in; /* one second */
	double *x = tone(n, sr_in, f, 0.5), *y = NULL;
	CHECK(resample(x, n, sr_in, sr_out, &y) == 0);
	double g = -999;
	if (y) {
		long m = resample_length(n, sr_in, sr_out);
		g = db(mid_rms(y, m), 0.5 / sqrt(2));
		/* all of the output's power is at f: the tone kept its frequency */
		CHECK_NEAR(db(mid_rms_at(y, m, sr_out, f), mid_rms(y, m)), 0, 0.05);
	}
	free(x);
	free(y);
	return g;
}

static void test_lengths(void)
{
	CHECK(resample_length(48000, 48000, 11025) == 11025);
	CHECK(resample_length(1000, 44100, 11025) == 250);
	CHECK(resample_length(7, 3, 2) == 5);  /* 4.67 rounds up */
	CHECK(resample_length(10, 3, 2) == 7); /* 6.67 */
	CHECK(resample_length(0, 48000, 11025) == 0);
	CHECK(resample_length(3000000, 48000, 20000) == 1250000); /* no overflow */
}

static void test_equal_rates_copy_exactly(void)
{
	double *x = tone(5000, 11025, 440, 0.7), *y = NULL;
	x[17] = 0.123456789;
	CHECK(resample(x, 5000, 11025, 11025, &y) == 0);
	CHECK(y && memcmp(x, y, sizeof(double) * 5000) == 0);
	free(x);
	free(y);
}

static void test_tones_keep_level(void)
{
	CHECK_NEAR(tone_gain_db(48000, 11025, 1000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(48000, 11025, 0.9 * 5512.5), 0, 0.1); /* the passband edge */
	CHECK_NEAR(tone_gain_db(44100, 11025, 3000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(16000, 11025, 3000), 0, 0.1);
	CHECK_NEAR(tone_gain_db(16000, 11025, 4961), 0, 0.1);
	CHECK_NEAR(tone_gain_db(22050, 11025, 200), 0, 0.1);
	CHECK_NEAR(tone_gain_db(11025, 16000, 4000), 0, 0.1);         /* upsampling */
	CHECK_NEAR(tone_gain_db(8000, 11025, 0.9 * 4000), 0, 0.1);    /* upsampling: the lower Nyquist is the input's */
}

/* Above the new Nyquist frequency: at least 60 dB down (whatever frequency it aliases to). */
static void test_images_rejected(void)
{
	const struct { int sr_in, sr_out; double f; } c[] = {
		{ 48000, 11025, 5600 }, { 48000, 11025, 6000 }, { 48000, 11025, 15000 }, { 44100, 11025, 21000 },
		{ 16000, 11025, 5700 }, { 16000, 11025, 7900 },
	};
	for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
		long n = c[i].sr_in;
		double *x = tone(n, c[i].sr_in, c[i].f, 0.5), *y = NULL;
		CHECK(resample(x, n, c[i].sr_in, c[i].sr_out, &y) == 0);
		if (y) {
			double g = db(mid_rms(y, resample_length(n, c[i].sr_in, c[i].sr_out)) + 1e-300, 0.5 / sqrt(2));
			if (!(g <= -60))
				printf("  %d -> %d, %g Hz: %.1f dB\n", c[i].sr_in, c[i].sr_out, c[i].f, g);
			CHECK(g <= -60);
		}
		free(x);
		free(y);
	}
}

static void test_bad_arguments(void)
{
	double x[4] = { 0 }, *y = x;
	CHECK(resample(x, 4, 0, 11025, &y) == -1 && y == NULL);
	CHECK(resample(x, 4, 11025, -1, &y) == -1);
	CHECK(resample(x, -1, 11025, 11025, &y) == -1);
}

void suite_resample(void)
{
	test_lengths();
	test_equal_rates_copy_exactly();
	test_tones_keep_level();
	test_images_rejected();
	test_bad_arguments();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_wavin(void);

```

with:

```c
void suite_wavin(void);
void suite_resample(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "wavin", suite_wavin },
};
```

with:

```c
	{ "wavin", suite_wavin },
	{ "resample", suite_resample },
};
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_resample.c:5:10: fatal error: resample.h: No such file or directory`.

- [ ] **Step 3: Write the resampler**

Create `kpe/resample.h`:

```c
/* resample.h - sample-rate conversion by a Kaiser-windowed sinc, any ratio. */
#ifndef KPE_RESAMPLE_H
#define KPE_RESAMPLE_H

/* The output length for n input samples: n * sr_out / sr_in, rounded to the nearest integer. */
long resample_length(long n, int sr_in, int sr_out);

/* Resamples x (n samples at sr_in Hz) to sr_out Hz into a new malloc'd array *out of resample_length(n, sr_in,
 * sr_out) samples (the caller frees it). Output sample j sits at input time j * sr_in / sr_out. Passband flat
 * within 0.1 dB up to 0.9 x the lower of the two Nyquist frequencies, stopband at least 60 dB down from that
 * Nyquist frequency up; samples beyond the ends count as zero. Equal rates copy x exactly.
 * Returns 0, or -1 for bad arguments or when out of memory (*out is then NULL). */
int resample(const double *x, long n, int sr_in, int sr_out, double **out);

#endif
```

Create `kpe/resample.c`:

```c
/* resample.c - see resample.h.
 *
 * The prototype low-pass is a sinc cut off at RS_CUTOFF of the lower Nyquist frequency, windowed by a Kaiser
 * window (Kaiser's design formulas for the attenuation RS_ATTEN_DB and a transition from 0.9 to 1.0 of the lower
 * Nyquist frequency give its length). It is tabulated once per call, in steps of 1/RS_TABLE_RES of a lower-rate
 * sample, and read with linear interpolation; for downsampling it is stretched by the rate ratio. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "resample.h"

#define PI 3.14159265358979323846

#define RS_ATTEN_DB 70.0   /* design stopband attenuation: 10 dB above the 60 dB required, as margin */
#define RS_CUTOFF 0.95     /* cutoff / lower Nyquist: the middle of the 0.9-1.0 transition band */
#define RS_TRANSITION 0.05 /* transition width in cycles per lower-rate sample (0.9 to 1.0 of Nyquist) */
#define RS_TABLE_RES 1024  /* table steps per lower-rate sample: interpolation error far below -70 dB */

long resample_length(long n, int sr_in, int sr_out)
{
	if (n <= 0 || sr_in <= 0 || sr_out <= 0)
		return 0;
	return (long)(((long long)n * sr_out * 2 + sr_in) / (2LL * sr_in));
}

/* Modified Bessel function of the first kind, order 0 (power series). */
static double bessel_i0(double x)
{
	double sum = 1, term = 1, q = x * x / 4;
	for (int k = 1; k < 200 && term > 1e-17 * sum; k++) {
		term *= q / ((double)k * k);
		sum += term;
	}
	return sum;
}

int resample(const double *x, long n, int sr_in, int sr_out, double **out)
{
	*out = NULL;
	if (n < 0 || sr_in <= 0 || sr_out <= 0 || (n > 0 && !x))
		return -1;
	long m = resample_length(n, sr_in, sr_out);
	double *y = malloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
	if (!y)
		return -1;
	if (sr_in == sr_out) {
		if (n > 0)
			memcpy(y, x, sizeof(double) * (size_t)n);
		*out = y;
		return 0;
	}
	/* Kaiser's formulas: beta for the attenuation, length for the transition width */
	double beta = 0.1102 * (RS_ATTEN_DB - 8.7);
	int half = (int)ceil((RS_ATTEN_DB - 7.95) / (2.285 * 2 * PI * RS_TRANSITION) / 2); /* lower-rate samples */
	int tn = half * RS_TABLE_RES + 2;
	double *tab = malloc(sizeof(double) * tn);
	if (!tab) {
		free(y);
		return -1;
	}
	double fc = 0.5 * RS_CUTOFF, i0b = bessel_i0(beta); /* cutoff in cycles per lower-rate sample */
	for (int i = 0; i < tn; i++) {
		double u = (double)i / RS_TABLE_RES, r = u / half;
		double sinc = u == 0 ? 1 : sin(2 * PI * fc * u) / (2 * PI * fc * u);
		double w = r < 1 ? bessel_i0(beta * sqrt(1 - r * r)) / i0b : 0;
		tab[i] = 2 * fc * sinc * w;
	}
	double stretch = sr_in > sr_out ? (double)sr_in / sr_out : 1; /* input samples per lower-rate sample */
	double reach = half * stretch;
	for (long j = 0; j < m; j++) {
		double t = (double)j * sr_in / sr_out;
		long lo = (long)ceil(t - reach), hi = (long)floor(t + reach);
		if (lo < 0)
			lo = 0;
		if (hi > n - 1)
			hi = n - 1;
		double acc = 0;
		for (long i = lo; i <= hi; i++) {
			double pos = fabs(i - t) / stretch * RS_TABLE_RES;
			int k = (int)pos;
			if (k >= tn - 1)
				continue;
			double fr = pos - k;
			acc += x[i] * (tab[k] + fr * (tab[k + 1] - tab[k]));
		}
		y[j] = acc / stretch;
	}
	free(tab);
	*out = y;
	return 0;
}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `1024 checks, 0 failures` twice, then `xarch: 33074 samples, max difference 0 LSB: ok`.

- [ ] **Step 5: Commit**

```bash
git add kpe/resample.h kpe/resample.c tests/test_resample.c tests/run_tests.c
git commit -m "Add a windowed-sinc resampler for the import

Kaiser-windowed sinc, any ratio: passband within 0.1 dB to 0.9 x the lower
Nyquist frequency, stopband at least 60 dB down; equal rates copy exactly.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: LPC, roots and formant candidates

**Files:**
- Create: `kpe/lpc.h`, `kpe/lpc.c`, `tests/test_lpc.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Produces (`kpe/lpc.h`): `lpc_cand` (`double f, bw;`), `CAND_MAX`, `LPC_MAX_ORDER`, `int lpc_order(int sr)`,
  `int lpc_window_len(int sr)`, `void lpc_frame(const double *x, long n, long centre, int len, double *out)`,
  `double lpc_burg(const double *x, int n, int order, double *a)`,
  `int poly_roots(const double *c, int deg, double *re, double *im)`,
  `int lpc_candidates(const double *a, int order, double sr, lpc_cand *out)`. Tasks 5 and 6 use them.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_lpc.c`:

```c
#include <math.h>
#include <stdlib.h>
#include "test.h"
#include "measure.h"
#include "klatt.h"
#include "lpc.h"

#define PI 3.14159265358979323846

/* Deterministic, roughly Gaussian noise: the sum of 12 uniform numbers from a 32-bit LCG, centred on zero. */
static double noise(unsigned *s)
{
	double v = 0;
	for (int i = 0; i < 12; i++) {
		*s = *s * 1664525u + 1013904223u;
		v += (*s >> 8) / 16777216.0;
	}
	return v - 6;
}

/* x[i] = e[i] - sum a[j] x[i-j]: the AR process whose prediction polynomial is a. */
static double *ar_process(const double *a, int p, int n)
{
	double *x = calloc(n, sizeof(double));
	unsigned s = 12345;
	for (int i = 0; i < n; i++) {
		double v = noise(&s);
		for (int j = 1; j <= p && j <= i; j++)
			v -= a[j] * x[i - j];
		x[i] = v;
	}
	return x;
}

/* A(z) from conjugate pole pairs (radius r[k], angle th[k]). */
static void poly_from_pairs(const double *r, const double *th, int pairs, double *a)
{
	a[0] = 1;
	for (int i = 1; i <= 2 * pairs; i++)
		a[i] = 0;
	for (int k = 0; k < pairs; k++) {
		double b1 = -2 * r[k] * cos(th[k]), b2 = r[k] * r[k];
		for (int i = 2 * k + 2; i >= 1; i--)
			a[i] += b1 * a[i - 1] + (i >= 2 ? b2 * a[i - 2] : 0);
	}
}

static void check_burg(const double *a, int p, int n)
{
	double *x = ar_process(a, p, n), est[LPC_MAX_ORDER + 1];
	CHECK(lpc_burg(x, n, p, est) > 0);
	CHECK_NEAR(est[0], 1, 0);
	for (int j = 1; j <= p; j++)
		CHECK_NEAR(est[j], a[j], 0.01 * fabs(a[j]));
	free(x);
}

static void test_burg_recovers_ar(void)
{
	double a2[3] = { 1, -1.6, 0.8 };
	check_burg(a2, 2, 20000);
	double r[2] = { 0.95, 0.9 }, th[2] = { 0.3, 1.2 }, a4[5];
	poly_from_pairs(r, th, 2, a4);
	check_burg(a4, 4, 20000);
}

static void test_burg_degenerate(void)
{
	double z[100] = { 0 }, a[5];
	CHECK_NEAR(lpc_burg(z, 100, 4, a), 0, 0); /* silence: no error, all coefficients zero */
	CHECK(a[0] == 1 && a[1] == 0 && a[4] == 0);
	CHECK(lpc_burg(z, 4, 4, a) == -1);
	CHECK(lpc_burg(z, 100, 0, a) == -1);
	CHECK(lpc_burg(z, 100, LPC_MAX_ORDER + 1, a) == -1);
}

/* Every expected root is found (to tol) exactly once. */
static void check_roots(const double *c, int deg, const double *wre, const double *wim, double tol)
{
	double re[LPC_MAX_ORDER], im[LPC_MAX_ORDER];
	int used[LPC_MAX_ORDER] = { 0 };
	CHECK(poly_roots(c, deg, re, im) == deg);
	for (int k = 0; k < deg; k++) {
		int best = -1;
		double bd = 1e300;
		for (int j = 0; j < deg; j++) {
			double d = hypot(re[j] - wre[k], im[j] - wim[k]);
			if (!used[j] && d < bd) {
				bd = d;
				best = j;
			}
		}
		if (best >= 0)
			used[best] = 1;
		CHECK(bd <= tol);
	}
}

static void test_roots(void)
{
	/* (z - 0.5)(z + 0.25)(z - 2) = z^3 - 2.25 z^2 + 0.375 z + 0.25 */
	double c3[4] = { 1, -2.25, 0.375, 0.25 }, r3[3] = { 0.5, -0.25, 2 }, i3[3] = { 0 };
	check_roots(c3, 3, r3, i3, 1e-12);
	/* 2 (z^2 + 1)(z - 3): leading coefficient not 1, roots on the unit circle */
	double c4[4] = { 2, -6, 2, -6 }, r4[3] = { 0, 0, 3 }, i4[3] = { 1, -1, 0 };
	check_roots(c4, 3, r4, i4, 1e-12);
	/* z^2 (z - 1): zero roots */
	double c5[4] = { 1, -1, 0, 0 }, r5[3] = { 0, 0, 1 }, i5[3] = { 0 };
	check_roots(c5, 3, r5, i5, 1e-12);
	/* (z - 1)^2 (z + 2): a double root converges more slowly and less precisely */
	double c6[4] = { 1, 0, -3, 2 }, r6[3] = { 1, 1, -2 }, i6[3] = { 0 };
	check_roots(c6, 3, r6, i6, 1e-6);
	/* degree 22 from eleven conjugate pairs near the unit circle, as LPC at 20 kHz gives */
	double rr[11], th[11], a[23], wre[22], wim[22];
	for (int k = 0; k < 11; k++) {
		rr[k] = 0.75 + 0.024 * k;
		th[k] = 0.1 + 0.27 * k;
		wre[2 * k] = wre[2 * k + 1] = rr[k] * cos(th[k]);
		wim[2 * k] = rr[k] * sin(th[k]);
		wim[2 * k + 1] = -wim[2 * k];
	}
	poly_from_pairs(rr, th, 11, a);
	check_roots(a, 22, wre, wim, 1e-9);
}

static void test_order_and_window(void)
{
	CHECK(lpc_order(11025) == 14);
	CHECK(lpc_order(16000) == 18);
	CHECK(lpc_order(8000) == 10);
	CHECK(lpc_order(12500) == 16); /* 12.5 rounds up to 14 */
	CHECK(lpc_order(20000) == 22);
	CHECK(lpc_window_len(11025) == 276); /* 275.6 */
	CHECK(lpc_window_len(16000) == 400);
}

static void test_frame_window(void)
{
	double x[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, w[5];
	lpc_frame(x, 10, 5, 5, w); /* samples 3..7 */
	CHECK_NEAR(w[0], 0, 1e-15);           /* the Hann window is zero at the ends */
	CHECK_NEAR(w[2], 1 - 0.97, 1e-15);    /* peak 1 at the centre, times the pre-emphasized 0.03 */
	CHECK_NEAR(w[1], 0.5 * 0.03, 1e-15);
	lpc_frame(x, 10, 0, 5, w);            /* samples -2..2: zeros before the start; x[0] has no predecessor */
	CHECK_NEAR(w[2], 1, 1e-15);
	CHECK_NEAR(w[1], 0, 0);
}

static void test_candidates_from_known_poly(void)
{
	double sr = 10000;
	/* pairs: 500 Hz/60 Hz, 1500 Hz/1200 Hz (too broad), 2500 Hz/r 0.6 (too close to 0), 60 Hz (too low),
	 * 4600 Hz (above 0.45 sr), 3300 Hz/200 Hz */
	double f[6] = { 500, 1500, 2500, 60, 4600, 3300 }, bw[6] = { 60, 1200, 0, 50, 100, 200 }, r[6], th[6], a[13];
	for (int k = 0; k < 6; k++) {
		r[k] = k == 2 ? 0.6 : exp(-PI * bw[k] / sr);
		th[k] = 2 * PI * f[k] / sr;
	}
	poly_from_pairs(r, th, 6, a);
	lpc_cand c[CAND_MAX];
	int n = lpc_candidates(a, 12, sr, c);
	CHECK(n == 2);
	if (n == 2) {
		CHECK_NEAR(c[0].f, 500, 1e-6);
		CHECK_NEAR(c[0].bw, 60, 1e-6);
		CHECK_NEAR(c[1].f, 3300, 1e-6);
		CHECK_NEAR(c[1].bw, 200, 1e-6);
	}
}

/* A steady synthesized vowel: its F1-F3 each have a candidate within 5 %. */
static void check_vowel(double f0, double f1, double f2, double f3)
{
	static short pcm[11025];
	static double x[11025];
	double g[KLATT_NUM_GLOBAL], fr[KLATT_NUM_FRAME], w[400], a[LPC_MAX_ORDER + 1];
	t_default_globals(g);
	t_default_frame(fr);
	g[KG_SRC] = 1;
	fr[KP_AV] = 60;
	fr[KP_F0] = f0;
	fr[KP_F1] = f1;
	fr[KP_F2] = f2;
	fr[KP_F3] = f3;
	int n = t_synth_steady(g, fr, 100, pcm, 11025);
	CHECK(n > 5000);
	for (int i = 0; i < n; i++)
		x[i] = pcm[i] / 32768.0;
	int len = lpc_window_len(11025), p = lpc_order(11025);
	lpc_frame(x, n, n / 2, len, w);
	CHECK(lpc_burg(w, len, p, a) > 0);
	lpc_cand c[CAND_MAX];
	int nc = lpc_candidates(a, p, 11025, c);
	double want[3] = { f1, f2, f3 };
	for (int k = 0; k < 3; k++) {
		double best = 1e9;
		for (int j = 0; j < nc; j++)
			if (fabs(c[j].f - want[k]) < fabs(best - want[k]))
				best = c[j].f;
		if (!(fabs(best - want[k]) <= 0.05 * want[k]))
			printf("  F%d = %g: nearest candidate %g\n", k + 1, want[k], best);
		CHECK(fabs(best - want[k]) <= 0.05 * want[k]);
	}
	for (int j = 0; j < nc; j++) {
		CHECK(c[j].f > CAND_MIN_HZ && c[j].f < CAND_MAX_FRAC * 11025 && c[j].bw < CAND_MAX_BW);
		if (j > 0)
			CHECK(c[j].f > c[j - 1].f);
	}
}

static void test_candidates_of_synthesized_vowels(void)
{
	check_vowel(120, 700, 1250, 2600); /* /a/ as in ba.csv */
	check_vowel(100, 300, 2300, 3000); /* /i/ */
	check_vowel(110, 350, 900, 2400);  /* /u/ */
	check_vowel(210, 850, 1650, 2900); /* female /ae/-like */
}

void suite_lpc(void)
{
	test_burg_recovers_ar();
	test_burg_degenerate();
	test_roots();
	test_order_and_window();
	test_frame_window();
	test_candidates_from_known_poly();
	test_candidates_of_synthesized_vowels();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_resample(void);

```

with:

```c
void suite_resample(void);
void suite_lpc(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "resample", suite_resample },
};
```

with:

```c
	{ "resample", suite_resample },
	{ "lpc", suite_lpc },
};
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_lpc.c:6:10: fatal error: lpc.h: No such file or directory`.

- [ ] **Step 3: Write the LPC module**

Create `kpe/lpc.h`:

```c
/* lpc.h - linear prediction by Burg's method, polynomial roots, and formant candidates from the roots. */
#ifndef KPE_LPC_H
#define KPE_LPC_H

#define LPC_MAX_ORDER 24     /* lpc_order(20000) = 22 */
#define LPC_PREEMPHASIS 0.97 /* x[i] - 0.97 x[i-1] before the window */
#define LPC_WINDOW_MS 25.0   /* Hann window length */

#define CAND_MIN_RADIUS 0.7  /* roots closer to the origin are too broad to be formants */
#define CAND_MIN_HZ 90.0     /* candidates must lie above this... */
#define CAND_MAX_FRAC 0.45   /* ...and below this fraction of the sample rate */
#define CAND_MAX_BW 1000.0   /* ...and be narrower than this */
#define CAND_MAX (LPC_MAX_ORDER / 2)

typedef struct {
	double f, bw; /* Hz */
} lpc_cand;

/* The order for a sample rate: 2 + sr/1000 rounded up to an even number (14 at 11025 Hz). */
int lpc_order(int sr);

/* The analysis window's length in samples at sr: LPC_WINDOW_MS, rounded. */
int lpc_window_len(int sr);

/* The analysis frame of len samples centred on sample centre of x (n samples): pre-emphasized by LPC_PREEMPHASIS
 * and Hann-windowed (symmetric, zero at both ends); samples beyond either end of x count as zero. */
void lpc_frame(const double *x, long n, long centre, int len, double *out);

/* Burg's method (Burg 1967, in Andersen's 1974 recursion): the prediction polynomial A(z) = 1 + a[1] z^-1 + ...
 * + a[order] z^-order (a[0] = 1) minimizing the summed forward and backward prediction error of x. Each stage's
 * reflection coefficient is k = -2 sum(f b) / sum(f^2 + b^2). A silent x (or a stage with no error left) gives
 * zeros from that stage on. Returns the final mean prediction error power, or -1 for bad arguments
 * (order < 1, order > LPC_MAX_ORDER, n <= order). */
double lpc_burg(const double *x, int n, int order, double *a);

/* All deg roots of c[0] z^deg + c[1] z^(deg-1) + ... + c[deg] (c[0] != 0, deg <= LPC_MAX_ORDER) by the
 * Aberth-Ehrlich simultaneous iteration, each root then polished by Newton's method on the full polynomial.
 * Returns deg, or -1 for bad arguments or if the iteration did not converge. */
int poly_roots(const double *c, int deg, double *re, double *im);

/* Formant candidates of A(z) (a[0..order], a[0] = 1) at sample rate sr: every root with a positive frequency and
 * radius >= CAND_MIN_RADIUS, as frequency and bandwidth, kept if CAND_MIN_HZ < f < CAND_MAX_FRAC * sr and
 * bw < CAND_MAX_BW; ascending by frequency. Returns the count (0 if the roots cannot be found). */
int lpc_candidates(const double *a, int order, double sr, lpc_cand *out);

#endif
```

Create `kpe/lpc.c`:

```c
/* lpc.c - see lpc.h. Written from the published algorithms: Burg (1967) in Andersen's (1974) recursion, and the
 * Aberth-Ehrlich iteration (Aberth 1973, Ehrlich 1967) for the roots. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lpc.h"

#define PI 3.14159265358979323846

#define ROOT_MAX_ITER 500  /* Aberth sweeps before giving up (a few dozen are typical) */
#define ROOT_TOL 1e-14     /* a sweep whose largest relative correction is below this has converged */
#define ROOT_POLISH 3      /* Newton steps per root on the full polynomial afterwards */

int lpc_order(int sr)
{
	int k = (sr + 999) / 1000; /* sr/1000 rounded up... */
	if (k & 1)
		k++;                   /* ...to an even number */
	return 2 + k;
}

int lpc_window_len(int sr)
{
	return (int)lround(LPC_WINDOW_MS * sr / 1000);
}

void lpc_frame(const double *x, long n, long centre, int len, double *out)
{
	long start = centre - len / 2;
	for (int i = 0; i < len; i++) {
		long s = start + i;
		double v = s >= 0 && s < n ? x[s] : 0;
		double prev = s - 1 >= 0 && s - 1 < n ? x[s - 1] : 0;
		double w = len > 1 ? 0.5 - 0.5 * cos(2 * PI * i / (len - 1)) : 1;
		out[i] = (v - LPC_PREEMPHASIS * prev) * w;
	}
}

double lpc_burg(const double *x, int n, int order, double *a)
{
	if (order < 1 || order > LPC_MAX_ORDER || n <= order || !x || !a)
		return -1;
	double *f = malloc(sizeof(double) * n * 2), *b = f + n;
	if (!f)
		return -1;
	memcpy(f, x, sizeof(double) * n); /* forward prediction errors of the current stage */
	memcpy(b, x, sizeof(double) * n); /* backward prediction errors */
	a[0] = 1;
	for (int i = 1; i <= order; i++)
		a[i] = 0;
	double err = 0;
	for (int i = 0; i < n; i++)
		err += x[i] * x[i];
	err /= n;
	for (int m = 1; m <= order; m++) {
		/* stage m pairs f[i] with b[i - 1] for i = m .. n-1 */
		double num = 0, den = 0;
		for (int i = m; i < n; i++) {
			num += f[i] * b[i - 1];
			den += f[i] * f[i] + b[i - 1] * b[i - 1];
		}
		if (!(den > 0))
			break; /* nothing left to predict: the remaining coefficients stay zero */
		double k = -2 * num / den;
		/* Levinson update of the polynomial: a_m[i] = a[i] + k a[m - i] */
		for (int i = 1, j = m - 1; i <= j; i++, j--) {
			double ai = a[i], aj = a[j];
			a[i] = ai + k * aj;
			if (i != j)
				a[j] = aj + k * ai;
		}
		a[m] = k;
		/* errors of stage m, from the top down so b[i - 1] is still stage m-1's */
		for (int i = n - 1; i >= m; i--) {
			double fi = f[i], bi = b[i - 1];
			f[i] = fi + k * bi;
			b[i] = bi + k * fi;
		}
		err *= 1 - k * k;
	}
	free(f);
	return err;
}

typedef struct {
	double re, im;
} cplx;

static cplx c_sub(cplx a, cplx b) { return (cplx){ a.re - b.re, a.im - b.im }; }
static cplx c_mul(cplx a, cplx b) { return (cplx){ a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; }
static double c_abs2(cplx a) { return a.re * a.re + a.im * a.im; }

static cplx c_div(cplx a, cplx b)
{
	double d = c_abs2(b);
	return (cplx){ (a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d };
}

/* p(z) and p'(z) of the monic polynomial z^deg + c[1] z^(deg-1) + ... + c[deg] (Horner). */
static void horner(const double *c, int deg, cplx z, cplx *p, cplx *dp)
{
	cplx v = { 1, 0 }, d = { 0, 0 };
	for (int i = 1; i <= deg; i++) {
		d = c_mul(d, z);
		d.re += v.re;
		d.im += v.im;
		v = c_mul(v, z);
		v.re += c[i];
	}
	*p = v;
	*dp = d;
}

int poly_roots(const double *c0, int deg, double *re, double *im)
{
	if (deg < 1 || deg > LPC_MAX_ORDER || !(c0[0] != 0))
		return -1;
	double c[LPC_MAX_ORDER + 1];
	for (int i = 0; i <= deg; i++)
		c[i] = c0[i] / c0[0];
	int nz = 0; /* roots at exactly zero: trailing zero coefficients */
	while (deg - nz > 0 && c[deg - nz] == 0)
		nz++;
	int d = deg - nz;
	for (int i = 0; i < nz; i++)
		re[d + i] = im[d + i] = 0;
	if (d == 0)
		return deg;
	/* start on a circle whose radius is the geometric mean of the root magnitudes, at angles offset from the
	 * real axis so no start is real (real starts stay real for a real polynomial) */
	cplx z[LPC_MAX_ORDER];
	double rad = pow(fabs(c[d]), 1.0 / d);
	for (int k = 0; k < d; k++) {
		double ang = 2 * PI * k / d + 0.4;
		z[k] = (cplx){ rad * cos(ang), rad * sin(ang) };
	}
	int converged = 0;
	for (int it = 0; it < ROOT_MAX_ITER && !converged; it++) {
		double worst = 0;
		for (int k = 0; k < d; k++) {
			cplx p, dp;
			horner(c, d, z[k], &p, &dp);
			if (c_abs2(p) == 0)
				continue;
			cplx ratio = c_div(p, dp), sum = { 0, 0 };
			for (int j = 0; j < d; j++)
				if (j != k) {
					cplx inv = c_div((cplx){ 1, 0 }, c_sub(z[k], z[j]));
					sum.re += inv.re;
					sum.im += inv.im;
				}
			cplx den = c_sub((cplx){ 1, 0 }, c_mul(ratio, sum));
			cplx w = c_div(ratio, den);
			if (!isfinite(w.re) || !isfinite(w.im))
				w = ratio; /* a degenerate denominator: plain Newton step */
			z[k] = c_sub(z[k], w);
			double rel = sqrt(c_abs2(w) / (1 + c_abs2(z[k])));
			if (rel > worst)
				worst = rel;
		}
		converged = worst < ROOT_TOL;
	}
	if (!converged)
		return -1;
	for (int k = 0; k < d; k++) {
		for (int s = 0; s < ROOT_POLISH; s++) {
			cplx p, dp;
			horner(c, d, z[k], &p, &dp);
			if (c_abs2(dp) == 0 || c_abs2(p) == 0)
				break;
			z[k] = c_sub(z[k], c_div(p, dp));
		}
		re[k] = z[k].re;
		im[k] = z[k].im;
	}
	return deg;
}

int lpc_candidates(const double *a, int order, double sr, lpc_cand *out)
{
	double re[LPC_MAX_ORDER], im[LPC_MAX_ORDER];
	if (poly_roots(a, order, re, im) != order)
		return 0;
	int n = 0;
	for (int k = 0; k < order; k++) {
		double r = hypot(re[k], im[k]);
		if (!(im[k] > 0) || r < CAND_MIN_RADIUS)
			continue;
		double f = atan2(im[k], re[k]) * sr / (2 * PI), bw = -log(r) * sr / PI;
		if (f <= CAND_MIN_HZ || f >= CAND_MAX_FRAC * sr || bw >= CAND_MAX_BW)
			continue;
		int i = n++; /* insertion sort by frequency */
		while (i > 0 && out[i - 1].f > f) {
			out[i] = out[i - 1];
			i--;
		}
		out[i] = (lpc_cand){ f, bw };
	}
	return n;
}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `1151 checks, 0 failures` twice, then `xarch: 33074 samples, max difference 0 LSB: ok`.

- [ ] **Step 5: Commit**

```bash
git add kpe/lpc.h kpe/lpc.c tests/test_lpc.c tests/run_tests.c
git commit -m "Add Burg LPC, a polynomial root finder and formant candidates

Burg's method from the published recursion, Aberth-Ehrlich roots with
Newton polishing, candidates per spec 4.3 step 2.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Pitch

**Files:**
- Create: `kpe/pitch.h`, `kpe/pitch.c`, `tests/test_pitch.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Produces (`kpe/pitch.h`): `pitch_frame`, `PITCH_NCAND`,
  `void pitch_yin_frame(const double *x, long n, int sr, long centre, double fmin, double fmax, pitch_frame *out)`,
  `int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
  double *f0, double *ap, unsigned char *voiced)`. Task 6 adds the 1.5 kHz low-pass inside `pitch_track` and
  changes `pitch_yin_frame`'s internals for speed; the signatures stay.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_pitch.c`:

```c
#include <math.h>
#include <stdlib.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "synthrun.h"
#include "pitch.h"

/* A document of nframes frames at defaults with SRC = 1; the caller sets the frames. */
static void new_doc(kpe_doc *d, int nframes)
{
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = 1;
	CHECK(doc_new(d, g, nframes * 5.0) == 0);
}

static void set(kpe_doc *d, int k, int p, double v)
{
	d->frames[(size_t)k * NP + p] = v;
}

/* Synthesizes d and runs pitch_track with frames centred like the import's; the arrays have d->nframes entries. */
static void track(const kpe_doc *d, double fmin, double fmax, double *f0, double *ap, unsigned char *voiced,
	double **xout, long *nout)
{
	synth_result s;
	CHECK(synth_doc(d, &s) == 0);
	double *x = malloc(sizeof(double) * s.n);
	long *c = malloc(sizeof(long) * d->nframes);
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	for (int k = 0; k < d->nframes; k++)
		c[k] = (synth_frame_start(d, k) + synth_frame_start(d, k + 1)) / 2;
	CHECK(pitch_track(x, s.n, s.sr, c, d->nframes, fmin, fmax, f0, ap, voiced) == 0);
	free(c);
	if (xout) {
		*xout = x;
		*nout = s.n;
	} else {
		free(x);
	}
	synth_result_free(&s);
}

static void test_steady_vowel(void)
{
	kpe_doc d;
	new_doc(&d, 100);
	for (int k = 0; k < 100; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 120);
		set(&d, k, KP_F1, 700);
		set(&d, k, KP_F2, 1250);
		set(&d, k, KP_F3, 2600);
	}
	double f0[100], ap[100];
	unsigned char v[100];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	int bad = 0;
	for (int k = 5; k < 95; k++)
		bad += !v[k] || fabs(f0[k] - 120) > 1.2 || ap[k] > 0.2;
	CHECK(bad == 0);
	CHECK_NEAR(f0[50], 120, 1.2);
	doc_free(&d);
}

static void test_glide(void)
{
	kpe_doc d;
	new_doc(&d, 200);
	for (int k = 0; k < 200; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 90 + 110.0 * k / 199);
	}
	double f0[200], ap[200];
	unsigned char v[200];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	double worst = 0;
	for (int k = 5; k < 195; k++) {
		double want = 90 + 110.0 * k / 199, e = v[k] ? fabs(f0[k] - want) / want : 1;
		if (e > worst)
			worst = e;
	}
	if (!(worst < 0.02))
		printf("  glide: worst F0 error %.1f %%\n", 100 * worst);
	CHECK(worst < 0.02);
	doc_free(&d);

	new_doc(&d, 120); /* a female-range fall with the female search range */
	for (int k = 0; k < 120; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 330 - 130.0 * k / 119);
		set(&d, k, KP_F1, 850);
		set(&d, k, KP_F2, 1650);
		set(&d, k, KP_F3, 2900);
	}
	track(&d, 120, 400, f0, ap, v, NULL, NULL);
	worst = 0;
	for (int k = 5; k < 115; k++) {
		double want = 330 - 130.0 * k / 119, e = v[k] ? fabs(f0[k] - want) / want : 1;
		if (e > worst)
			worst = e;
	}
	if (!(worst < 0.02))
		printf("  female fall: worst F0 error %.1f %%\n", 100 * worst);
	CHECK(worst < 0.02);
	doc_free(&d);
}

/* silence, vowel, voiceless fricative, vowel, aspiration, silence: 30 frames each */
static void test_voicing_sequence(void)
{
	enum { SEG = 30, N = 6 * SEG };
	kpe_doc d;
	new_doc(&d, N);
	for (int k = 0; k < N; k++) {
		int seg = k / SEG;
		set(&d, k, KP_F0, 110);
		set(&d, k, KP_AV, seg == 1 || seg == 3 ? 60 : 0);
		set(&d, k, KP_AF, seg == 2 ? 60 : 0);
		set(&d, k, KP_A5, 50);
		set(&d, k, KP_A6, 55);
		set(&d, k, KP_AH, seg == 4 ? 55 : 0);
	}
	double f0[N], ap[N];
	unsigned char v[N];
	track(&d, 60, 250, f0, ap, v, NULL, NULL);
	int wrong = 0, counted = 0;
	for (int k = 0; k < N; k++) {
		int pos = k % SEG;
		if (pos < 4 || pos >= SEG - 4) /* at least 20 ms (half the YIN window) from a boundary */
			continue;
		int seg = k / SEG, want = seg == 1 || seg == 3;
		counted++;
		if (v[k] != want) {
			wrong++;
			printf("  frame %d (segment %d): voiced %d, ap %.2f\n", k, seg, v[k], ap[k]);
		}
		if (want)
			CHECK_NEAR(f0[k], 110, 2.2);
		else
			CHECK(f0[k] == 0);
	}
	CHECK(counted > 100 && wrong == 0);
	doc_free(&d);
}

/* Diplophonia makes every second pulse weaker and later, so the signal really repeats every two periods: in
 * those 60 ms the deepest dip is an octave down (DI = 10 already does it; from about DI = 20 the true period is
 * no longer a dip at all), and only the frames around them say otherwise. */
static void test_octave_trap(void)
{
	enum { N = 120 };
	kpe_doc d;
	new_doc(&d, N);
	for (int k = 0; k < N; k++) {
		set(&d, k, KP_AV, 60);
		set(&d, k, KP_F0, 120);
		set(&d, k, KP_DI, k >= 54 && k < 66 ? 10 : 0);
	}
	double f0[N], ap[N], *x;
	long n;
	unsigned char v[N];
	track(&d, 60, 250, f0, ap, v, &x, &n);
	int trapped = 0;
	for (int k = 56; k < 64; k++) {
		pitch_frame pf;
		long c = (synth_frame_start(&d, k) + synth_frame_start(&d, k + 1)) / 2;
		pitch_yin_frame(x, n, 11025, c, 60, 250, &pf);
		trapped += pf.ncand > 0 && fabs(pf.f0[0] - 60) < 3; /* the best single-frame candidate: an octave down */
	}
	if (trapped < 6)
		printf("  only %d of 8 frames trap a single-frame choice\n", trapped);
	CHECK(trapped >= 6);
	int bad = 0;
	for (int k = 5; k < N - 5; k++)
		bad += !v[k] || fabs(f0[k] - 120) > 2.4;
	if (bad)
		printf("  %d frames off 120 Hz\n", bad);
	CHECK(bad == 0);
	free(x);
	doc_free(&d);
}

static void test_single_frame_basics(void)
{
	static double x[4410];
	for (int i = 0; i < 4410; i++)
		x[i] = sin(2 * 3.14159265358979323846 * 150 * i / 11025.0);
	pitch_frame pf;
	pitch_yin_frame(x, 4410, 11025, 2205, 60, 250, &pf);
	CHECK(pf.voiced && pf.ncand >= 1);
	CHECK_NEAR(pf.yin_f0, 150, 0.3);
	CHECK(pf.ap < 0.05);
	for (int i = 0; i < 4410; i++)
		x[i] = 0;
	pitch_yin_frame(x, 4410, 11025, 2205, 60, 250, &pf);
	CHECK(!pf.voiced && pf.ap == 1);
}

void suite_pitch(void)
{
	test_single_frame_basics();
	test_steady_vowel();
	test_glide();
	test_voicing_sequence();
	test_octave_trap();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_lpc(void);

```

with:

```c
void suite_lpc(void);
void suite_pitch(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "lpc", suite_lpc },
};
```

with:

```c
	{ "lpc", suite_lpc },
	{ "pitch", suite_pitch },
};
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_pitch.c:7:10: fatal error: pitch.h: No such file or directory`.

- [ ] **Step 3: Write the pitch tracker**

Create `kpe/pitch.h`:

```c
/* pitch.h - F0, aperiodicity and voicing per frame: YIN (de Cheveigne & Kawahara 2002) with a dynamic-programming
 * pass over the whole signal against octave jumps, then smoothing. */
#ifndef KPE_PITCH_H
#define KPE_PITCH_H

#define PITCH_WINDOW_MS 40.0 /* the span each frame's difference function looks at, centred on the frame */
#define PITCH_NCAND 4        /* candidates kept per frame */

typedef struct {
	int ncand;
	double f0[PITCH_NCAND];  /* candidate F0s (Hz), best (lowest d) first */
	double d[PITCH_NCAND];   /* their cumulative-mean-normalized difference: 0 periodic, about 1 noise */
	double yin_f0;           /* YIN's own choice: the first dip below the threshold, else the lowest dip */
	double ap;               /* aperiodicity: the normalized difference at yin_f0, at most 1 */
	int voiced;              /* a dip fell below the threshold */
} pitch_frame;

/* YIN on the PITCH_WINDOW_MS of x (n samples at sr) centred on sample centre, searching fmin..fmax. Samples
 * beyond the ends count as zero. A frame without any dip in range gets ncand 0, ap 1, not voiced. */
void pitch_yin_frame(const double *x, long n, int sr, long centre, double fmin, double fmax, pitch_frame *out);

/* The whole signal: nframes frames centred on centres[k]. Fills f0[k] (Hz, smoothed; 0 where unvoiced), ap[k] and
 * voiced[k]. Returns 0, or -1 when out of memory. */
int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
	double *f0, double *ap, unsigned char *voiced);

#endif
```

Create `kpe/pitch.c`:

```c
/* pitch.c - see pitch.h. YIN steps 1-5 as published: difference function over a fixed integration window,
 * cumulative mean normalization, absolute threshold, parabolic interpolation. The dynamic-programming pass
 * replaces YIN's step 6 (best local estimate) with a Viterbi search over each voiced run. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pitch.h"

#define PI 3.14159265358979323846

#define YIN_THRESHOLD 0.2    /* YIN's absolute threshold; the value the fextr probe used (1.4 Hz median error) */
#define PITCH_LAG_COST 0.15  /* DP cost per octave below fmax: a preference for the shorter period, as YIN's
                              * threshold rule has, since a periodic signal also repeats at 2, 3... periods. Tuned on
                              * synthesized voices: at 330 Hz the whole-sample pulse timing of the DLL makes the dip
                              * at 2 periods up to 0.08 deeper than the true one; 0.15 keeps the true octave there */
#define PITCH_FLOOR_DB 40.0  /* frames this far below the loudest are unvoiced: the ringing of a resonance
                              * decaying after a sound is periodic too (tuned: 25 ms after an aspiration it is still
                              * voiced by YIN at 43 dB down) */
#define PITCH_JUMP_COST 1.0  /* DP cost per octave of F0 change between neighbouring frames: an octave jump
                              * must be backed by a clearly deeper dip in several frames */
#define PITCH_HOLD 0.4       /* hysteresis: a frame next to a voiced one stays voiced while its aperiodicity is below
                              * this (the threshold starts voicing; transitions, e.g. into diplophonia, reach 0.25) */
#define PITCH_GATE_MS 10.0   /* the window whose energy PITCH_FLOOR_DB applies to */
#define PITCH_MEDIAN 5       /* frames in the median that smooths each voiced run's F0 */

#define MAX_LAG 1024         /* longest lag: sr / fmin = 20000 / 60 = 334 fits */
#define INTERP_HALF 8        /* windowed-sinc taps each side for the difference at a fractional lag */
#define REFINE_MAX 8         /* dips (the deepest at whole lags) evaluated at their fractional lag */

/* The difference function at the fractional lag tau over w samples of buf (which has INTERP_HALF samples of
 * margin before index 0 and after w + tau): x[j + tau] comes from a Hann-windowed sinc interpolation. Evaluating
 * the dip where it really is matters because the difference at the nearest whole lag of a period that falls
 * between samples can be large for a bright voice (0.15 instead of 0 at F0 = 250 Hz, 11025 Hz). */
static double frac_difference(const double *buf, int w, double tau)
{
	int ti = (int)floor(tau);
	double fr = tau - ti, h[2 * INTERP_HALF];
	for (int m = 0; m < 2 * INTERP_HALF; m++) {
		double u = m - (INTERP_HALF - 1) - fr; /* tap offset from the fractional position */
		double sinc = fabs(u) < 1e-12 ? 1 : sin(PI * u) / (PI * u);
		h[m] = sinc * (0.5 + 0.5 * cos(PI * u / INTERP_HALF));
	}
	double d = 0;
	for (int j = 0; j < w; j++) {
		const double *p = buf + j + ti - (INTERP_HALF - 1);
		double v = 0;
		for (int m = 0; m < 2 * INTERP_HALF; m++)
			v += h[m] * p[m];
		double e = buf[j] - v;
		d += e * e;
	}
	return d;
}

void pitch_yin_frame(const double *x, long n, int sr, long centre, double fmin, double fmax, pitch_frame *out)
{
	memset(out, 0, sizeof *out);
	out->ap = 1;
	int span = (int)lround(PITCH_WINDOW_MS * sr / 1000);
	int tmin = (int)floor(sr / fmax), tmax = (int)ceil(sr / fmin);
	if (tmin < 2)
		tmin = 2;
	if (tmax + 2 >= MAX_LAG)
		tmax = MAX_LAG - 3;
	if (tmax <= tmin)
		return;
	/* fixed integration window of one longest period (every lag compares the same w samples), so a gliding F0
	 * drifts little within it; shortened if the span cannot hold it and the longest lag */
	int w = tmax + 1;
	if (w > span - (tmax + 1))
		w = span - (tmax + 1);
	if (w < tmin)
		w = tmin;
	long start = centre - (w + tmax + 1) / 2; /* the samples compared straddle the centre */
	/* the samples used, zero beyond the ends, with INTERP_HALF samples of margin on either side */
	static const int M = INTERP_HALF;
	double store[MAX_LAG * 2 + 2 * INTERP_HALF + 2], *buf = store + M;
	int total = w + tmax + 2;
	for (int i = -M; i < total + M; i++) {
		long s = start + i;
		buf[i] = s >= 0 && s < n ? x[s] : 0;
	}
	/* difference function and its cumulative mean normalization, lags 1..tmax+1 */
	double cm[MAX_LAG], mean[MAX_LAG], run = 0;
	cm[0] = mean[0] = 1;
	for (int tau = 1; tau <= tmax + 1; tau++) {
		double d = 0;
		for (int j = 0; j < w; j++) {
			double e = buf[j] - buf[j + tau];
			d += e * e;
		}
		run += d;
		mean[tau] = run / tau;
		cm[tau] = run > 0 ? d / mean[tau] : 1;
	}
	/* local minima in tmin..tmax */
	int lag[MAX_LAG / 2], nmin = 0;
	for (int tau = tmin; tau <= tmax; tau++)
		if (cm[tau] < 1 && cm[tau] <= cm[tau - 1] && cm[tau] < cm[tau + 1])
			lag[nmin++] = tau;
	if (nmin == 0)
		return;
	/* the REFINE_MAX deepest, in lag order */
	while (nmin > REFINE_MAX) {
		int worst = 0;
		for (int i = 1; i < nmin; i++)
			if (cm[lag[i]] > cm[lag[worst]])
				worst = i;
		memmove(lag + worst, lag + worst + 1, sizeof(int) * (nmin - worst - 1));
		nmin--;
	}
	/* each at its parabolically interpolated lag, with the difference evaluated there */
	double tf[REFINE_MAX], dv[REFINE_MAX];
	for (int i = 0; i < nmin; i++) {
		int t = lag[i];
		double a = cm[t - 1], b = cm[t], c = cm[t + 1], den = a - 2 * b + c;
		double shift = den > 0 ? 0.5 * (a - c) / den : 0;
		if (shift > 0.5)
			shift = 0.5;
		if (shift < -0.5)
			shift = -0.5;
		tf[i] = t + shift;
		double m = shift >= 0 ? mean[t] + shift * (mean[t + 1] - mean[t]) : mean[t] + shift * (mean[t] - mean[t - 1]);
		dv[i] = m > 0 ? frac_difference(buf, w, tf[i]) / m : 1;
		if (dv[i] > b)
			dv[i] = b; /* never worse than the whole lag */
	}
	/* YIN's choice: the first dip below the threshold, else the deepest */
	int choice = -1;
	for (int i = 0; i < nmin && choice < 0; i++)
		if (dv[i] < YIN_THRESHOLD)
			choice = i;
	out->voiced = choice >= 0;
	if (choice < 0) {
		choice = 0;
		for (int i = 1; i < nmin; i++)
			if (dv[i] < dv[choice])
				choice = i;
	}
	/* keep the PITCH_NCAND deepest, making sure YIN's choice is among them */
	int order[REFINE_MAX];
	for (int i = 0; i < nmin; i++) {
		int j = i;
		while (j > 0 && dv[order[j - 1]] > dv[i]) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	int nc = nmin < PITCH_NCAND ? nmin : PITCH_NCAND, has_choice = 0;
	for (int i = 0; i < nc; i++)
		has_choice |= order[i] == choice;
	if (!has_choice)
		order[nc - 1] = choice;
	for (int i = 0; i < nc; i++) {
		out->f0[i] = sr / tf[order[i]];
		out->d[i] = dv[order[i]];
		if (order[i] == choice) {
			out->yin_f0 = out->f0[i];
			out->ap = out->d[i] < 1 ? out->d[i] : 1;
		}
	}
	out->ncand = nc;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Viterbi over frames a..b-1 (all voiced): writes the chosen candidate's F0 into f0. */
static int best_path(const pitch_frame *pf, int a, int b, double fmax, double *f0)
{
	int len = b - a;
	double *cost = malloc(sizeof(double) * len * PITCH_NCAND);
	int *back = malloc(sizeof(int) * len * PITCH_NCAND);
	if (!cost || !back) {
		free(cost);
		free(back);
		return -1;
	}
	for (int t = 0; t < len; t++) {
		const pitch_frame *p = &pf[a + t];
		for (int i = 0; i < p->ncand; i++) {
			double local = p->d[i] + PITCH_LAG_COST * log2(fmax / p->f0[i]);
			double best = 0;
			int bi = 0;
			if (t > 0) {
				const pitch_frame *q = &pf[a + t - 1];
				best = 1e300;
				for (int j = 0; j < q->ncand; j++) {
					double c = cost[(t - 1) * PITCH_NCAND + j] + PITCH_JUMP_COST * fabs(log2(p->f0[i] / q->f0[j]));
					if (c < best) {
						best = c;
						bi = j;
					}
				}
			}
			cost[t * PITCH_NCAND + i] = best + local;
			back[t * PITCH_NCAND + i] = bi;
		}
	}
	int s = 0;
	const pitch_frame *last = &pf[b - 1];
	for (int i = 1; i < last->ncand; i++)
		if (cost[(len - 1) * PITCH_NCAND + i] < cost[(len - 1) * PITCH_NCAND + s])
			s = i;
	for (int t = len - 1; t >= 0; t--) {
		f0[a + t] = pf[a + t].f0[s];
		s = back[t * PITCH_NCAND + s];
	}
	free(cost);
	free(back);
	return 0;
}

int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
	double *f0, double *ap, unsigned char *voiced)
{
	pitch_frame *pf = malloc(sizeof(pitch_frame) * (nframes > 0 ? nframes : 1));
	double *raw = malloc(sizeof(double) * (nframes > 0 ? nframes : 1));
	if (!pf || !raw) {
		free(pf);
		free(raw);
		return -1;
	}
	double top = 0;
	int span = (int)lround(PITCH_GATE_MS * sr / 1000);
	for (int k = 0; k < nframes; k++) {
		double e = 0;
		for (long i = centres[k] - span / 2; i < centres[k] - span / 2 + span; i++)
			if (i >= 0 && i < n)
				e += x[i] * x[i];
		raw[k] = e;
		if (e > top)
			top = e;
	}
	for (int k = 0; k < nframes; k++) {
		pitch_yin_frame(x, n, sr, centres[k], fmin, fmax, &pf[k]);
		if (!(raw[k] > top * pow(10, -PITCH_FLOOR_DB / 10))) {
			pf[k].voiced = 0;
			pf[k].ap = 1;
		}
		ap[k] = pf[k].ap;
		voiced[k] = (unsigned char)pf[k].voiced;
		f0[k] = raw[k] = 0;
	}
	/* hysteresis, both ways */
	for (int k = 1; k < nframes; k++)
		if (!voiced[k] && voiced[k - 1] && pf[k].ncand > 0 && ap[k] < PITCH_HOLD)
			voiced[k] = 1;
	for (int k = nframes - 2; k >= 0; k--)
		if (!voiced[k] && voiced[k + 1] && pf[k].ncand > 0 && ap[k] < PITCH_HOLD)
			voiced[k] = 1;
	int rc = 0;
	for (int a = 0; a < nframes && rc == 0;) {
		if (!voiced[a]) {
			a++;
			continue;
		}
		int b = a;
		while (b < nframes && voiced[b])
			b++;
		rc = best_path(pf, a, b, fmax, raw);
		/* median smoothing inside the run (the window shrinks at its ends) */
		for (int k = a; k < b && rc == 0; k++) {
			double v[PITCH_MEDIAN];
			int m = 0;
			for (int j = k - PITCH_MEDIAN / 2; j <= k + PITCH_MEDIAN / 2; j++)
				if (j >= a && j < b)
					v[m++] = raw[j];
			qsort(v, m, sizeof(double), cmp_double);
			f0[k] = m & 1 ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]);
		}
		a = b;
	}
	free(pf);
	free(raw);
	return rc;
}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `1309 checks, 0 failures` twice, then `xarch: 33074 samples, max difference 0 LSB: ok`.

- [ ] **Step 5: Commit**

```bash
git add kpe/pitch.h kpe/pitch.c tests/test_pitch.c tests/run_tests.c
git commit -m "Add YIN pitch tracking for the import

YIN per frame (one-period integration window centred on the frame, dips
evaluated at their fractional lag), a Viterbi pass per voiced run against
octave jumps, voicing hysteresis, an energy floor, and a 5-frame median.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Formant tracking

**Files:**
- Create: `kpe/track.h`, `kpe/track.c`, `tests/test_track.c`
- Modify: `tests/run_tests.c`

**Interfaces:**
- Consumes: `lpc_cand`, `CAND_MAX` (Task 3).
- Produces (`kpe/track.h`): `track_setup` (`ref`, `lo`, `hi` per track), `TRACK_N`,
  `int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, int nframes,
  const track_setup *setup, double *f, double *bw)`. Task 6 uses it.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_track.c`:

```c
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "synthrun.h"
#include "track.h"

enum { NF = 60 };

static lpc_cand cands[NF * CAND_MAX];
static int ncand[NF];
static unsigned char quiet[NF];
static double f[NF * TRACK_N], bw[NF * TRACK_N];

static const track_setup male = {
	{ 500, 1500, 2500, 3500 }, { 150, 500, 1300, 2500 }, { 1300, 3000, 4000, 4500 },
};

static void clear(void)
{
	memset(ncand, 0, sizeof ncand);
	memset(quiet, 0, sizeof quiet);
}

static void add(int k, double fr, double b)
{
	lpc_cand *c = cands + k * CAND_MAX;
	int i = ncand[k]++;
	while (i > 0 && c[i - 1].f > fr) {
		c[i] = c[i - 1];
		i--;
	}
	c[i] = (lpc_cand){ fr, b };
}

/* Frames a..b-1 get the four formants given (0: that one missing). */
static void formants(int a, int b, double f1, double f2, double f3, double f4)
{
	for (int k = a; k < b; k++) {
		if (f1) add(k, f1, 60);
		if (f2) add(k, f2, 80);
		if (f3) add(k, f3, 120);
		if (f4) add(k, f4, 200);
	}
}

static void test_clean(void)
{
	clear();
	formants(0, NF, 520, 1480, 2450, 3600);
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	int bad = 0;
	for (int k = 0; k < NF; k++)
		bad += f[k * 4] != 520 || f[k * 4 + 1] != 1480 || f[k * 4 + 2] != 2450 || f[k * 4 + 3] != 3600 ||
			bw[k * 4] != 60 || bw[k * 4 + 1] != 80 || bw[k * 4 + 2] != 120;
	CHECK(bad == 0);
}

static void test_missing_candidate(void)
{
	clear();
	for (int k = 0; k < NF; k++) /* F2 rises 1200 -> 1790, and has no candidate in frames 20-24 */
		formants(k, k + 1, 600, k >= 20 && k < 25 ? 0 : 1200 + 10 * k, 2500, 3500);
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	for (int k = 18; k < 27; k++) {
		CHECK_NEAR(f[k * 4 + 1], 1200 + 10 * k, 1e-9); /* interpolated across the gap */
		CHECK_NEAR(f[k * 4 + 2], 2500, 0);             /* F3 did not drop into F2's slot */
		CHECK_NEAR(f[k * 4], 600, 0);
	}
}

static void test_spurious_candidates(void)
{
	clear();
	formants(0, NF, 500, 1500, 2500, 3500);
	for (int k = 10; k < 14; k++)
		add(k, 1000, 90);  /* between F1 and F2 */
	add(30, 280, 150);     /* below F1 */
	add(40, 3000, 100);    /* between F3 and F4 */
	add(41, 3000, 100);
	for (int k = 50; k < 52; k++)
		add(k, 2000, 70);
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	int bad = 0;
	for (int k = 0; k < NF; k++)
		bad += f[k * 4] != 500 || f[k * 4 + 1] != 1500 || f[k * 4 + 2] != 2500 || f[k * 4 + 3] != 3500;
	CHECK(bad == 0);
}

static void test_silence_interpolates(void)
{
	clear();
	formants(0, 20, 400, 1800, 2600, 3400);
	formants(40, NF, 700, 1200, 2400, 3600);
	for (int k = 20; k < 40; k++) { /* quiet frames with junk candidates */
		quiet[k] = 1;
		add(k, 150 + 37 * k, 300);
		add(k, 2900, 50);
	}
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	for (int k = 19; k <= 40; k++) {
		double t = (k - 19) / 21.0;
		CHECK_NEAR(f[k * 4], 400 + t * 300, 1e-9);
		CHECK_NEAR(f[k * 4 + 1], 1800 - t * 600, 1e-9);
		CHECK_NEAR(f[k * 4 + 3], 3400 + t * 200, 1e-9);
	}
	/* all frames quiet or without candidates: the references */
	clear();
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	CHECK(f[0] == 500 && f[1] == 1500 && f[2] == 2500 && f[3] == 3500);
	CHECK(bw[0] >= 40 && bw[0] <= 500);
}

static void test_clamps_and_bandwidth_smoothing(void)
{
	clear();
	formants(0, NF, 500, 1500, 2500, 3500);
	for (int k = 0; k < NF; k++) {
		lpc_cand *c = cands + k * CAND_MAX;
		c[0].bw = k == 30 ? 900 : 70; /* one wild bandwidth */
		c[1].bw = 20;                 /* below the clamp */
		c[3].f = 4700;                /* above F4's range */
	}
	CHECK(track_formants(cands, ncand, quiet, NF, &male, f, bw) == 0);
	CHECK_NEAR(bw[30 * 4], 70, 0);
	CHECK_NEAR(bw[10 * 4 + 1], 40, 0);
	CHECK_NEAR(f[10 * 4 + 3], 4500, 0);
	track_setup low = male;
	low.hi[2] = 2400; /* e.g. 0.45 SR below F3's maximum */
	CHECK(track_formants(cands, ncand, quiet, NF, &low, f, bw) == 0);
	CHECK_NEAR(f[10 * 4 + 2], 2400, 0);
}

/* Synthesized F2 glide 1000 -> 2000 Hz under steady F1 and F3: LPC candidates, then the tracker. */
static void test_synthesized_glide(void)
{
	enum { N = 100 };
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = 1;
	kpe_doc d;
	CHECK(doc_new(&d, g, N * 5.0) == 0);
	for (int k = 0; k < N; k++) {
		double *fr = d.frames + (size_t)k * NP;
		fr[KP_AV] = 60;
		fr[KP_F0] = 110;
		fr[KP_F1] = 450;
		fr[KP_F2] = 1000 + 10.0 * k;
		fr[KP_F3] = 2500;
	}
	synth_result s;
	CHECK(synth_doc(&d, &s) == 0);
	double *x = malloc(sizeof(double) * s.n), w[400], a[LPC_MAX_ORDER + 1];
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	static lpc_cand gc[N * CAND_MAX];
	int gn[N], len = lpc_window_len(s.sr), p = lpc_order(s.sr);
	unsigned char gq[N] = { 0 };
	for (int k = 0; k < N; k++) {
		long c = (synth_frame_start(&d, k) + synth_frame_start(&d, k + 1)) / 2;
		lpc_frame(x, s.n, c, len, w);
		lpc_burg(w, len, p, a);
		gn[k] = lpc_candidates(a, p, s.sr, gc + k * CAND_MAX);
	}
	static double gf[N * TRACK_N], gb[N * TRACK_N];
	CHECK(track_formants(gc, gn, gq, N, &male, gf, gb) == 0);
	double worst[3] = { 0 };
	for (int k = 5; k < N - 5; k++) {
		double want[3] = { 450, 1000 + 10.0 * k, 2500 };
		for (int i = 0; i < 3; i++) {
			double e = fabs(gf[k * 4 + i] - want[i]) / want[i];
			if (e > worst[i])
				worst[i] = e;
		}
	}
	if (!(worst[0] < 0.05 && worst[1] < 0.05 && worst[2] < 0.05))
		printf("  glide: worst F1 %.1f %%, F2 %.1f %%, F3 %.1f %%\n", 100 * worst[0], 100 * worst[1],
			100 * worst[2]);
	CHECK(worst[0] < 0.05 && worst[1] < 0.05 && worst[2] < 0.05);
	free(x);
	synth_result_free(&s);
	doc_free(&d);
}

void suite_track(void)
{
	test_clean();
	test_missing_candidate();
	test_spurious_candidates();
	test_silence_interpolates();
	test_clamps_and_bandwidth_smoothing();
	test_synthesized_glide();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_pitch(void);

```

with:

```c
void suite_pitch(void);
void suite_track(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "pitch", suite_pitch },
};
```

with:

```c
	{ "pitch", suite_pitch },
	{ "track", suite_track },
};
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_track.c:8:10: fatal error: track.h: No such file or directory`.

- [ ] **Step 3: Write the tracker**

Create `kpe/track.h`:

```c
/* track.h - formant tracks F1-F4 from per-frame LPC candidates by a Viterbi search over the whole signal. */
#ifndef KPE_TRACK_H
#define KPE_TRACK_H

#include "lpc.h"

#define TRACK_N 4         /* tracks: F1-F4 */
#define TRACK_MAX_CANDS 6 /* the lowest this many candidates of a frame are considered: at most 15 states */

typedef struct {
	double ref[TRACK_N];      /* the voice type's reference F1-F4 */
	double lo[TRACK_N];       /* each track is clamped to lo..hi (the parameter range, and below 0.45 SR) */
	double hi[TRACK_N];
} track_setup;

/* Tracks nframes frames. cands[k * CAND_MAX + j], j < ncand[k], are frame k's candidates (ascending);
 * quiet[k] marks frames that add no cost at all (too quiet to trust: their values are interpolated from the
 * neighbouring frames, like a slot a frame has no candidate for). Each frame's states are the ordered choices of
 * up to four of its candidates for F1-F4 (a frame with fewer than four candidates leaves some tracks empty);
 * the Viterbi cost adds |f - ref| / ref and bw / 500 per track, a fixed cost per empty track, and |df| / 200 per
 * track between neighbouring frames.
 * Writes f[k * TRACK_N + i] (clamped to lo..hi) and bw[k * TRACK_N + i] (the chosen candidates' bandwidths,
 * interpolated the same way, median-smoothed over 7 frames and clamped to 40-500 Hz). Returns 0, or -1 when out
 * of memory. */
int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, int nframes,
	const track_setup *setup, double *f, double *bw);

#endif
```

Create `kpe/track.c`:

```c
/* track.c - see track.h. The costs are those the fextr probe found to work on ba.csv (F1-F3 median errors 5-12 Hz):
 * distance from the reference, bandwidth, and frame-to-frame jump; extended to F4 and to empty tracks. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "track.h"

#define TRACK_BW_SCALE 500.0    /* local cost bw / this per track: a broad candidate is a weak formant */
#define TRACK_JUMP_SCALE 200.0  /* transition cost |df| / this per track */
#define TRACK_EMPTY_COST 1.5    /* local cost per empty track: more than a typical candidate's (0.1-1), so a track
                                 * stays empty only when the frame really lacks a candidate for it */
#define TRACK_BW_MEDIAN 7       /* frames in the bandwidth median */
#define TRACK_BW_MIN 40.0       /* bandwidth clamp (the B1-B3 parameter range, 40-500 Hz) */
#define TRACK_BW_MAX 500.0
#define MAX_STATES 15           /* C(TRACK_MAX_CANDS, 4) */

static const double default_bw[TRACK_N] = { 50, 70, 110, 250 }; /* a track that is never found: the DLL defaults */

typedef struct {
	signed char pick[TRACK_N]; /* candidate index per track, -1 empty */
} state;

/* All ordered assignments of frame candidates to the four tracks: every 4-subset when there are at least four,
 * else every placement of all of them with the rest empty. Returns the count. */
static int states_of(int n, state *out)
{
	int count = 0;
	if (n >= TRACK_N) {
		for (int a = 0; a < n; a++)
			for (int b = a + 1; b < n; b++)
				for (int c = b + 1; c < n; c++)
					for (int d = c + 1; d < n; d++)
						out[count++] = (state){ { (signed char)a, (signed char)b, (signed char)c, (signed char)d } };
		return count;
	}
	/* choose which n of the four tracks are filled, in order */
	for (int mask = 0; mask < 1 << TRACK_N; mask++) {
		int bits = 0;
		for (int i = 0; i < TRACK_N; i++)
			bits += mask >> i & 1;
		if (bits != n)
			continue;
		state s;
		int j = 0;
		for (int i = 0; i < TRACK_N; i++)
			s.pick[i] = mask >> i & 1 ? (signed char)j++ : -1;
		out[count++] = s;
	}
	return count;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Fills the NAN entries of v[k * TRACK_N + i] (one track i) by linear interpolation between the nearest known
 * frames, holding the end values; fallback if the track is never known. */
static void fill_gaps(double *v, int nframes, int i, double fallback)
{
	int prev = -1;
	for (int k = 0; k <= nframes; k++) {
		if (k < nframes && isnan(v[k * TRACK_N + i]))
			continue;
		/* frames prev+1 .. k-1 are gaps */
		for (int j = prev + 1; j < k; j++) {
			if (prev < 0 && k == nframes)
				v[j * TRACK_N + i] = fallback;
			else if (prev < 0)
				v[j * TRACK_N + i] = v[k * TRACK_N + i];
			else if (k == nframes)
				v[j * TRACK_N + i] = v[prev * TRACK_N + i];
			else {
				double a = v[prev * TRACK_N + i], b = v[k * TRACK_N + i];
				v[j * TRACK_N + i] = a + (b - a) * (j - prev) / (k - prev);
			}
		}
		prev = k;
	}
}

int track_formants(const lpc_cand *cands, const int *ncand, const unsigned char *quiet, int nframes,
	const track_setup *setup, double *f, double *bw)
{
	if (nframes <= 0)
		return 0;
	int *nst = malloc(sizeof(int) * nframes);
	state *st = malloc(sizeof(state) * MAX_STATES * nframes);
	double *cost = malloc(sizeof(double) * MAX_STATES * nframes);
	unsigned char *back = malloc((size_t)MAX_STATES * nframes);
	double *tmp = malloc(sizeof(double) * nframes);
	if (!nst || !st || !cost || !back || !tmp) {
		free(nst);
		free(st);
		free(cost);
		free(back);
		free(tmp);
		return -1;
	}
	for (int k = 0; k < nframes; k++) {
		int n = quiet[k] ? 0 : ncand[k] < TRACK_MAX_CANDS ? ncand[k] : TRACK_MAX_CANDS;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		state *s = st + (size_t)k * MAX_STATES;
		nst[k] = states_of(n, s);
		for (int q = 0; q < nst[k]; q++) {
			double local = 0;
			for (int i = 0; i < TRACK_N; i++) {
				int j = s[q].pick[i];
				if (j < 0)
					local += n > 0 ? TRACK_EMPTY_COST : 0; /* a frame with nothing to offer costs nothing */
				else
					local += fabs(c[j].f - setup->ref[i]) / setup->ref[i] + c[j].bw / TRACK_BW_SCALE;
			}
			double best = 0;
			int bi = 0;
			if (k > 0) {
				const lpc_cand *pc = cands + (size_t)(k - 1) * CAND_MAX;
				const state *ps = st + (size_t)(k - 1) * MAX_STATES;
				best = 1e300;
				for (int r = 0; r < nst[k - 1]; r++) {
					double t = cost[(size_t)(k - 1) * MAX_STATES + r];
					for (int i = 0; i < TRACK_N; i++) {
						int a = s[q].pick[i], b = ps[r].pick[i];
						if (a >= 0 && b >= 0)
							t += fabs(c[a].f - pc[b].f) / TRACK_JUMP_SCALE;
					}
					if (t < best) {
						best = t;
						bi = r;
					}
				}
			}
			cost[(size_t)k * MAX_STATES + q] = best + local;
			back[(size_t)k * MAX_STATES + q] = (unsigned char)bi;
		}
	}
	int q = 0;
	for (int r = 1; r < nst[nframes - 1]; r++)
		if (cost[(size_t)(nframes - 1) * MAX_STATES + r] < cost[(size_t)(nframes - 1) * MAX_STATES + q])
			q = r;
	for (int k = nframes - 1; k >= 0; k--) {
		const state *s = st + (size_t)k * MAX_STATES + q;
		const lpc_cand *c = cands + (size_t)k * CAND_MAX;
		for (int i = 0; i < TRACK_N; i++) {
			int j = s->pick[i];
			f[k * TRACK_N + i] = j >= 0 ? c[j].f : NAN;
			bw[k * TRACK_N + i] = j >= 0 ? c[j].bw : NAN;
		}
		q = back[(size_t)k * MAX_STATES + q];
	}
	for (int i = 0; i < TRACK_N; i++) {
		fill_gaps(f, nframes, i, setup->ref[i]);
		fill_gaps(bw, nframes, i, default_bw[i]);
		for (int k = 0; k < nframes; k++) {
			double *v = &f[k * TRACK_N + i];
			*v = *v < setup->lo[i] ? setup->lo[i] : *v > setup->hi[i] ? setup->hi[i] : *v;
		}
		/* bandwidth: median over TRACK_BW_MEDIAN frames (shrinking at the ends), then clamped */
		for (int k = 0; k < nframes; k++) {
			double win[TRACK_BW_MEDIAN];
			int m = 0;
			for (int j = k - TRACK_BW_MEDIAN / 2; j <= k + TRACK_BW_MEDIAN / 2; j++)
				if (j >= 0 && j < nframes)
					win[m++] = bw[j * TRACK_N + i];
			qsort(win, m, sizeof(double), cmp_double);
			double v = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
			tmp[k] = v < TRACK_BW_MIN ? TRACK_BW_MIN : v > TRACK_BW_MAX ? TRACK_BW_MAX : v;
		}
		for (int k = 0; k < nframes; k++)
			bw[k * TRACK_N + i] = tmp[k];
	}
	free(nst);
	free(st);
	free(cost);
	free(back);
	free(tmp);
	return 0;
}
```

- [ ] **Step 4: Run the tests on both architectures**

Run: `MAKE test`
Expected: `1421 checks, 0 failures` twice, then `xarch: 33074 samples, max difference 0 LSB: ok`.

- [ ] **Step 5: Commit**

```bash
git add kpe/track.h kpe/track.c tests/test_track.c tests/run_tests.c
git commit -m "Add Viterbi formant tracking for the import

Viterbi over the whole file for F1-F4 from LPC candidates (reference,
bandwidth and jump costs, empty tracks when a frame lacks candidates),
gaps and quiet frames interpolated, B1-B3 median-smoothed and clamped.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The import pipeline

The largest task: spec 4.3 steps 5-9 plus the frame layout, the round-trip tests of spec 4.6 and the speed test.
The code below is the tuned result; its constants carry the measurements that chose them.

**Files:**
- Create: `kpe/wavimport.h`, `kpe/wavimport.c`, `tests/test_wavimport.c`
- Modify: `kpe/pitch.c`, `kpe/pitch.h`, `kpe/spectro.c`, `kpe/spectro.h`, `kpe/optimize.c`, `kpe/optimize.h`,
  `kpe/dialogs.c`, `tests/test_xarch.c`, `tests/run_tests.c`

**Interfaces:**
- Consumes: Tasks 1-5; `synth_doc` and `synth_result_free` (`kpe/synthrun.h`); `opt_simplify` (`kpe/optimize.h`);
  `kpe_doc`, `doc_init_from`, `doc_free` and `doc_param_step` (`kpe/model.h`).
- Produces (`kpe/wavimport.h`): `import_options` (`int voice, sr, simplify;`), `VOICE_MALE`/`VOICE_FEMALE`/
  `VOICE_CHILD`/`VOICE_COUNT`, `const voice_info *import_voice(int voice)` (`name`, `f0_min`, `f0_max`, `ref[4]`),
  `CLS_*`, `import_info` and `import_info_free`, `import_levels` and `import_measure`, `import_frame_centre`,
  `int import_samples(const double *x, long n, int sr_in, const import_options *o, kpe_doc *d, import_info *info,
  char *msg, size_t msglen)` and `int import_wav(const wchar_t *path, const import_options *o, kpe_doc *d,
  import_info *info, char *msg, size_t msglen)`. Task 7 uses `import_options`, `import_voice`, `VOICE_COUNT` and
  `import_wav`.
- Also produces: `void spectro_fft(double *re, double *im, int n, const double *tw_re, const double *tw_im)`
  (`kpe/spectro.h`); `OPT_TOL_*`, `opt_tol_default[]`, `int opt_tol_class(int p)` (`kpe/optimize.h`).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_wavimport.c`:

```c
/* WAV import: round trips on documents synthesized by the DLL (spec 4.6), frame classes of realistic fricatives,
 * aspiration, level normalization, refusals, and the speed test. Files are written into the working directory
 * (build/32 or build/64). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test.h"
#include "measure.h"
#include "model.h"
#include "csvio.h"
#include "synthrun.h"
#include "wavout.h"
#include "wavimport.h"

/* spec 4.6 limits */
#define LIM_F0_MEDIAN 0.02
#define LIM_F_MEDIAN 0.05
#define LIM_F_P90 0.10
#define LIM_VUV 0.95
#define LIM_AV_DB 3.0
#define LIM_RD 0.3
#define LIM_BAND_DB 3.0
#define HIGH_VOICE_F0 200.0    /* from this median F0 up, AV and Rd are held to their limits at the median: 25 ms LPC
                                * cannot resolve B1 there, and Rd and AV absorb the bandwidth error */
#define LIM_CLASS 0.90         /* every class with at least MIN_CLASS_FRAMES steady frames: this share right */
#define MIN_CLASS_FRAMES 10
#define LIM_AH_DB 5.0          /* a breathy vowel's AH, median over its steady frames */
#define PEAK_MAX_DB -0.5       /* the resynthesis of a full-scale WAV peaks in this range (dBFS) */
#define PEAK_MIN_DB -3.0
#define BOUNDARY_FRAMES 2      /* compared frames are at least 10 ms from a class boundary */
#define BAND_RANGE_DB 30.0     /* fricative bands more than this below the frame's strongest band are not compared */

typedef struct {
	double ms, v;
} pt;

/* Sets param p of every frame by linear interpolation between the points (held beyond the ends). */
static void track(kpe_doc *d, int p, const pt *pts, int n)
{
	for (int k = 0; k < d->nframes; k++) {
		double t = k * doc_frame_ms(d), v = pts[n - 1].v;
		if (t <= pts[0].ms)
			v = pts[0].v;
		else
			for (int i = 1; i < n; i++)
				if (t <= pts[i].ms) {
					v = pts[i - 1].v + (pts[i].v - pts[i - 1].v) * (t - pts[i - 1].ms) / (pts[i].ms - pts[i - 1].ms);
					break;
				}
		d->frames[(size_t)k * NP + p] = v;
	}
}

#define TRACK(d, p, ...) do { \
	const pt pts_[] = { __VA_ARGS__ }; \
	track(d, p, pts_, (int)(sizeof pts_ / sizeof pts_[0])); \
} while (0)

static void new_doc(kpe_doc *d, double ms, int src)
{
	double g[NG];
	t_default_globals(g);
	g[KG_SRC] = src;
	CHECK(doc_new(d, g, ms) == 0);
}

/* Rounds every value to its parameter's step, as a saved document holds them. */
static void round_doc(kpe_doc *d)
{
	for (int k = 0; k < d->nframes; k++)
		for (int p = 0; p < NP; p++) {
			double s = doc_param_step(p), *v = &d->frames[(size_t)k * NP + p];
			*v = round(*v / s) * s;
		}
}

static void female_vowels(kpe_doc *d)
{
	new_doc(d, 1200, 1);
	TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 1100, 60 }, { 1140, 0 });
	TRACK(d, KP_F0, { 0, 210 }, { 400, 250 }, { 900, 200 }, { 1200, 220 });
	TRACK(d, KP_F1, { 0, 310 }, { 250, 310 }, { 350, 850 }, { 550, 850 }, { 650, 370 }, { 850, 370 }, { 950, 860 });
	TRACK(d, KP_F2, { 0, 2790 }, { 250, 2790 }, { 350, 1220 }, { 550, 1220 }, { 650, 950 }, { 850, 950 },
		{ 950, 2050 });
	TRACK(d, KP_F3, { 0, 3310 }, { 250, 3310 }, { 350, 2810 }, { 550, 2810 }, { 650, 2670 }, { 850, 2670 },
		{ 950, 2850 });
	TRACK(d, KP_F4, { 0, 4000 });
	round_doc(d);
}

/* A fricative between two /a/ vowels: AV during the fricative (0: voiceless), AF, the parallel amplitudes A2-A6
 * and AB, and F3 in the fricative (the /a/s have 700/1220/2600 Hz). */
typedef struct {
	double av, af, a[5], ab, f3, b2, b3; /* b2, b3: B2 and B3 in the fricative, 0 for the defaults */
} fric_spec;

static void fric_doc(kpe_doc *d, const fric_spec *f)
{
	double voiced = f->av;
	new_doc(d, 800, 1);
	if (voiced > 0)
		TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 280, 60 }, { 310, voiced }, { 480, voiced }, { 510, 60 },
			{ 720, 60 }, { 750, 0 });
	else
		TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 280, 60 }, { 300, 0 }, { 490, 0 }, { 510, 60 },
			{ 720, 60 }, { 750, 0 });
	TRACK(d, KP_AF, { 0, 0 }, { 290, 0 }, { 310, f->af }, { 480, f->af }, { 500, 0 });
	for (int j = 0; j < 5; j++)
		TRACK(d, KP_A2 + j, { 0, f->a[j] });
	TRACK(d, KP_AB, { 0, f->ab });
	TRACK(d, KP_F0, { 0, 125 }, { 800, 100 });
	TRACK(d, KP_F1, { 0, 700 }, { 260, 700 }, { 300, 400 }, { 490, 400 }, { 530, 700 });
	TRACK(d, KP_F2, { 0, 1220 }, { 260, 1220 }, { 300, 1600 }, { 490, 1600 }, { 530, 1220 });
	TRACK(d, KP_F3, { 0, 2600 }, { 260, 2600 }, { 300, f->f3 }, { 490, f->f3 }, { 530, 2600 });
	if (f->b2 > 0) {
		TRACK(d, KP_B2, { 0, 70 }, { 260, 70 }, { 300, f->b2 }, { 490, f->b2 }, { 530, 70 });
		TRACK(d, KP_B3, { 0, 110 }, { 260, 110 }, { 300, f->b3 }, { 490, f->b3 }, { 530, 110 });
	}
	round_doc(d);
}

/* /a/ - fricative - /a/: voiced = 0 gives /s/, otherwise /z/ with that AV during the fricative */
static void fricative(kpe_doc *d, double voiced)
{
	fric_spec f = { voiced, 58, { 0, 0, 38, 50, 56 }, 0, 2700, 0, 0 };
	fric_doc(d, &f);
}

/* Fricatives as the user's recordings have them (measured at 11025 Hz): a /z/ whose frication is 10 dB weaker than
 * the one above, a /sh/ with its energy at 2-4 kHz (below the 3859 Hz cutoff of the energy fraction), and a /v/: flat
 * frication (through AB) over a weak voicing whose F2 and F3 are damped by the constriction, so that above 1 kHz the
 * spectrum is flat (upper band 2 dB below the densest 500 Hz band above 1 kHz; the recording's reads 6 dB). */
static const fric_spec weak_z = { 55, 46, { 0, 0, 30, 40, 48 }, 0, 2700, 0, 0 };
static const fric_spec sh = { 0, 58, { 0, 52, 56, 44, 30 }, 0, 2400, 0, 0 };
static const fric_spec v_fric = { 45, 50, { 0, 0, 0, 0, 0 }, 58, 2700, 200, 300 };

static void rd_sweep(kpe_doc *d)
{
	new_doc(d, 1500, 1);
	TRACK(d, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 1440, 60 }, { 1470, 0 });
	TRACK(d, KP_F0, { 0, 115 }, { 1500, 105 });
	TRACK(d, KP_F1, { 0, 700 });
	TRACK(d, KP_F2, { 0, 1220 });
	TRACK(d, KP_F3, { 0, 2600 });
	TRACK(d, KP_RD, { 0, 0.4 }, { 100, 0.4 }, { 1400, 2.5 });
	round_doc(d);
}

static int truth_class(const kpe_doc *d, int k)
{
	const double *f = d->frames + (size_t)k * NP;
	if (f[KP_AV] > 0)
		return f[KP_AF] > 0 ? CLS_VOICED_FRIC : CLS_VOICED;
	if (f[KP_AF] > 0)
		return CLS_FRIC;
	return f[KP_AH] > 0 ? CLS_ASPIRATION : CLS_SILENCE;
}

/* The frame and its BOUNDARY_FRAMES neighbours on either side have the same class. */
static int steady(const kpe_doc *d, int k)
{
	int c = truth_class(d, k);
	for (int j = k - BOUNDARY_FRAMES; j <= k + BOUNDARY_FRAMES; j++)
		if (j < 0 || j >= d->nframes || truth_class(d, j) != c)
			return 0;
	return 1;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* The q-quantile (0.5 median, 0.9 90th percentile) of v[0..n-1]; sorts v. */
static double quantile(double *v, int n, double q)
{
	if (n == 0)
		return NAN;
	qsort(v, n, sizeof(double), cmp_double);
	int i = (int)ceil(q * n) - 1;
	return v[i < 0 ? 0 : i];
}

static double *pcm_to_double(const synth_result *s)
{
	double *x = malloc(sizeof(double) * (s->n > 0 ? s->n : 1));
	for (int i = 0; i < s->n; i++)
		x[i] = s->pcm[i] / 32768.0;
	return x;
}

/* Synthesizes src, imports it and compares with the spec 4.6 limits. AV and Rd are compared only when src uses the
 * LF source like the import (with the 1980 source neither means the same). The import sets its own G0 (level
 * normalization) and leaves the amplitudes as calibrated at the default G0, so the band levels of its synthesis are
 * compared less the G0 difference, and AV directly. "Within" is the 90th percentile of the per-frame error, or the
 * median for a voice whose median F0 is HIGH_VOICE_F0 or more. Also: every well-represented class right in at least
 * LIM_CLASS of its steady frames, no steady vowel frame imported with frication, no overload in the resynthesis. */
static void round_trip(const char *name, const kpe_doc *src, int voice)
{
	int same_source = src->globals[KG_SRC] == 1;
	synth_result s;
	CHECK(synth_doc(src, &s) == 0);
	double *x = pcm_to_double(&s);
	import_options o = { voice, s.sr, 0 };
	kpe_doc d;
	import_info info;
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, &info, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc != 0) {
		printf("  %s: %s\n", name, msg);
		free(x);
		synth_result_free(&s);
		return;
	}
	CHECK(d.nframes == src->nframes);
	CHECK(d.globals[KG_SRC] == 1 && d.globals[KG_SR] == s.sr && d.globals[KG_FRAME_MS] == 5);
	int nf = d.nframes < src->nframes ? d.nframes : src->nframes;
	double *e0 = malloc(sizeof(double) * nf), *ef[3], *eav = malloc(sizeof(double) * nf);
	double *erd = malloc(sizeof(double) * nf), *eband = malloc(sizeof(double) * nf * 5);
	double *tf0 = malloc(sizeof(double) * nf), dg0 = d.globals[KG_G0] - src->globals[KG_G0];
	int ntf0 = 0, vowel_fric = 0;
	for (int i = 0; i < 3; i++)
		ef[i] = malloc(sizeof(double) * nf);
	int n0 = 0, nfm = 0, nav = 0, nrd = 0, nband = 0, vuv_ok = 0, vuv_n = 0;

	/* the imported document's synthesis, for the fricative band levels */
	synth_result sy;
	CHECK(synth_doc(&d, &sy) == 0);
	CHECK(sy.overload == 0);
	double *y = pcm_to_double(&sy);
	int class_ok[CLS_COUNT] = { 0 }, class_n[CLS_COUNT] = { 0 };
	for (int k = 0; k < nf; k++) {
		if (!steady(src, k))
			continue;
		const double *a = src->frames + (size_t)k * NP, *b = d.frames + (size_t)k * NP;
		int tc = truth_class(src, k);
		class_n[tc]++;
		class_ok[tc] += info.cls[k] == tc;
		vuv_n++;
		vuv_ok += (a[KP_AV] > 0) == (b[KP_AV] > 0);
		if (a[KP_AV] > 0 && b[KP_AV] > 0)
			e0[n0++] = fabs(b[KP_F0] - a[KP_F0]) / a[KP_F0];
		if (tc == CLS_VOICED) {
			for (int i = 0; i < 3; i++)
				ef[i][nfm] = fabs(b[KP_F1 + i] - a[KP_F1 + i]) / a[KP_F1 + i];
			nfm++;
			tf0[ntf0++] = a[KP_F0];
			vowel_fric += b[KP_AF] > 0;
			if (same_source && b[KP_AV] > 0) /* an unvoiced frame counts as a voicing error, not a level error */
				eav[nav++] = fabs(b[KP_AV] - a[KP_AV]);
			if (same_source)
				erd[nrd++] = fabs(b[KP_RD] - a[KP_RD]);
		}
		if (tc == CLS_FRIC || tc == CLS_VOICED_FRIC) {
			double f[6] = { b[KP_F1], b[KP_F2], b[KP_F3], b[KP_F4], d.globals[KG_F5], d.globals[KG_F6] };
			import_levels lw, ls;
			long c = import_frame_centre(s.sr, 5, k);
			import_measure(x, s.n, s.sr, c, f, &lw);
			import_measure(y, sy.n, sy.sr, c, f, &ls);
			double top = -1e300;
			for (int i = 0; i < 5; i++)
				if (lw.band[i] > top)
					top = lw.band[i];
			for (int i = 0; i < 5; i++)
				if (lw.band[i] > top - BAND_RANGE_DB)
					eband[nband++] = fabs(lw.band[i] - (ls.band[i] - dg0));
		}
	}
	double f0m = quantile(e0, n0, 0.5), vuv = vuv_n ? (double)vuv_ok / vuv_n : 0;
	int high = ntf0 > 0 && quantile(tf0, ntf0, 0.5) >= HIGH_VOICE_F0;
	double fm[3], fp[3];
	for (int i = 0; i < 3; i++) {
		fm[i] = quantile(ef[i], nfm, 0.5);
		fp[i] = quantile(ef[i], nfm, 0.9);
	}
	double avm = quantile(eav, nav, 0.5), avp = quantile(eav, nav, 0.9), avx = quantile(eav, nav, 1.0);
	double rdm = quantile(erd, nrd, 0.5), rdp = quantile(erd, nrd, 0.9), rdx = quantile(erd, nrd, 1.0);
	double bm = quantile(eband, nband, 0.5), bp = quantile(eband, nband, 0.9), bx = quantile(eband, nband, 1.0);
	printf("  %s (%d frames, %d steady voiced, Rd fitted in %d%s; G0 %g):\n", name, nf, nfm, info.rd_frames,
		high ? ", a high voice: AV and Rd at the median" : "", d.globals[KG_G0]);
	printf("    F0 median %.2f %% (n %d); V/UV %.1f %% correct (n %d)\n", 100 * f0m, n0, 100 * vuv, vuv_n);
	printf("    F1/F2/F3 median %.1f/%.1f/%.1f %%, 90th %.1f/%.1f/%.1f %%\n", 100 * fm[0], 100 * fm[1],
		100 * fm[2], 100 * fp[0], 100 * fp[1], 100 * fp[2]);
	if (nav)
		printf("    AV |error| median %.1f, 90th %.1f, max %.1f dB\n", avm, avp, avx);
	if (nrd)
		printf("    Rd |error| median %.2f, 90th %.2f, max %.2f\n", rdm, rdp, rdx);
	if (nband)
		printf("    fricative bands |error| median %.1f, 90th %.1f, max %.1f dB (n %d)\n", bm, bp, bx, nband);
	printf("    classes right:");
	for (int c = 0; c < CLS_COUNT; c++)
		if (class_n[c])
			printf(" %s %d/%d", (const char *[]){ "silence", "voiced", "voiced fric", "fric", "aspiration" }[c],
				class_ok[c], class_n[c]);
	printf("; steady vowel frames with frication %d; resynthesis peak %.1f dBFS\n", vowel_fric,
		20 * log10(m_peak(sy.pcm, sy.n) / 32768 + 1e-12));
	CHECK(n0 > 0 && f0m < LIM_F0_MEDIAN);
	CHECK(vuv >= LIM_VUV);
	for (int i = 0; i < 3; i++) {
		CHECK(nfm > 0 && fm[i] < LIM_F_MEDIAN);
		CHECK(fp[i] < LIM_F_P90);
	}
	if (same_source) {
		CHECK(nav > 0 && (high ? avm : avp) <= LIM_AV_DB);
		CHECK(nrd > 0 && (high ? rdm : rdp) <= LIM_RD);
	}
	for (int c = 0; c < CLS_COUNT; c++)
		if (class_n[c] >= MIN_CLASS_FRAMES && !(class_ok[c] >= LIM_CLASS * class_n[c])) {
			printf("    class %d: %d of %d right\n", c, class_ok[c], class_n[c]);
			CHECK(0);
		}
	CHECK(vowel_fric == 0);
	if (nband)
		CHECK(bp <= LIM_BAND_DB);
	free(e0);
	free(tf0);
	free(eav);
	free(erd);
	free(eband);
	for (int i = 0; i < 3; i++)
		free(ef[i]);
	free(x);
	free(y);
	synth_result_free(&s);
	synth_result_free(&sy);
	import_info_free(&info);
	doc_free(&d);
}

static void test_round_trips(void)
{
	kpe_doc d;
	csv_report r;
	int rc = csv_load(L"../../samples/ba.csv", &d, &r);
	CHECK(rc == 0);
	if (rc == 0) {
		round_trip("ba.csv (1980 source)", &d, VOICE_MALE);
		doc_free(&d);
	}
	female_vowels(&d);
	round_trip("female vowels", &d, VOICE_FEMALE);
	doc_free(&d);
	fricative(&d, 0);
	round_trip("/asa/", &d, VOICE_MALE);
	doc_free(&d);
	fricative(&d, 50);
	round_trip("/aza/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &weak_z);
	round_trip("weak /aza/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &sh);
	round_trip("/asha/", &d, VOICE_MALE);
	doc_free(&d);
	fric_doc(&d, &v_fric);
	round_trip("/ava/", &d, VOICE_MALE);
	doc_free(&d);
	rd_sweep(&d);
	round_trip("Rd sweep 0.4-2.5", &d, VOICE_MALE);
	doc_free(&d);
}

/* A steady /a/ at AV 60 with aspiration ah (0: none) and Rd rd, imported: returns the median AH over its steady
 * frames and counts those with AH > 0. */
static double aspiration_of(double ah, double rd, int *with_ah, int *n)
{
	kpe_doc src, d;
	new_doc(&src, 600, 1);
	TRACK(&src, KP_AV, { 0, 0 }, { 40, 0 }, { 60, 60 }, { 540, 60 }, { 560, 0 });
	TRACK(&src, KP_AH, { 0, 0 }, { 40, 0 }, { 60, ah }, { 540, ah }, { 560, 0 });
	TRACK(&src, KP_F0, { 0, 125 }, { 600, 105 });
	TRACK(&src, KP_F1, { 0, 700 });
	TRACK(&src, KP_F2, { 0, 1220 });
	TRACK(&src, KP_F3, { 0, 2600 });
	TRACK(&src, KP_RD, { 0, rd });
	round_doc(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s), v[200], med = -1;
	import_options o = { VOICE_MALE, s.sr, 0 };
	char msg[256];
	*with_ah = *n = 0;
	if (import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg) == 0) {
		for (int k = 20; k < 100; k++) { /* 100-500 ms: at least 40 ms from the ramps */
			double a = d.frames[(size_t)k * NP + KP_AH];
			v[(*n)++] = a;
			*with_ah += a > 0;
		}
		med = quantile(v, *n, 0.5);
		doc_free(&d);
	} else {
		CHECK(0);
	}
	free(x);
	synth_result_free(&s);
	doc_free(&src);
	return med;
}

/* Only a clearly aperiodic voice gets aspiration: a modal vowel none, a breathy one about its own. */
static void test_aspiration(void)
{
	int with_ah, n;
	double m = aspiration_of(0, 1.0, &with_ah, &n);
	printf("  modal vowel: %d of %d steady frames with AH, median AH %g\n", with_ah, n, m);
	CHECK(n > 0 && with_ah == 0);
	m = aspiration_of(55, 2.2, &with_ah, &n);
	printf("  breathy vowel (AV 60, AH 55, Rd 2.2): median AH %g, %d of %d frames with AH\n", m, with_ah, n);
	CHECK_NEAR(m, 55, LIM_AH_DB);
}

/* A WAV that peaks at 0 dBFS: G0 is set so its resynthesis peaks at about -1 dBFS, without overload. */
static void test_normalization(void)
{
	kpe_doc src, d;
	fricative(&src, 50);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s), peak = m_peak(s.pcm, s.n) / 32768;
	for (int i = 0; i < s.n; i++)
		x[i] = x[i] / peak * (32767.0 / 32768); /* full scale */
	import_options o = { VOICE_MALE, s.sr, 0 };
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		synth_result t;
		CHECK(synth_doc(&d, &t) == 0);
		double db = 20 * log10(m_peak(t.pcm, t.n) / 32768);
		printf("  full-scale WAV: resynthesis peaks at %.2f dBFS with G0 %g, %d samples clipped\n", db,
			d.globals[KG_G0], t.overload);
		CHECK(t.overload == 0);
		CHECK(db >= PEAK_MIN_DB && db <= PEAK_MAX_DB);
		synth_result_free(&t);
		doc_free(&d);
	}
	free(x);
	synth_result_free(&s);
	doc_free(&src);
}

static void test_result_document(void)
{
	kpe_doc src, d;
	rd_sweep(&src);
	synth_result s;
	CHECK(synth_doc(&src, &s) == 0);
	double *x = pcm_to_double(&s);
	import_options o = { VOICE_MALE, 11025, 0 };
	char msg[256];
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc != 0) {
		free(x);
		synth_result_free(&s);
		doc_free(&src);
		return;
	}
	CHECK(!d.dirty && !doc_can_undo(&d));
	double g[NG];
	t_default_globals(g);
	for (int i = 0; i < NG; i++)
		if (i != KG_SRC && i != KG_G0)
			CHECK(d.globals[i] == g[i]);
	CHECK(d.globals[KG_SRC] == 1);
	CHECK(d.globals[KG_G0] == round(d.globals[KG_G0])); /* level normalization: a whole number */
	int unrounded = 0, outside = 0;
	for (int k = 0; k < d.nframes; k++)
		for (int p = 0; p < NP; p++) {
			double v = d.frames[(size_t)k * NP + p], st = doc_param_step(p);
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			unrounded += fabs(v / st - round(v / st)) > 1e-9;
			outside += v < pi.min || v > pi.max;
		}
	CHECK(unrounded == 0 && outside == 0);
	/* silence at the start: every amplitude 0 */
	for (int p = KP_AV; p <= KP_AVS; p++)
		CHECK(d.frames[p] == 0);
	doc_free(&d);

	/* resampled input: the same sound at 22050 Hz, imported at 11025 Hz */
	double *x2 = malloc(sizeof(double) * s.n * 2);
	for (int i = 0; i < s.n * 2; i++) /* linear upsampling is enough: the sound is band-limited well below */
		x2[i] = i / 2 + 1 < s.n ? (i & 1 ? 0.5 * (x[i / 2] + x[i / 2 + 1]) : x[i / 2]) : x[s.n - 1];
	rc = import_samples(x2, s.n * 2, 22050, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		CHECK(abs(d.nframes - src.nframes) <= 1);
		CHECK_NEAR(d.frames[(size_t)150 * NP + KP_F0], src.frames[(size_t)150 * NP + KP_F0], 2);
		CHECK_NEAR(d.frames[(size_t)150 * NP + KP_F1], 700, 35);
		doc_free(&d);
	}

	/* simplify: fewer distinct values, still within the parameter ranges */
	o.simplify = 1;
	rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		int changes = 0;
		for (int k = 2; k < d.nframes; k++) {
			double a = d.frames[(size_t)(k - 2) * NP + KP_F1], b = d.frames[(size_t)(k - 1) * NP + KP_F1];
			double c = d.frames[(size_t)k * NP + KP_F1];
			changes += fabs((c - b) - (b - a)) > 1e-6; /* a breakpoint: the slope changes */
		}
		CHECK(changes < d.nframes / 4);
		doc_free(&d);
	}
	free(x);
	free(x2);
	synth_result_free(&s);
	doc_free(&src);
}

static void test_refusals(void)
{
	kpe_doc d;
	char msg[256];
	import_options o = { VOICE_MALE, 11025, 0 };
	d.nframes = -12345; /* untouched on failure */

	long n = 11025; /* -66 dBFS: silent */
	double *x = malloc(sizeof(double) * n);
	for (long i = 0; i < n; i++)
		x[i] = 0.0005 * sin(i * 0.1);
	CHECK(import_samples(x, n, 11025, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "No sound found") != NULL);
	CHECK(d.nframes == -12345);
	free(x);

	n = 1000L * 601; /* 10 minutes and 1 second at 1000 Hz */
	x = calloc(n, sizeof(double));
	x[5] = 0.5;
	CHECK(import_samples(x, n, 1000, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "longer than 10 minutes") != NULL);
	free(x);

	CHECK(import_wav(L"no_such_file.wav", &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(strstr(msg, "Cannot open") != NULL);
	o.sr = 100; /* outside SR's range */
	double one[4] = { 0.5, -0.5, 0.5, -0.5 };
	CHECK(import_samples(one, 4, 11025, &o, &d, NULL, msg, sizeof msg) == -1);
	CHECK(d.nframes == -12345);
}

/* 60 s of speech-like sound (ba.csv 75 times) as a WAV file, imported and timed. */
static void test_speed(void)
{
	kpe_doc ba, big;
	csv_report r;
	if (csv_load(L"../../samples/ba.csv", &ba, &r) != 0) {
		CHECK(0);
		return;
	}
	double *frames = malloc(sizeof(double) * NP * ba.nframes * 75);
	for (int i = 0; i < 75; i++)
		memcpy(frames + (size_t)i * NP * ba.nframes, ba.frames, sizeof(double) * NP * ba.nframes);
	CHECK(doc_init_from(&big, ba.globals, ba.nframes * 75, frames) == 0);
	synth_result s;
	CHECK(synth_doc(&big, &s) == 0);
	CHECK(wav_write(L"speed.wav", s.pcm, s.n, s.sr) == 0);
	import_options o = { VOICE_MALE, 11025, 0 };
	kpe_doc d;
	import_info info;
	char msg[256];
	clock_t t0 = clock();
	int rc = import_wav(L"speed.wav", &o, &d, &info, msg, sizeof msg);
	double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
	CHECK(rc == 0);
	printf("  speed: %.1f s of sound imported in %.2f s (analysis %.2f, Rd %.2f in %d frames, calibration %.2f)\n",
		s.n / (double)s.sr, secs, info.seconds[0], info.seconds[1], info.rd_frames, info.seconds[2]);
	CHECK(secs <= 5.0);
	if (rc == 0) {
		import_info_free(&info);
		doc_free(&d);
	}
	synth_result_free(&s);
	doc_free(&big);
	doc_free(&ba);
}

void suite_wavimport(void)
{
	test_refusals();
	test_result_document();
	test_round_trips();
	test_aspiration();
	test_normalization();
	test_speed();
}
```

In `tests/run_tests.c`, replace:

```c
void suite_track(void);

```

with:

```c
void suite_track(void);
void suite_wavimport(void);

```

In `tests/run_tests.c`, replace:

```c
	{ "track", suite_track },
};
```

with:

```c
	{ "track", suite_track },
	{ "wavimport", suite_wavimport },
};
```

The 32/64-bit comparison also imports the synthesized `ba.csv` (male path) and a 300 ms female vowel (formant
correction path) and appends each imported document's synthesis to `xarch.raw`:

In `tests/test_xarch.c`, replace:

```c
#include <stdio.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"

```

with:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "klatt.h"
#include "measure.h"
#include "csvio.h"
#include "synthrun.h"
#include "wavimport.h"

#define XARCH_MAX 80000

```

In `tests/test_xarch.c`, replace:

```c

void suite_xarch(void)
{
	static short buf[40000];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
```

with:

```c

/* Synthesizes src, imports it (voice type voice) and appends the synthesis of the imported document, so the
 * whole import has to agree between the architectures too. Returns the new n. */
static int imported(const kpe_doc *src, int voice, short *buf, int n, int max)
{
	synth_result s, t;
	kpe_doc d;
	char msg[256];
	CHECK(synth_doc(src, &s) == 0);
	double *x = malloc(sizeof(double) * s.n);
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	import_options o = { voice, s.sr, 0 };
	int rc = import_samples(x, s.n, s.sr, &o, &d, NULL, msg, sizeof msg);
	CHECK(rc == 0);
	if (rc == 0) {
		CHECK(synth_doc(&d, &t) == 0);
		int m = t.n < max - n ? t.n : max - n;
		memcpy(buf + n, t.pcm, sizeof(short) * m);
		n += m;
		synth_result_free(&t);
		doc_free(&d);
	}
	free(x);
	synth_result_free(&s);
	return n;
}

void suite_xarch(void)
{
	static short buf[XARCH_MAX];
	double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
```

In `tests/test_xarch.c`, replace:

```c
	f[KP_FNZ] = 330;
	int n = glide(g, f, buf, 0, 40000);    /* the 1980 source, Rd tilting it */
	g[KG_SRC] = 1;
	g[KG_FL] = 50;
	f[KP_DI] = 30;
	n = glide(g, f, buf, n, 40000);        /* the LF source with flutter and diplophonia */
	FILE *fp = fopen("xarch.raw", "wb");
```

with:

```c
	f[KP_FNZ] = 330;
	int n = glide(g, f, buf, 0, XARCH_MAX);    /* the 1980 source, Rd tilting it */
	g[KG_SRC] = 1;
	g[KG_FL] = 50;
	f[KP_DI] = 30;
	n = glide(g, f, buf, n, XARCH_MAX);        /* the LF source with flutter and diplophonia */

	kpe_doc d;
	csv_report r;
	if (csv_load(L"../../samples/ba.csv", &d, &r) == 0) { /* WAV import, male path */
		n = imported(&d, VOICE_MALE, buf, n, XARCH_MAX);
		doc_free(&d);
	} else {
		CHECK(0);
	}
	t_default_globals(g);
	g[KG_SRC] = 1;
	if (doc_new(&d, g, 300) == 0) { /* WAV import, a high voice: formant correction */
		for (int k = 0; k < d.nframes; k++) {
			double *v = d.frames + (size_t)k * NP;
			v[KP_AV] = k < 4 || k > 55 ? 0 : 60;
			v[KP_F0] = 220 + k * 0.3;
			v[KP_F1] = 850;
			v[KP_F2] = 1220;
			v[KP_F3] = 2810;
		}
		n = imported(&d, VOICE_FEMALE, buf, n, XARCH_MAX);
		doc_free(&d);
	} else {
		CHECK(0);
	}

	FILE *fp = fopen("xarch.raw", "wb");
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `MAKE ARCH=32 check`
Expected: the build stops with `tests/test_wavimport.c:15:10: fatal error: wavimport.h: No such file or directory` (and the same for `tests/test_xarch.c:10:10`).

- [ ] **Step 3: Make the FFT public**

In `kpe/spectro.h`, replace:

```c

#define SPECTRO_FLOOR_DB 20.0 /* below the power of a 1-LSB signal: cells under it are silence */
```

with:

```c

/* In-place iterative radix-2 FFT; n is a power of two. tw_re/tw_im[j] hold cos/sin(-2*pi*j/n) for j in
 * [0, n/2), precomputed once by the caller instead of recomputed in every butterfly. */
void spectro_fft(double *re, double *im, int n, const double *tw_re, const double *tw_im);

#define SPECTRO_FLOOR_DB 20.0 /* below the power of a 1-LSB signal: cells under it are silence */
```

In `kpe/spectro.c`, replace:

```c

/* In-place iterative radix-2 FFT; n is a power of two. tw_re/tw_im[j] hold cos/sin(-2*PI*j/n) for j in
 * [0, n/2), precomputed once per spectro_compute call instead of recomputed in every butterfly. */
static void fft(double *re, double *im, int n, const double *tw_re, const double *tw_im)
{
```

with:

```c

void spectro_fft(double *re, double *im, int n, const double *tw_re, const double *tw_im)
{
```

In `kpe/spectro.c`, replace:

```c
		}
		fft(re, im, nfft, tw_re, tw_im);
		for (int b = 0; b < nbins; b++)
```

with:

```c
		}
		spectro_fft(re, im, nfft, tw_re, tw_im);
		for (int b = 0; b < nbins; b++)
```

- [ ] **Step 4: Move Optimize's default tolerances into the core**

The import simplifies with the same defaults the Optimize dialog shows, so they move out of `dialogs.c`.

In `kpe/optimize.h`, replace:

```c

#endif
```

with:

```c

/* The Optimize dialog's tolerance classes and their defaults: Hz (formants and bandwidths) 20, F0 2 Hz, dB 1,
 * Rd 0.05, % (DI) 2. */
enum { OPT_TOL_HZ, OPT_TOL_F0, OPT_TOL_DB, OPT_TOL_RD, OPT_TOL_PCT, OPT_NTOL };
extern const double opt_tol_default[OPT_NTOL];
/* The tolerance class of frame parameter p. */
int opt_tol_class(int p);

#endif
```

In `kpe/optimize.c`, replace:

```c
#include <stdlib.h>
#include "optimize.h"

```

with:

```c
#include <stdlib.h>
#include <string.h>
#include "klatt.h"
#include "optimize.h"

const double opt_tol_default[OPT_NTOL] = { 20, 2, 1, 0.05, 2 };

int opt_tol_class(int p)
{
	if (p == KP_F0)
		return OPT_TOL_F0;
	if (p == KP_RD)
		return OPT_TOL_RD;
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	if (strcmp(pi.unit, "dB") == 0)
		return OPT_TOL_DB;
	return strcmp(pi.unit, "%") == 0 ? OPT_TOL_PCT : OPT_TOL_HZ;
}

```

In `kpe/dialogs.c`, replace:

```c

/* The tolerance fields, in the order of tol_ids: Hz (formants and bandwidths), F0, dB, Rd and %. */
enum { TOL_HZ, TOL_F0, TOL_DB, TOL_RD, TOL_PCT, NTOL };
static const int tol_ids[NTOL] = { IDC_TOL_HZ, IDC_TOL_F0, IDC_TOL_DB, IDC_TOL_RD, IDC_TOL_PCT };
```

with:

```c

/* The tolerance fields, in the order of the OPT_TOL_* classes (optimize.h). */
#define NTOL OPT_NTOL
static const int tol_ids[NTOL] = { IDC_TOL_HZ, IDC_TOL_F0, IDC_TOL_DB, IDC_TOL_RD, IDC_TOL_PCT };
```

In `kpe/dialogs.c`, replace:

```c
{
	if (p == KP_F0)
		return tol[TOL_F0];
	if (p == KP_RD)
		return tol[TOL_RD];
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	if (strcmp(pi.unit, "dB") == 0)
		return tol[TOL_DB];
	return strcmp(pi.unit, "%") == 0 ? tol[TOL_PCT] : tol[TOL_HZ];
}
```

with:

```c
{
	return tol[opt_tol_class(p)];
}
```

In `kpe/dialogs.c`, replace:

```c
		SendMessageW(grid, CB_SETCURSEL, 0, 0);
		static const double tol_default[NTOL] = { 20, 2, 1, 0.05, 2 };
		for (int i = 0; i < NTOL; i++)
			set_number(dlg, tol_ids[i], tol_default[i]);
		previewed = 0;
```

with:

```c
		SendMessageW(grid, CB_SETCURSEL, 0, 0);
		for (int i = 0; i < NTOL; i++)
			set_number(dlg, tol_ids[i], opt_tol_default[i]);
		previewed = 0;
```

- [ ] **Step 5: Pitch: low-pass at 1.5 kHz, faster refinement**

Unfiltered, a voiced fricative's frication hides its voicing (a synthetic /z/ read aperiodicity 0.6-0.8); the YIN
paper recommends the low-pass.

In `kpe/pitch.h`, replace:

```c

/* The whole signal: nframes frames centred on centres[k]. Fills f0[k] (Hz, smoothed; 0 where unvoiced), ap[k] and
 * voiced[k]. Returns 0, or -1 when out of memory. */
int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
```

with:

```c

/* The whole signal, low-passed at 1.5 kHz first: nframes frames centred on centres[k]. Fills f0[k] (Hz, smoothed;
 * 0 where unvoiced), ap[k] and voiced[k]. Frames more than 40 dB below the loudest are unvoiced; a frame next to a
 * voiced one stays voiced while its aperiodicity is below 0.45. Returns 0, or -1 when out of memory. */
int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
```

In `kpe/pitch.c`, replace:

```c
                              * must be backed by a clearly deeper dip in several frames */
#define PITCH_HOLD 0.4       /* hysteresis: a frame next to a voiced one stays voiced while its aperiodicity is below
                              * this (the threshold starts voicing; transitions, e.g. into diplophonia, reach 0.25) */
#define PITCH_GATE_MS 10.0   /* the window whose energy PITCH_FLOOR_DB applies to */
#define PITCH_MEDIAN 5       /* frames in the median that smooths each voiced run's F0 */
```

with:

```c
                              * must be backed by a clearly deeper dip in several frames */
#define PITCH_HOLD 0.45      /* hysteresis: a frame next to a voiced one stays voiced while its aperiodicity is below
                              * this (the threshold starts voicing). Tuned on the import's round trips: fast formant
                              * transitions after a burst or into a fricative read 0.3-0.44; aspiration 0.45 and up
                              * unfiltered, down to 0.24 low-passed (so aspiration never starts voicing) */
#define PITCH_GATE_MS 10.0   /* the window whose energy PITCH_FLOOR_DB applies to */
#define PITCH_LOWPASS_HZ 1500.0 /* pitch_track low-passes the signal first, as the YIN paper recommends (its
                                 * section on low-pass prefiltering): frication above it would otherwise hide the
                                 * voicing of a voiced fricative (a /z/ with AV 50 under AF 58 read aperiodicity
                                 * 0.6-0.8 unfiltered); at 1000 Hz aspiration through F1 alone looked periodic */
#define PITCH_LOWPASS_MS 4.0 /* half-length of that FIR (Hann-windowed sinc): a transition about 500 Hz wide */
#define PITCH_MEDIAN 5       /* frames in the median that smooths each voiced run's F0 */
```

In `kpe/pitch.c`, replace:

```c
#define INTERP_HALF 8        /* windowed-sinc taps each side for the difference at a fractional lag */
#define REFINE_MAX 8         /* dips (the deepest at whole lags) evaluated at their fractional lag */

```

with:

```c
#define INTERP_HALF 8        /* windowed-sinc taps each side for the difference at a fractional lag */
#define REFINE_MAX 5         /* dips (the deepest at whole lags) evaluated at their fractional lag */

```

In `kpe/pitch.c`, replace:

```c

int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
```

with:

```c

/* Zero-phase FIR low-pass of x into y (n samples; beyond the ends counts as zero). */
static void lowpass(const double *x, long n, int sr, double *y)
{
	int h = (int)lround(PITCH_LOWPASS_MS * sr / 1000);
	double fc = PITCH_LOWPASS_HZ / sr, tap[1024];
	if (h > 511)
		h = 511;
	for (int m = -h; m <= h; m++) {
		double s = m == 0 ? 2 * fc : sin(2 * PI * fc * m) / (PI * m);
		tap[m + h] = s * (0.5 + 0.5 * cos(PI * m / (h + 1)));
	}
	for (long i = 0; i < n; i++) {
		double acc = 0;
		long a = i - h < 0 ? -(i - h) : 0, b = i + h >= n ? 2 * h - (i + h - n + 1) : 2 * h;
		for (long m = a; m <= b; m++)
			acc += tap[m] * x[i - h + m];
		y[i] = acc;
	}
}

int pitch_track(const double *x, long n, int sr, const long *centres, int nframes, double fmin, double fmax,
```

In `kpe/pitch.c`, replace:

```c
	double *raw = malloc(sizeof(double) * (nframes > 0 ? nframes : 1));
	if (!pf || !raw) {
		free(pf);
		free(raw);
		return -1;
	}
	double top = 0;
```

with:

```c
	double *raw = malloc(sizeof(double) * (nframes > 0 ? nframes : 1));
	double *lp = malloc(sizeof(double) * (n > 0 ? n : 1));
	if (!pf || !raw || !lp) {
		free(pf);
		free(raw);
		free(lp);
		return -1;
	}
	lowpass(x, n, sr, lp);
	double top = 0;
```

In `kpe/pitch.c`, replace:

```c
	for (int k = 0; k < nframes; k++) {
		pitch_yin_frame(x, n, sr, centres[k], fmin, fmax, &pf[k]);
		if (!(raw[k] > top * pow(10, -PITCH_FLOOR_DB / 10))) {
```

with:

```c
	for (int k = 0; k < nframes; k++) {
		pitch_yin_frame(lp, n, sr, centres[k], fmin, fmax, &pf[k]);
		if (!(raw[k] > top * pow(10, -PITCH_FLOOR_DB / 10))) {
```

In `kpe/pitch.c`, replace:

```c
	free(raw);
	return rc;
```

with:

```c
	free(raw);
	free(lp);
	return rc;
```

- [ ] **Step 6: Write the pipeline**

Create `kpe/wavimport.h`:

```c
/* wavimport.h - File > Import WAV: analyses a recording into a Klatt parameter document (F0, voicing, F1-F4,
 * B1-B3, Rd, AV, AH, AF and the parallel amplitudes). No Win32 code. */
#ifndef KPE_WAVIMPORT_H
#define KPE_WAVIMPORT_H

#include <stddef.h>
#include <wchar.h>
#include "model.h"

enum { VOICE_MALE, VOICE_FEMALE, VOICE_CHILD, VOICE_COUNT };

typedef struct {
	int voice;     /* VOICE_* */
	int sr;        /* target sample rate: the document's SR */
	int simplify;  /* run Optimize on every track afterwards (every frame grid, default tolerances) */
} import_options;

/* The voice types' F0 search ranges and reference F1-F4 (spec 4.4). */
typedef struct {
	const char *name;
	double f0_min, f0_max;
	double ref[4];
} voice_info;
const voice_info *import_voice(int voice);

/* Frame classes. */
enum { CLS_SILENCE, CLS_VOICED, CLS_VOICED_FRIC, CLS_FRIC, CLS_ASPIRATION, CLS_COUNT };

/* Per-frame analysis results the tests and the diagnostics look at (all arrays nframes long). */
typedef struct {
	int nframes;
	unsigned char *cls;     /* CLS_* */
	double *ap;             /* YIN aperiodicity */
	double *hf;             /* fraction of the energy above the fricative cutoff */
	double *level;          /* frame level, dB (arbitrary reference) */
	double *top;            /* import_levels.top of the frame */
	int rd_frames;          /* frames whose Rd was fitted by analysis-by-synthesis */
	double seconds[4];      /* time spent: analysis, Rd, calibration, total */
} import_info;
void import_info_free(import_info *info);

/* Levels of the frame centred on sample centre of x (n samples at sr), in dB with a fixed reference, measured
 * the same way on a recording and on a synthesis: a 25 ms Hann window, no pre-emphasis. f[0..5] are F1-F6: low is
 * the band below the F1/F2 midpoint, band[i] (i = 0..4, for A2-A6) the band from the midpoint below F(i+2) to the
 * one above it (the last one ends at sr/2), hf the fraction of the energy above min(4000 Hz, 0.35 sr), and top the
 * power density (per Hz) from 0.41 sr to sr/2 relative to the densest 500 Hz band between 1 kHz and 0.41 sr, in dB:
 * about 0 for the flat or rising spectrum of a fricative, -15 to -55 for a vowel's falling one. */
typedef struct {
	double total, low, band[5], hf, top;
} import_levels;
void import_measure(const double *x, long n, int sr, long centre, const double *f, import_levels *out);

/* The sample frame k's analysis window is centred on: the middle of the samples the synthesizer renders for it. */
long import_frame_centre(int sr, double frame_ms, int k);

/* Analyses x (n samples at sr_in Hz, in [-1, 1)) into *d, a new document (not dirty). info may be NULL; otherwise
 * it receives the per-frame analysis (free it with import_info_free). Returns 0, or -1 with msg set (UTF-8) and
 * *d untouched: too long (over DOC_MAX_DURATION_MS), no sound (peak below -60 dBFS), bad arguments, out of memory. */
int import_samples(const double *x, long n, int sr_in, const import_options *o, kpe_doc *d, import_info *info,
	char *msg, size_t msglen);

/* Reads the WAV file (wavin.h) and imports it. Same return and msg as import_samples, plus the reader's refusals. */
int import_wav(const wchar_t *path, const import_options *o, kpe_doc *d, import_info *info, char *msg,
	size_t msglen);

#endif
```

Create `kpe/wavimport.c`:

```c
/* wavimport.c - see wavimport.h. The pipeline of spec 4.3: resample, per-frame pitch (pitch.c), LPC candidates
 * (lpc.c) and formant tracks (track.c), frame classes, start levels, the formants of a high voice corrected by
 * analysis-by-synthesis, Rd by analysis-by-synthesis through the DLL, levels calibrated against the synthesizer
 * (the frication fitted with a model of the parallel branch), and the result document. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wavimport.h"
#include "wavin.h"
#include "resample.h"
#include "lpc.h"
#include "pitch.h"
#include "track.h"
#include "spectro.h"
#include "synthrun.h"
#include "optimize.h"

#define PI 3.14159265358979323846

#define IMPORT_FRAME_MS 5.0   /* the result document's FRAME_MS (spec 4.3) */
#define NO_SOUND_DBFS -60.0   /* a recording whose peak is below this is refused as silent (spec 4.2) */
#define SILENCE_DB 50.0       /* frames this far below the loudest are silence (spec 4.3 step 6) */
#define QUIET_DB 40.0         /* frames this far below the loudest add no cost to the formant tracks (step 3) */
#define HF_CUTOFF_HZ 4000.0   /* the fricative energy lies above min(this, HF_CUTOFF_FRAC * SR) (step 6) */
#define HF_CUTOFF_FRAC 0.35
#define FRIC_HF 0.25          /* an unvoiced frame is a fricative from this high-frequency fraction on, else
                               * aspiration: on the test corpus aspiration reads up to 0.06, /s/ 0.76 and up */
#define VFRIC_HF 0.15         /* a voiced frame is a voiced fricative from this fraction on: vowels read up to 0.09
                               * (a female /i/, F3 3310 Hz), /z/ 0.22 and up (both on frames 10 ms from a boundary) */
#define TOP_VFRIC_DB -12.0    /* ...or its upper band is this flat (import_levels.top). At 11025 Hz the fraction above
                               * 3859 Hz misses real voiced fricatives (the user's /z/ 0.035-0.09, /zh/ 0.009-0.027,
                               * /v/ 0.003-0.010 against vowels up to 0.03); the upper band's flatness separates them:
                               * /v/ -9..-4, /z/ -5..2, /zh/ -12..-5 dB (10th-90th percentile), the vowels' middle
                               * parts -40..-11 (90th percentile -11 to -30), the synthetic vowels -19 at most */
#define TOP_FRIC_DB -25.0     /* an unvoiced frame is a fricative by a flat upper band from here on... */
#define LOW_FRIC_DB -8.0      /* ...if at most this share (dB) of its energy lies below the F1/F2 midpoint: the user's
                               * /sh/ reads -18..-10 dB flatness and -6..-15 dB low share, the breathy onsets and
                               * offsets of the vowels -29..-12 and -8..0, synthetic aspiration -46..-32 */
#define CLASS_SMOOTH 5        /* the fricative decision is the majority of the non-silent frames within this many... */
#define FRIC_MIN_FRAMES 8     /* ...and a fricative run shorter than this (40 ms) is none: the user's /ih/ has a 25 ms
                               * noisy patch with a flat upper band mid-vowel */
#define TOP_FROM_FRAC 0.41    /* import_levels.top: the upper band from this fraction of SR to SR/2... */
#define TOP_REF_FROM_HZ 1000.0 /* ...against the densest band of TOP_REF_WIDTH_HZ from here up to it */
#define TOP_REF_WIDTH_HZ 500.0
#define TOP_REF_BANDS 40
#define LEVEL_FLOOR -200.0    /* dB given to an empty band */
#define AP_ASP_ON 0.25        /* a voiced frame gets aspiration only from this (smoothed) aperiodicity on: the user's
                               * modal vowels read 0.01-0.2 in their middle halves (0.15 left 5-11 frames with AH,
                               * 0.25 none in AE and OW and 3 of 49 in IH's noisy patch); aspiration 5 dB below AV
                               * reads 0.2-0.4, 10 dB below 0.09-0.13 (and stays off) */
#define AP_SMOOTH 5           /* frames in the aperiodicity's median */
#define NORM_HEADROOM_DB 24.0 /* the normalization's synthesis runs this far below the default G0, clear of clipping */
#define NORM_PEAK_DB -1.0     /* the resynthesis peaks at most here (dBFS) after normalization */

#define REF_LEVEL 60.0        /* the amplitude the reference syntheses use for the start values */
#define CAL_PASSES 2          /* level corrections after the first pass (which synthesizes voicing and aspiration
                               * and fits the frication): three syntheses in all, as spec 4.3 step 8 has it. A
                               * third correction pass moved the fricative bands' 90th percentile by 0.1 dB */
#define FRIC_WEAK_WEIGHT 0.1  /* weight of a band more than BAND_RANGE_DB below the strongest in the frication fit */
#define FRIC_SWEEPS 6         /* coordinate sweeps of the frication fit */
#define FRIC_STEP_DB 12.0     /* its pattern search: first step... */
#define FRIC_STEP_MIN_DB 0.25 /* ...and last */
#define PAR_TOP 70.0          /* the largest parallel amplitude of a fricative frame (AF takes the rest), clear of the
                               * 80 dB limit so a correction never clamps */
#define BAND_RANGE_DB 30.0    /* fricative bands more than this below the frame's strongest weigh less in the fit */

#define FCORR_PASSES 2        /* formant correction passes (F1-F3 by analysis-by-synthesis) */
#define FCORR_MIN_F0 150.0    /* ...for a voice whose median F0 is this or higher, where LPC's bias matters: it took
                               * the female vowels' F1 from 5.5 % median / 13.8 % 90th-percentile error to 3.2 /
                               * 9.2 %; a male voice gains less (F1 median 1.9 -> 0.9 %) than the passes cost (0.6 s
                               * per 60 s) */
#define FCORR_SEARCH 0.25     /* the synthesis's candidate must lie within this fraction of the current value */
#define FCORR_MAX_STEP 0.2    /* and a pass moves a formant by at most this fraction */

#define RD_STEP 12            /* Rd is fitted on every RD_STEP-th voiced frame (60 ms) and interpolated between: the
                               * candidate syntheses are the import's main cost (every 4th frame took 7.7 s of a
                               * 60 s import; every 12th 1.6 s, with the same accuracy on the test corpus) */
#define RD_TOL 0.1            /* golden-section search stops when the Rd bracket is this narrow: the midpoint is
                               * then within 0.05 (Optimize's default Rd tolerance) */
#define RD_NEAR 0.5           /* a fit in the same voiced run as the previous one first searches this far on either
                               * side of it (the whole range again if the minimum lands on that edge): 7 syntheses
                               * instead of 9. 0.3 fell back to the whole range too often and was slower */
#define RD_MAX_ITER 20
#define RD_AV 60.0            /* candidate level: the comparison is mean-normalized, so AV only has to keep the
                               * candidate well above the 16-bit rounding */
#define RD_WARM_MS 10.0       /* the candidate is synthesized at least this long before the window... */
#define RD_WARM_PERIODS 3.0   /* ...and at least this many periods: with 1.5 the Rd sweep's 90th-percentile error
                               * grew from 0.09 to 0.38 (the source is not steady yet) */
#define RD_BAND_MAX_HZ 5000.0 /* comparison band: F0 to min(5 F0, this) (fextr's Method B) */
#define RD_MEDIAN 3           /* fitted Rd values are median-smoothed over this many fits */

static const voice_info voices[VOICE_COUNT] = {
	{ "Adult male", 60, 250, { 500, 1500, 2500, 3500 } },
	{ "Adult female", 120, 400, { 560, 1680, 2800, 3900 } },
	{ "Child", 180, 500, { 650, 1950, 3250, 4400 } },
};

const voice_info *import_voice(int voice)
{
	return voice >= 0 && voice < VOICE_COUNT ? &voices[voice] : NULL;
}

void import_info_free(import_info *info)
{
	free(info->cls);
	free(info->ap);
	free(info->hf);
	free(info->level);
	free(info->top);
	memset(info, 0, sizeof *info);
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

long import_frame_centre(int sr, double frame_ms, int k)
{
	return (long)floor((k + 0.5) * sr * frame_ms / 1000 + 1e-9);
}

static double param_min(int p)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	return pi.min;
}

static double param_max(int p)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_FRAME, p, &pi);
	return pi.max;
}

static int sr_ok(int sr)
{
	klatt_param_info pi;
	KlattParamInfo(KLATT_GLOBAL, KG_SR, &pi);
	return sr >= pi.min && sr <= pi.max;
}

/* Clamped to the parameter's range and rounded to its step. */
static double settle(int p, double v)
{
	double lo = param_min(p), hi = param_max(p), s = doc_param_step(p);
	v = v < lo ? lo : v > hi ? hi : v;
	return round(v / s) * s;
}

/* ---- spectra ---- */

typedef struct {
	int sr, len, nfft;
	double *re, *im, *tw_re, *tw_im, *hann, *buf;
	double *hw_re, *hw_im;   /* twiddles of the nfft/2-point FFT the real input is packed into */
	double *cw, *sw;         /* cos and sin of w and 2w for each bin (w = 2 pi bin / nfft): resonator responses */
} spec_ctx;

static void spec_free(spec_ctx *c)
{
	free(c->re);
	free(c->im);
	free(c->tw_re);
	free(c->tw_im);
	free(c->hann);
	free(c->buf);
	free(c->hw_re);
	free(c->hw_im);
	free(c->cw);
	free(c->sw);
	memset(c, 0, sizeof *c);
}

static int spec_init(spec_ctx *c, int sr)
{
	memset(c, 0, sizeof *c);
	c->sr = sr;
	c->len = lpc_window_len(sr);
	c->nfft = 256;
	while (c->nfft < c->len)
		c->nfft <<= 1;
	c->re = malloc(sizeof(double) * c->nfft);
	c->im = malloc(sizeof(double) * c->nfft);
	c->tw_re = malloc(sizeof(double) * c->nfft / 2);
	c->tw_im = malloc(sizeof(double) * c->nfft / 2);
	c->hann = malloc(sizeof(double) * c->len);
	c->buf = malloc(sizeof(double) * c->len);
	c->hw_re = malloc(sizeof(double) * c->nfft / 4);
	c->hw_im = malloc(sizeof(double) * c->nfft / 4);
	c->cw = malloc(sizeof(double) * (c->nfft + 2));
	c->sw = malloc(sizeof(double) * (c->nfft + 2));
	if (!c->re || !c->im || !c->tw_re || !c->tw_im || !c->hann || !c->buf || !c->hw_re || !c->hw_im || !c->cw ||
	    !c->sw) {
		spec_free(c);
		return -1;
	}
	for (int j = 0; j < c->nfft / 2; j++) {
		c->tw_re[j] = cos(-2 * PI * j / c->nfft);
		c->tw_im[j] = sin(-2 * PI * j / c->nfft);
	}
	for (int b = 0; b <= c->nfft / 2; b++) {
		double w = 2 * PI * b / c->nfft;
		c->cw[2 * b] = cos(w);
		c->cw[2 * b + 1] = cos(2 * w);
		c->sw[2 * b] = sin(w);
		c->sw[2 * b + 1] = sin(2 * w);
	}
	for (int j = 0; j < c->nfft / 4; j++) {
		c->hw_re[j] = c->tw_re[2 * j];
		c->hw_im[j] = c->tw_im[2 * j];
	}
	for (int i = 0; i < c->len; i++)
		c->hann[i] = 0.5 - 0.5 * cos(2 * PI * i / (c->len - 1));
	return 0;
}

/* Power spectrum of v[0..len-1] (zero-padded to nfft) left in re[0..nfft/2]. The real input is packed into an
 * nfft/2-point complex FFT (even samples real, odd imaginary) and split afterwards: half the work. */
static void power_spectrum(spec_ctx *c, const double *v)
{
	int m = c->nfft / 2;
	double *zr = c->im, *zi = c->im + m; /* the packed transform, in the spare im buffer */
	for (int i = 0; i < m; i++) {
		zr[i] = 2 * i < c->len ? v[2 * i] : 0;
		zi[i] = 2 * i + 1 < c->len ? v[2 * i + 1] : 0;
	}
	spectro_fft(zr, zi, m, c->hw_re, c->hw_im);
	for (int k = 0; k <= m; k++) {
		int a = k % m, b = (m - k) % m;
		double er = 0.5 * (zr[a] + zr[b]), ei = 0.5 * (zi[a] - zi[b]);   /* even part: (Z[k] + conj Z[m-k]) / 2 */
		double orr = 0.5 * (zi[a] + zi[b]), oi = -0.5 * (zr[a] - zr[b]); /* odd part: (Z[k] - conj Z[m-k]) / 2i */
		double wr = k < m ? c->tw_re[k] : -1, wi = k < m ? c->tw_im[k] : 0;
		double xr = er + wr * orr - wi * oi, xi = ei + wr * oi + wi * orr;
		c->re[k] = xr * xr + xi * xi;
	}
}

static double to_db(double p)
{
	return p > 0 ? 10 * log10(p) : LEVEL_FLOOR;
}

/* The band [lo, hi) of each formant F1-F6 (f[0..5]): edges midway to the neighbouring formants in frequency order
 * (formants need not be ordered: F4 may lie above F5), the first from 0, the last to sr/2. */
static void band_edges(const double *f, double sr, double *lo, double *hi)
{
	int idx[6] = { 0, 1, 2, 3, 4, 5 };
	for (int i = 1; i < 6; i++)
		for (int j = i; j > 0 && f[idx[j - 1]] > f[idx[j]]; j--) {
			int t = idx[j];
			idx[j] = idx[j - 1];
			idx[j - 1] = t;
		}
	for (int r = 0; r < 6; r++) {
		lo[idx[r]] = r == 0 ? 0 : 0.5 * (f[idx[r - 1]] + f[idx[r]]);
		hi[idx[r]] = r == 5 ? sr / 2 + 1 : 0.5 * (f[idx[r]] + f[idx[r + 1]]);
	}
}

static void measure(spec_ctx *c, const double *x, long n, long centre, const double *f, import_levels *out)
{
	long start = centre - c->len / 2;
	for (int i = 0; i < c->len; i++) {
		long s = start + i;
		c->buf[i] = (s >= 0 && s < n ? x[s] : 0) * c->hann[i];
	}
	power_spectrum(c, c->buf);
	double lo[6], hi[6], total = 0, low = 0, high = 0, band[5] = { 0 }, upper = 0, ref[TOP_REF_BANDS] = { 0 };
	int nupper = 0, nref[TOP_REF_BANDS] = { 0 };
	double cut = HF_CUTOFF_HZ < HF_CUTOFF_FRAC * c->sr ? HF_CUTOFF_HZ : HF_CUTOFF_FRAC * c->sr;
	band_edges(f, c->sr, lo, hi);
	double low_edge = 0.5 * (f[0] + f[1]);
	for (int b = 1; b <= c->nfft / 2; b++) {
		double fr = (double)b * c->sr / c->nfft, p = c->re[b];
		total += p;
		if (fr < low_edge)
			low += p;
		if (fr >= cut)
			high += p;
		for (int i = 0; i < 5; i++)
			if (fr >= lo[i + 1] && fr < hi[i + 1])
				band[i] += p;
		if (fr >= TOP_FROM_FRAC * c->sr) {
			upper += p;
			nupper++;
		} else if (fr >= TOP_REF_FROM_HZ) {
			int q = (int)((fr - TOP_REF_FROM_HZ) / TOP_REF_WIDTH_HZ);
			if (q < TOP_REF_BANDS) {
				ref[q] += p;
				nref[q]++;
			}
		}
	}
	double dense = 0;
	for (int q = 0; q < TOP_REF_BANDS; q++)
		if (nref[q] && ref[q] / nref[q] > dense)
			dense = ref[q] / nref[q];
	out->top = nupper && upper > 0 && dense > 0 ? 10 * log10(upper / nupper / dense) : LEVEL_FLOOR;
	out->total = to_db(total);
	out->low = to_db(low);
	for (int i = 0; i < 5; i++)
		out->band[i] = to_db(band[i]);
	out->hf = total > 0 ? high / total : 0;
}

void import_measure(const double *x, long n, int sr, long centre, const double *f, import_levels *out)
{
	spec_ctx c;
	memset(out, 0, sizeof *out);
	if (spec_init(&c, sr) != 0)
		return;
	measure(&c, x, n, centre, f, out);
	spec_free(&c);
}

/* ---- Rd by analysis-by-synthesis (step 7) ---- */

typedef struct {
	spec_ctx *spec;
	klatt_h h;
	int order, per;          /* LPC order; DLL samples per frame */
	short *pcm;              /* candidate synthesis */
	double *x;               /* the same as doubles */
	int nsyn, nframes_syn;   /* samples allocated; frames synthesized per candidate for the current frame */
	double *w, *res;         /* windowed frame, residual */
	double a[LPC_MAX_ORDER + 1];
	double frame[NP];        /* the candidate's frame parameters */
	int b0, b1;              /* comparison band bins */
	double *target;          /* the WAV frame's mean-normalized dB spectrum over the band */
	double *cand;
} rd_ctx;

/* Inverse-filters the windowed frame w with a (FIR, zero history) and leaves the band's mean-normalized dB
 * spectrum in out. */
static void residual_band_db(rd_ctx *r, const double *w, double *out)
{
	int len = r->spec->len;
	for (int i = 0; i < len; i++) {
		double e = 0;
		for (int j = 0; j <= r->order && j <= i; j++)
			e += r->a[j] * w[i - j];
		r->res[i] = e;
	}
	power_spectrum(r->spec, r->res);
	double mean = 0;
	for (int b = r->b0; b <= r->b1; b++) {
		out[b - r->b0] = 10 * log10(r->spec->re[b] + 1e-30);
		mean += out[b - r->b0];
	}
	mean /= r->b1 - r->b0 + 1;
	for (int b = r->b0; b <= r->b1; b++)
		out[b - r->b0] -= mean;
}

static double rd_error(rd_ctx *r, double rd)
{
	r->frame[KP_RD] = rd;
	KlattReset(r->h, 1);
	int n = 0;
	for (int k = 0; k < r->nframes_syn; k++) {
		int got = KlattSynthFrame(r->h, r->frame, r->pcm + n, r->per);
		if (got < 0)
			return 1e300;
		n += got;
	}
	for (int i = 0; i < n; i++)
		r->x[i] = r->pcm[i] / 32768.0;
	int len = r->spec->len;
	lpc_frame(r->x, n, n - len + len / 2, len, r->w); /* the window ends with the synthesis */
	residual_band_db(r, r->w, r->cand);
	double e = 0;
	for (int b = 0; b <= r->b1 - r->b0; b++) {
		double d = r->cand[b] - r->target[b];
		e += d * d;
	}
	return e;
}

/* Golden-section search for the Rd between lo and hi with the least rd_error. */
static double golden(rd_ctx *r, double lo, double hi)
{
	double g = (sqrt(5.0) - 1) / 2;
	double c = hi - g * (hi - lo), d = lo + g * (hi - lo), fc = rd_error(r, c), fd = rd_error(r, d);
	for (int it = 0; it < RD_MAX_ITER && hi - lo > RD_TOL; it++) {
		if (fc < fd) {
			hi = d;
			d = c;
			fd = fc;
			c = hi - g * (hi - lo);
			fc = rd_error(r, c);
		} else {
			lo = c;
			c = d;
			fc = fd;
			d = lo + g * (hi - lo);
			fd = rd_error(r, d);
		}
	}
	return 0.5 * (lo + hi);
}

/* Fits Rd for one frame: x is the (resampled) recording, fr the frame's analysed parameters, prior an earlier fit
 * nearby (0: none). Returns the Rd, or NAN if the comparison band is empty. */
static double fit_rd(rd_ctx *r, const double *x, long n, long centre, const double *fr, double prior)
{
	int len = r->spec->len;
	lpc_frame(x, n, centre, len, r->w);
	if (lpc_burg(r->w, len, r->order, r->a) <= 0)
		return NAN;
	double f0 = fr[KP_F0], top = 5 * f0 < RD_BAND_MAX_HZ ? 5 * f0 : RD_BAND_MAX_HZ;
	int sr = r->spec->sr, nfft = r->spec->nfft;
	r->b0 = (int)ceil(f0 * nfft / sr);
	r->b1 = (int)floor(top * nfft / sr);
	if (r->b1 > nfft / 2)
		r->b1 = nfft / 2;
	if (r->b1 - r->b0 < 2)
		return NAN;
	residual_band_db(r, r->w, r->target);
	for (int p = 0; p < NP; p++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_FRAME, p, &pi);
		r->frame[p] = pi.def;
	}
	r->frame[KP_AV] = RD_AV;
	r->frame[KP_F0] = f0;
	for (int p = KP_F1; p <= KP_F4; p++)
		r->frame[p] = fr[p];
	for (int p = KP_B1; p <= KP_B3; p++)
		r->frame[p] = fr[p];
	/* long enough to reach a steady state before the window */
	double spf = sr * IMPORT_FRAME_MS / 1000, warm = RD_WARM_MS * sr / 1000;
	if (warm < RD_WARM_PERIODS * sr / f0)
		warm = RD_WARM_PERIODS * sr / f0;
	r->nframes_syn = (int)ceil((len + warm) / spf) + 1;
	if (r->nframes_syn * r->per > r->nsyn)
		r->nframes_syn = r->nsyn / r->per;
	double rmin = param_min(KP_RD), rmax = param_max(KP_RD);
	if (prior > 0) {
		double lo = prior - RD_NEAR < rmin ? rmin : prior - RD_NEAR, hi = prior + RD_NEAR > rmax ? rmax : prior + RD_NEAR;
		double v = golden(r, lo, hi);
		if ((v - lo > RD_TOL || lo == rmin) && (hi - v > RD_TOL || hi == rmax))
			return v;
	}
	return golden(r, rmin, rmax);
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* Rd for every frame of fr (nframes x NP): fitted on every RD_STEP-th CLS_VOICED frame, median-smoothed over
 * RD_MEDIAN fits, interpolated between fits within a run of voiced frames, and held (the last value) elsewhere. */
static int fit_rd_track(const double *x, long n, int sr, const unsigned char *cls, int nframes, double *fr,
	spec_ctx *spec, int *fitted)
{
	rd_ctx r;
	memset(&r, 0, sizeof r);
	r.spec = spec;
	r.order = lpc_order(sr);
	double g[NG];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SR] = sr;
	g[KG_SRC] = 1;
	g[KG_FRAME_MS] = IMPORT_FRAME_MS;
	if (KlattCreate(g, &r.h) < 0)
		return -1;
	r.per = KlattMaxFrameSamples(r.h);
	double spf = sr * IMPORT_FRAME_MS / 1000;
	int *idx = malloc(sizeof(int) * (nframes + 1));
	double *val = malloc(sizeof(double) * (nframes + 1)), *sm = malloc(sizeof(double) * (nframes + 1));
	/* room for the longest warm-up: RD_WARM_PERIODS at the lowest F0 any voice type searches */
	double maxwarm = RD_WARM_PERIODS * sr / 60.0 > RD_WARM_MS * sr / 1000 ? RD_WARM_PERIODS * sr / 60.0
		: RD_WARM_MS * sr / 1000;
	r.nsyn = ((int)ceil((spec->len + maxwarm) / spf) + 1) * r.per;
	r.pcm = malloc(sizeof(short) * r.nsyn);
	r.x = malloc(sizeof(double) * r.nsyn);
	r.w = malloc(sizeof(double) * spec->len);
	r.res = malloc(sizeof(double) * spec->len);
	r.target = malloc(sizeof(double) * (spec->nfft / 2 + 1));
	r.cand = malloc(sizeof(double) * (spec->nfft / 2 + 1));
	int rc = -1;
	if (!idx || !val || !sm || !r.pcm || !r.x || !r.w || !r.res || !r.target || !r.cand)
		goto done;
	int nfit = 0;
	for (int k = 0; k < nframes; k++) {
		if (cls[k] != CLS_VOICED || k % RD_STEP != 0)
			continue;
		double prior = 0; /* the previous fit, if no unvoiced frame lies between */
		if (nfit > 0) {
			int gap = 0;
			for (int j = idx[nfit - 1]; j <= k && !gap; j++)
				gap = cls[j] != CLS_VOICED && cls[j] != CLS_VOICED_FRIC;
			prior = gap ? 0 : val[nfit - 1];
		}
		double v = fit_rd(&r, x, n, import_frame_centre(sr, IMPORT_FRAME_MS, k), fr + (size_t)k * NP, prior);
		if (isnan(v))
			continue;
		idx[nfit] = k;
		val[nfit++] = v;
	}
	*fitted = nfit;
	/* median over neighbouring fits of the same voiced run (no voiced gap between them) */
	for (int i = 0; i < nfit; i++) {
		double win[RD_MEDIAN];
		int m = 0;
		for (int j = i - RD_MEDIAN / 2; j <= i + RD_MEDIAN / 2; j++) {
			if (j < 0 || j >= nfit)
				continue;
			int a = idx[j < i ? j : i], b = idx[j < i ? i : j], gap = 0;
			for (int k = a; k <= b && !gap; k++)
				gap = cls[k] != CLS_VOICED && cls[k] != CLS_VOICED_FRIC;
			if (!gap)
				win[m++] = val[j];
		}
		qsort(win, m, sizeof(double), cmp_double);
		sm[i] = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
	}
	/* fill: interpolate between consecutive fits when every frame between them is voiced, else hold */
	double last = 1.0;
	int next = 0;
	for (int k = 0; k < nframes; k++) {
		while (next < nfit && idx[next] < k)
			next++;
		double v;
		if (next < nfit && idx[next] == k) {
			v = sm[next];
		} else if (next > 0 && next < nfit) {
			int a = idx[next - 1], b = idx[next], gap = 0;
			for (int j = a; j <= b && !gap; j++)
				gap = cls[j] != CLS_VOICED && cls[j] != CLS_VOICED_FRIC;
			v = gap ? last : sm[next - 1] + (sm[next] - sm[next - 1]) * (k - a) / (b - a);
		} else if (next == 0 && nfit > 0) {
			v = sm[0]; /* before the first fit: its value */
		} else {
			v = last;
		}
		fr[(size_t)k * NP + KP_RD] = settle(KP_RD, v);
		last = v;
	}
	rc = 0;
done:
	KlattDestroy(r.h);
	free(idx);
	free(val);
	free(sm);
	free(r.pcm);
	free(r.x);
	free(r.w);
	free(r.res);
	free(r.target);
	free(r.cand);
	return rc;
}

/* ---- levels (step 8) ---- */

/* The six formant frequencies F1-F6 the bands of frame fr are centred on. */
static void formants_of(const double *fr, const double *g, double *f)
{
	f[0] = fr[KP_F1];
	f[1] = fr[KP_F2];
	f[2] = fr[KP_F3];
	f[3] = fr[KP_F4];
	f[4] = g[KG_F5];
	f[5] = g[KG_F6];
}

/* Levels of a steady synthesis of frame fr (60 frames, measured in the middle) with globals g. */
static int reference_levels(const double *g, const double *fr, spec_ctx *spec, import_levels *out)
{
	kpe_doc d;
	double *frames = malloc(sizeof(double) * NP * 60);
	if (!frames)
		return -1;
	for (int k = 0; k < 60; k++)
		memcpy(frames + (size_t)k * NP, fr, sizeof(double) * NP);
	doc_init_from(&d, g, 60, frames);
	synth_result s;
	int rc = synth_doc(&d, &s);
	if (rc == 0) {
		double *x = malloc(sizeof(double) * s.n), f[6];
		if (x) {
			for (int i = 0; i < s.n; i++)
				x[i] = s.pcm[i] / 32768.0;
			formants_of(fr, g, f);
			measure(spec, x, s.n, s.n / 2, f, out);
			free(x);
		} else {
			rc = -1;
		}
		synth_result_free(&s);
	}
	doc_free(&d);
	return rc;
}

/* Measures every frame of the synthesis of d into lv. */
static int measure_synth(const kpe_doc *d, spec_ctx *spec, import_levels *lv)
{
	synth_result s;
	if (synth_doc(d, &s) != 0)
		return -1;
	double *x = malloc(sizeof(double) * (s.n > 0 ? s.n : 1));
	if (!x) {
		synth_result_free(&s);
		return -1;
	}
	for (int i = 0; i < s.n; i++)
		x[i] = s.pcm[i] / 32768.0;
	for (int k = 0; k < d->nframes; k++) {
		double f[6];
		formants_of(d->frames + (size_t)k * NP, d->globals, f);
		measure(spec, x, s.n, import_frame_centre(s.sr, IMPORT_FRAME_MS, k), f, &lv[k]);
	}
	free(x);
	synth_result_free(&s);
	return 0;
}

/* AH in a voiced frame of amplitude av: off (0) below the aperiodicity AP_ASP_ON, else av + 10 log10(ap / (1 - ap))
 * (spec 4.3 step 8), ap being YIN's aperiodicity median-smoothed over AP_SMOOTH frames. Checked against the
 * synthesizer: vowels synthesized with AH 15, 10 and 5 dB below AV import with AH 15, 10 and 5 dB below (Rd 1) and
 * 13, 8 and 3 (Rd 2.2) when the threshold is off, so the formula needs no correction factor. */
static double voiced_ah(double av, double ap)
{
	if (ap < AP_ASP_ON)
		return 0;
	ap = ap > 1 - 1e-4 ? 1 - 1e-4 : ap;
	return settle(KP_AH, av + 10 * log10(ap / (1 - ap)));
}

/* ---- the frication model: the parallel resonators A2-A6 as the DLL sums them ---- */

/* Complex response a / (1 - b e^-jw - c e^-2jw) of a Klatt (1980) resonator at FFT bin `bin`. */
static void reson_response(const spec_ctx *s, double a, double b, double c, int bin, double *re, double *im)
{
	double dr = 1 - b * s->cw[2 * bin] - c * s->cw[2 * bin + 1], di = b * s->sw[2 * bin] + c * s->sw[2 * bin + 1];
	double d = dr * dr + di * di;
	*re = a * dr / d;
	*im = -a * di / d;
}

/* q[b][j][k]: band b's power is the quadratic form sum_jk x_j x_k q[b][j][k] of the resonators' gains x (A2-A6,
 * F2-F6 with the synthesizer's bandwidths, summed with the DLL's alternating signs: -R2 +R3 -R4 +R5 -R6), for
 * white noise, over the FFT bins of the band. Neighbouring resonators leak into each other's bands and interfere,
 * so band-by-band corrections alone can settle on a wrong split (a /z/ ended 5-10 dB off in its weak bands). */
static void fric_model(const spec_ctx *c, const double *fr, const double *g, double q[5][5][5])
{
	static const double sign[5] = { -1, 1, -1, 1, -1 };
	double f[6], lo[6], hi[6];
	formants_of(fr, g, f);
	band_edges(f, c->sr, lo, hi);
	const double bw[5] = { fr[KP_B2], fr[KP_B3], g[KG_B4], g[KG_B5], g[KG_B6] };
	double ra[5], rb[5], rc[5]; /* the resonators' coefficients, as the DLL sets them (Klatt 1980) */
	for (int j = 0; j < 5; j++) {
		double fc = f[j + 1] < c->sr * 0.5 ? f[j + 1] : c->sr * 0.5;
		rc[j] = -exp(-2 * PI * bw[j] / c->sr);
		rb[j] = 2 * exp(-PI * bw[j] / c->sr) * cos(2 * PI * fc / c->sr);
		ra[j] = 1 - rb[j] - rc[j];
	}
	memset(q, 0, sizeof(double) * 125);
	for (int bin = 1; bin <= c->nfft / 2; bin++) {
		double fq = (double)bin * c->sr / c->nfft, re[5], im[5];
		int b = -1;
		for (int i = 0; i < 5; i++)
			if (fq >= lo[i + 1] && fq < hi[i + 1])
				b = i;
		if (b < 0)
			continue;
		for (int j = 0; j < 5; j++) {
			reson_response(c, ra[j], rb[j], rc[j], bin, &re[j], &im[j]);
			re[j] *= sign[j];
			im[j] *= sign[j];
		}
		for (int j = 0; j < 5; j++)
			for (int k = 0; k < 5; k++)
				q[b][j][k] += re[j] * re[k] + im[j] * im[k];
	}
}

static double fric_power(double q[5][5][5], const double *x, int b)
{
	double p = 0;
	for (int j = 0; j < 5; j++)
		for (int k = 0; k < 5; k++)
			p += x[j] * x[k] * q[b][j][k];
	return p;
}

/* Squared log errors of the model (scaled by scale) against the target powers t, weight 1 for bands within
 * BAND_RANGE_DB of the strongest target and FRIC_WEAK_WEIGHT below. */
static double fric_cost(double q[5][5][5], double scale, const double *x, const double *t)
{
	double top = 0, e = 0;
	for (int b = 0; b < 5; b++)
		if (t[b] > top)
			top = t[b];
	for (int b = 0; b < 5; b++) {
		double d = log(scale * fric_power(q, x, b) + 1e-30) - log(t[b]);
		e += (t[b] > top * pow(10, -BAND_RANGE_DB / 10) ? 1 : FRIC_WEAK_WEIGHT) * d * d;
	}
	return e;
}

/* Fits the gains x >= 0 (amplitude ratios; x_j = 10^((AF + A_j - 2 REF_LEVEL) / 20)) to the target band powers t
 * by a pattern search on each gain in dB (steps from FRIC_STEP_DB down to FRIC_STEP_MIN_DB), trying 0 too. */
static void fric_fit(double q[5][5][5], double scale, const double *t, double *x)
{
	for (int sweep = 0; sweep < FRIC_SWEEPS; sweep++)
		for (int j = 0; j < 5; j++) {
			double keep = x[j];
			x[j] = 0;
			double c0 = fric_cost(q, scale, x, t);
			x[j] = keep > 0 ? keep : 1e-3;
			double cur = fric_cost(q, scale, x, t);
			for (double step = FRIC_STEP_DB; step >= FRIC_STEP_MIN_DB; step /= 2)
				for (int moved = 1; moved;) {
					moved = 0;
					for (int dir = -1; dir <= 1 && !moved; dir += 2) {
						double old = x[j];
						x[j] = old * pow(10, dir * step / 20);
						double c = fric_cost(q, scale, x, t);
						if (c < cur) {
							cur = c;
							moved = 1;
						} else {
							x[j] = old;
						}
					}
				}
			if (c0 <= cur)
				x[j] = 0;
		}
}

/* AF and A2-A6 of frame v from the fitted gains: the largest amplitude is PAR_TOP, AF takes the rest. */
static void fric_set(double *v, const double *x)
{
	double top = -1e300, tot[5];
	for (int j = 0; j < 5; j++) {
		tot[j] = x[j] > 0 ? 2 * REF_LEVEL + 20 * log10(x[j]) : -1e300; /* AF + A(j+2), dB */
		if (tot[j] > top)
			top = tot[j];
	}
	if (top < -1e299) {
		v[KP_AF] = 0;
		for (int j = 0; j < 5; j++)
			v[KP_A2 + j] = 0;
		return;
	}
	double af = settle(KP_AF, top - PAR_TOP);
	v[KP_AF] = af;
	for (int j = 0; j < 5; j++)
		v[KP_A2 + j] = tot[j] > -1e299 ? settle(KP_A2 + j, tot[j] - af) : 0;
}

/* The gains x of frame v's AF and A2-A6 (see fric_fit). */
static void fric_get(const double *v, double *x)
{
	for (int j = 0; j < 5; j++)
		x[j] = v[KP_AF] > 0 && v[KP_A2 + j] > 0 ? pow(10, (v[KP_AF] + v[KP_A2 + j] - 2 * REF_LEVEL) / 20) : 0;
}

/* ---- formant correction by analysis-by-synthesis ---- */

/* LPC's formant estimates are biased, most at high F0, where a formant between two harmonics is pulled towards
 * one (a female /u/'s F1 of 370 Hz at F0 215 Hz read 420). The synthesizer's output analysed the same way has the
 * same bias, so each pass synthesizes the voicing of the document, measures F1-F3 exactly as on the WAV (the
 * candidate nearest the current value), and moves each formant by the WAV's measurement less the synthesis's. */
static int correct_formants(const double *g, double *frames, int nframes, const unsigned char *cls,
	const double *ftr, spec_ctx *spec)
{
	/* only for a high voice: the median F0 of the voiced frames */
	double *fr = malloc(sizeof(double) * NP * nframes), *w = malloc(sizeof(double) * spec->len), *x = NULL;
	if (!fr || !w) {
		free(fr);
		free(w);
		return -1;
	}
	int nv = 0;
	for (int k = 0; k < nframes; k++)
		if (cls[k] == CLS_VOICED)
			fr[nv++] = frames[(size_t)k * NP + KP_F0];
	qsort(fr, nv, sizeof(double), cmp_double);
	if (nv == 0 || fr[nv / 2] < FCORR_MIN_F0) {
		free(fr);
		free(w);
		return 0;
	}
	int sr = spec->sr, order = lpc_order(sr);
	for (int pass = 0; pass < FCORR_PASSES; pass++) {
		/* the voicing alone: the analysis sees the same source and tract as in the WAV's voiced frames */
		for (int k = 0; k < nframes; k++) {
			double *v = fr + (size_t)k * NP;
			memcpy(v, frames + (size_t)k * NP, sizeof(double) * NP);
			v[KP_AH] = v[KP_AF] = 0;
			for (int p = KP_A1; p <= KP_AB; p++)
				v[p] = 0;
		}
		kpe_doc d;
		doc_init_from(&d, g, nframes, fr);
		synth_result s;
		int rc = synth_doc(&d, &s);
		if (rc != 0 || !(x = realloc(x, sizeof(double) * (s.n > 0 ? s.n : 1)))) {
			if (rc == 0)
				synth_result_free(&s);
			free(fr);
			free(w);
			free(x);
			return -1;
		}
		for (int i = 0; i < s.n; i++)
			x[i] = s.pcm[i] / 32768.0;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP, a[LPC_MAX_ORDER + 1];
			if (cls[k] != CLS_VOICED)
				continue;
			lpc_frame(x, s.n, import_frame_centre(sr, IMPORT_FRAME_MS, k), spec->len, w);
			lpc_cand c[CAND_MAX];
			int nc = lpc_burg(w, spec->len, order, a) > 0 ? lpc_candidates(a, order, sr, c) : 0;
			for (int i = 0; i < 3; i++) {
				double cur = v[KP_F1 + i];
				int best = -1;
				for (int j = 0; j < nc; j++)
					if (fabs(c[j].f - cur) < FCORR_SEARCH * cur && (best < 0 || fabs(c[j].f - cur) < fabs(c[best].f - cur)))
						best = j;
				if (best < 0)
					continue;
				double step = ftr[k * TRACK_N + i] - c[best].f, lim = FCORR_MAX_STEP * cur;
				step = step > lim ? lim : step < -lim ? -lim : step;
				v[KP_F1 + i] = settle(KP_F1 + i, cur + step);
			}
		}
		synth_result_free(&s);
	}
	free(fr);
	free(w);
	free(x);
	return 0;
}

/* ---- the pipeline ---- */

int import_samples(const double *x0, long n0, int sr_in, const import_options *o, kpe_doc *d, import_info *info,
	char *msg, size_t msglen)
{
	clock_t t_start = clock();
	if (info)
		memset(info, 0, sizeof *info);
	if (msglen)
		msg[0] = 0;
	const voice_info *vt = o ? import_voice(o->voice) : NULL;
	if (!vt || !x0 || n0 <= 0 || sr_in <= 0 || !sr_ok(o->sr))
		return fail(msg, msglen, "Cannot import: bad arguments.");
	if (n0 * 1000.0 / sr_in > DOC_MAX_DURATION_MS)
		return fail(msg, msglen, "The recording is longer than 10 minutes.");
	double peak = 0;
	for (long i = 0; i < n0; i++)
		if (fabs(x0[i]) > peak)
			peak = fabs(x0[i]);
	if (!(peak >= pow(10, NO_SOUND_DBFS / 20)))
		return fail(msg, msglen, "No sound found (the recording's peak is below %.0f dBFS).", NO_SOUND_DBFS);

	int sr = o->sr, rc = -1;
	double *x = NULL;
	if (resample(x0, n0, sr_in, sr, &x) != 0)
		return fail(msg, msglen, "Out of memory.");
	long n = resample_length(n0, sr_in, sr);
	double spf = sr * IMPORT_FRAME_MS / 1000;
	int nframes = (int)ceil(n / spf - 1e-9);
	if (nframes < 1)
		nframes = 1;

	double g[NG];
	for (int i = 0; i < NG; i++) {
		klatt_param_info pi;
		KlattParamInfo(KLATT_GLOBAL, i, &pi);
		g[i] = pi.def;
	}
	g[KG_SR] = sr;
	g[KG_SRC] = 1;
	g[KG_FRAME_MS] = IMPORT_FRAME_MS;

	spec_ctx spec = { 0 };
	long *centre = malloc(sizeof(long) * nframes);
	double *f0 = malloc(sizeof(double) * nframes), *ap = malloc(sizeof(double) * nframes);
	unsigned char *voiced = malloc(nframes), *quiet = malloc(nframes), *cls = malloc(nframes);
	lpc_cand *cands = malloc(sizeof(lpc_cand) * CAND_MAX * nframes);
	int *ncand = malloc(sizeof(int) * nframes);
	double *ftr = malloc(sizeof(double) * TRACK_N * nframes), *btr = malloc(sizeof(double) * TRACK_N * nframes);
	import_levels *lw = malloc(sizeof(import_levels) * nframes), *ls = malloc(sizeof(import_levels) * nframes);
	double *frames = malloc(sizeof(double) * NP * nframes), *w = NULL, *level = malloc(sizeof(double) * nframes);
	double *hf = malloc(sizeof(double) * nframes), *upper = malloc(sizeof(double) * nframes);
	double *lowrel = malloc(sizeof(double) * nframes), *aps = malloc(sizeof(double) * nframes);
	unsigned char *fric = malloc(nframes);
	double *vb = NULL;
	kpe_doc tmp = { 0 };
	int have_tmp = 0;
	if (!centre || !f0 || !ap || !voiced || !quiet || !cls || !cands || !ncand || !ftr || !btr || !lw || !ls ||
	    !frames || !level || !hf || !upper || !lowrel || !aps || !fric || spec_init(&spec, sr) != 0 ||
	    !(w = malloc(sizeof(double) * spec.len))) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}

	/* per frame: LPC candidates (step 2) and the levels with neutral formants (for classes and quiet frames) */
	int order = lpc_order(sr);
	double top = LEVEL_FLOOR;
	for (int k = 0; k < nframes; k++) {
		centre[k] = import_frame_centre(sr, IMPORT_FRAME_MS, k);
		double a[LPC_MAX_ORDER + 1];
		lpc_frame(x, n, centre[k], spec.len, w);
		ncand[k] = lpc_burg(w, spec.len, order, a) > 0 ? lpc_candidates(a, order, sr, cands + (size_t)k * CAND_MAX)
			: 0;
		double fn[6] = { vt->ref[0], vt->ref[1], vt->ref[2], vt->ref[3], g[KG_F5], g[KG_F6] };
		measure(&spec, x, n, centre[k], fn, &lw[k]);
		level[k] = lw[k].total;
		hf[k] = lw[k].hf;
		upper[k] = lw[k].top;
		lowrel[k] = lw[k].low - lw[k].total;
		if (level[k] > top)
			top = level[k];
	}
	/* pitch (step 1) */
	if (pitch_track(x, n, sr, centre, nframes, vt->f0_min, vt->f0_max, f0, ap, voiced) != 0) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}
	for (int k = 0; k < nframes; k++) { /* aperiodicity median-smoothed, for AH */
		double win[AP_SMOOTH];
		int m = 0;
		for (int j = k - AP_SMOOTH / 2; j <= k + AP_SMOOTH / 2; j++)
			if (j >= 0 && j < nframes)
				win[m++] = ap[j];
		qsort(win, m, sizeof(double), cmp_double);
		aps[k] = m & 1 ? win[m / 2] : 0.5 * (win[m / 2 - 1] + win[m / 2]);
	}
	/* formant tracks (steps 3 and 4) */
	track_setup ts;
	for (int i = 0; i < TRACK_N; i++) {
		int p = KP_F1 + i;
		ts.ref[i] = vt->ref[i];
		ts.lo[i] = param_min(p);
		ts.hi[i] = param_max(p) < CAND_MAX_FRAC * sr ? param_max(p) : CAND_MAX_FRAC * sr;
		if (ts.ref[i] > ts.hi[i])
			ts.ref[i] = ts.hi[i];
	}
	for (int k = 0; k < nframes; k++)
		quiet[k] = level[k] < top - QUIET_DB;
	if (track_formants(cands, ncand, quiet, nframes, &ts, ftr, btr) != 0) {
		fail(msg, msglen, "Out of memory.");
		goto done;
	}
	/* frame classes (step 6): a frame is fricative by its energy fraction above the cutoff or by a flat upper band
	 * (import_levels.top), then by the majority of the non-silent frames within CLASS_SMOOTH frames */
	for (int k = 0; k < nframes; k++)
		fric[k] = voiced[k] ? hf[k] >= VFRIC_HF || upper[k] >= TOP_VFRIC_DB
			: hf[k] >= FRIC_HF || (upper[k] >= TOP_FRIC_DB && lowrel[k] <= LOW_FRIC_DB);
	for (int k = 0; k < nframes; k++) {
		int yes = 0, all = 0;
		for (int j = k - CLASS_SMOOTH / 2; j <= k + CLASS_SMOOTH / 2; j++)
			if (j >= 0 && j < nframes && level[j] >= top - SILENCE_DB) {
				all++;
				yes += fric[j] & 1;
			}
		fric[k] |= (2 * yes > all) << 1; /* bit 1: the smoothed decision */
	}
	for (int k = 0; k < nframes;) { /* runs shorter than FRIC_MIN_FRAMES are not fricatives */
		if (!(fric[k] & 2) || level[k] < top - SILENCE_DB) {
			fric[k++] = 0;
			continue;
		}
		int e = k;
		while (e < nframes && (fric[e] & 2) && level[e] >= top - SILENCE_DB)
			e++;
		for (int j = k; j < e; j++)
			fric[j] = e - k >= FRIC_MIN_FRAMES;
		k = e;
	}
	for (int k = 0; k < nframes; k++) {
		if (level[k] < top - SILENCE_DB)
			cls[k] = CLS_SILENCE;
		else
			cls[k] = voiced[k] ? (fric[k] ? CLS_VOICED_FRIC : CLS_VOICED) : (fric[k] ? CLS_FRIC : CLS_ASPIRATION);
	}
	/* the frames: F0 held across unvoiced frames (interpolated between voiced ones), formants and bandwidths */
	double f0_default = sqrt(vt->f0_min * vt->f0_max);
	for (int k = 0; k < nframes; k++) {
		double *fr = frames + (size_t)k * NP;
		for (int p = 0; p < NP; p++) {
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			fr[p] = pi.def;
		}
		for (int i = 0; i < TRACK_N; i++)
			fr[KP_F1 + i] = settle(KP_F1 + i, ftr[k * TRACK_N + i]);
		for (int i = 0; i < 3; i++)
			fr[KP_B1 + i] = settle(KP_B1 + i, btr[k * TRACK_N + i]);
		if (!(f0[k] > 0))
			f0[k] = NAN;
	}
	{
		int prev = -1;
		for (int k = 0; k <= nframes; k++) {
			if (k < nframes && isnan(f0[k]))
				continue;
			for (int j = prev + 1; j < k; j++)
				f0[j] = prev < 0 && k == nframes ? f0_default : prev < 0 ? f0[k] : k == nframes ? f0[prev]
					: f0[prev] + (f0[k] - f0[prev]) * (j - prev) / (k - prev);
			prev = k;
		}
		for (int k = 0; k < nframes; k++)
			frames[(size_t)k * NP + KP_F0] = settle(KP_F0, f0[k]);
	}
	/* levels (step 8): start values for AV and AH from reference syntheses at REF_LEVEL */
	double fric_scale = 1; /* measured / modelled band power of the reference fricative (AF, A2-A6 at REF_LEVEL) */
	vb = malloc(sizeof(double) * 5 * nframes); /* per frame and band: the voicing's and aspiration's power */
	if (!vb)
		goto oom;
	{
		double fr[NP];
		import_levels rv, rh, rf;
		for (int p = 0; p < NP; p++) {
			klatt_param_info pi;
			KlattParamInfo(KLATT_FRAME, p, &pi);
			fr[p] = pi.def;
		}
		fr[KP_AV] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rv) != 0)
			goto oom;
		fr[KP_AV] = 0;
		fr[KP_AH] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rh) != 0)
			goto oom;
		fr[KP_AH] = 0;
		fr[KP_AF] = REF_LEVEL;
		for (int p = KP_A2; p <= KP_A6; p++)
			fr[p] = REF_LEVEL;
		if (reference_levels(g, fr, &spec, &rf) != 0)
			goto oom;
		double q[5][5][5], lsum = 0;
		static const double one[5] = { 1, 1, 1, 1, 1 };
		int nb = 0;
		fric_model(&spec, fr, g, q);
		for (int b = 0; b < 5; b++) {
			double model = fric_power(q, one, b);
			if (model > 0 && rf.band[b] > LEVEL_FLOOR) {
				lsum += log(pow(10, rf.band[b] / 10) / model);
				nb++;
			}
		}
		fric_scale = nb ? exp(lsum / nb) : 1;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP, f[6];
			formants_of(v, g, f);
			measure(&spec, x, n, centre[k], f, &lw[k]);
			int c = cls[k];
			if (c == CLS_VOICED || c == CLS_VOICED_FRIC)
				v[KP_AV] = settle(KP_AV, REF_LEVEL + lw[k].low - rv.low);
			if (c == CLS_VOICED)
				v[KP_AH] = voiced_ah(v[KP_AV], aps[k]);
			if (c == CLS_ASPIRATION)
				v[KP_AH] = settle(KP_AH, REF_LEVEL + lw[k].total - rh.total);
		}
	}
	/* formants corrected by analysis-by-synthesis (step 5), then Rd (step 7) with the corrected formants */
	clock_t t_rd = clock();
	if (correct_formants(g, frames, nframes, cls, ftr, &spec) != 0)
		goto oom;
	int fitted = 0;
	if (fit_rd_track(x, n, sr, cls, nframes, frames, &spec, &fitted) != 0)
		goto oom;
	clock_t t_cal = clock();

	if (doc_init_from(&tmp, g, nframes, frames) != 0)
		goto oom;
	have_tmp = 1;
	/* calibration against the synthesizer (step 8): each pass synthesizes the document, measures it like the WAV
	 * and corrects AV by the low band's dB difference (AH keeping its offset in voiced frames) and AH in
	 * aspiration frames by the overall level's. The first pass has no frication yet: in fricative frames what the
	 * WAV has beyond the voicing and aspiration is the frication, and AF and A2-A6 are fitted to its five band
	 * powers with a model of the parallel resonators (fric_model); later passes scale each band's target by the
	 * WAV's frication over the synthesis's and fit again */
	for (int pass = 0; pass <= CAL_PASSES; pass++) {
		if (measure_synth(&tmp, &spec, ls) != 0)
			goto oom;
		for (int k = 0; k < nframes; k++) {
			double *v = frames + (size_t)k * NP;
			int c = cls[k];
			if (c == CLS_VOICED || c == CLS_VOICED_FRIC)
				v[KP_AV] = settle(KP_AV, v[KP_AV] + lw[k].low - ls[k].low);
			if (c == CLS_VOICED) /* in voiced fricatives the aperiodic energy is the frication's */
				v[KP_AH] = voiced_ah(v[KP_AV], aps[k]);
			if (c == CLS_ASPIRATION)
				v[KP_AH] = settle(KP_AH, v[KP_AH] + lw[k].total - ls[k].total);
			if (c != CLS_FRIC && c != CLS_VOICED_FRIC)
				continue;
			/* the frication's band powers: the WAV's (and in later passes the synthesis's) less the voicing and
			 * aspiration the first pass measured */
			double tw[5], q[5][5][5], xj[5], top = 0;
			fric_model(&spec, v, g, q);
			for (int b = 0; b < 5; b++) {
				if (pass == 0)
					vb[(size_t)k * 5 + b] = pow(10, ls[k].band[b] / 10);
				tw[b] = pow(10, lw[k].band[b] / 10) - vb[(size_t)k * 5 + b];
				if (tw[b] > top)
					top = tw[b];
			}
			if (!(top > 0)) {
				static const double none[5] = { 0 };
				fric_set(v, none);
				continue;
			}
			for (int b = 0; b < 5; b++)
				if (!(tw[b] > top * 1e-6))
					tw[b] = top * 1e-6; /* nothing left in this band: 60 dB below the strongest */
			if (pass == 0) {
				for (int j = 0; j < 5; j++) /* start: each band from its own resonator */
					xj[j] = q[j][j][j] > 0 ? sqrt(tw[j] / (fric_scale * q[j][j][j])) : 0;
			} else {
				/* correct the targets by what the synthesis really gave: the model's prediction times the ratio
				 * of the WAV's frication to the synthesis's, band by band */
				fric_get(v, xj);
				for (int b = 0; b < 5; b++) {
					double syn = pow(10, ls[k].band[b] / 10) - vb[(size_t)k * 5 + b];
					double model = fric_scale * fric_power(q, xj, b);
					if (syn > top * 1e-6 && model > 0)
						tw[b] = model * tw[b] / syn;
				}
			}
			fric_fit(q, fric_scale, tw, xj);
			fric_set(v, xj);
		}
	}
	/* silence: every amplitude 0 */
	for (int k = 0; k < nframes; k++)
		if (cls[k] == CLS_SILENCE)
			for (int p = 0; p < NP; p++)
				if (p == KP_AV || p == KP_AF || p == KP_AH || p == KP_AVS || p == KP_AN || (p >= KP_A1 && p <= KP_AB))
					frames[(size_t)k * NP + p] = 0;
	if (o->simplify) {
		double *v = malloc(sizeof(double) * nframes);
		if (!v)
			goto oom;
		for (int p = 0; p < NP; p++) {
			for (int k = 0; k < nframes; k++)
				v[k] = frames[(size_t)k * NP + p];
			opt_simplify(v, nframes, 0, 1, opt_tol_default[opt_tol_class(p)], NULL);
			for (int k = 0; k < nframes; k++)
				frames[(size_t)k * NP + p] = v[k];
		}
		free(v);
	}
	/* level normalization: G0 so the resynthesis peaks at about NORM_PEAK_DB; the amplitudes keep their levels */
	{
		double g0 = g[KG_G0];
		tmp.globals[KG_G0] = g0 - NORM_HEADROOM_DB;
		synth_result s;
		if (synth_doc(&tmp, &s) != 0)
			goto oom;
		double peak = 0;
		for (int i = 0; i < s.n; i++)
			if (abs(s.pcm[i]) > peak)
				peak = abs(s.pcm[i]);
		synth_result_free(&s);
		if (peak > 0) {
			double v = floor(g0 - NORM_HEADROOM_DB + NORM_PEAK_DB - 20 * log10(peak / 32768));
			klatt_param_info pi;
			KlattParamInfo(KLATT_GLOBAL, KG_G0, &pi);
			g0 = v < pi.min ? pi.min : v > pi.max ? pi.max : v;
		}
		tmp.globals[KG_G0] = g0;
	}
	*d = tmp; /* takes the frames */
	d->dirty = 0;
	have_tmp = 0;
	frames = NULL;
	if (info) {
		info->nframes = nframes;
		info->cls = cls;
		info->ap = ap;
		info->hf = hf;
		info->top = upper;
		info->level = level;
		info->rd_frames = fitted;
		info->seconds[0] = (double)(t_rd - t_start) / CLOCKS_PER_SEC;
		info->seconds[1] = (double)(t_cal - t_rd) / CLOCKS_PER_SEC;
		info->seconds[2] = (double)(clock() - t_cal) / CLOCKS_PER_SEC;
		info->seconds[3] = (double)(clock() - t_start) / CLOCKS_PER_SEC;
		cls = NULL;
		ap = hf = level = upper = NULL;
	}
	rc = 0;
	goto done;
oom:
	fail(msg, msglen, "Out of memory.");
done:
	if (have_tmp) {
		tmp.frames = NULL; /* frames is freed below */
		doc_free(&tmp);
	}
	spec_free(&spec);
	free(x);
	free(w);
	free(centre);
	free(f0);
	free(ap);
	free(voiced);
	free(quiet);
	free(cls);
	free(cands);
	free(ncand);
	free(ftr);
	free(btr);
	free(lw);
	free(ls);
	free(frames);
	free(level);
	free(hf);
	free(upper);
	free(lowrel);
	free(aps);
	free(fric);
	free(vb);
	return rc;
}

int import_wav(const wchar_t *path, const import_options *o, kpe_doc *d, import_info *info, char *msg,
	size_t msglen)
{
	wav_data wd;
	if (info)
		memset(info, 0, sizeof *info);
	if (wav_read(path, DOC_MAX_DURATION_MS / 1000, &wd, msg, msglen) != 0) {
		if (msglen && strstr(msg, "too long"))
			snprintf(msg, msglen, "The recording is longer than 10 minutes.");
		return -1;
	}
	int rc = import_samples(wd.x, wd.n, wd.sr, o, d, info, msg, msglen);
	wav_data_free(&wd);
	return rc;
}
```

- [ ] **Step 7: Run the tests on both architectures**

Run: `MAKE test`
Expected: `1635 checks, 0 failures` twice, then `xarch: 45201 samples, max difference 0 LSB: ok`. The speed test
prints the 60 s import's time; on the 32-bit build it was 3.8-3.9 s (the limit is 5 s).

- [ ] **Step 8: Commit**

```bash
git add kpe/wavimport.h kpe/wavimport.c kpe/pitch.c kpe/pitch.h kpe/spectro.c kpe/spectro.h kpe/optimize.c kpe/optimize.h kpe/dialogs.c tests/test_wavimport.c tests/test_xarch.c tests/run_tests.c
git commit -m "Add the WAV import pipeline

Frames centred per spec 4.3; classes from the energy above the cutoff and
the flatness of the upper band, smoothed, with a minimum fricative length;
formants of a high voice corrected by analysis-by-synthesis; Rd by
golden-section analysis-by-synthesis through the DLL; levels calibrated in
three syntheses (the frication fitted with a model of the parallel branch);
AH only above an aperiodicity threshold; G0 set so the resynthesis peaks at
about -1 dBFS; result document. Round-trip corpus with realistic /z/, /sh/
and /v/, aspiration, normalization, refusals and a 60 s speed test; the
import also joins the 32/64-bit comparison. YIN now low-passes at 1.5 kHz
and holds voicing up to 0.45.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: File > Import WAV in the editor

**Files:**
- Create: `tools/ui/check_import.ps1`
- Modify: `kpe/main.c`, `kpe/dialogs.c`, `kpe/app.h`, `kpe/kpe.rc`, `kpe/resource.h`, `docs/manual-checklist.md`,
  `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, `README.md`

**Interfaces:**
- Consumes: `import_options`, `import_voice`, `VOICE_COUNT`, `import_wav` (Task 6).
- Produces: `int dlg_import(HWND owner, import_options *o)` (`kpe/app.h`; 1 on Import, 0 on Cancel);
  `IDM_IMPORTWAV` 1007, `IDD_IMPORT` 204, `IDC_VOICE` 2401, `IDC_TARGET_SR` 2402, `IDC_SIMPLIFY` 2403.

- [ ] **Step 1: Write the UI script**

Create `tools/ui/check_import.ps1`:

```powershell
param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)
# File > Import WAV: exports ba.csv with File > Export WAV, imports that WAV through the file and import dialogs,
# checks the new document's title and the Save As suggestion, then imports a CSV file as a WAV to see the error
# leave the document untouched. Screenshots go to build\ui.
$Out = "$Repo\build\ui"; New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
  // The texts of every Edit control below h (the file dialogs nest theirs several levels deep).
  public static string[] EditTexts(IntPtr h) {
    var list = new System.Collections.Generic.List<string>();
    EnumChildWindows(h, (c, l) => {
      var cls = new StringBuilder(64); GetClassNameW(c, cls, 64);
      if (cls.ToString() == "Edit") { var t = new StringBuilder(512); SendMessageW(c, 0x0D, (IntPtr)512, t); list.Add(t.ToString()); }
      return true; }, IntPtr.Zero);
    return list.ToArray();
  }
  public struct RECT { public int L, T, R, B; }
}
"@
[U]::SetProcessDPIAware() | Out-Null
function Shot($h, $name) {
  $r = New-Object U+RECT; [U]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [U]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $bmp.Save("$Out\$name.png"); "saved $name"
}
function Title($h) { $s = New-Object System.Text.StringBuilder 512; [U]::GetWindowTextW($h, $s, 512) | Out-Null; $s.ToString() }
# Waits for a top-level dialog with this title; throws after about 10 s.
function WaitDialog($title) {
  for ($i = 0; $i -lt 100; $i++) {
    $h = [U]::FindWindowW("#32770", $title)
    if ($h -ne [IntPtr]::Zero) { Start-Sleep -Milliseconds 300; return $h }
    Start-Sleep -Milliseconds 100
  }
  throw "no dialog '$title'"
}
function WaitTitle($h, $want) {
  for ($i = 0; $i -lt 100; $i++) { if ((Title $h) -eq $want) { return }; Start-Sleep -Milliseconds 100 }
  throw "title is '$(Title $h)', expected '$want'"
}
# Types a path into a file dialog's file name box (it has the focus when the dialog opens) and confirms it.
function TypePath($dlg, $path) {
  [U]::SetForegroundWindow($dlg) | Out-Null; Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait($path); Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
}
$WM_COMMAND = 0x111
$IDM_SAVEAS = 1004; $IDM_EXPORTWAV = 1005; $IDM_IMPORTWAV = 1007; $IDOK = 1; $IDCANCEL = 2; $IDNO = 7

$wav = "$Out\ui_import.wav"; Remove-Item $wav -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$Repo\build\32\kpe.exe" -ArgumentList "`"$Repo\samples\ba.csv`"" -PassThru
Start-Sleep -Milliseconds 1500
$p.Refresh(); $main = $p.MainWindowHandle
try {
[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_EXPORTWAV, [IntPtr]::Zero) | Out-Null   # File > Export WAV
TypePath (WaitDialog "Save As") $wav
for ($i = 0; $i -lt 50 -and -not (Test-Path $wav); $i++) { Start-Sleep -Milliseconds 100 }
if (-not (Test-Path $wav)) { throw "Export WAV wrote no file" }
"exported $((Get-Item $wav).Length) bytes"

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_IMPORTWAV, [IntPtr]::Zero) | Out-Null   # File > Import WAV
TypePath (WaitDialog "Open") $wav
$dlg = WaitDialog "Import WAV"
Shot $dlg "ui_import_dialog"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
Start-Sleep -Milliseconds 500
Shot $main "ui_import_result"
"imported: $(Title $main)"

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_SAVEAS, [IntPtr]::Zero) | Out-Null      # File > Save As
$dlg = WaitDialog "Save As"
$names = [U]::EditTexts($dlg)                                                               # the file name box
Shot $dlg "ui_import_saveas"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDCANCEL, [IntPtr]::Zero) | Out-Null
if (-not ($names -contains "ui_import.csv")) { throw "Save As suggests '$($names -join "', '")', not ui_import.csv" }
"Save As suggests ui_import.csv"
Start-Sleep -Milliseconds 400

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_IMPORTWAV, [IntPtr]::Zero) | Out-Null   # a CSV file as the WAV
$ask = WaitDialog "Klatt Parameter Editor"                                                 # unsaved changes: No
[U]::PostMessageW($ask, $WM_COMMAND, [IntPtr]$IDNO, [IntPtr]::Zero) | Out-Null
TypePath (WaitDialog "Open") "$Repo\samples\ba.csv"
$dlg = WaitDialog "Import WAV"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
$err = WaitDialog "Klatt Parameter Editor"
Shot $err "ui_import_error"
[U]::PostMessageW($err, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
"after the error: $(Title $main)"
} finally {
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `MAKE ARCH=32` then `powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui/check_import.ps1`
Expected: the script exports `ba.csv` with File > Export WAV, then stops with `no dialog 'Open'` (the editor has no Import WAV command yet).

- [ ] **Step 3: Resources**

In `kpe/resource.h`, replace:

```c
#define IDM_EXIT          1006
#define IDM_UNDO          1101
```

with:

```c
#define IDM_EXIT          1006
#define IDM_IMPORTWAV     1007
#define IDM_UNDO          1101
```

In `kpe/resource.h`, replace:

```c
#define IDD_ASKNUM        203

```

with:

```c
#define IDD_ASKNUM        203
#define IDD_IMPORT        204

```

In `kpe/resource.h`, replace:

```c
#define IDC_ASK_VALUE     2302

```

with:

```c
#define IDC_ASK_VALUE     2302
#define IDC_VOICE         2401
#define IDC_TARGET_SR     2402
#define IDC_SIMPLIFY      2403

```

In `kpe/kpe.rc`, replace:

```
		MENUITEM SEPARATOR
		MENUITEM "&Export WAV...", IDM_EXPORTWAV
```

with:

```
		MENUITEM SEPARATOR
		MENUITEM "&Import WAV...", IDM_IMPORTWAV
		MENUITEM "&Export WAV...", IDM_EXPORTWAV
```

In `kpe/kpe.rc`, replace:

```

IDD_ASKNUM DIALOGEX 0, 0, 200, 60
```

with:

```

IDD_IMPORT DIALOGEX 0, 0, 250, 104
STYLE DS_MODALFRAME | DS_SHELLFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "Import WAV"
FONT 9, "Segoe UI"
BEGIN
	LTEXT "Voice type:", -1, 7, 9, 70, 8
	COMBOBOX IDC_VOICE, 80, 7, 90, 60, CBS_DROPDOWNLIST | WS_TABSTOP
	LTEXT "Sample rate (Hz):", -1, 7, 27, 70, 8
	EDITTEXT IDC_TARGET_SR, 80, 25, 50, 12, ES_AUTOHSCROLL
	LTEXT "Children's voices need about 16000 Hz, so that their formants fit below the Nyquist frequency.", -1, 7, 42, 236, 18
	AUTOCHECKBOX "Simplify with Optimize afterwards", IDC_SIMPLIFY, 7, 64, 200, 10
	DEFPUSHBUTTON "Import", IDOK, 139, 83, 50, 14
	PUSHBUTTON "Cancel", IDCANCEL, 193, 83, 50, 14
END

IDD_ASKNUM DIALOGEX 0, 0, 200, 60
```

- [ ] **Step 4: The options dialog**

In `kpe/app.h`, replace:

```c
#include "synthrun.h"

```

with:

```c
#include "synthrun.h"
#include "wavimport.h"

```

In `kpe/app.h`, replace:

```c
	wchar_t path[MAX_PATH];   /* L"" for a document never saved */

```

with:

```c
	wchar_t path[MAX_PATH];   /* L"" for a document never saved */
	wchar_t import_path[MAX_PATH]; /* the WAV an unsaved imported document came from, else L"" */

```

In `kpe/app.h`, replace:

```c
int dlg_asknum(HWND owner, const wchar_t *title, const wchar_t *label, double *value, double lo, double hi);

```

with:

```c
int dlg_asknum(HWND owner, const wchar_t *title, const wchar_t *label, double *value, double lo, double hi);
int dlg_import(HWND owner, import_options *o); /* File > Import WAV's options; 1 on Import, 0 on Cancel */

```

In `kpe/dialogs.c`, replace:

```c
/* dialogs.c - File > New and Parameters > Globals (one dialog), Parameters > Optimize, Help > Parameter
 * reference, and a small number prompt. */
#include <math.h>
```

with:

```c
/* dialogs.c - File > New and Parameters > Globals (one dialog), File > Import WAV's options, Parameters >
 * Optimize, Help > Parameter reference, and a small number prompt. */
#include <math.h>
```

In `kpe/dialogs.c`, replace:

```c
	DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_OPTIMIZE), owner, optimize_proc, 0);
}
```

with:

```c
	DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_OPTIMIZE), owner, optimize_proc, 0);
}

/* ---- Import WAV ---- */

static INT_PTR CALLBACK import_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	static import_options *o;
	switch (msg) {
	case WM_INITDIALOG: {
		o = (import_options *)lp;
		HWND voice = GetDlgItem(dlg, IDC_VOICE);
		for (int v = 0; v < VOICE_COUNT; v++) {
			wchar_t t[64];
			swprintf(t, 64, L"%hs", import_voice(v)->name);
			SendMessageW(voice, CB_ADDSTRING, 0, (LPARAM)t);
		}
		SendMessageW(voice, CB_SETCURSEL, o->voice, 0);
		set_number(dlg, IDC_TARGET_SR, o->sr);
		CheckDlgButton(dlg, IDC_SIMPLIFY, o->simplify ? BST_CHECKED : BST_UNCHECKED);
		return TRUE;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK) {
			double sr;
			klatt_param_info pi;
			KlattParamInfo(KLATT_GLOBAL, KG_SR, &pi);
			if (!get_number(dlg, IDC_TARGET_SR, &sr) || sr < pi.min || sr > pi.max) {
				wchar_t m[96];
				swprintf(m, 96, L"Enter a sample rate from %g to %g Hz.", pi.min, pi.max);
				reject(dlg, IDC_TARGET_SR, m);
				return TRUE;
			}
			o->voice = (int)SendMessageW(GetDlgItem(dlg, IDC_VOICE), CB_GETCURSEL, 0, 0);
			o->sr = (int)lround(sr);
			o->simplify = IsDlgButtonChecked(dlg, IDC_SIMPLIFY) == BST_CHECKED;
			EndDialog(dlg, 1);
			return TRUE;
		}
		if (LOWORD(wp) == IDCANCEL) {
			EndDialog(dlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

int dlg_import(HWND owner, import_options *o)
{
	return (int)DialogBoxParamW(app.inst, MAKEINTRESOURCEW(IDD_IMPORT), owner, import_proc, (LPARAM)o);
}
```

- [ ] **Step 5: The command**

Import asks to save unsaved changes, then shows the file dialog and the options (remembered for the session); while
it runs the cursor waits and the status bar says "Analyzing…". The result opens as a new unsaved document titled
after the WAV, and Save As suggests `<wav name>.csv`. Errors leave the current document untouched.

In `kpe/main.c`, replace:

```c

static void update_title(void)
{
	wchar_t t[MAX_PATH + 64];
	const wchar_t *name = app.path;
	for (const wchar_t *p = app.path; *p; p++)
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	swprintf(t, sizeof t / sizeof t[0], L"%ls%ls - Klatt Parameter Editor", app.path[0] ? name : L"Untitled",
	         app.doc.dirty ? L"*" : L"");
	SetWindowTextW(app.main, t);
```

with:

```c

/* The file name part of path. */
static const wchar_t *file_name(const wchar_t *path)
{
	const wchar_t *name = path;
	for (const wchar_t *p = path; *p; p++)
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	return name;
}

/* The saved file's name; an unsaved import is titled after its WAV. */
static void update_title(void)
{
	wchar_t t[MAX_PATH + 64];
	const wchar_t *name = app.path[0] ? file_name(app.path) : app.import_path[0] ? file_name(app.import_path)
		: L"Untitled";
	swprintf(t, sizeof t / sizeof t[0], L"%ls%ls - Klatt Parameter Editor", name, app.doc.dirty ? L"*" : L"");
	SetWindowTextW(app.main, t);
```

In `kpe/main.c`, replace:

```c

static void replace_doc(kpe_doc *d, const wchar_t *path)
```

with:

```c

/* Remembers the WAV an imported document came from (absolute, like app.path). */
static void set_import_path(const wchar_t *path)
{
	if (!GetFullPathNameW(path, MAX_PATH, app.import_path, NULL)) {
		wcsncpy(app.import_path, path, MAX_PATH - 1);
		app.import_path[MAX_PATH - 1] = 0;
	}
}

static void replace_doc(kpe_doc *d, const wchar_t *path)
```

In `kpe/main.c`, replace:

```c
	set_path(path);
	app.sel_a = -1;
```

with:

```c
	set_path(path);
	app.import_path[0] = 0;
	app.sel_a = -1;
```

In `kpe/main.c`, replace:

```c
	wcsncpy(path, app.path, MAX_PATH);
	if (!ask_path(1, csv_filter, L"csv", path))
```

with:

```c
	wcsncpy(path, app.path, MAX_PATH);
	if (!app.path[0] && app.import_path[0]) { /* an unsaved import: suggest <wav name>.csv next to the WAV */
		wcsncpy(path, app.import_path, MAX_PATH - 5);
		path[MAX_PATH - 5] = 0;
		wchar_t *dot = wcsrchr(path, L'.');
		if (dot && dot > file_name(path))
			*dot = 0;
		wcscat(path, L".csv");
	}
	if (!ask_path(1, csv_filter, L"csv", path))
```

In `kpe/main.c`, replace:

```c
	set_path(path); /* only after the save succeeds, so a failed Save As cannot repoint later Saves */
	update_title();
```

with:

```c
	set_path(path); /* only after the save succeeds, so a failed Save As cannot repoint later Saves */
	app.import_path[0] = 0;
	update_title();
```

In `kpe/main.c`, replace:

```c
		open_path(path);
}
```

with:

```c
		open_path(path);
}

/* File > Import WAV: the WAV becomes a new unsaved document (not an undo step, like Open); on any error the
 * current document stays as it was. */
static void cmd_import_wav(void)
{
	static import_options o = { VOICE_MALE, 11025, 0 }; /* remembered for the next import */
	wchar_t path[MAX_PATH] = L"";
	if (!confirm_discard() || !ask_path(0, L"WAV files (*.wav)\0*.wav\0All files (*.*)\0*.*\0", L"wav", path) ||
	    !dlg_import(app.main, &o))
		return;
	HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
	app_status(L"Analyzing\x2026");
	UpdateWindow(app.status);
	kpe_doc d;
	char msg[512];
	int rc = import_wav(path, &o, &d, NULL, msg, sizeof msg);
	SetCursor(old);
	app_status(L"");
	if (rc != 0) {
		app_error(msg);
		return;
	}
	replace_doc(&d, L"");
	set_import_path(path);
	app.doc.dirty = 1; /* unsaved: closing it asks first */
	update_title();
}
```

In `kpe/main.c`, replace:

```c
	case IDM_UNDO: case IDM_REDO: case IDM_GLOBALS: case IDM_OPTIMIZE:
	case IDM_NEW: case IDM_OPEN:
		return 1;
```

with:

```c
	case IDM_UNDO: case IDM_REDO: case IDM_GLOBALS: case IDM_OPTIMIZE:
	case IDM_NEW: case IDM_OPEN: case IDM_IMPORTWAV:
		return 1;
```

In `kpe/main.c`, replace:

```c
	case IDM_SAVEAS: save_as(); break;
	case IDM_EXPORTWAV: cmd_export_wav(); break;
```

with:

```c
	case IDM_SAVEAS: save_as(); break;
	case IDM_IMPORTWAV: cmd_import_wav(); break;
	case IDM_EXPORTWAV: cmd_export_wav(); break;
```

- [ ] **Step 6: Build and run the UI scripts**

Run: `MAKE test`
Expected: `1635 checks, 0 failures` twice, `xarch: 45201 samples, max difference 0 LSB: ok`, and `build/32/kpe.exe`
built without warnings.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui/check_import.ps1`
Expected: ends with `after the error: ui_import.wav* - Klatt Parameter Editor`. Look at
`build/ui/ui_import_dialog.png` (voice type, sample rate, the note on children's voices, the Simplify checkbox)
and `build/ui/ui_import_result.png` (the imported document's tracks).

Run each of `tools/ui/check_main.ps1` and `tools/ui/check_edit.ps1` in its own PowerShell process, as above.
Expected: `saved ui_after_cancel` and `message box after play: 0` at their ends.

- [ ] **Step 7: Documentation**

The base spec's Part B items (spec section 5: sections 1, 3, 5.5, 8 and 9):

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
- Resynthesis from WAV (see section 9, future work).
- A command-line synthesis tool.
```

with:

```markdown
- Resynthesis from WAV in v1: it is designed in `2026-09-29-voice-source-and-wav-import-design.md` (Part B, File >
  Import WAV…).
- A command-line synthesis tool.
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
    view.c/.h       plot geometry: strip layout, mapping, ticks (no Win32)
    app.h, resource.h  state and IDs shared by the GUI files
```

with:

```markdown
    view.c/.h       plot geometry: strip layout, mapping, ticks (no Win32)
    wavin.c/.h      WAV reader (no Win32)
    resample.c/.h   sample-rate conversion (no Win32)
    lpc.c/.h        Burg LPC, polynomial roots, formant candidates (no Win32)
    pitch.c/.h      YIN pitch and voicing (no Win32)
    track.c/.h      formant tracking (no Win32)
    wavimport.c/.h  WAV import pipeline (no Win32)
    app.h, resource.h  state and IDs shared by the GUI files
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
    table.c         frame table window
    dialogs.c       New, Globals, Optimize, About, Parameter reference
    audio.c         waveOut playback
```

with:

```markdown
    table.c         frame table window
    dialogs.c       New, Globals, Import WAV, Optimize, About, Parameter reference
    audio.c         waveOut playback
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown
  (AV = AF = AH = AVS = 0) with the frame parameters at their defaults.
- **Help > Parameter reference:** the parameter table, read from the DLL.
```

with:

```markdown
  (AV = AF = AH = AVS = 0) with the frame parameters at their defaults.
- **File > Import WAV…:** asks to save unsaved changes, then a file dialog, then voice type, target sample rate and
  "Simplify with Optimize afterwards"; the recording opens as a new unsaved document titled after the WAV. See
  `2026-09-29-voice-source-and-wav-import-design.md`, section 4.
- **Help > Parameter reference:** the parameter table, read from the DLL.
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown

**Manual:** a GUI checklist in `docs/` covering painting, the line tool, selection, the frame table, playback,
```

with:

```markdown

- **WAV import:** the WAV reader, resampler, Burg LPC, root finder, pitch and formant tracking as units, and round
  trips of documents synthesized by the DLL through the import (`tests/test_wavimport.c`); see the import design's
  section 4.6.

**Manual:** a GUI checklist in `docs/` covering painting, the line tool, selection, the frame table, playback,
```

In `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, replace:

```markdown

- **Resynthesis:** import a WAV file and convert it to a CSV pair. Starting point: the `fextr` project
  (`C:\Users\abart\Desktop\fextr_sept2026`), a Python formant tracker with Burg LPC, Kalman smoothing and YIN pitch,
  plus obstruent-spectrum and Rd/breathiness estimators. The voicing and voice-quality parts can be simplified to
  the Klatt 1980 parameters. The result would then be cleaned with Parameter optimization.
```

with:

```markdown

- Resynthesis from WAV is no longer future work: see `2026-09-29-voice-source-and-wav-import-design.md`.
```

In `docs/manual-checklist.md`, replace:

```markdown
Run before a release, at 100 % and at 150 % display scaling. Start `build/32/kpe.exe samples/ba.csv`.
`tools/ui/check_main.ps1` and `tools/ui/check_edit.ps1` automate part of this and save screenshots in
`build/ui/`; the items below are the ones that need a person.

```

with:

```markdown
Run before a release, at 100 % and at 150 % display scaling. Start `build/32/kpe.exe samples/ba.csv`.
`tools/ui/check_main.ps1`, `tools/ui/check_edit.ps1` and `tools/ui/check_import.ps1` automate part of this and
save screenshots in `build/ui/`; the items below are the ones that need a person.

```

In `docs/manual-checklist.md`, replace:

```markdown
- [ ] Closing, New and Open with unsaved changes ask to save; Cancel keeps the editor open.
```

with:

```markdown
- [ ] Closing, New and Open with unsaved changes ask to save; Cancel keeps the editor open.

## Import WAV

- [ ] File > Import WAV... with unsaved changes asks to save first; Cancel there, or in the file dialog, or in the
      import dialog, leaves the document as it was.
- [ ] The import dialog offers Adult male (default), Adult female and Child, a sample rate of 11025 with the note
      about children's voices, and "Simplify with Optimize afterwards" unticked.
- [ ] A sample rate outside 5000-20000 Hz is refused with a message and the focus goes back to the field.
- [ ] While it runs the pointer is the wait cursor and the status bar says "Analyzing…" (noticeable on a file of
      a minute or more).
- [ ] The result opens as a new document titled `<wav name>.wav*`; Edit > Undo is disabled (import is not an undo
      step); File > Save asks for a name and suggests `<wav name>.csv`; closing asks to save.
- [ ] Errors are shown in a message box and leave the current document untouched: a non-WAV file ("Not a WAV
      file"), an unsupported format (for example an ADPCM or 64-bit float WAV, whose message names the format), a
      recording longer than 10 minutes, and a silent one ("No sound found").
- [ ] Simplify with Optimize afterwards gives tracks with visibly fewer breakpoints.
- [ ] Listening on the user's recordings in `C:\Users\abart\Desktop\fextr_sept2026\wavs` (adult male, 11025 Hz):
      `AE`, `IH` and `OW` resynthesize as the same vowels at a similar pitch, without aspiration in their steady
      parts; `S` and `SH` as the two different hisses; `Z`, `ZH` and `V` as voiced fricatives (V is weak: its
      frication is quiet and missing in some frames). Every resynthesis peaks at about -1 dBFS without clipping.
```

In `README.md`, replace:

```markdown
- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
- `kpe/`: the editor. Its core (document and undo, CSV files, optimization, synthesis, WAV, spectrogram) has no
  user interface and is covered by the tests; the Win32 GUI is built on top of it.
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
```

with:

```markdown
- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
- `kpe/`: the editor. Its core (document and undo, CSV files, optimization, synthesis, WAV, spectrogram, and the
  WAV import's analysis) has no user interface and is covered by the tests; the Win32 GUI is built on top of it.
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
```

In `README.md`, replace:

```markdown

Before a release, go through `docs/manual-checklist.md`; `tools/ui/*.ps1` automate part of it.
```

with:

```markdown

File > Import WAV... turns a recording (PCM or float WAV, any sample rate and channel count, up to 10 minutes) into
a first draft: F0, voicing, F1-F4, B1-B3, Rd, AV, AH, and frication with its parallel amplitudes, using the LF
source (SRC = 1), with G0 set so the result peaks at about -1 dBFS. Choose the voice type; children need a sample
rate of about 16000 Hz. It is a draft for hand editing, not a copy: stops and nasals are not analysed.

Before a release, go through `docs/manual-checklist.md`; `tools/ui/*.ps1` automate part of it.
```

- [ ] **Step 8: Commit**

```bash
git add tools/ui/check_import.ps1 kpe/main.c kpe/dialogs.c kpe/app.h kpe/kpe.rc kpe/resource.h docs/manual-checklist.md docs/superpowers/specs/2026-09-28-kpe-open-design.md README.md
git commit -m "Add File > Import WAV to the editor

Menu item next to Export WAV, the import dialog (voice type, sample rate
with the note about children, Simplify with Optimize afterwards), wait
cursor and Analyzing... in the status bar, the result as a new unsaved
document titled after the WAV with Save suggesting <wav name>.csv, errors
in a message box. UI script tools/ui/check_import.ps1; checklist, base
spec sections 1/3/5.5/8/9 and README updated.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Listening check on the user's recordings (needs the user)

This task is done by the controller with the user; no implementer is dispatched.

- [ ] **Step 1: Ask the user to listen**

Ask the user to import each recording in `C:\Users\abart\Desktop\fextr_sept2026\wavs` (AE, IH, OW, S, SH, V, Z, ZH;
adult male, 11025 Hz) in `build/32/kpe.exe`, play the result and compare, going through the "Import WAV" items of
`docs/manual-checklist.md`. The known limits (spec 4.6): part of V gets no frication, and a burst shorter than 40 ms
imports as aspiration. Stop here until the user answers.

- [ ] **Step 2: Record the outcome**

If the user accepts, note it in the ledger; nothing is committed. Otherwise record what they heard as findings for
the final review and stop.
