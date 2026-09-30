"""Create a valid sparse MOV of black RGB24 video from a one-frame seed.

Seed: ffmpeg -f lavfi -i color=black:s=1920x1080:r=30 -frames:v 1
            -c:v rawvideo -pix_fmt rgb24 seed.mov
Usage: python tools/CreateSparseVideoFixture.py seed.mov output.mov 20
The 20 GiB payload contains indexed raw frames, not trailing padding. This
checks large offsets and container loading, not compressed-codec performance.
"""
import ctypes
import os
from pathlib import Path
import struct
import sys


def atoms(data):
    offset = 0
    while offset < len(data):
        size, kind = struct.unpack_from(">I4s", data, offset)
        header = 8
        if size == 1:
            size = struct.unpack_from(">Q", data, offset + 8)[0]
            header = 16
        if size == 0 or size < header or offset + size > len(data):
            raise ValueError("Invalid seed atom")
        yield kind, data[offset + header:offset + size]
        offset += size


def pack(kind, body):
    return struct.pack(">I4s", 8 + len(body), kind) + body


seed = Path(sys.argv[1]).read_bytes()
top = dict(atoms(seed))
properties = {}


def inspect(data):
    for kind, body in atoms(data):
        if kind in (b"trak", b"mdia", b"minf", b"stbl"):
            inspect(body)
        elif kind == b"mdhd":
            if body[0] != 0:
                raise ValueError("Expected version-zero seed")
            properties["timescale"] = struct.unpack_from(">I", body, 12)[0]
        elif kind == b"stts":
            entries, count, delta = struct.unpack_from(">III", body, 4)
            if entries != 1 or count != 1:
                raise ValueError("Seed must contain exactly one frame")
            properties["delta"] = delta
        elif kind == b"stsz":
            size, count = struct.unpack_from(">II", body, 4)
            if count != 1:
                raise ValueError("Seed must contain one sample")
            properties["sample_size"] = size or struct.unpack_from(">I", body, 12)[0]
        elif kind == b"stsd" and body[12:16] != b"raw ":
            raise ValueError("Only RGB24 raw-video MOV seeds are supported")


inspect(top[b"moov"])
frames = int(sys.argv[3]) * 1024**3 // properties["sample_size"]
payload_bytes = frames * properties["sample_size"]
track_duration = frames * properties["delta"]
if not 0 < track_duration < 2**32:
    raise ValueError("Requested duration does not fit version-zero MOV")


def rewrite(data, chunk_offset=0):
    result = []
    for kind, body in atoms(data):
        if kind == b"edts":
            continue
        if kind in (b"trak", b"mdia", b"minf", b"stbl"):
            body = rewrite(body, chunk_offset)
        elif kind in (b"mvhd", b"tkhd", b"mdhd"):
            body = bytearray(body)
            duration = track_duration
            at = 16
            if kind == b"mvhd":
                scale = struct.unpack_from(">I", body, 12)[0]
                duration = round(track_duration * scale / properties["timescale"])
            elif kind == b"tkhd":
                scale = struct.unpack_from(">I", dict(atoms(top[b"moov"]))[b"mvhd"], 12)[0]
                duration = round(track_duration * scale / properties["timescale"])
                at = 20
            struct.pack_into(">I", body, at, duration)
        elif kind == b"stts":
            body = body[:4] + struct.pack(">III", 1, frames, properties["delta"])
        elif kind == b"stsc":
            body = body[:4] + struct.pack(">IIII", 1, 1, frames, 1)
        elif kind == b"stsz":
            body = body[:4] + struct.pack(">II", properties["sample_size"], frames)
        elif kind in (b"stco", b"co64"):
            kind = b"co64"
            body = body[:4] + struct.pack(">IQ", 1, chunk_offset)
        result.append(pack(kind, body))
    return b"".join(result)


ftyp = pack(b"ftyp", top[b"ftyp"])
moov_size = len(pack(b"moov", rewrite(top[b"moov"])))
header = ftyp + pack(b"moov", rewrite(top[b"moov"], len(ftyp) + moov_size + 16))
header += struct.pack(">I4sQ", 1, b"mdat", 16 + payload_bytes)
path = Path(sys.argv[2]).resolve()
with path.open("xb") as file:
    if os.name == "nt":
        import msvcrt

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = msvcrt.get_osfhandle(file.fileno())
        control = kernel.DeviceIoControl
        control.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong,
                            ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
        returned = ctypes.c_ulong()
        if not control(handle, 0x900C4, None, 0, None, 0, ctypes.byref(returned), None):
            raise ctypes.WinError(ctypes.get_last_error())
    file.write(header)
    file.flush()
    length = len(header) + payload_bytes
    if os.name == "nt":
        seek = kernel.SetFilePointerEx
        seek.argtypes = [ctypes.c_void_p, ctypes.c_longlong, ctypes.c_void_p, ctypes.c_ulong]
        end = kernel.SetEndOfFile
        end.argtypes = [ctypes.c_void_p]
        if not seek(handle, length, None, 0) or not end(handle):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        file.truncate(length)
print(f"{path}: {path.stat().st_size} bytes; {frames} raw RGB24 frames")
