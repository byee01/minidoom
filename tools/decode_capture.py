"""Rebuild PNGs from [capture] hex rows printed by lens_src/DebugCapture.js.
usage: python3 tools/decode_capture.py <logfile> <outdir>"""
import re
import subprocess
import sys
from collections import defaultdict

log, outdir = sys.argv[1], sys.argv[2]
frames = defaultdict(dict)
sizes = {}
for line in open(log, encoding="latin-1"):
    m = re.search(r"\[capture\] begin (\S+) (\d+)x(\d+)", line)
    if m:
        sizes[m.group(1)] = (int(m.group(2)), int(m.group(3)))
        continue
    m = re.search(r"\[capture\] (\S+) (\d+) ([0-9a-f]+)\s*$", line)
    if m:
        frames[m.group(1)][int(m.group(2))] = bytes.fromhex(m.group(3))
for tag, rows in frames.items():
    w, h = sizes.get(tag, (len(next(iter(rows.values()))) // 3, max(rows) + 1))
    data = b"".join(rows.get(y, b"\0" * (w * 3)) for y in range(h))
    ppm = f"{outdir}/capture_{tag}.ppm"
    with open(ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + data)
    png = ppm[:-4] + ".png"
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", ppm, "-vf", "scale=iw*3:ih*3:flags=neighbor", png], check=True)
    print(f"{png}: {w}x{h}, {len(rows)} rows")
