# ACE Font Bitmap Format (.fnt)

> Binary specification for the bitmap font format used by the ACE (Amiga C Engine) library

---

## Overview

ACE fonts are 1-bitplane (1bpp) bitmap fonts stored in a compact binary format. All glyphs are packed horizontally into a single continuous bitmap strip. Glyph widths are variable and derived implicitly from an offset table — no explicit width array is stored.

All multi-byte values are **big-endian** (Motorola byte order).

---

## File Layout

```
┌──────────────────────────────────────────────┐
│  Header (5 bytes)                            │
├──────────────────────────────────────────────┤
│  Character Offset Table (ubCharCount × 2)    │
├──────────────────────────────────────────────┤
│  Glyph Bitmap Data (planar 1bpp)             │
└──────────────────────────────────────────────┘
```

---

## Header

5 bytes, no magic number or version field.

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0 | 2 | UWORD | uwWidth | Total bitmap width in pixels (multiple of 16) |
| 2 | 2 | UWORD | uwHeight | Bitmap height in pixels (= font line height) |
| 4 | 1 | UBYTE | ubCharCount | Number of entries in the offset table |

- `uwWidth` is always rounded up to a 16-pixel boundary.
- `uwHeight` is uniform for all glyphs — the font is monospaced in height.

---

## Character Offset Table

`ubCharCount` entries, each a big-endian UWORD (2 bytes).

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 5 | 2 | UWORD | Offset[0] — X pixel offset of character code 0 |
| 7 | 2 | UWORD | Offset[1] — X pixel offset of character code 1 |
| … | … | … | … |
| 5 + (N−1)×2 | 2 | UWORD | Offset[N−1] — sentinel offset (= total packed width) |

**Total size:** `ubCharCount × 2` bytes.

### Interpretation

- Characters are indexed by their raw byte value (e.g. `'A'` = 65).
- `ubCharCount` = highest character code + 2 (the +2 accounts for the zero-based index and a **sentinel entry** at the end).
- **Glyph width** of character `c` = `Offset[c + 1] − Offset[c]`.
- The last entry is a sentinel that stores the total packed pixel width, enabling the width calculation for the final glyph.
- Missing glyphs (gaps in the character set) have `Offset[c + 1] == Offset[c]`, yielding width 0.

### Example

Font containing ` ` (32), `A` (65), `B` (66), `C` (67) with widths 4, 6, 6, 5:

| Index | Character | Offset | Width |
|-------|-----------|--------|-------|
| 0–31 | (missing) | 0 | 0 |
| 32 | ` ` (space) | 0 | 4 |
| 33–64 | (missing) | 4 | 0 |
| 65 | `A` | 4 | 6 |
| 66 | `B` | 10 | 6 |
| 67 | `C` | 16 | 5 |
| 68 | (sentinel) | 21 | — |

`ubCharCount` = 69 (68 + 1 sentinel). Offset table = 138 bytes.

---

## Glyph Bitmap Data

A single 1-bitplane bitmap containing all glyphs packed side-by-side horizontally.

### Layout

```
← uwWidth pixels (rounded to 16) →
┌───┬───┬─────────┬───┬───┬───┬────┐  ─┐
│   │   │ (empty) │ A │ B │ C │pad │   │
│spc│   │  gap    │   │   │   │    │   │ uwHeight
│   │   │         │   │   │   │    │   │ pixels
└───┴───┴─────────┴───┴───┴───┴────┘  ─┘
 4px      31×0px    6   6   5   pad
```

- Glyphs occupy columns `Offset[c]` through `Offset[c+1] − 1`.
- Missing characters occupy zero columns (consecutive offsets are equal).
- The bitmap width is padded to the next 16-pixel boundary; padding pixels are zero.

### Pixel Encoding

