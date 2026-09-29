"""ctypes bindings for the Klatt synthesizer DLL, plus a vowel-glide example.

Python and the DLL must have the same bitness: 64-bit Python loads klatt64.dll, 32-bit Python klatt32.dll.
Run:  python klatt.py path/to/klatt64.dll glide.wav
"""
import ctypes
import struct
import sys
import wave
from collections import namedtuple

KLATT_FRAME, KLATT_GLOBAL = 0, 1
Param = namedtuple("Param", "name unit description min max default")


class ParamInfo(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("unit", ctypes.c_char_p), ("description", ctypes.c_char_p),
                ("min", ctypes.c_double), ("max", ctypes.c_double), ("default", ctypes.c_double)]


class Klatt:
    """One synthesizer handle. Frame and global parameters are dicts keyed by name; missing ones use defaults."""

    def __init__(self, dll_path, **globals_):
        self.lib = lib = ctypes.WinDLL(dll_path)  # __stdcall on 32-bit; the only convention on 64-bit
        lib.KlattParamCount.argtypes = [ctypes.c_int]
        lib.KlattParamInfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ParamInfo)]
        lib.KlattErrorText.restype = ctypes.c_char_p
        lib.KlattCreate.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_void_p)]
        lib.KlattMaxFrameSamples.argtypes = [ctypes.c_void_p]
        lib.KlattSynthFrame.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double),
                                        ctypes.POINTER(ctypes.c_short), ctypes.c_int]
        lib.KlattDestroy.argtypes = [ctypes.c_void_p]
        self.frame_params = self._table(KLATT_FRAME)
        self.global_params = self._table(KLATT_GLOBAL)
        g = self._values(self.global_params, globals_)
        self.sr = g[[p.name for p in self.global_params].index("SR")]
        self.handle = ctypes.c_void_p()
        self._check(lib.KlattCreate(g, ctypes.byref(self.handle)))
        self.buf = (ctypes.c_short * lib.KlattMaxFrameSamples(self.handle))()

    def _table(self, kind):
        out = []
        for i in range(self.lib.KlattParamCount(kind)):
            info = ParamInfo()
            self._check(self.lib.KlattParamInfo(kind, i, ctypes.byref(info)))
            out.append(Param(info.name.decode(), info.unit.decode(), info.description.decode(),
                             info.min, info.max, info.default))
        return out

    def _values(self, table, given):
        unknown = set(given) - {p.name for p in table}
        if unknown:
            raise KeyError(f"unknown parameters: {sorted(unknown)}")
        return (ctypes.c_double * len(table))(*[given.get(p.name, p.default) for p in table])

    def _check(self, code):
        if code < 0:
            raise RuntimeError(self.lib.KlattErrorText(code).decode())
        return code

    def frame(self, **params):
        """Synthesizes one frame and returns its samples as a list of ints."""
        n = self._check(self.lib.KlattSynthFrame(self.handle, self._values(self.frame_params, params),
                                                  self.buf, len(self.buf)))
        return self.buf[:n]

    def close(self):
        if self.handle:
            self.lib.KlattDestroy(self.handle)
            self.handle = None


def main():
    dll, out = sys.argv[1], sys.argv[2]
    k = Klatt(dll)
    samples = []
    frames = 100  # 100 x 5 ms = 0.5 s: "ah" gliding to "ee"
    for i in range(frames):
        t = i / (frames - 1)
        samples += k.frame(AV=60, F0=130 - 30 * t,
                           F1=700 + (300 - 700) * t, F2=1220 + (2250 - 1220) * t, F3=2600 + (3000 - 2600) * t)
    k.close()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(k.sr))
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    print(f"wrote {len(samples)} samples to {out}")


if __name__ == "__main__":
    main()
