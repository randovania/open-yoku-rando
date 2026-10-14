"""
Reshapes the game's `.sim` item textures without decoding them.

A `.sim` is a header of nine little-endian u32 (magic, width, height, width and height padded to 4, format 7 = BC3,
0, uncompressed and compressed size) followed by one zlib stream holding the BC3 mip chain. Each BC3 block covers
4x4 texels in 16 bytes, so whole blocks can be moved and transparent ones added without touching a pixel.
"""

import struct
import zlib

HEADER = struct.Struct("<9I")
FORMAT_BC3 = 7
BLOCK_SIZE = 16
EMPTY_BLOCK = bytes(BLOCK_SIZE)
"""Both alpha endpoints 0, so every texel is fully transparent."""


def pad4(value: int) -> int:
    return (value + 3) // 4 * 4


def mip_sizes(padded_width: int, padded_height: int) -> list[tuple[int, int]]:
    """The levels the game reads: halved until a level would drop below 2 blocks in either direction."""
    levels = []
    width, height = padded_width, padded_height
    while True:
        levels.append((width, height))
        if min(width, height) <= 9:
            return levels
        width //= 2
        height //= 2


def blocks(width: int, height: int) -> tuple[int, int]:
    return pad4(width) // 4, pad4(height) // 4


def widen_to_square(data: bytes) -> bytes:
    """
    The texture centred on a transparent square as wide as it is tall; a texture at least that wide is returned as is.

    Every level keeps its rows and gets transparent block columns on both sides. The square chain can have more levels
    than the original one; those extra, smallest levels are left transparent.
    """
    magic, width, height, padded_width, padded_height, fmt, zero, size, _ = HEADER.unpack_from(data)
    if fmt != FORMAT_BC3:
        raise ValueError(f"only BC3 textures can be widened, this one has format {fmt}")
    if width >= height:
        return data
    raw = zlib.decompress(data[HEADER.size:])
    if len(raw) != size:
        raise ValueError(f"the texture holds {len(raw)} bytes, its header says {size}")

    side = padded_height
    old_levels = mip_sizes(padded_width, padded_height)
    out = bytearray()
    position = 0
    for index, (level_width, level_height) in enumerate(mip_sizes(side, padded_height)):
        new_columns, rows = blocks(level_width, level_height)
        if index >= len(old_levels):
            out += EMPTY_BLOCK * (new_columns * rows)
            continue
        old_columns, old_rows = blocks(*old_levels[index])
        assert old_rows == rows, "the height and so every level's rows stay the same"
        left = (new_columns - old_columns) // 2
        right = new_columns - old_columns - left
        row_size = old_columns * BLOCK_SIZE
        for _ in range(rows):
            out += EMPTY_BLOCK * left + raw[position:position + row_size] + EMPTY_BLOCK * right
            position += row_size
    if position != len(raw):
        raise ValueError(f"the texture's mip chain is {len(raw)} bytes, its size says {position}")

    compressed = zlib.compress(bytes(out), 9)
    return HEADER.pack(magic, side, height, side, padded_height, fmt, zero, len(out), len(compressed)) + compressed
