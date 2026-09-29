# kpe_open

A clean-room, 32-bit Windows successor to the IPOX Klatt Parameter Editor (1995), and a Klatt 1980 formant
synthesizer DLL (32- and 64-bit) that other programs can use. Nothing here is derived from the original IPOX
binaries.

- `klatt/`: the synthesizer DLL. See `docs/klatt-dll.md`.
- `kpe/`: the editor. Its core (document and undo, CSV files, optimization, synthesis, WAV, spectrogram) has no
  user interface and is covered by the tests; the Win32 GUI is built on top of it.
- `tests/`: the test runner. `tools/`: build helpers. `samples/python/`: using the DLL from Python.
- Design: `docs/superpowers/specs/2026-09-28-kpe-open-design.md`.

## Building

Needs winlibs GCC 14.2 in `C:\winlibs\mingw32` and `C:\winlibs\mingw64`, and Git Bash. From Git Bash:

```bash
/c/winlibs/mingw32/bin/make.exe ARCH=32    # build/32: klatt32.dll + tests
/c/winlibs/mingw32/bin/make.exe ARCH=64    # build/64: klatt64.dll + tests
/c/winlibs/mingw32/bin/make.exe test       # both, run the tests, compare 32- and 64-bit output
```
