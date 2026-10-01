# kpe_open: speaking speed, cropping, steadier imports and smoothing

Four additions requested after the user's listening check of the WAV import: a speaking-speed global in the DLL
and an analysis-quality choice for the import; cropping in the editor and in the import dialog; fewer single-frame
jumps in imported tracks; and adjustable per-track smoothing in Optimize.

Date: 2026-09-30. Status: approved in brainstorming, pending written-spec review.
Base design: `2026-09-28-kpe-open-design.md` ("the base spec"); WAV import design:
`2026-09-29-voice-source-and-wav-import-design.md` ("the import spec"). Section 7 lists the sections of both that
this design amends.

## 1. Goals and non-goals

**Goals**

- **Speed:** a global SPD (1 = as written) that plays a document faster or slower without changing its pitch or
  formants; and Fast / Normal / Best analysis presets for the import, Fast bringing a 60 s high voice under 5 s.
- **Cropping:** Edit > Crop to selection in the editor, and a range selection on a waveform in the Import WAV dialog.
- **Steadier imports:** no single-frame spikes in imported formants and amplitudes inside a stretch of one sound
  class, in particular in voiced fricatives, where today's F1-F3 jump in up to 15 % of the frames.
- **Smoothing:** a smoothing width per track in Parameters > Optimize, applied before simplification.

**Non-goals**

- Changing pitch with SPD (it is a time scale only), or a speed control on the playback device.
- Editing the waveform itself: the import dialog only selects a range of the recording.
- A Kalman smoother (the user chose median + Gaussian), or smoothing inside the DLL.
- Changing the spike-free behaviour at class boundaries: a jump where the sound class changes (an onset, a
  fricative edge) is real and stays.

## 2. Sources and clean-room rules

The base spec's and the import spec's rules hold. The spike filter is Hampel's (a running median with a robust
spread estimate, Hampel 1974); the smoothing is a running median followed by a Gaussian window. Nothing new is
taken from another program.

## 3. Speaking speed and import quality

### 3.1 SPD (Klatt DLL 1.2.0)

- New global, appended after FL so no index moves: `{ "SPD", "-", "speaking speed: 1 = as written", 0.25, 4, 1 }`.
  `KG_SPD` = 20, `KLATT_NUM_GLOBAL` = 21, `KLATT_VERSION` = 10200. Frame parameters are unchanged (23).
- Frame timing (base spec 4.3) becomes: a frame is `SR·FRAME_MS/(1000·SPD)` samples, and frame *k* ends at sample
  `floor((k+1)·SR·FRAME_MS/(1000·SPD))`. `KlattMaxFrameSamples` returns the ceiling of that length. With SPD = 1
  every sample is the same as in 1.1.
- Everything else runs in real time: F0, the formants, the pulse shape, flutter, diplophonia, the noise and the
  AF/AH ramps are unaffected by SPD; only how long each frame's values last changes. Values still change at frame
  boundaries as in 1.1.
- `KlattReset` resets the frame counter as before.

### 3.2 SPD in the editor

- SPD appears in the Globals and New dialogs (from the table) and in the left panel's globals text.
- The strips, the spectrogram and the frame table always show the document at SPD = 1: the editor's display
  synthesis ignores SPD, so the spectrogram stays aligned with the tracks.
- Play, Play selection and Export WAV synthesize with SPD. The playhead maps the playing sample back to document
  time with the SPD frame formula, so it still moves over the right frames.
- `synth_frame_start` (`kpe/synthrun.h`) gains the SPD factor; the display synthesis passes 1.

### 3.3 Import analysis presets

The Import WAV dialog gains "Analysis: Fast / Normal / Best" (default Normal, remembered for the session):

| Preset | Rd fitted on | Formant correction passes (F0 ≥ 150 Hz) | Calibration passes after the first |
|---|---|---|---|
| Fast | every 24th voiced frame | 1 | 1 |
| Normal | every 12th (today) | 3 (today) | 2 (today) |
| Best | every 4th | 3 | 2 |

The verification synthesis and the G0 normalization run in every preset. The prototype may change Fast's numbers
if they miss the speed target, and records the final values here. Targets: a 60 s high voice (median F0 ≥ 200 Hz)
imports with Fast in at most 5 s on the 32-bit build; Normal and Best keep the import spec's 4.6 limits; Fast's
round-trip results are measured, and any limit Fast misses is listed with its value in the import spec's known
limits.

`import_options` gains `int quality` (`IMPORT_FAST`, `IMPORT_NORMAL`, `IMPORT_BEST`).

