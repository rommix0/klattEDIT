"""Writes samples/ba.csv: a /ba/-like syllable for trying the editor. Run from the repository root."""


def lerp(pts, t):
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if t <= t1:
            return v0 + (v1 - v0) * (t - t0) / (t1 - t0) if t1 > t0 else v1
    return pts[-1][1]


TRACKS = {
    "AV": [(0, 0), (95, 0), (112, 60), (600, 60), (700, 0), (800, 0)],
    "AF": [(0, 0), (86, 0), (92, 55), (102, 55), (108, 0), (800, 0)],
    "AH": [(0, 0), (640, 0), (680, 45), (720, 0), (800, 0)],
    "F0": [(0, 110), (250, 125), (500, 110), (700, 95), (800, 95)],
    "F1": [(0, 450), (100, 250), (160, 700), (600, 700), (700, 600), (800, 450)],
    "F2": [(0, 1450), (100, 1100), (180, 1250), (600, 1250), (700, 1350), (800, 1450)],
    "F3": [(0, 2450), (100, 2200), (180, 2600), (600, 2600), (800, 2450)],
    "A5": [(0, 50), (800, 50)],
    "A6": [(0, 50), (800, 50)],
}

with open("samples/ba.csv", "w", newline="") as f:
    f.write("time_ms," + ",".join(TRACKS) + "\r\n")
    for k in range(160):
        t = k * 5
        f.write(f"{t}," + ",".join(str(round(lerp(p, t))) for p in TRACKS.values()) + "\r\n")
