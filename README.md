# kpe_open

A clean-room, 32-bit Windows successor to the IPOX Klatt Parameter Editor (1995), and a Klatt 1980 formant
synthesizer DLL (32- and 64-bit) that other programs can use, with an optional LF voicing source, the flutter
and diplophonia controls of Klatt & Klatt (1990) and a speaking-speed control. Nothing here is derived from the
original IPOX binaries.

- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
- `kpe/`: the editor. Its core (document and undo, CSV files, optimization, synthesis, WAV, spectrogram, and the
  WAV import's analysis) has no user interface and is covered by the tests; the Win32 GUI is built on top of it.
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
- Design: `docs/superpowers/specs/2026-09-28-kpe-open-design.md`, for the voice source and WAV import
  `docs/superpowers/specs/2026-09-29-voice-source-and-wav-import-design.md`, and for the speaking speed, cropping,
  steadier imports and smoothing `docs/superpowers/specs/2026-09-30-speed-crop-smoothing-design.md`.

## Building

Needs winlibs GCC 14.2 in `C:\winlibs\mingw32` and `C:\winlibs\mingw64`, and Git Bash. From Git Bash:

```bash
/c/winlibs/mingw32/bin/make.exe ARCH=32    # build/32: klatt32.dll + tests
/c/winlibs/mingw32/bin/make.exe ARCH=64    # build/64: klatt64.dll + tests
/c/winlibs/mingw32/bin/make.exe test       # both, run the tests, compare 32- and 64-bit output
/c/winlibs/mingw32/bin/make.exe dist       # test, then the release in dist/ (editor, ba.csv, sdk/; not committed)
```

## The editor

`build/32/kpe.exe [file.csv]` (keep `klatt32.dll` next to it). Parameters are painted with the mouse:
drag in a strip to draw its track, Shift+drag for a straight line, drag in the time ruler to select frames.
Drag the gap between two strips to resize them, or a strip's label to move it (View > Reset strip layout undoes it).
Every edit re-synthesizes; Space plays, Ctrl+Z undoes. The global SPD plays the document faster or slower without
changing its pitch (the strips always show it as written). Edit > Crop to selection keeps only the selected frames.
Parameters > Optimize smooths tracks (F1-F4 by default) and then simplifies them. A document is a pair of CSV files,
`name.csv` (one row per frame) and `name.globals.csv`; see the design spec, section 5.7. Try `samples/ba.csv`.

File > Import WAV... turns a recording (PCM or float WAV, any sample rate and channel count, up to 10 minutes) into
a first draft: F0, voicing, F1-F4, B1-B3, Rd, AV, AH, and frication with its parallel amplitudes, using the LF
source (SRC = 1), with G0 set so the result peaks at about -1 dBFS. Drag on the recording's waveform to import
only part of it; choose the voice type (children need a sample rate of about 16000 Hz) and the analysis (Fast,
Normal or Best). It is a draft for hand editing, not a copy: stops and nasals are not analysed.

Before a release, go through `docs/manual-checklist.md`; `tools/ui/*.ps1` automate part of it.