## 4. Cropping

### 4.1 Edit > Crop to selection

- Menu item after "Delete frames", shortcut Ctrl+Shift+X. Keeps the selected frames and deletes the rest as one
  undo step labelled "Crop"; the selection then covers the whole document.
- Disabled when nothing is selected or the whole document is selected.
- `doc_crop(kpe_doc *d, int first, int count)` in `kpe/model.c`, built on the existing frame deletion inside one
  `doc_begin`/`doc_end` group.

### 4.2 Range selection when importing

- The import reads the WAV first (errors as before), then shows the Import WAV dialog with a waveform strip of
  the whole recording above the existing fields: a min/max envelope per pixel column, the selected range shaded.
- Drag on the strip selects a range (in samples, snapped to whole milliseconds); a double-click selects the whole
  recording. "Start" and "End" fields (seconds, 3 decimals) show the range and can be typed into; a "Play" button
  plays the range at the recording's own rate. The default is the whole recording.
- Only the selected range is analysed: `import_samples` receives that part of the samples. A range shorter than
  50 ms is refused with a message; the 10-minute limit applies to the file as before.
- The imported document is titled after the WAV as before.
- The waveform strip is a small custom control in `kpe/dialogs.c` (GDI only); the range logic (pixel to sample,
  snapping, clamping, the envelope) lives in a core file with unit tests (`kpe/waverange.c`).

## 5. Steadier imports (the import spec's 4.3)

### 5.1 Measured now

On the user's recordings (adult male, 11025 Hz), counting frames inside a run of one class whose value differs
from the mean of its two neighbours by more than 8 % (formants) or 6 dB (amplitudes):

| Recording | F1 | F2 | F3 | F4 | AV | AF | AH |
|---|---|---|---|---|---|---|---|
| V (89 voiced-fricative frames) | 13 | 15 | 12 | 0 | 9 | 13 | 0 |
| Z (156) | 8 | 0 | 11 | 0 | 0 | 1 | 0 |
| ZH (164) | 9 | 11 | 10 | 8 | 10 | 15 | 0 |
| AE / IH / OW (voiced, 59-86) | 3-7 | 0-2 | 0-1 | 0-2 | 0-2 | - | 4-7 |

The vowels' AH spikes are single frames where the aperiodicity crosses the 0.25 threshold and AH switches on.

### 5.2 Formant candidates weighted by reliability

- **Frame reliability.** Before tracking, each frame gets a reliability r in [0, 1] from measures the pipeline
  already has: the frication measures of step 6 (the energy fraction above the cutoff and the top band's flatness,
  with neutral formants) and YIN's aperiodicity. A periodic frame with little frication has r near 1; a
  frication-dominated frame has r near 0.
- **Tracker.** `track_formants` takes r per frame and multiplies each frame's local cost (distance from the
  reference and bandwidth) by r, so that in unreliable frames continuity (the transition cost) decides. The
  transition and empty-track costs are unchanged.
- **A second analysis.** In frames with r below a threshold, a second LPC runs on the frame low-passed at
  min(3000 Hz, 0.3·SR) (order for that band), where voicing dominates F1-F3. A full-band candidate within 10 % of
  a low-band candidate is "confirmed": its local cost is halved. Low-band candidates are not added as states.
- The constants (the r mapping, the threshold, the low-pass, the confirmation distance and bonus) are named,
  tuned on the round-trip corpus and the user's recordings, and carry their measured ranges in comments.

### 5.3 Spike filter

