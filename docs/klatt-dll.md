# Klatt synthesizer DLL

`klatt32.dll` (32-bit) and `klatt64.dll` (64-bit) implement the cascade/parallel formant synthesizer of
D.H. Klatt (1980), "Software for a cascade/parallel formant synthesizer", JASA 67, 971-995. They are built from
the same source and produce the same samples. Version 1.1 adds a selectable Liljencrants-Fant (LF) voicing source
shaped by Rd, Rd's tilt on the 1980 source, flutter (FL) and diplophonia (DI), and brings the noise source in line
with the paper; see "Signal path" below. Version 1.2 adds the speaking speed SPD, a time scale of the frames.

## Using it

- Include `klatt/klatt.h`; link `libklatt32.a` or `libklatt64.a` from `build/32` or `build/64`, or load the DLL
  at run time. A release (`make dist`) has them in `sdk/`: `klatt.h`, this file, `32/` and `64/` (DLL and import
  library) and `python/klatt.py`.
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
int max = KlattMaxFrameSamples(h);
short *buf = malloc(max * sizeof *buf);
for (int k = 0; k < 200; k++) {                  /* 200 frames of 5 ms = 1 s */
	int n = KlattSynthFrame(h, f, buf, max);
	if (n < 0) break;
	/* ... use n samples ... */
}
free(buf);
KlattDestroy(h);
```

## Functions

| Function | Returns |
|---|---|
| `KlattVersion()` | `KLATT_VERSION` (10200 = 1.2.0) |
| `KlattParamCount(kind)` | 23 for `KLATT_FRAME`, 21 for `KLATT_GLOBAL` |
| `KlattParamInfo(kind, i, &info)` | name, unit, description, min, max, default |
| `KlattParamIndex(kind, name)` | the index, case-insensitive; `KLATT_ERR_ARG` if unknown |
| `KlattErrorText(code)` | a short English message |
| `KlattCreate(globals, &h)` | a handle; `globals` holds `KLATT_NUM_GLOBAL` values in table order |
| `KlattMaxFrameSamples(h)` | the largest number of samples one frame can produce: the frame length's ceiling |
| `KlattSynthFrame(h, frame, out, max)` | samples written; `frame` holds `KLATT_NUM_FRAME` values |
| `KlattOverloadCount(h)` | samples clipped to 16 bits since create or reset |
| `KlattReset(h, seed)` | clears filters, noise, frame clock and the overload count |
| `KlattDestroy(h)` | frees the handle |

Version 1.1 appended Rd and DI to the frame parameters and SRC and FL to the globals, and version 1.2 appended SPD
to the globals; no earlier index changed. A program built against an older header passes a globals array one or
three values short and must be rebuilt.

- Values outside a parameter's limits are clamped, NaN becomes the minimum, and frequencies are also kept below
  SR/2.
- Globals are fixed for a handle's lifetime; create a new handle to change them.
- A handle must be used by one thread at a time. Separate handles are independent.
- Frame *k* covers samples `floor(k·SR·FRAME_MS/(1000·SPD))` to `floor((k+1)·SR·FRAME_MS/(1000·SPD))`, so the total
  length is exact even when a frame is not a whole number of samples (5 ms at 11025 Hz alternates 55 and 56 at
  SPD = 1, and is 27.5625 samples at SPD = 2).
- **SPD** (speaking speed, 0.25-4, default 1) only changes how long each frame's values last: F0, the formants, the
  pulse shape, flutter, diplophonia, the noise and the AF/AH ramps run in real time, so pitch and timbre stay and
  the document plays SPD times as fast. Values still change at frame boundaries. At SPD = 1 every sample is the same
  as in version 1.1. `KlattReset` restarts the frame count.
- The same inputs and seed always give the same samples.

### `klatt_param_info` layout

`klatt_param_info` (`const char *name, *unit, *description; double min, max, def;`) is not packed, so the three
pointers before the doubles are followed by whatever padding gives `min` its natural 8-byte alignment. That
padding differs by bitness:

| Build | `sizeof(klatt_param_info)` | `offsetof(min)` |
|---|---|---|
| 32-bit (klatt32.dll) | 40 | 16 (4 bytes of padding after the three 4-byte pointers) |
| 64-bit (klatt64.dll) | 48 | 24 (no padding: the three 8-byte pointers already align to 8) |

VB6/Delphi callers must declare the record with natural alignment (the default) and must not mark it packed;
a packed record reads `min`/`max`/`def` from the wrong offset on 32-bit.

## Signal path

- **Pulses:** an impulse each time the glottal phase (which advances by F0/SR per sample) wraps past 1. F0 is read
  every sample, so a change takes effect within the current period; AV, AVS, Rd and DI are latched at each pulse.
  **FL** (flutter, a global) adds (FL/50)·(F0/100)·[sin 2π·12.7t + sin 2π·7.1t + sin 2π·4.7t] Hz to F0, t in
  seconds since create or reset (Klatt & Klatt 1990, Eq. 1), and the result is clamped to F0's range (0-500 Hz).
  **DI** (diplophonia) delays every second pulse by DI% of the closed phase and weakens it by DI%, so at 100 % it
  vanishes (Klatt & Klatt 1990, p. 840).
- **Voicing, SRC = 0 (Klatt 1980, the default):** the pulses go through the glottal resonator RGP (FGP, BGP), then
  the glottal antiresonator RGZ (FGZ, BGZ) scaled by AV, and RGS (0 Hz, BGS) scaled by AVS. **Rd** tilts this
  source by as much as it tilts the LF source (level near 3 kHz relative to F0, from Rd = 1): below 1 RGP widens,
  above 1 a one-pole low-pass darkens the AV voicing, and at 1 nothing changes. Away from 1 the pulse is scaled so
  that the neutral vowel keeps the level it has at Rd = 1 (the same harmonic sum as the LF source's level).
- **Voicing, SRC = 1 (LF):** at each pulse Rd sets Ra, Rk and Rg (Fant 1997) and from them a two-pole glottal
  formant and a one-pole tilt low-pass (Perrotin et al. 2021). The pulse is sized so that a neutral vowel gets the
  same power for every Rd, and `K_LF` matches it to the 1980 source's level. AV scales it, and RGS gives AVS.
  The normalization targets the vowel band, not the low band RGS passes, so AVS's level changes with Rd (at
  F0 = 100 Hz about -10 dB at Rd 0.3 and +7 dB at 2.7, against Rd = 1). `K_LF` is calibrated on the cascade vowel:
  with SW = 1 the LF source is about 5 dB quieter than the 1980 source.
- **Radiation:** the voicing is computed as glottal flow and its first difference is taken here, before the
  branches, instead of at the output.
- **Noise:** the sum of 16 uniform random numbers (paper p. 977), white, halved in the closed phase of each voiced
  period (from the open quotient on: 0.5 for SRC = 0, the LF value for SRC = 1). AH scales it into aspiration, AF
  into frication; both ramp linearly across each frame, except that AF jumps when it rises by more than 50 dB.
- **Cascade** (SW = 0): voicing + aspiration through the nasal pole (FNP, BNP), nasal zero (FNZ, BNZ) and
  formants R1 to R`NFC`.
- **Parallel:** R1 gets the voicing (SW = 1 only); R2-R6 get the first difference of the voicing (SW = 1 only)
  plus frication; the nasal formant (FNP, BNP) gets the voicing difference; AB passes the R2-R6 input directly.
  Signs alternate: +R1 -R2 +R3 -R4 +R5 -R6.
- **Output:** gain `K_OUT·10^(G0/20)`, rounded and saturated to 16 bits.
- **Amplitudes:** 0 dB is off; otherwise the linear gain is `10^((dB-80)/20)`.

## Calibration

`make calibrate` measures the unscaled levels and prints the constants in `klatt/klatt.c`:

| Constant | Value | Rule |
|---|---|---|
| `K_OUT` | 4.125e5 | neutral vowel, AV = 60, G0 = 47: peak about 16000 |
| `K_NOISE` | 2.070e-4 | AH = 60 alone has the RMS of AV = 60 alone |
| `K_PAR` | 6.0 | SW = 1 with A1-A4 = 60 has the RMS of the cascade vowel |
| `K_LF` | 5.941e-3 | SRC = 1 with Rd = 1 has the RMS of the cascade vowel |

Re-run it after any change to the signal path and copy the new values into `klatt.c`.

## Parameters

See Help > Parameter reference in the editor, or `klatt/params.c`. Defaults of the globals follow Table I of the
paper, except SR = 11025 Hz and FRAME_MS = 5 ms, the editor's chosen defaults.