- **1 bit per pixel**: 1 = foreground (glyph), 0 = background (transparent).
- Data is stored as big-endian 16-bit words, each representing 16 consecutive horizontal pixels.
- Bit 15 (MSB) = leftmost pixel, Bit 0 (LSB) = rightmost pixel in each word.

### Scanline Order

Top-to-bottom, left-to-right:

```
For y = 0 to uwHeight − 1:
    For x = 0 to (uwWidth / 16) − 1:
        UWORD — 16 pixels of scanline y
```

### Data Size

```
Bitmap bytes = (uwWidth / 16) × 2 × uwHeight
             = uwWidth/8 × uwHeight
```

---

## Total File Size

```
Total = 5 + (ubCharCount × 2) + (uwWidth / 8 × uwHeight)
```

---

## Runtime Structure

At load time, the font is deserialized into this C structure:

```c
typedef struct _tFont {
    UWORD    uwWidth;       // Packed bitmap width (pixels)
    UWORD    uwHeight;      // Packed bitmap height (pixels, = font height)
    UBYTE    ubChars;       // Entry count in offset table
    UWORD   *pCharOffsets;  // Offset table (FAST RAM)
    tBitMap *pRawData;      // 1-bitplane bitmap (CHIP RAM via bitmapCreate)
} tFont;
```

Glyph width lookup is O(1):

```c
UBYTE fontGlyphWidth(const tFont *pFont, char c) {
    UBYTE ubIdx = (UBYTE)c;
    return pFont->pCharOffsets[ubIdx + 1] - pFont->pCharOffsets[ubIdx];
}
```

---

## Rendering

Drawing is done by blitting individual glyphs from the packed source bitmap to a 1bpp destination buffer:

```c
blitCopy(
    pFont->pRawData,                 // source: packed glyph strip
    pFont->pCharOffsets[(UBYTE)c],   // srcX: glyph's X offset
    0,                               // srcY: always 0
    pDestBitMap,                     // destination bitmap
    uwX, uwY,                       // dest position
    glyphWidth, pFont->uwHeight,    // blit dimensions
    MINTERM_COOKIE                   // blit operation
);
```

Letter spacing is 1 pixel (hardcoded in `fontDrawStr1bpp`). Line breaks (`\n`) reset X to the start position and advance Y by `uwHeight`.

---

## Conversion Pipeline

### Tool

`font_conv` (in `modules/ACE/tools/src/font_conv.cpp`)

### Supported Source Formats

| Format | Extension | Method |
|--------|-----------|--------|
| TrueType | .ttf | FreeType rasterization at configurable pixel size |
| PNG (ProMotion NG) | .png | Delimiter-based glyph separation |
| Directory of PNGs | (dir) | One PNG per glyph, filename = character code |
| ACE font | .fnt | Re-read and re-write |

### Command Line

```
font_conv <input> fnt -out <output.fnt> [-size N] [-chars "CHARSET"] [-charfile chars.txt] [-fc N]
```

| Flag | Default | Description |
|------|---------|-------------|
| `-size` | 20 | Pixel size for TTF rasterization |
| `-chars` | A–Z, a–z, 0–9, space | Character set to include |
| `-charfile` | — | Load charset from UTF-8 text file |
| `-fc` | 33 | First character index (PMNG format) |
| `-out` | input name + .fnt | Output path |

### CMake Integration

```cmake
convertFont(
    TARGET    myTarget
    SOURCE    assets/myfont.ttf
    DESTINATION data/myfont.fnt
    SIZE      8
)
```

---

## Constraints & Notes

- Maximum glyph count: 255 (UBYTE limit on `ubCharCount`, minus 1 for sentinel = 254 usable character codes, indices 0–253).
- No kerning support — fixed 1px inter-character spacing.
- No multi-bitplane support — fonts are always 1bpp. Color is applied at blit time by the engine (cookie-cut or shadow modes).
- Bitmap must reside in **Chip RAM** (required by the Amiga blitter hardware).
- Offset table resides in **Fast RAM**.
