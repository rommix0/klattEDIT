# AGENTS.md

Notes for coding agents working on kpe_open: an open-source Win32 C editor for Klatt synthesis parameters
(`kpe.exe`) with its own Klatt synthesizer DLL (`klatt32.dll`/`klatt64.dll`) and a WAV import (copy synthesis).
The design lives in `docs/superpowers/specs/` (base spec `2026-09-28-kpe-open-design.md`; DLL 1.1 and WAV import
`2026-09-29-...`; speed, crop, smoothing `2026-09-30-...`). Read the spec before changing behaviour.

## Hard rules

- Clean room: never read `C:\klsyn\src` or `C:\Users\abart\Desktop\prose2000_reborn`; never port Numerical Recipes code
  (e.g. fextr's `burg_lpc`). The user's own code in `fextr_sept2026` may be ported.
- The DLL is synthesis only (plus the parameter table and error text): no file I/O, no UI. Ask the user before
  adding anything else to it. The analysis core (`kpe/` files other than main, strips, table, dialogs, audio) has
  no Win32 code.
- SPD = 1 stays bit-identical: `tests/golden/ba.raw` is re-recorded (`make golden`) only after the user listens.
- The user's recordings (`C:\Users\abart\Desktop\fwavs`) are for manual checks only; never commit them.
- Commit or push only when asked; work on a feature branch and let the user choose merge / PR / keep. Commit
  messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Present a design and get the user's approval before implementing, even for small features.

## Build and test (Git Bash)

```bash
/c/winlibs/mingw32/bin/make.exe test       # 32 + 64 bit runners, then "xarch: ... max difference 0 LSB: ok"
/c/winlibs/mingw32/bin/make.exe ARCH=32    # also builds the editor build/32/kpe.exe
/c/winlibs/mingw32/bin/make.exe dist       # test, then the release in dist/ (git-ignored)
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui/check_main.ps1   # also check_edit, check_import
```

- Green means: every check passing on both architectures, zero warnings (runner and editor), xarch 0 LSB,
  golden passing, and the speed tests within 5 s on 32-bit (60 s male/Normal, 60 s high voice/Fast).
- Run each UI script in its own process, one at a time, and look at the screenshots in `build/ui/`.
- The Makefile picks up `kpe/*.c` and `tests/*.c` by wildcard. Tests are hand-rolled (`tests/test.h`, `CHECK`);
  register new suites in `tests/run_tests.c`.

## Conventions

- C (gnu11), tabs, K&R braces, short comments only where the code does not explain itself. Tuned constants carry
  their measured range in a comment. Edit files with the editor tools, not shell heredocs (tabs, backslashes).
- Pure logic goes in a Win32-free core file with unit tests (e.g. `view.c`, `waverange.c`); Win32 files stay thin.
- Documents are CSV only: `name.csv` plus the `name.globals.csv` sidecar (missing = defaults).
- Editor UI state (strip layout, zoom) is per session; nothing is saved between runs.

## Methods that worked

- Feature flow: brainstorm, then spec (committed), then a prototype in a scratch worktree with one commit per
  task, then a plan generated mechanically from those commits (full new files plus exact find/replace blocks).
  Verify the plan by applying it to the base and diffing against the prototype head. Execute with
  subagent-driven development: a ledger, one implementer and one reviewer per task, and a final whole-branch
  review on the strongest model with one fix wave.
- TDD: write the tests first and capture RED (a build error is fine), then GREEN. For a test of a safety
  property, prove it can fail by temporarily breaking the code (a mutation), then restore it.
- Compare each task's tree with the prototype (`git diff <proto-commit> HEAD`): stray whitespace in a replace
  target breaks later tasks' exact edits.
- Measure, don't guess: a probe over the user's recordings measured spikes per class; report misses with numbers
  instead of loosening tests.

## Findings worth knowing

- 32-bit DLL: `__stdcall` with plain export names (`--kill-at`); an import library made by `dlltool -k` maps the
  decorated names. Forcing SSE2 math on 32-bit makes 32/64 output identical and immune to the x87 control word.
- SPD only divides the samples per frame, so SPD = 1 is exact. The display synthesis always runs at SPD 1;
  Play and Export use the document's SPD, and the playhead maps back with `synth_sample_ms`.
- WAV import known limits (accepted by the user): /v/'s F1 jumps to the next formant in three short stretches;
  ZH AF dips; 8 kHz and 16 kHz results (see the import spec 4.6); a mostly-fricative recording imports about
  three times slower (13-18 s per minute, calibration-bound, not a regression).
- A Hampel filter within sound-class runs, then Gaussian smoothing of F1-F4 within runs, removed almost all
  formant spikes; jumps at class boundaries are real and are kept. Simplify after import smooths F1-F4 again;
  the user chose to keep that.
- `.gitignore`: anchor root folders (`/dist/`); a bare `dist/` also hides `tools/dist/`.
