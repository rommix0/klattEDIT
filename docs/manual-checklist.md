# Editor manual checklist

Run before a release, at 100 % and at 150 % display scaling. Start `build/32/kpe.exe samples/ba.csv`.
`tools/ui/check_main.ps1`, `tools/ui/check_edit.ps1` and `tools/ui/check_import.ps1` automate part of this and
save screenshots in `build/ui/`; the items below are the ones that need a person.

## Display

- [ ] Strips for F0, AV, AF and AH, then the spectrogram with F1-F4 drawn over it; F5 and F6 are dotted lines.
- [ ] Every strip has a value axis with its unit on the left; the time axis in ms is at the bottom.
- [ ] Ticking and unticking parameters in the left panel adds and removes strips (F1-F4 only add or remove
      tracks on the spectrogram).
- [ ] View > Narrowband shows harmonics as horizontal lines; turning it off shows vertical glottal pulses.
- [ ] View > Frequency range... changes the top of the spectrogram strip.
- [ ] Ctrl+wheel zooms around the pointer; the wheel and the scrollbar pan; Ctrl+0 fits the whole file.
- [ ] Ticking Rd and DI (group "Voice") adds their strips right after AVS; the Rd axis shows 1 and 2 and the
      status bar shows the pointer's Rd with two decimals.
- [ ] At the default window size the left panel shows a scrollbar; the wheel over the panel and the scrollbar
      reach the last global line (F6). Enlarging the window until everything fits hides the scrollbar.

## Editing

- [ ] Dragging in a strip paints its track; releasing re-synthesizes and updates the spectrogram.
- [ ] Dragging near a formant on the spectrogram paints that formant and no other.
- [ ] Shift+drag draws a straight line from the press point to the release point.
- [ ] Dragging in the time ruler selects frames across all strips; a click without dragging moves the cursor.
- [ ] Edit > Undo and Redo name the step ("Undo Paint F2") and restore exactly.
- [ ] Cut, Copy, Paste, Delete frames and Insert frames... change the duration shown in the status bar.
- [ ] After Delete frames the table row matches the cursor.
- [ ] View > Frame table: selecting a row moves the cursor; F2 or a double-click edits a cell; Enter commits and
      the plot follows; Escape cancels.

## Sound

- [ ] Space plays the whole file with a moving red playhead; Space again stops.
- [ ] Shift+Space plays only the selection.
- [ ] The status bar reports an overload when AV is raised to 80 dB.
- [ ] Listening: `samples/ba.csv` sounds like "ba"; a steady AV = 60 vowel with the default formants sounds like
      a neutral "uh".
- [ ] Listening, voice source: with Globals SRC = 1, `ba.csv` still sounds like "ba" at about the same loudness;
      painting Rd from 0.5 to 2.5 moves the voice from pressed to breathy in both SRC = 0 and SRC = 1; FL = 50 adds
      a slow natural wobble; DI = 50 over the last 200 ms gives a rough, creaky ending.
- [ ] Once the listening checks are approved (and after any intended change to the synthesizer), run
      `make golden` and commit `tests/golden/` so later changes to the sound are caught by the tests.

## Dialogs

- [ ] Parameters > Globals...: a value outside its range is refused with a message naming the parameter.
- [ ] Parameters > Globals... and File > New... list every global on its own row with its name, description and
      range; SW and SRC cannot be mistaken for each other.
- [ ] Changing FRAME_MS from 5 to 10 keeps the duration, halves the frame count, and shows a notice that the
      frames were resampled and the duration stays about the same.
- [ ] Parameters > Optimize...: Preview shows the original tracks faintly and a summary per track; Apply keeps
      the result as one undo step; Cancel restores the original. (Accepted limitation: recording the preview
      step clears any redo history that existed before Preview was first pressed in this dialog session.)
- [ ] Optimize > Preview > Cancel leaves the title without * and Redo disabled.
- [ ] Parameters > Optimize... has Rd and DI (%) tolerances (0.05 and 2).
- [ ] Help > Parameter reference lists all 23 frame parameters and 20 globals.

## Files

- [ ] File > Save As... writes `name.csv` and `name.globals.csv`; reopening them gives the same document.
- [ ] Opening `name.globals.csv` directly is refused with a message.
- [ ] Opening a CSV with an unknown column warns and skips it; a bad number is refused with its line and column.
- [ ] File > Export WAV... writes a file that plays in another program.
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