- A Hampel filter runs within each run of frames of one class: a frame whose value differs from the median of the
  five frames centred on it (the run's frames only) by more than max(3 × 1.4826 × MAD, a floor) is replaced by that
  median. Floors: 3 % for formants and bandwidths, 3 dB for amplitudes. An amplitude of 0 (off) is neither changed
  nor used as a neighbour's value. Runs shorter than three frames are left alone; class boundaries are never
  smoothed across.
- It runs twice: on F1-F4 and B1-B3 right after tracking and the formant correction (so Rd and the calibration see
  the filtered formants), and on AV, AF, AH and A2-A6 after the last calibration pass, before the verification
  synthesis (import spec 4.3 step 8) and the G0 normalization, so the burst and peak guarantees still hold.
- **Aspiration in voiced frames** gets hysteresis: AH switches on when the smoothed aperiodicity rises above 0.25
  and off only when it falls below 0.20, and an on-run shorter than 3 frames is turned off. The prototype may move
  the off threshold and records the value here.

### 5.4 Targets

- On the user's recordings: no frame inside a class run differs from the mean of its neighbours by more than 8 %
  (F1-F4) or 6 dB (AV, AF, AH), except where a run is shorter than three frames; AH is not switched on for fewer
  than 3 frames inside a voiced run.
- The import spec's 4.6 round-trip limits still hold at every rate and level tested there.
- The speed test (Normal, male) stays within 5 s on 32-bit.

## 6. Smoothing in Optimize

- **Dialog.** Each row of the track list shows the track's name and its smoothing width ("F2 · 15 ms"). Below the
  list, "Smoothing (ms)" edits the width of the selected track(s); 0 turns smoothing off. Widths are remembered for
  the session.
- **Defaults:** F1 10, F2 15, F3 20, F4 25 ms; every other track 0. The higher formants get more because their
  estimates wobble more (5.1).
- **Algorithm** (`kpe/optimize.c`, `opt_smooth`): on the range (selection or whole file), per checked track with a
  width w > 0: a 3-frame running median, then a Gaussian whose full width at half maximum is w (σ = w / 2.3548),
  truncated at ±3σ and renormalized at the range's ends. Amplitude tracks are smoothed only within runs of non-zero
  values, so an onset stays an onset. Then the existing simplification runs on the smoothed values with the
  existing tolerances.
- Preview, the summary text ("F2: smoothed 15 ms, 160 -> 14 breakpoints, max error 18 Hz"; the error is against
  the original values) and the single undo step work as before.
- The import's "Simplify with Optimize afterwards" uses the same defaults (smoothing, then simplification).

## 7. Changes to the earlier specs

Applied by the implementing plan:

- Base spec: 4.2 (the SPD row, global count 21), 4.3 (frame timing with SPD), 4.4 (version 1.2.0, counts 23 and
  21), 5.2 (Crop to selection), 5.3 (Play and Export use SPD; display at SPD 1), 5.5 (the import dialog's range and
  preset), 5.6 (smoothing), 8 (the new tests).
- Import spec: 4.3 (reliability-weighted tracking, the second analysis, the spike filter), 4.5 (the dialog), 4.6
  (the new tests; the user's recordings now live in `C:\Users\abart\Desktop\fwavs`), 4.7 (the presets' speeds).
- `docs/klatt-dll.md`: SPD and the frame-timing formula; `docs/manual-checklist.md`: the new items; README.

## 8. Tests

All automated tests run on both architectures; the 32/64-bit comparison stays at 0 LSB.

- **DLL:** SPD = 1 is bit-identical to 1.1 (the golden `ba.raw` and the xarch stretch are unchanged); SPD = 2 and
  0.5 give total lengths within one sample of half and double; F0 and F1 measured on a steady vowel are within 1 %
  at SPD 0.5, 1 and 2; `KlattMaxFrameSamples` matches the longest frame at SPD 0.25; the xarch comparison gains an
  SPD 1.5 stretch. Parameter-table tests: 21 globals, the SPD row.
- **Editor core:** `synth_frame_start` with SPD; `doc_crop` (contents, one undo step, undo/redo, refusal of empty
  and whole ranges); the waveform range logic (pixel/sample mapping, snapping, clamping, envelope min/max);
  `opt_smooth` (a single spike removed by the median, a step kept, a Gaussian's FWHM measured on an impulse,
  amplitude onsets kept, w = 0 a no-op).
- **Import:** the presets (Fast within 5 s on a 60 s high voice on 32-bit, the 4.6 limits for Normal and Best,
  Fast's measured values); the reliability weighting (a synthetic /ava/ and /aza/ whose frication is 6 dB stronger
  than the corpus's: no spikes over the 5.4 thresholds within class runs); the Hampel filter on hand-made tracks
  (a spike removed, a step kept, a short run untouched, zeros kept); a range import equal to importing a WAV of
  just that range; every existing round trip.
- **UI script:** `tools/ui/check_import.ps1` drags a range on the waveform and imports it (the document is as long
  as the range); a new step in `check_edit.ps1` crops a selection and undoes it.
- **Manual:** listening with SPD 0.5 and 2; the waveform dialog; Optimize smoothing on an imported F3/F4; the user's
  recordings in `C:\Users\abart\Desktop\fwavs` re-imported and checked for jumps.

## 9. Delivery

One plan (plan 6), prototyped first like plans 4 and 5 (the spike fix and Fast's numbers need measurements), with
tasks roughly: DLL SPD; editor SPD; crop to selection; Optimize smoothing; import presets; reliability weighting and
the second analysis; spike filter; the import dialog's waveform range; docs; the user's listening check.
