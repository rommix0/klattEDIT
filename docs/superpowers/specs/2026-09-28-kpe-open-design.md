# kpe_open: design

A clean-room, 32-bit Windows successor to the IPOX Klatt Parameter Editor (KPE, 1995), plus a standalone
Klatt 1980 formant synthesizer DLL that other programs can use.

Date: 2026-09-28. Status: approved in brainstorming, pending written-spec review.

## 1. Goals and non-goals

**Goals**

- A native 32-bit Windows editor (`kpe.exe`) for Klatt synthesizer parameters, with the look and workflow agreed
  in brainstorming: stacked parameter strips, a spectrogram strip with formant tracks drawn over it, a shared
  time axis in milliseconds, and painting to edit.
- A Klatt 1980 cascade/parallel formant synthesizer as a DLL, built both as `klatt32.dll` (i686) and
  `klatt64.dll` (x86_64) from the same source, callable from C, Python `ctypes`, VB, Delphi and similar.
- A license-clean result: nothing derived from the original IPOX binaries, `klsyn`'s source, or the
  `prose2000_reborn` decompilation.

**Non-goals (v1)**

- Any support for IPOX's `.PAR` format (text or binary), or a clone of the old `KLATT.DLL` `Klatt()` entry point.
- Porting any other IPOX program (IPOX.EXE, IRC.EXE, PLAY.EXE, SETUP.EXE).
- Resynthesis from WAV (see section 9, future work).
- A command-line synthesis tool.

## 2. Clean-room rules

- The UI is designed fresh from what is publicly known about KPE: its menu names (File, Synthesis, Sound,
  Parameters, Help), that it edits and displays formant tracks, and that global parameters were read-only.
  KPE.EXE's code and resources are never inspected, and it is never run.
- The synthesizer is implemented from the equations, block diagram and parameter table in
  D.H. Klatt (1980), "Software for a cascade/parallel formant synthesizer", JASA 67, 971-995. The paper's Fortran
  listing is copyrighted and is not transcribed.
