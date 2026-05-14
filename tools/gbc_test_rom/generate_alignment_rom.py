#!/usr/bin/env python3
"""Generate an RGBDS assembly source for a deterministic GBC alignment ROM."""

from __future__ import annotations


SCREEN_TILES_W = 20
SCREEN_TILES_H = 18
MAP_STRIDE = 32

TILE_BLACK = 0
TILE_SOLID = 1
TILE_VSTRIPE = 2
TILE_HSTRIPE = 3
TILE_CHECKER = 4
TILE_DIAG = 5
TILE_CROSS_V = 6
TILE_CROSS_H = 7

PAL_GRAY = 0
PAL_RED = 1
PAL_GREEN = 2
PAL_BLUE = 3
PAL_YELLOW = 4
PAL_CYAN = 5
PAL_MAGENTA = 6
PAL_WHITE = 7


def rgb555(r: int, g: int, b: int) -> int:
    return (r & 31) | ((g & 31) << 5) | ((b & 31) << 10)


def word_bytes(value: int) -> tuple[int, int]:
    return value & 0xFF, (value >> 8) & 0xFF


def tile_rows(kind: int) -> list[tuple[int, int]]:
    rows: list[tuple[int, int]] = []
    for y in range(8):
        lo = 0
        hi = 0
        for x in range(8):
            color = 0
            if kind == TILE_SOLID:
                color = 3
            elif kind == TILE_VSTRIPE:
                color = 3 if (x & 1) == 0 else 0
            elif kind == TILE_HSTRIPE:
                color = 3 if (y & 1) == 0 else 0
            elif kind == TILE_CHECKER:
                color = 3 if ((x ^ y) & 1) == 0 else 0
            elif kind == TILE_DIAG:
                color = 3 if x == y else 0
            elif kind == TILE_CROSS_V:
                color = 3 if x in (3, 4) else 0
            elif kind == TILE_CROSS_H:
                color = 3 if y in (3, 4) else 0
            bit = 7 - x
            lo |= (color & 1) << bit
            hi |= ((color >> 1) & 1) << bit
        rows.append((lo, hi))
    return rows


