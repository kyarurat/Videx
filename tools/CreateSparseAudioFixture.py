"""Create valid RF64 PCM silence for optional large-file playback checks.

Usage: python tools/CreateSparseAudioFixture.py artifacts/large-silence.wav 20
Only an unused destination is accepted. Holes represent actual silent samples,
not padding appended to a short recording. Do not infer compressed-video or
cold-disk performance from this fixture.
"""
import ctypes
import os
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1]).resolve()
data_bytes = int(sys.argv[2]) * 1024**3
if data_bytes <= 0:
    raise ValueError("Size must be a positive number of GiB")
with path.open("xb") as file:
    if os.name == "nt":
        import msvcrt

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        control = kernel.DeviceIoControl
        control.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p,
                            ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong,
                            ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
        control.restype = ctypes.c_int
        returned = ctypes.c_ulong()
        if not control(msvcrt.get_osfhandle(file.fileno()), 0x900C4, None, 0,
                       None, 0, ctypes.byref(returned), None):
            raise ctypes.WinError(ctypes.get_last_error())
    file.write(b"RF64" + struct.pack("<I", 0xFFFFFFFF) + b"WAVE")
    file.write(b"ds64" + struct.pack("<IQQQI", 28, data_bytes + 72,
                                    data_bytes, data_bytes // 2, 0))
    file.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16))
    file.write(b"data" + struct.pack("<I", 0xFFFFFFFF))
    file.flush()
    if os.name == "nt":
        # CRT truncate can physically write zeros throughout a sparse file.
        # SetEndOfFile extends the zero-filled sparse range without doing so.
        seek = kernel.SetFilePointerEx
        seek.argtypes = [ctypes.c_void_p, ctypes.c_longlong, ctypes.c_void_p, ctypes.c_ulong]
        end = kernel.SetEndOfFile
        end.argtypes = [ctypes.c_void_p]
        handle = msvcrt.get_osfhandle(file.fileno())
        if not seek(handle, 80 + data_bytes, None, 0) or not end(handle):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        file.truncate(80 + data_bytes)
print(f"{path}: {path.stat().st_size} bytes; PCM silence {data_bytes / 96000:.2f}s")
