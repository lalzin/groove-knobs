#!/usr/bin/env python3
"""A Windows .ico from a PNG (16, 32, 48, 256 px, PNG-compressed entries, Vista+):
    python3 scripts/make_ico.py <in.png> <out.ico>
Uses sips (macOS) to resize."""
import struct, subprocess, sys, tempfile, os

src, out = sys.argv[1], sys.argv[2]
sizes = [16, 32, 48, 256]
blobs = []
with tempfile.TemporaryDirectory() as tmp:
    for s in sizes:
        p = os.path.join(tmp, f"{s}.png")
        subprocess.run(["sips", "-z", str(s), str(s), src, "--out", p], check=True, capture_output=True)
        blobs.append(open(p, "rb").read())
header = struct.pack("<HHH", 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
entries = b""
for s, b in zip(sizes, blobs):
    entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(b), offset)
    offset += len(b)
open(out, "wb").write(header + entries + b"".join(blobs))
print(out, offset, "bytes")
