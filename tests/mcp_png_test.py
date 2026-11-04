"""Decode MCP PNG output independently with Python's zlib and verify pixels."""
import binascii
import struct
import subprocess
import sys
import zlib


def decode_png(png):
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed, dimensions = 8, bytearray(), None
    while offset < len(png):
        length, = struct.unpack_from(">I", png, offset)
        kind = png[offset + 4:offset + 8]
        data = png[offset + 8:offset + 8 + length]
        crc, = struct.unpack_from(">I", png, offset + 8 + length)
        assert binascii.crc32(kind + data) == crc, (kind, "CRC mismatch")
        if kind == b"IHDR":
            width, height, bits, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data)
            assert (bits, color, compression, filtering, interlace) == (8, 2, 0, 0, 0)
            dimensions = width, height
        elif kind == b"IDAT":
            compressed.extend(data)
        elif kind == b"IEND":
            assert offset + 12 == len(png)
            break
        offset += length + 12
    else:
        raise AssertionError("PNG has no IEND")
    assert dimensions is not None
    raw = zlib.decompress(compressed)
    width, height = dimensions
    assert len(raw) == (width * 3 + 1) * height
    assert all(raw[y * (width * 3 + 1)] == 0 for y in range(height)), "Expected unfiltered RGB rows"
    return dimensions, raw


def main():
    png = subprocess.check_output([sys.argv[1], "--png"], timeout=10)
    (width, height), raw = decode_png(png)
    assert (width, height) == (128, 200)
    expected = bytearray()
    for y in range(height):
        expected.append(0)
        for x in range(width):
            expected.extend((x, y, (x + y) & 255))
    assert raw == expected, "PNG pixel colors or row orientation differ"
    print("MCP PNG: independent CRC, zlib, channel and row checks passed")


if __name__ == "__main__":
    main()
