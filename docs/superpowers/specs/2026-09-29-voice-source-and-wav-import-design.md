# kpe_open: LF voice source and WAV import

Two additions to kpe_open: a Klatt DLL 1.1 with a selectable Liljencrants-Fant (LF) voicing source and two voice
irregularity controls, and a File > Import WAV command that turns a recording into a Klatt parameter document.

Date: 2026-09-29. Status: approved in brainstorming, pending written-spec review.
Base design: `2026-09-28-kpe-open-design.md` (called "the base spec" below). Section 5 lists the base-spec sections
this design amends.

## 1. Goals and non-goals

**Goals**

- **Part A, DLL 1.1:** an LF voicing source controlled by one shape parameter, Rd, which also approximates the same
  tilt change on the 1980 source; flutter (FL) and diplophonia
  (DI) from Klatt & Klatt (1990); and two corrections that bring the noise source in line with Klatt (1980).
- **Part B, WAV import:** File > Import WAV… analyses a recording and opens it as a new document: F0, voicing,
  F1-F4, B1-B3, Rd, aspiration, and frication with its parallel-branch spectrum. The result is a first draft for
  hand editing, not a perfect copy.

**Non-goals**

- Stops and nasals: import makes no burst detection and no nasal pole/zero estimate. FNZ, AN, AVS, DI and FL stay at
  their defaults for the user to paint.
- KLGLOTT88, OQ and TL (Klatt & Klatt 1990): the LF source with Rd replaces them.
- KLSYN88's other controls (DF1/DB1, tracheal pole/zero, SQ): not added.
- A worker thread for import (see 4.7), and a command-line import tool.

## 2. Sources and clean-room rules

- The base spec's rules still hold: nothing from `C:\klsyn\src`, the IPOX binaries or `prose2000_reborn`; the
  Klatt (1980) Fortran listing is not transcribed.
- **LF source:** Fant (1997) for the Rd to Ra/Rk/Rg mapping, Perrotin et al. (2021, Eqs. C2-C5) for the glottal-formant
  and tilt filters, as implemented in the user's own `fextr_sept2026/_vtm.py` (`LFSource`), which may be ported.
- **FL and DI:** Klatt & Klatt (1990), "Analysis, synthesis, and perception of voice quality variations among female
  and male talkers", JASA 87, 820-857: Eq. 1 (p. 839) and the double-pulsing description (p. 840). Both papers are in
  `Desktop\KPE\papers`.
- **Analysis:** algorithms are written from their publications: YIN (de Cheveigné & Kawahara 2002), Burg's method
  (Burg 1967/Andersen 1974), formant candidates from LPC roots, Viterbi tracking. The user's `fextr` code (`_utils.py`,
  `NOTES.md`) is the reference for how the pieces fit together and may be ported, **except `burg_lpc`**, which is a
  translation of *Numerical Recipes* `memcof` and must not be used or translated.

## 3. Part A: Klatt DLL 1.1

### 3.1 New parameters

Appended after the existing ones, so no existing index changes.

| Kind | Name | Index | Meaning | Unit | Min | Max | Default |
|---|---|---|---|---|---|---|---|
| frame | Rd | 21 | voice quality: about 0.3 pressed, 1 modal, 2.7 breathy; LF shape (SRC = 1) or tilt (SRC = 0, 3.3) | - | 0.3 | 2.7 | 1.0 |
| frame | DI | 22 | diplophonia (double pulsing) | % | 0 | 100 | 0 |
| global | SRC | 18 | voicing source: 0 = Klatt 1980 impulse, 1 = LF | - | 0 | 1 | 0 |
| global | FL | 19 | flutter | % | 0 | 100 | 0 |

- Counts become 23 frame parameters and 20 globals. `KLATT_VERSION` becomes 10100 (1.1.0). A program built against
  the 1.0 header must be rebuilt (its frame array is two values short); nothing has been released, so this is
  accepted and noted in `docs/klatt-dll.md`.
