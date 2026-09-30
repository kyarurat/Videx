"""Generate an original, uncompressed Bayer DNG for the decoder regression test.

No camera image or third-party fixture is redistributed. The tiny TIFF/DNG uses
an identity color matrix and a constant synthetic sensor pattern.
"""
from pathlib import Path
import struct

width, height = 64, 48
entries = []


def tag(number, kind, values):
    if kind == 2:
        data = values.encode("ascii") + b"\0"
        count = len(data)
    elif kind == 1:
        data = bytes(values)
        count = len(values)
    elif kind in (3, 4):
        data = struct.pack("<" + ("H" if kind == 3 else "I") * len(values), *values)
        count = len(values)
    else:
        data = b"".join(struct.pack("<ii" if kind == 10 else "<II", *v) for v in values)
        count = len(values)
    entries.append((number, kind, count, data))


tag(256, 4, [width])
tag(257, 4, [height])
tag(258, 3, [16])
tag(259, 3, [1])
tag(262, 3, [32803])
tag(271, 2, "Videx")
tag(272, 2, "Synthetic Bayer")
tag(273, 4, [0])  # patched once the IFD size is known
tag(274, 3, [1])
tag(277, 3, [1])
tag(278, 4, [height])
tag(279, 4, [width * height * 2])
tag(284, 3, [1])
tag(33421, 3, [2, 2])
tag(33422, 1, [0, 1, 1, 2])
tag(50706, 1, [1, 4, 0, 0])
tag(50707, 1, [1, 1, 0, 0])
tag(50708, 2, "Videx Synthetic Bayer")
tag(50710, 1, [0, 1, 2])
tag(50711, 3, [1])
tag(50714, 4, [0])
tag(50717, 4, [4095])
tag(50721, 10, [(1 if i % 4 == 0 else 0, 1) for i in range(9)])
tag(50728, 5, [(1, 1)] * 3)
tag(50778, 3, [21])
entries.sort()
offset = 8 + 2 + len(entries) * 12 + 4
extra = bytearray()
records = bytearray()
strip_offset = offset + sum((len(e[3]) + 1) // 2 * 2 for e in entries if len(e[3]) > 4)
for number, kind, count, data in entries:
    if number == 273:
        data = struct.pack("<I", strip_offset)
    if len(data) > 4:
        field = struct.pack("<I", offset + len(extra))
        extra.extend(data)
        if len(extra) % 2:
            extra.append(0)
    else:
        field = data.ljust(4, b"\0")
    records.extend(struct.pack("<HHI", number, kind, count) + field)
pixels = b"".join(struct.pack("<H", 800 + (x + y) * 16) for y in range(height) for x in range(width))
output = Path(__file__).resolve().parents[1] / "tests/fixtures/synthetic.dng"
output.parent.mkdir(exist_ok=True)
output.write_bytes(b"II\x2a\x00" + struct.pack("<I", 8) + struct.pack("<H", len(entries)) + records + b"\0" * 4 + extra + pixels)
print(output)
