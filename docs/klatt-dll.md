# Klatt synthesizer DLL

`klatt32.dll` (32-bit) and `klatt64.dll` (64-bit) implement the cascade/parallel formant synthesizer of
D.H. Klatt (1980), "Software for a cascade/parallel formant synthesizer", JASA 67, 971-995. They are built from
the same source and produce the same samples.

## Using it

- Include `klatt/klatt.h`; link `libklatt32.a` or `libklatt64.a` from `build/32` or `build/64`, or load the DLL
  at run time.
- On 32-bit the functions are `__stdcall`, exported under plain names. Python: `ctypes.WinDLL`. See
  `samples/python/klatt.py`.
- The program and the DLL must have the same bitness.

```c
double g[KLATT_NUM_GLOBAL], f[KLATT_NUM_FRAME];
klatt_param_info pi;
for (int i = 0; i < KLATT_NUM_GLOBAL; i++) { KlattParamInfo(KLATT_GLOBAL, i, &pi); g[i] = pi.def; }
for (int i = 0; i < KLATT_NUM_FRAME; i++) { KlattParamInfo(KLATT_FRAME, i, &pi); f[i] = pi.def; }
f[KP_AV] = 60;                                   /* a neutral vowel */

klatt_h h;
if (KlattCreate(g, &h) < 0) return;
short buf[256];                                  /* at least KlattMaxFrameSamples(h) */
for (int k = 0; k < 200; k++) {                  /* 200 frames of 5 ms = 1 s */
	int n = KlattSynthFrame(h, f, buf, 256);
	/* ... use n samples ... */
}
KlattDestroy(h);
```

## Functions

| Function | Returns |
|---|---|
| `KlattVersion()` | `KLATT_VERSION` (10000 = 1.0.0) |
| `KlattParamCount(kind)` | 21 for `KLATT_FRAME`, 18 for `KLATT_GLOBAL` |
| `KlattParamInfo(kind, i, &info)` | name, unit, description, min, max, default |
| `KlattParamIndex(kind, name)` | the index, case-insensitive; `KLATT_ERR_ARG` if unknown |
| `KlattErrorText(code)` | a short English message |
| `KlattCreate(globals, &h)` | a handle; `globals` holds `KLATT_NUM_GLOBAL` values in table order |
| `KlattMaxFrameSamples(h)` | the largest number of samples one frame can produce |
| `KlattSynthFrame(h, frame, out, max)` | samples written; `frame` holds `KLATT_NUM_FRAME` values |
| `KlattOverloadCount(h)` | samples clipped to 16 bits since create or reset |
| `KlattReset(h, seed)` | clears filters, noise, frame clock and the overload count |
| `KlattDestroy(h)` | frees the handle |

- Values outside a parameter's limits are clamped, NaN becomes the minimum, and frequencies are also kept below
  SR/2.
- Globals are fixed for a handle's lifetime; create a new handle to change them.
- A handle must be used by one thread at a time. Separate handles are independent.
- Frame *k* covers samples `floor(k·SR·FRAME_MS/1000)` to `floor((k+1)·SR·FRAME_MS/1000)`, so the total length is
  exact even when a frame is not a whole number of samples (5 ms at 11025 Hz alternates 55 and 56).
- The same inputs and seed always give the same samples.

## Signal path

- **Voicing:** an impulse at the start of each glottal period, through the glottal resonator RGP (FGP, BGP). That
  goes through the glottal antiresonator RGZ (FGZ, BGZ) scaled by AV, and through RGS (FGP, BGS) scaled by AVS.
  AV, AVS and F0 change only at the start of a period.
- **Noise:** a sum of four uniform random numbers, low-pass filtered (`y = x + 0.9·y[n-1]`), halved in the second
  half of each voiced period. AH scales it into aspiration, AF into frication.
- **Cascade** (SW = 0): voicing + aspiration through the nasal pole (FNP, BNP), nasal zero (FNZ, BNZ) and
  formants R1 to R`NFC`.
- **Parallel:** R1 gets the voicing (SW = 1 only); R2-R6 get the first difference of the voicing (SW = 1 only)
  plus frication; the nasal formant (FNP, BNP) gets the voicing difference; AB passes the R2-R6 input directly.
  Signs alternate: +R1 -R2 +R3 -R4 +R5 -R6.
- **Output:** first difference (lip radiation), gain `K_OUT·10^(G0/20)`, rounded and saturated to 16 bits.
- **Amplitudes:** 0 dB is off; otherwise the linear gain is `10^((dB-80)/20)`.

## Calibration

`make calibrate` measures the unscaled levels and prints the constants in `klatt/klatt.c`:

| Constant | Value | Rule |
|---|---|---|
| `K_OUT` | 4.125e5 | neutral vowel, AV = 60, G0 = 47: peak about 16000 |
| `K_NOISE` | 4.071e-4 | AH = 60 alone has the RMS of AV = 60 alone |
| `K_PAR` | 6.0 | SW = 1 with A1-A4 = 60 has the RMS of the cascade vowel |

Re-run it after any change to the signal path and copy the new values into `klatt.c`.

## Parameters

See Help > Parameter reference in the editor, or `klatt/params.c`. Defaults of the globals follow Table I of the
paper, except SR = 11025 Hz and FRAME_MS = 5 ms, which come from IPOX.