- SRC, like SW and NFC, is a whole number (the editor rounds it). Rd has a resolution of 0.01; every other parameter
  keeps whole units.
- Default globals give exactly the 1980 source, and missing CSV columns take their defaults, so existing documents
  keep their voicing.

### 3.2 LF source (SRC = 1)

- **Timing** is unchanged: the same phase accumulator issues a pulse when the glottal phase wraps, with fractional
  periods and F0 read every sample. Rd is latched at each pulse, like AV and AVS.
- **At each pulse**, from the current F0 (T0 = 1/F0) and the latched Rd (clamped to 0.3-2.7):
  - Ra = (-1 + 4.8·Rd)/100, Rk = (22.4 + 11.8·Rd)/100, Rg = Rk·(0.5 + 1.2·Rk) / (0.44·Rd - 4·Ra·(0.5 + 1.2·Rk))
  - OQ = (1 + Rk)/(2·Rg), αm = 1/(1 + Rk), Ta = Ra·T0
  - glottal formant: Fg = 1/(2·OQ·T0), Bg = 1/(OQ·T0·tan(π(1 - αm))); a two-pole filter at Fg, Bg whose impulse
    response is the LF glottal flow (fextr adds a zero at 0 Hz to get the flow derivative; here the radiation
    difference of 3.5 does that)
  - tilt: a one-pole low-pass with pole exp(-2π·Fa/SR), Fa = 1/(2π·Ta), unity gain at 0 Hz
  - Degenerate values (a non-finite result, or a near-zero denominator) skip the pulse, as fextr does.
- **Level:** each pulse is scaled so that a neutral vowel (the default cascade: F1-F5 = 500/1500/2500/3500/3750 Hz,
  B1-B5 = 50/70/110/250/200 Hz) gets the same power for every Rd: the power of the radiated pulse train through
  that tract, summed over the harmonics below SR/2 (evaluated at max(F0, 1 Hz), so very low F0 values cannot make
  the sum unbounded), is set to F0/SR, one unit of energy per period. A calibration
  constant `K_LF` then makes a steady neutral vowel at AV = 60, F0 = 100 Hz, Rd = 1 match the 1980 source's RMS at
  default globals (within 0.5 dB). Output level depends on F0 the same way for both sources. (Equal energy per pulse
  was tried first and let the vowel's loudness swing by about ±6 dB across Rd.)
- **Noise modulation** follows the open phase: noise is halved while the glottal phase is at or beyond OQ (the 1980
  source keeps the half-period rule).
- **AVS** with SRC = 1 feeds the LF source's output through RGS, as the 1980 source feeds RGP's output.
- The source must be free of per-period discontinuities: the filters run continuously across pulses (their
  coefficients change at the pulse), as in fextr.

### 3.3 Rd with the 1980 source (SRC = 0)

Rd approximates the LF source's change of spectral tilt on the 1980 source:

- At each pulse, with the current F0 and the latched Rd, compute L(Rd) = 20·log10|H(f_hi)| - 20·log10|H(F0)|, where
  H is the LF source's frequency response from 3.2 (glottal formant times tilt, at that F0) and
  f_hi = min(3000 Hz, 0.45·SR). Then Δ = L(Rd) - L(1) in dB.
- **Brighter (Δ > 0, Rd < 1):** RGP widens to the bandwidth, between BGP and 0.45·SR and found by bisection, at which
  RGP's own level at f_hi relative to F0 is Δ higher than at BGP.
- **Darker (Δ < 0, Rd > 1):** at the default BGP of 100 Hz both F0 and f_hi already lie on RGP's -12 dB/octave slope,
  so narrowing RGP cannot darken it further (found while prototyping: within 2 dB of its limit). Instead a one-pole
  low-pass (unity gain at 0 Hz) on the AV voicing gives the change; its pole solves |T(f_hi)|²/|T(F0)|² = 10^(Δ/10)
  in closed form (a quadratic), capped at 0.999 when more is asked than one pole can give.
- **Level:** the pulse is scaled so that the neutral vowel keeps the power it has at Rd = 1 (the same harmonic sum
  as the LF source's level, 3.2, over the 1980 chain with the widened RGP or the low-pass); at Rd = 1 the scale is
  exactly 1. Without it, brightening raised the vowel by up to 34 dB (found in the final review).
- At Rd = 1, Δ is exactly 0: RGP keeps BGP and the low-pass is off, so the default gives the 1980 source bit for bit.
  Rd below 1 brightens the source (pressed), above 1 darkens it (breathy), and an Rd fitted by the import means
  roughly the same in either mode. FGP, FGZ, BGZ and RGS are unchanged.
- Like the LF source's other settings, this changes only at pulses (the paper updates the voicing-source filters
  pitch-synchronously).

### 3.4 Flutter and diplophonia

- **FL** (global): the phase advances by F0' = F0 + (FL/50)·(F0/100)·[sin(2π·12.7·t) + sin(2π·7.1·t) +
  sin(2π·4.7·t)] Hz per second, with t in seconds since create/reset (deterministic), only while F0 > 0, and F0'
  clamped to the F0 range.
