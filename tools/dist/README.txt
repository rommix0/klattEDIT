klattEDIT - an editor for Klatt synthesis parameters, and its Klatt synthesizer DLL
=============================================================

A clean-room editor for Klatt (1980) synthesis parameters, with its own synthesizer DLL (version 1.2).


The editor
----------

Start klattEDIT.exe; keep klatt32.dll in the same folder. File > Open... and choose ba.csv for an example, or
File > Import WAV... to start from a recording.

- Drag in a strip to draw its track; Shift+drag draws a straight line; drag in the time ruler to select.
- Space plays, Shift+Space plays the selection, Ctrl+Z undoes.
- Drag the gap between two strips to resize them, or a strip's label to move it
  (View > Reset strip layout undoes it).
- Parameters > Globals sets the sample rate, frame length, speaking speed (SPD) and the other globals;
  Parameters > Optimize smooths and simplifies tracks.
- Help > Parameter reference lists every parameter with its unit and range.

A document is a pair of CSV files: name.csv (one row per frame) and name.globals.csv. A missing globals file
means the defaults, as for ba.csv.


The synthesizer DLL (sdk folder)
--------------------------------

For using the synthesizer from your own programs:

  sdk/klatt.h            the API
  sdk/klatt-dll.md       how to use it
  sdk/32/klatt32.dll     32-bit DLL (__stdcall, plain export names) and its MinGW import library libklatt32.a
  sdk/64/klatt64.dll     64-bit DLL and its MinGW import library libklatt64.a
  sdk/python/klatt.py    a Python (ctypes) example; from this folder, with 64-bit Python:
                           python sdk/python/klatt.py sdk/64/klatt64.dll glide.wav

The program and its DLL must have the same bitness. The DLL only synthesizes: it reads no files and has no user
interface.