def screen_tile_and_attr(x: int, y: int) -> tuple[int, int]:
    # Unique 2x2 corner identity blocks.
    if x < 2 and y < 2:
        return TILE_SOLID, PAL_RED
    if x >= SCREEN_TILES_W - 2 and y < 2:
        return TILE_SOLID, PAL_GREEN
    if x < 2 and y >= SCREEN_TILES_H - 2:
        return TILE_SOLID, PAL_BLUE
    if x >= SCREEN_TILES_W - 2 and y >= SCREEN_TILES_H - 2:
        return TILE_SOLID, PAL_WHITE

    # Full-screen border.
    if x == 0 or x == SCREEN_TILES_W - 1 or y == 0 or y == SCREEN_TILES_H - 1:
        return TILE_SOLID, PAL_WHITE

    # Top color-bar row, after border/corners.
    if y == 1:
        bars = [PAL_RED, PAL_GREEN, PAL_BLUE, PAL_WHITE, PAL_YELLOW, PAL_CYAN, PAL_MAGENTA, PAL_GRAY]
        return TILE_SOLID, bars[((x - 2) * len(bars)) // (SCREEN_TILES_W - 4)]

    # Center crosshair.
    if x == SCREEN_TILES_W // 2:
        return TILE_CROSS_V, PAL_WHITE
    if y == SCREEN_TILES_H // 2:
        return TILE_CROSS_H, PAL_WHITE

    # Diagonal reference, useful for row slip/skew.
    if 2 <= y <= 16 and x == y:
        return TILE_DIAG, PAL_YELLOW

    # Pattern fields.
    if y >= 12:
        return TILE_CHECKER, PAL_WHITE
    if x < SCREEN_TILES_W // 2:
        return TILE_VSTRIPE, PAL_WHITE
    return TILE_HSTRIPE, PAL_WHITE


def emit_db(values: list[int], indent: str = "    ") -> None:
    for i in range(0, len(values), 16):
        chunk = values[i : i + 16]
        print(f"{indent}db " + ", ".join(f"${v:02x}" for v in chunk))


def main() -> None:
    tile_bytes: list[int] = []
    for tile in range(8):
        for lo, hi in tile_rows(tile):
            tile_bytes.extend([lo, hi])

    tile_map = [TILE_BLACK] * (MAP_STRIDE * 32)
    attr_map = [PAL_GRAY] * (MAP_STRIDE * 32)
    for y in range(SCREEN_TILES_H):
        for x in range(SCREEN_TILES_W):
            tile, attr = screen_tile_and_attr(x, y)
            tile_map[y * MAP_STRIDE + x] = tile
            attr_map[y * MAP_STRIDE + x] = attr

    palette_words = [
        [rgb555(0, 0, 0), rgb555(10, 10, 10), rgb555(20, 20, 20), rgb555(31, 31, 31)],
        [rgb555(0, 0, 0), rgb555(12, 0, 0), rgb555(22, 0, 0), rgb555(31, 0, 0)],
        [rgb555(0, 0, 0), rgb555(0, 12, 0), rgb555(0, 22, 0), rgb555(0, 31, 0)],
        [rgb555(0, 0, 0), rgb555(0, 0, 12), rgb555(0, 0, 22), rgb555(0, 0, 31)],
        [rgb555(0, 0, 0), rgb555(12, 12, 0), rgb555(22, 22, 0), rgb555(31, 31, 0)],
        [rgb555(0, 0, 0), rgb555(0, 12, 12), rgb555(0, 22, 22), rgb555(0, 31, 31)],
        [rgb555(0, 0, 0), rgb555(12, 0, 12), rgb555(22, 0, 22), rgb555(31, 0, 31)],
        [rgb555(0, 0, 0), rgb555(10, 10, 10), rgb555(20, 20, 20), rgb555(31, 31, 31)],
    ]
    palette_bytes: list[int] = []
    for palette in palette_words:
        for color in palette:
            palette_bytes.extend(word_bytes(color))

    print(
        """DEF rLCDC EQU $ff40
DEF rSCY  EQU $ff42
DEF rSCX  EQU $ff43
DEF rLY   EQU $ff44
DEF rBGP  EQU $ff47
DEF rVBK  EQU $ff4f
DEF rBCPS EQU $ff68
DEF rBCPD EQU $ff69

SECTION "Header", ROM0[$0100]
    jp Start
    ds $0150 - @, 0

SECTION "Main", ROM0[$0150]
Start:
    di
    ld sp, $dfff

.wait_vblank:
    ldh a, [rLY]
    cp 144
    jr c, .wait_vblank

    xor a
    ldh [rLCDC], a
    ldh [rSCX], a
    ldh [rSCY], a
    ld a, %11100100
    ldh [rBGP], a

    ld a, $80
    ldh [rBCPS], a
    ld hl, PaletteData
    ld bc, PaletteDataEnd - PaletteData
    ld de, $ff69
    call CopyToIoPort

    xor a
    ldh [rVBK], a
    ld hl, TileData
    ld de, $8000
    ld bc, TileDataEnd - TileData
    call MemCopy

    ld hl, TileMap
    ld de, $9800
    ld bc, TileMapEnd - TileMap
    call MemCopy

    ld a, 1
    ldh [rVBK], a
    ld hl, AttrMap
    ld de, $9800
    ld bc, AttrMapEnd - AttrMap
    call MemCopy

    xor a
    ldh [rVBK], a
    ld a, %10010001
    ldh [rLCDC], a

.forever:
    halt
    jr .forever

MemCopy:
    ld a, b
    or c
    ret z
    ld a, [hli]
    ld [de], a
    inc de
    dec bc
    jr MemCopy

CopyToIoPort:
    ld a, b
    or c
    ret z
    ld a, [hli]
    ld [de], a
    dec bc
    jr CopyToIoPort
"""
    )

    print("TileData:")
    emit_db(tile_bytes)
    print("TileDataEnd:")
    print("TileMap:")
    emit_db(tile_map)
    print("TileMapEnd:")
    print("AttrMap:")
    emit_db(attr_map)
    print("AttrMapEnd:")
    print("PaletteData:")
    emit_db(palette_bytes)
    print("PaletteDataEnd:")


if __name__ == "__main__":
    main()