- **DI** (frame): every second pulse after voicing starts is delayed by (DI/100)·(1 - OQ)·T0 and its amplitude
  multiplied by (1 - DI/100). At DI = 100 the delayed pulse closes as the next unaltered pulse opens (Klatt &
  Klatt 1990, p. 840). OQ is the LF open quotient for SRC = 1 and 0.5 for SRC = 0. DI is latched at the pulse.

### 3.5 Noise and radiation (corrections to match Klatt 1980)

- **Noise generator:** each sample is the sum of 16 uniform pseudo-random numbers, centred on zero (paper p. 977;
  was 4).
- **No noise low-pass, radiation on the voicing source:** the paper's noise low-pass y(n) = x(n) + y(n - 1) exists
  only to cancel the output's radiation difference, and the paper notes both can be removed from the noise path.
  A literal integrator would drift, and AF changes would turn the drift into thumps. So: noise enters the branches
  white, and the radiation first difference moves from the output to the voicing: either source computes glottal
  flow, scaled by AV (and through RGS by AVS), and the first difference of that sum enters the branches. The output
  stage keeps G0, rounding and saturation, without a difference.
  The parallel branch still gives R2-R6, RNP and AB the first difference of the voicing (paper p. 982) and gives
  frication to R2-R6 and AB undifferenced. Every path therefore keeps the same overall filtering as before; only the
  noise's spectrum (now truly white after radiation) and the place where the difference is taken change.
- **AF and AH ramps:** both are interpolated linearly across each frame from the previous frame's value to the new
  one (paper p. 978). Exception: if AF rises by more than 50 dB from the previous frame, it jumps at the frame start
  (plosive bursts).
- These change the sound of every document slightly. `K_NOISE` and `K_PAR` are recalibrated with `make calibrate`
  (their definitions in klatt.c stay), and the approved `tests/golden/ba.raw` is recorded again after the user
  listens.

### 3.6 Editor support