- `C:\klsyn\src` (Klatt's copyright, no license) is not read or reused.
- From `prose2000_reborn` only the idea of the CSV layout is borrowed (a header row of parameter names, one row
  per frame, columns matched by name). No code or text is taken from it.

## 3. Repository layout

`C:\Users\abart\Desktop\IPOX\kpe_open`, its own git repository.

```
kpe_open/
  klatt/          the synthesizer DLL: plain C, no Win32 UI, no file I/O
    klatt.h         public API
    klatt.c         synthesizer
    params.c        parameter table
    klatt.def       exports, undecorated names
  kpe/            the editor
    model.c/.h      document, edit operations, undo (no Win32)
    csvio.c/.h      CSV pair load/save (no Win32)
    optimize.c/.h   track simplification (no Win32)
    spectro.c/.h    FFT and spectrogram image (no Win32)
    wavout.c/.h     WAV writer (no Win32)
    synthrun.c/.h   runs a document through the DLL (no Win32)
    view.c/.h       plot geometry: strip layout, mapping, ticks (no Win32)
    app.h, resource.h  state and IDs shared by the GUI files
    main.c          window, menus, accelerators
    strips.c        strips, axes, painting, cursor, selection
    table.c         frame table window
    dialogs.c       New, Globals, Optimize, About, Parameter reference
    audio.c         waveOut playback
    kpe.rc, kpe.manifest
  tests/          console test runner
  samples/python/ klatt.py: ctypes bindings for klatt64.dll + vowel glide example
  docs/
  Makefile
```

Everything marked "no Win32" is portable C that the test runner links directly.

## 4. The synthesizer DLL

### 4.1 Signal path (Klatt 1980)

- **Voicing source:** an impulse train at F0, started pitch-synchronously, through the glottal resonator RGP
  (FGP, BGP) and the glottal antiresonator RGZ (FGZ, BGZ), scaled by AV. Quasi-sinusoidal voicing: the same
  pulses through RGS (bandwidth BGS), scaled by AVS.
- **Noise source:** a seeded pseudo-random generator, low-pass filtered. While voicing is on, the noise is
  amplitude-modulated over each pitch period as the paper describes. AH scales it for aspiration, AF for frication.
- **Cascade branch** (voicing + aspiration): nasal pole RNP (FNP, BNP), nasal zero RNZ (FNZ, BNZ), then formant
  resonators R1 to R`NFC` (NFC 4-6).
- **Parallel branch** (frication; also voicing when SW = 1): A1-A6 through R1-R6 with alternating signs, AN
  through the nasal pole, AB as a bypass.
- **Output:** first difference (lip radiation), gain G0, rounding to 16-bit with saturation. Saturated samples
  are counted, not hidden.
- **Resonator:** `y[n] = A·x[n] + B·y[n-1] + C·y[n-2]` with `C = -exp(-2πBW·T)`,
  `B = 2·exp(-πBW·T)·cos(2πF·T)`, `A = 1 - B - C`. Antiresonator: the inverse filter. Coefficients are updated at
  each frame; the voicing-source filters follow the paper's pitch-synchronous update.
- **Amplitudes:** dB are converted to linear gain as in the paper, with 0 dB meaning off.
- Internal arithmetic is `double`.

### 4.2 Parameters

Per-frame parameters (21) and globals (18), following Table I of the paper. The paper's SR and NWS become `SR`
and `FRAME_MS`.

- **Min/max** are this design's hard limits, somewhat wider than the paper's typical ranges so that, for example,
  high F1 values can be drawn. The DLL clamps to them, and also clamps frequencies below SR/2. The editor's default
  Y-axis ranges are the same limits.
- **Defaults of globals** control how the synthesizer sounds, so they must match the paper: the implementation
  checks them against Table I and uses the paper's value wherever it differs from the table below. SR = 11025 Hz
  and FRAME_MS = 5 ms are deliberate exceptions, taken from IPOX.
- **Defaults of frame parameters** are the starting values for a new document and for missing CSV columns.

| Kind | Name | Meaning | Unit | Min | Max | Default |
|---|---|---|---|---|---|---|
| frame | AV | voicing amplitude | dB | 0 | 80 | 0 |
| frame | AF | frication amplitude | dB | 0 | 80 | 0 |
| frame | AH | aspiration amplitude | dB | 0 | 80 | 0 |
| frame | AVS | sinusoidal voicing amplitude | dB | 0 | 80 | 0 |
| frame | F0 | fundamental frequency | Hz | 0 | 500 | 100 |
| frame | F1 | formant 1 | Hz | 150 | 1300 | 500 |
| frame | F2 | formant 2 | Hz | 500 | 3000 | 1500 |
| frame | F3 | formant 3 | Hz | 1300 | 4000 | 2500 |
| frame | F4 | formant 4 | Hz | 2500 | 4500 | 3500 |
| frame | FNZ | nasal zero frequency | Hz | 200 | 700 | 250 |
| frame | AN | nasal formant amplitude | dB | 0 | 80 | 0 |
| frame | A1-A6 | parallel formant amplitudes | dB | 0 | 80 | 0 |
| frame | AB | bypass amplitude | dB | 0 | 80 | 0 |
| frame | B1 | formant 1 bandwidth | Hz | 40 | 500 | 50 |
| frame | B2 | formant 2 bandwidth | Hz | 40 | 500 | 70 |
| frame | B3 | formant 3 bandwidth | Hz | 40 | 500 | 110 |
| global | SW | 0 = cascade/parallel, 1 = all parallel | - | 0 | 1 | 0 |
| global | FGP | glottal resonator frequency | Hz | 0 | 600 | 0 |
| global | BGP | glottal resonator bandwidth | Hz | 100 | 2000 | 100 |
| global | FGZ | glottal zero frequency | Hz | 0 | 5000 | 1500 |
| global | BGZ | glottal zero bandwidth | Hz | 100 | 9000 | 6000 |
| global | BGS | sinusoidal-voicing resonator bandwidth | Hz | 100 | 1000 | 200 |
| global | FNP | nasal pole frequency | Hz | 200 | 500 | 250 |
| global | BNP | nasal pole bandwidth | Hz | 50 | 500 | 100 |
| global | BNZ | nasal zero bandwidth | Hz | 50 | 500 | 100 |
| global | B4 | formant 4 bandwidth | Hz | 100 | 500 | 250 |
| global | F5 | formant 5 | Hz | 3500 | 4900 | 3750 |
| global | B5 | formant 5 bandwidth | Hz | 150 | 700 | 200 |
| global | F6 | formant 6 | Hz | 4000 | 4999 | 4900 |
| global | B6 | formant 6 bandwidth | Hz | 200 | 2000 | 1000 |
| global | SR | sample rate | Hz | 5000 | 20000 | 11025 |
| global | FRAME_MS | frame length | ms | 1 | 20 | 5 |
| global | G0 | overall gain | dB | 0 | 80 | 47 |
| global | NFC | number of cascade formants | - | 4 | 6 | 5 |

The frame parameter order is AV AF AH AVS F0 F1 F2 F3 F4 FNZ AN A1 A2 A3 A4 A5 A6 AB B1 B2 B3 (indices 0-20).
The global order is as listed (indices 0-17).

### 4.3 Frame timing

A frame is `SR·FRAME_MS/1000` samples, which need not be whole (5 ms at 11025 Hz is 55.125). The synthesizer keeps
a running fractional sample count, so frame *k* ends at sample `floor((k+1)·SR·FRAME_MS/1000)`. Frames are
therefore 55 or 56 samples long and the total length is exact.

### 4.4 API

`__stdcall` on 32-bit; names undecorated through `klatt.def`. Functions returning `int` return 0 or more on
success and a negative `KLATT_ERR_*` code on failure. Parameter values are `double` in physical units.

```c
typedef struct klatt_synth *klatt_h;

enum { KLATT_FRAME = 0, KLATT_GLOBAL = 1 };        /* parameter kinds */

typedef struct {
    const char *name, *unit, *description;
    double min, max, def;
} klatt_param_info;

int         KlattVersion(void);
int         KlattParamCount(int kind);                          /* 21 or 18 */
int         KlattParamInfo(int kind, int index, klatt_param_info *out);
int         KlattParamIndex(int kind, const char *name);        /* case-insensitive; negative if unknown */
const char *KlattErrorText(int code);

int  KlattCreate(const double *globals, klatt_h *out);          /* globals: KlattParamCount(KLATT_GLOBAL) values */
int  KlattMaxFrameSamples(klatt_h h);                           /* buffer size for KlattSynthFrame */
int  KlattSynthFrame(klatt_h h, const double *frame, short *out, int max);  /* returns samples written */
int  KlattOverloadCount(klatt_h h);                             /* saturated samples since create/reset */
void KlattReset(klatt_h h, unsigned seed);                      /* clears filter state, noise, frame clock */
void KlattDestroy(klatt_h h);
```

- Globals are fixed for a handle's lifetime. To change them, create a new handle.
- Out-of-range values are clamped silently; `NULL` pointers and bad indices return `KLATT_ERR_ARG`.
- A handle is used by one thread at a time; separate handles are independent.
- The same inputs and seed give the same output on every call; 32- and 64-bit builds agree within 1 LSB.

## 5. The editor

### 5.1 Main window

- **Menu bar:** File, Edit, Synthesis, Sound, Parameters, View, Help.
- **Left panel:** a checkbox per parameter, grouped (Source: F0, AV, AF, AH, AVS; Cascade: F1-F4 on the
  spectrogram, B1-B3; Parallel: A1-A6, AB; Nasal: FNZ, AN), then the globals as read-only text.
- **Strips:** one strip per ticked parameter, stacked, each with its own Y axis showing units and automatically
  chosen ticks, and horizontal grid lines at the ticks.
- **Spectrogram strip:** the spectrogram of the current synthesis with the ticked F1-F4 tracks drawn over it.
  F5 and F6 are globals, drawn as dashed constant lines.
- **Time axis:** shared by all strips, in milliseconds along the bottom; vertical grid lines through every strip.
  Ctrl+wheel zooms time, the horizontal scrollbar pans.
- **Status bar:** time (ms), frame number, the values of the strip under the pointer, and the pointer's value in
  that strip's unit; on the right, SR, frame length, frame count and duration.

### 5.2 Editing

- **Paint:** left-drag in a strip sets every frame the pointer crosses. Gaps between mouse events are filled by
  linear interpolation; values are clamped to the parameter's range and rounded to whole units.
- **Formant strip:** a drag edits the visible formant track nearest the press point.
- **Line tool:** Shift+drag sets a straight ramp from the press point to the release point.
- **Selection:** drag in the time ruler to select a time range across all strips. Edit menu: Cut, Copy, Paste,
  Delete frames, Insert frames (inserted frames copy the frame at the cursor).
- **Undo/Redo:** unlimited. Each step stores only the frames and parameters it changed (or the old globals).
  Edits from strips, the frame table and all dialogs go through the same stack.

### 5.3 Synthesis and sound

- After every completed edit (mouse release, cell commit, dialog OK), the document is synthesized again through
  `klatt32.dll` and the spectrogram is recomputed. Synthesis > Synthesize (F5) forces it.
- The status bar reports an overload if any samples saturated.
- **Sound:** Space plays or stops; Shift+Space plays the selection. A playhead line moves across the strips.
  Playback uses `waveOut`.
- File > Export WAV… writes 16-bit mono PCM at SR.

### 5.4 Spectrogram

- Computed in `kpe/spectro.c` with a built-in radix-2 FFT, pre-emphasis and a Hann window.
- Wideband by default (about 6 ms window); View > Narrowband switches to about 30 ms.
- Frequency range 0-5000 Hz by default; View > Frequency range sets the top, up to SR/2.
- Greyscale, with dark meaning more energy, readable in light and dark window themes.

### 5.5 Other windows

- **View > Frame table:** a separate window with a virtual list: rows are frames, columns are `time_ms` and the
  21 frame parameters. F2 or double-click edits a cell. The selected row and the main cursor follow each other.
- **Parameters > Globals…:** every global, each validated against its range. When FRAME_MS changes, the frames
  are resampled by linear interpolation so the duration stays the same, and a notice says so.
- **Parameters > Optimize…:** see 5.6.
- **File > New…:** duration in ms and the globals (defaults from the table). The document starts as silence
  (AV = AF = AH = AVS = 0) with the frame parameters at their defaults.
- **Help > Parameter reference:** the parameter table, read from the DLL.
- **Help > About:** a clean-room editor inspired by IPOX's Klatt Parameter Editor; contains no original IPOX code.

### 5.6 Parameter optimization

- **Dialog:** tracks to optimize (checklist, defaulting to the visible strips), range (selection or whole file),
  grid (every frame, 5 ms or 10 ms; grids that are not a multiple of FRAME_MS are disabled), and a tolerance per
  unit: 20 Hz for formants and bandwidths, 2 Hz for F0, 1 dB for amplitudes.
- **Algorithm:** Douglas-Peucker restricted to grid times. It keeps the fewest breakpoints for which linear
  interpolation stays within the tolerance of the original values, always keeps the range's endpoints, and then
  refills the frames between breakpoints by linear interpolation. Steps larger than the tolerance (such as
  voicing onsets) survive.
- **Preview:** the original track stays visible as a faint line under the result, and the dialog lists each track's
  result, for example "F2: 160 -> 14 breakpoints, max error 18 Hz".
- **Apply** is one undo step; Cancel restores the original.

### 5.7 Files

- **Native format:** a pair of CSV files, comma-separated, UTF-8, first row the header.
  - `name.csv`: header `time_ms` then frame parameter names; one row per frame, values in physical units.
    `time_ms` is the frame's start time, `k·FRAME_MS` for frame *k*.
    Columns are matched by name, case-insensitively, in any order; missing columns get defaults; unknown columns
    are skipped with a warning.
  - `name.globals.csv`: row 1 is the global names, row 2 their values. Same matching rules.
  - If `name.globals.csv` is missing, defaults are used and FRAME_MS is taken from the `time_ms` spacing (the
    default 5 ms for a single-row file).
  - If both are present and FRAME_MS disagrees with the `time_ms` spacing, the file is rejected with an error
    naming both values.
- **Saving** writes both files, each to a temporary file first and then renamed over the target. The frame file
  always has all 21 columns in table order.
- **Prompts:** unsaved changes prompt before New, Open and Exit.

## 6. Error handling

- The DLL never crashes on bad input; every failure is an error code with a message from `KlattErrorText`.
- CSV errors name the file, line and column, e.g. `sound.csv line 42, column F2: "abc" is not a number`.
- A frame file is rejected when it is empty, has no `time_ms` column, has decreasing times, or has uneven
  spacing (more than 0.01 ms from the first interval).
- Out-of-range values are clamped on load; the editor reports how many.
- A failed save leaves the previous files untouched.
- The editor shows errors in message boxes and never exits on a file error.

## 7. Build

- One `Makefile` using winlibs GCC 14.2: `C:\winlibs\mingw32` for 32-bit, `C:\winlibs\mingw64` for 64-bit.
- Targets: `make ARCH=32` (klatt32.dll + kpe.exe + tests), `make ARCH=64` (klatt64.dll + tests), `make all`,
  `make test` (runs the tests for both).
- Flags: `-O2 -Wall -Wextra`, `-static-libgcc`, `-municode` for the editor. Output in `build/32/` and `build/64/`.
- `kpe.exe` links `klatt32.dll` through its import library. It is a 32-bit Unicode (`W` API) GUI program with a
  DPI-aware manifest, painting double-buffered with GDI.

## 8. Testing

**Automated** (plain C runner, built and run for both architectures):

- **Resonators:** peak at F, -3 dB width within 2 % of B, unity DC gain; antiresonator notch at FNZ.
- **End to end:** steady vowels with known F0 and F1-F3, measured back (F0 by autocorrelation, formants by LPC):
  F0 within 2 %, formants within 5 % or 40 Hz, whichever is larger.
- **Edge cases:** all amplitudes 0 give exact silence; the same seed gives identical output; frame lengths add up
  to the exact duration; clamping and argument errors.
- **Cross-architecture:** 32- and 64-bit output for the same input agree within 1 LSB.
- **Editor logic** (`model.c`, `csvio.c`, `optimize.c`): CSV round trip (load, save, load gives identical data);
  every rejection case; optimizer tolerance, grid alignment and endpoints; each edit operation and undo/redo.

**Manual:** a GUI checklist in `docs/` covering painting, the line tool, selection, the frame table, playback,
Globals, Optimize, save prompts and DPI scaling. Listening checks are done by the user; sounds the user approves
are frozen as regression files.

**Sample:** `samples/python/klatt.py` binds `klatt64.dll` with `ctypes` and synthesizes a vowel glide to a WAV
file, proving the DLL is usable from other programs.

## 9. Future work (not in v1)

- **Resynthesis:** import a WAV file and convert it to a CSV pair. Starting point: the `fextr` project
  (`C:\Users\abart\Desktop\fextr_sept2026`), a Python formant tracker with Burg LPC, Kalman smoothing and YIN pitch,
  plus obstruent-spectrum and Rd/breathiness estimators. The voicing and voice-quality parts can be simplified to
  the Klatt 1980 parameters. The result would then be cleaned with Parameter optimization.
