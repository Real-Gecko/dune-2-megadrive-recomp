// Screens: the screen table at 0xC7C72, text on the planes, window setup.
#include "game.hpp"

namespace game {

// 0xC7EDC: window position (VDP register 0x11 and 0x12 values).
void SetWindowPos(uint16_t h, uint16_t v) {
    display::cfg.win_h = uint8_t(h);
    display::cfg.win_v = uint8_t(v);
}

// 0xC7EB2, 0xC7ECA
void SetWindowBase(uint16_t a) {
    window_base = a;
    display::cfg.window = uint16_t((a >> 10 << 10) & 0xF800);
}
void ResetWindowBase() {
    display::cfg.window = 0x6000;
    window_base = 0x6000;
}

// 0xC7D68: shows screen n of the table at 0xC7C72 (16 bytes each): tile
// list for VRAM 0; maps (top byte: 0 window, 2 plane A, 4 plane B) as a
// list of (word length, bit 15 compressed; long source) at the plane's
// address, then the window position; palette (-1 none); font tiles for
// VRAM 0x8000 (length-prefixed). Returns the palette (0xFFFFE334 for none).
uint32_t LoadScreen(uint16_t n) {
    BuildSpriteTable(0);
    uint32_t e = 0xC7C72 + n * 16u;
    if (uint32_t tiles = rd32(e)) {
        LoadTileList(tiles, 0);
        FlushDma();
    }
    uint8_t kind = rd8(e + 4);
    if (uint32_t maps = rd32(e + 4)) {
        uint16_t at = rd16(0xFF0000u | rd16(0xC7E34 + kind));
        maps &= 0xFFFFFF;
        for (;;) {
            uint16_t len = rd16(maps);
            maps += 2;
            if (!len) break;
            uint32_t src = at; // (an uncompressed entry would read from its own VRAM address)
            if (len & 0x8000) {
                Decompress(rd32(maps), 0xFF0000);
                maps += 4;
                src = 0xFF0000;
            }
            len &= 0x7FFF;
            QueueDma(2, src, uint32_t(at) << 16 | uint16_t(len >> 1));
            at = uint16_t(at + len);
            FlushDma();
        }
        uint32_t pos = rd32(0xC7E3A + kind * 2u);
        SetWindowPos(uint16_t(pos >> 16), uint16_t(pos));
    }
    uint32_t pal = rd32(e + 8);
    if (pal) {
        if (int32_t(pal) < 0) pal = 0xFFFFE334;
        else {
            UploadPalette(pal);
            FlushDma();
        }
    }
    if (uint32_t font = rd32(e + 12)) {
        QueueDma(2, font + 2, 0x8000u << 16 | uint16_t(rd16(font) >> 1));
        FlushDma();
    }
    return pal;
}

// 0x19A52: the menu font (compressed, length-prefixed at 0x506C8) to VRAM 0x9000.
void LoadFont() {
    uint16_t len = rd16(0x506C8);
    Decompress(0x506CA, 0xFF0000);
    QueueDma(2, 0xFF0000, 0x9000u << 16 | uint16_t(len >> 1));
    FlushDma();
}

// Text: characters from 0x20 as tiles, line feed / carriage return go to
// column 1 of the next row. Rows are 0x80 bytes apart.
static void Print(uint32_t s, uint16_t x, uint16_t y, uint16_t base, uint16_t (*tile)(uint8_t, uint16_t),
                  uint16_t arg) {
    FlushDma();
    for (;;) {
        uint8_t c = rd8(s++);
        if (!c) return;
        if (c < 0x20) {
            if (c == '\r' || c == '\n') {
                y++;
                x = 1;
            }
            continue;
        }
        display::vram_w16(uint16_t(base + y * 0x80 + x * 2), tile(c, arg));
        md::int_mask = 0;
        x++;
    }
}

// 0x1FE62: on the window, palette line 1, tiles 0x400 + character.
void PrintWindow(uint32_t s, uint16_t x, uint16_t y) {
    Print(s, x, y, window_base, [](uint8_t c, uint16_t) { return uint16_t(0x2000 | (c + 0x400)); }, 0);
}

// 0x1FEE4: on the window, palette line 3, tiles 0x480 + character.
void PrintWindowHi(uint32_t s, uint16_t x, uint16_t y) {
    Print(s, x, y, window_base, [](uint8_t c, uint16_t) { return uint16_t(0x6000 | 0x480 | (c & 0x7F)); }, 0);
}

// 0x1FF66: on plane A in a palette line, tiles 0x480 + character.
void PrintPlaneA(uint32_t s, uint16_t x, uint16_t y, uint16_t line) {
    uint16_t attr = uint16_t(line >> 3 | line << 13);
    Print(s, x, y, plane_a_base, [](uint8_t c, uint16_t a) { return uint16_t(a | 0x480 | (c & 0x7F)); }, attr);
}

// 0x1FE18: one colour in the palette buffer and CRAM.
void SetColor(uint16_t line, uint16_t index, uint16_t c) {
    wr16(kPalette + uint16_t(line * 32 + index * 2), c);
    display::cram_w16((line * 16 + index) & 63, c);
    md::int_mask = 0;
}

// 0x20064: the window plane cleared to tile 0x2420.
void ClearWindowPlane() {
    FlushDma();
    WaitVBlank();
    uint16_t at = window_base;
    for (int i = 0; i < 0x800; i++) display::vram_w16(uint16_t(at + i * 2), 0x2420);
}

// 0x1FFEC, 0x20028: clears rows + 1 rows of a width (cells) on the window
// or plane A.
static void ClearRows(uint16_t base, uint16_t rows, uint16_t width) {
    uint16_t at = base;
    for (int r = 0; r <= int16_t(rows); r++) {
        QueueDma(0x301, 0, uint32_t(at) << 16 | uint16_t(width * 2));
        at = uint16_t(at + 0x80);
    }
    FlushDma();
}
void ClearWindowRows(uint16_t rows, uint16_t width) { ClearRows(window_base, rows, width); }
void ClearPlaneARows(uint16_t rows, uint16_t width) { ClearRows(plane_a_base, rows, width); }

} // namespace game