- Left panel: a new group "Voice" with Rd and DI, whose strips come right after AVS's. The two extra rows would push
  the globals off a default-size window, so the panel gets a vertical scrollbar that appears when its content is
  taller than the window. The panel's globals text adds SRC. The Globals dialog lists SRC and FL like any other
  global (it is generated from the DLL's table).
- Painting and the line tool round Rd to 0.01 and everything else to whole units (cell edits keep the typed value,
  as before). The status bar shows the pointer's value at the same step.
- Optimize gains two tolerances: Rd (default 0.05) and % (default 2 %, for DI).
- The frame table, CSV files and Parameter reference pick up the new parameters from the DLL's table. A saved frame
  file has all 23 columns.
- `samples/python/klatt.py` keeps working (it reads the counts from the DLL); checked, not assumed.

### 3.7 Tests (automated, both architectures)

- **LF shape:** at F0 = 100 Hz and Rd = 0.5, 1 and 2.5, harmonics 2-30 of the source (SW = 1, AB only, corrected for
  that path's two first differences) match the formulas' |H| relative to H1 within 1 dB, wherever the formula puts
  them within 60 dB of H1 (quieter ones sink into the 16-bit rounding); a larger Rd rolls off more.
- **LF level:** at fixed AV, the output RMS stays within ±1 dB as Rd sweeps 0.3-2.7 (F0 = 100 and 200 Hz); the LF and
  1980 sources match within 0.5 dB at the calibration point.
- **1980 tilt:** with SRC = 0, at F0 = 100 and 200 Hz the level near f_hi relative to H1 falls monotonically as Rd
  rises, and its change from Rd = 1 is within 4 dB of the LF source's
  change for Rd 0.5-2.5; the neutral vowel's RMS stays within ±1.5 dB of Rd = 1 for Rd 0.3-2.7.
- **Timing:** SRC = 1 keeps the pitch accuracy of the existing F0 tests (fractional periods, no rounding to whole
  samples).
- **FL:** FL = 100 moves F0 by several Hz, and F0 measured over four periods at a time follows the formula within
  10 % of its excursion.
- **DI:** DI = 50 gives alternating pulse amplitudes with ratio 0.5 ± 0.05 and the delayed pulses shifted by the
  formula's delay ± 1.5 samples.
- **Noise:** the spectrum of AF-only frication through AB is flat within 3 dB from 30 Hz to 0.45·SR (the old noise was
  about 11 dB down at 50 Hz); an AF step
  from 60 to 70 dB ramps linearly across the frame; an AF jump of more than 50 dB lands at the frame start.
- The existing suites stay green with updated expectations; 32- and 64-bit output still agree (0 LSB is the current
  result; the requirement stays 1 LSB), now also over a stretch using the LF source, flutter, diplophonia and an Rd
  glide.

## 4. Part B: WAV import

### 4.1 Where it lives

A new analysis core in `kpe/`, plain C with no Win32 code, linked by the test runner like `model.c`. It uses the DLL
only to synthesize. The GUI adds the menu command and dialog.

### 4.2 Input

- **WAV reader:** RIFF/WAVE with PCM (8-, 16-, 24- or 32-bit integer) or IEEE float (32-bit), including
  WAVE_FORMAT_EXTENSIBLE with those subformats. Any channel count is mixed to mono by averaging. Anything else is
  refused with a message naming the format.
- **Resampler** to the target SR: windowed sinc (Kaiser window), any ratio; passband flat within 0.1 dB up to 0.9 × the
  lower Nyquist frequency, stopband at least 60 dB down.
- A recording longer than the document maximum (10 minutes) is refused; a silent one (peak below -60 dBFS) is
  refused with "No sound found".

### 4.3 Analysis

Frames are FRAME_MS (5 ms) apart; the analysis window for frame *k* is centred on (k + 0.5)·FRAME_MS, the middle of
the samples the synthesizer renders for that frame.

1. **Pitch:** YIN on the signal low-passed at 1.5 kHz (as the YIN paper recommends: unfiltered, a voiced fricative's
   frication hides its voicing), an integration window of one longest period centred on the frame, 40 ms at most, F0
   range from the voice type; per frame the best candidates, an aperiodicity value and a voiced flag. Frames more than
   40 dB below the loudest are unvoiced; a frame next to a voiced one stays voiced while its aperiodicity is below
   0.45. A dynamic-programming pass over each voiced run removes octave jumps, then the voiced F0 track is smoothed
   (5-frame median); unvoiced frames take F0 interpolated from their voiced neighbours.
2. **Formant candidates:** pre-emphasis 0.97, 25 ms Hann window, Burg LPC of order 2 + SR/1000 (rounded up to even),
   roots by a polynomial root finder; each root with positive frequency and radius at least 0.7 becomes a candidate
   (frequency, bandwidth), keeping those above 90 Hz, below 0.45·SR and narrower than 1000 Hz.
3. **Formant tracks:** Viterbi over the whole file. Each frame's states are the ordered choices of up to four
   candidates for F1-F4; the cost adds the distance from the voice type's reference values, the candidates'
   bandwidths and the frame-to-frame jump. Frames more than 40 dB below the loudest frame take no part in the search
   (no local and no transition cost) and their tracks are interpolated linearly across them. Tracks are clamped to the
   parameter ranges and below 0.45·SR.
4. **Bandwidths B1-B3:** from the chosen candidates, median-smoothed over 7 frames and clamped to 40-500 Hz.
5. **Formant correction** (voices whose median voiced F0 is at least 150 Hz, where LPC's harmonic bias matters): two
   analysis-by-synthesis passes synthesize the document's voicing, run the same LPC on it, and move F1-F3 by the WAV's
   value minus the synthesis's (the synthesis candidate nearest the current value within 25 %, at most 20 % per pass).
   Lower voices skip it: it gained them less than it cost.
6. **Frame classes:** silence (more than 50 dB below the loudest frame), voiced, voiced fricative, voiceless
   fricative, or aspiration (voiceless but not fricative, e.g. [h]), from the voiced flag and three measures: the
   fraction of energy above 4 kHz (or 0.35·SR when that is lower); the flatness of the top band (power density from
   0.41·SR to SR/2 against the densest 500 Hz band between 1 kHz and 0.41·SR); and the share of energy below the F1/F2
   midpoint. A voiced frame is a voiced fricative from a fraction of 0.15 or a top band within 12 dB; an unvoiced frame
   is a fricative from a fraction of 0.25, or from a top band within 25 dB with at most -8 dB of its energy below the
   F1/F2 midpoint (this separates [ʃ] from breathy vowel onsets). The fricative decision is then the majority of the
   non-silent frames within 5 frames, and a fricative run shorter than 40 ms is dropped. Class thresholds are named
   constants tuned on the test corpus and the user's recordings; their comments give the measured ranges.
7. **Rd** (every 12th frame of class voiced, 60 ms apart; not in voiced fricatives, whose residual is frication):
   inverse-filter the pre-emphasized, windowed frame with its own LPC coefficients; golden-
   section search Rd in 0.3-2.7 to minimize the squared difference between the residual's mean-normalized dB
   spectrum and a candidate's, over F0 to min(5·F0, 5000 Hz) (fextr's Method B), to a bracket of 0.1, first within
   0.5 of the previous fit in the same voiced run. Each candidate is a DLL synthesis of the frame's own analysed
   values (F0, F1-F4, B1-B3; AV fixed at 60 dB, since the comparison is mean-normalized) with SRC = 1 and the trial
   Rd, starting at least max(10 ms, 3 periods) before the window, and it goes through exactly the same pre-emphasis,
   window and inverse filter (the WAV frame's LPC coefficients) as the WAV frame. Anything the two sides share
   cancels, so a fitted Rd means what the synthesizer does with it. The fits are median-smoothed over 3 fits,
   interpolated inside voiced runs and held elsewhere (1.0 if a file has no fit).
8. **Levels, calibrated against the synthesizer:**
   - Start values from three reference syntheses at 60 dB (a neutral vowel with AV, with AH, and AF with A2-A6):
     AV from the frame level (voiced classes); in aspiration frames AH from the frame level; for fricative classes AF
     and A2-A6 from the energy in bands centred on F2-F6 (band edges midway between neighbouring formants). AB stays
     0: A2-A6 already tile the band above the F1/F2 midpoint, and AB would claim the same energy.
   - Aspiration in voiced frames: AH = AV + 10·log10(ap/(1 - ap)) from YIN's aperiodicity ap (median-smoothed over 5
     frames), which is meaningful because `K_NOISE` makes AH and AV equally loud at equal dB, but only from ap = 0.25
     on; below it AH is 0. Real vowels read 0.01-0.2 in their steady part, and with a lower threshold they resynthesize
     breathy. The cost: aspiration more than about 7 dB below AV is not imported. Voiced fricatives get AH 0 (their
     aperiodic energy is the frication).
   - Then calibration: the first pass synthesizes voicing and aspiration only and corrects AV (low-band level below
     the F1/F2 midpoint) and AH in aspiration frames (overall level) by the dB difference; in voiced frames AH keeps
     its offset from AV. The frication's band powers are the WAV's minus that synthesis's, and AF + A2-A6 are fitted
     to them jointly with a model of the parallel branch (the resonators' complex responses with the DLL's signs,
     scaled by a reference synthesis), because the parallel resonators leak into each other's bands and a per-band
     correction settles on wrong splits. The largest parallel amplitude is set to 70 dB and AF takes the rest, but at
     least 1 dB, so that the frication of a quiet recording is not switched off (only AF + A*n* matters to the DLL).
     Two more passes synthesize the whole document, scale each band's target by the WAV/synthesis ratio and refit, and
     correct AV and AH again.
   - Silence frames get all amplitudes 0. Values are clamped to their ranges and rounded (Rd to 0.01).
9. **Result:** globals at their defaults except SR (the target), SRC = 1 and FRAME_MS = 5; the frames as analysed.
   Optionally simplified with Optimize (every frame grid, default tolerances). Finally the document is synthesized
   once 24 dB below the default G0 and G0 is set to the whole number that puts the resynthesis's peak at or just
   below -1 dBFS; the amplitudes keep their calibrated values, so AV means the same as in the recording.

### 4.4 Voice types

| Voice type | F0 search range | Reference F1-F4 |
|---|---|---|
| Adult male | 60-250 Hz | 500, 1500, 2500, 3500 Hz |
| Adult female | 120-400 Hz | 560, 1680, 2800, 3900 Hz |
| Child | 180-500 Hz | 650, 1950, 3250, 4400 Hz |

References above the parameter maximum or 0.45·SR are clamped.

### 4.5 Editor

- **File > Import WAV…** (asks to save unsaved changes first), then a file dialog, then a dialog with voice type
  (default adult male), target SR (default 11025; a note says children's voices need about 16000 so their formants
  fit below Nyquist), and "Simplify with Optimize afterwards" (off by default). The choices are remembered for the
  session.
- While it runs: wait cursor and "Analyzing…" in the status bar.
- The result opens as a new unsaved document titled after the WAV; Save asks for a name, suggesting
  `<wav name>.csv`. Import is not an undo step (like Open).
- Errors (unreadable, unsupported, too long, silent, out of memory) are shown in a message box and leave the current
  document untouched.

### 4.6 Tests

All automated tests use synthetic audio made by the DLL from known documents, so the right answer is known.

- **Units:** WAV reader for every supported format and every refusal; resampler (tone frequency and level, image
  rejection); Burg on a known AR process (coefficients within 1 %); the root finder on polynomials with known roots;
  Viterbi on hand-made candidate sequences, including a missing and a spurious candidate.
- **Round trip:** `ba.csv`, a female-range vowel sequence, /asa/, /asa/ 48 dB quieter, /aza/, a /z/ with 10 dB weaker
  frication, an /aʃa/ with its frication at 2-4 kHz, an /ava/ with flat frication over damped voicing, and an Rd sweep
  (all but `ba.csv` with SRC = 1), synthesized and imported, compared on frames of each class at least 10 ms from a
  class boundary (in the source document):

  | Measure | Limit |
  |---|---|
  | F0 median error | < 2 % |
  | F1-F3 median error, steady voiced frames | < 5 % |
  | F1-F3 90th-percentile error, steady voiced frames | < 10 % |
  | Voiced/unvoiced decision | ≥ 95 % correct |
  | AV | within 3 dB |
  | Rd, steady voiced frames | within 0.3 |
  | Fricative band levels after calibration | within 3 dB |
  | Every class with at least 10 steady frames | ≥ 90 % of them right |
  | Steady vowel frames with frication | none |
  | Resynthesis | no overload, peak at or below -1 dBFS |

  "Within" is the 90th percentile of the per-frame absolute error. AV and Rd are compared only when the source
  document uses the LF source like the import (so not for `ba.csv`: at equal AV the two sources' low-band levels
  differ by a few dB depending on Rd, and Rd does not mean the same). For a voice whose median steady-voiced F0 is at
  least 200 Hz, AV and Rd are judged on their medians: a 25 ms LPC cannot resolve B1 when the harmonics are 200 Hz or
  more apart, and the Rd fit and AV then compensate the wrong bandwidth (female vowels: AV median 2 dB, 90th
  percentile 7; Rd median 0.11, 90th 1.5). Fricative band levels are compared on bands within 30 dB of the frame's
  strongest, less the G0 difference.
- **Frame classes of the user's recordings** (manual, from the prototype): S and SH import as fricatives, V, Z and
  ZH as voiced fricatives, the vowels as voiced with aspiration only at their onsets and offsets. Known limits: 30 of
  V's 91 frames get no frication (the voicing alone already reaches the recording's level above 1 kHz), and a
  plosive burst shorter than 40 ms imports as aspiration.
- **Speed:** a 60 s WAV imports in at most 5 s (32-bit build, this machine), measured by a test that reports the time.
- **Manual:** checklist items for the dialog and error messages, and listening checks on the user's recordings in
  `fextr_sept2026\wavs` (AE, IH, OW, S, SH, V, Z, ZH). Those recordings are not committed; their origin is unknown.

### 4.7 Performance

The prototype imports a 60 s, 11025 Hz WAV in 3.9 s on the 32-bit build (2.5 s on 64-bit): analysis 0.9 s, Rd
1.6 s, calibration and normalization 1.3 s. That needed Rd on every 12th voiced frame, three calibration syntheses,
a half-size complex FFT for real input, 5 refined YIN dips and tabulated resonator trig. A 48 kHz recording adds
0.6 s of resampling; the formant correction of a high voice about 0.6 s per minute. The import stays synchronous;
if the speed test ever misses 5 s, a worker thread with progress and cancel is designed then.

## 5. Changes to the base spec

Applied to `2026-09-28-kpe-open-design.md` by the implementing plans:

- §1: resynthesis moves from non-goals to this design.
- §3: the repository is now `C:\Users\abart\Desktop\KPE\kpe_open`, with new analysis files in `kpe/`.
- §4.1: source selection (SRC), the LF source, Rd's tilt on the 1980 source, FL/DI, the noise changes and the radiation's new place.
- §4.2: the four new rows; counts 23 and 20; Rd's resolution; the frame and global orders.
- §4.4: `KlattParamCount` returns 23 or 20; version 1.1.0.
- §5.1: the "Voice" group; §5.5: File > Import WAV…; §5.6: the Rd and % tolerances; §5.7: 23 columns.
- §8: the new tests; §9: resynthesis removed from future work.

## 6. Delivery

Two plans, executed one after the other with subagent-driven development:

1. **DLL 1.1** (Part A): parameters, LF source, FL, DI, noise and radiation changes, recalibration, editor support,
   docs; ends with the user's listening check and the re-recorded golden file.
2. **WAV import** (Part B): WAV reader and resampler, analysis core (pitch, LPC, roots, tracking, classes, Rd,
   calibration), then the dialog and menu command, UI script and checklist.
