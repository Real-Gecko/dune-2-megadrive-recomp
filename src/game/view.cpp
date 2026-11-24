// The scrolling map view (hand-written asm around 0x5900-0x6300): two
// overlapping view records, 0xFFE3BE (plane B) and 0xFFE3EC (plane A):
// +0 x and +2 y in pixels, +0xC, +0x10/+0x12 y limits, +0x14/+0x16 x
// limits, +0x18 the map (high byte a flag), +0xE4, +0xE6 the tile source.
// 0xFFE3B6/0xFFE3B8 the horizontal scroll words, 0xFFE3BA/0xFFE3BC the
// vertical ones.
#include "game.hpp"

namespace game {

namespace {

// 0x59DE: a view record over the view cell, kept inside rect (x, y, w, h
// in cells).
void InitView(uint32_t v, uint32_t rect) {
    uint16_t cell = U16{0xFFC238};
    wr16(v, uint16_t(((cell << 5) & 0x7E0) + 7));
    wr16(v + 2, uint16_t(((cell >> 1) & 0x7E0) + 7));
    wr16(v + 0xE4, 0);
    wr32(v + 0xE6, 0x4ADF0);
    wr32(v + 0xC, 0x800080);
    uint32_t d = rd32(rect) << 5;
    U32{0xFFBF54} = d;
    wr16(v + 0x10, uint16_t(d));
    wr16(v + 0x12, uint16_t(d));
    wr16(v + 0x14, uint16_t(d >> 16));
    wr16(v + 0x16, uint16_t(d >> 16));
    d = rd32(rect + 4) << 5;
    d = (d & 0xFFFF0000) | uint16_t(d - 0xE0);
    U16{v + 0x12} += uint16_t(d);
    U16{v + 0x16} += uint16_t((d >> 16) - screen_width);
    int16_t y = int16_t(rd16(v + 2));
    if (int16_t(rd16(v + 0x10)) > y) wr16(v + 2, rd16(v + 0x10));
    else if (int16_t(rd16(v + 0x12)) < y) wr16(v + 2, rd16(v + 0x12));
    int16_t x = int16_t(rd16(v));
    if (int16_t(rd16(v + 0x14)) > x) wr16(v, rd16(v + 0x14));
    else if (int16_t(rd16(v + 0x16)) < x) wr16(v, rd16(v + 0x16));
}

// The strip writers 0x746C, 0x7508 (a column: ground on plane B, overlay
// on plane A) and 0x75B4, 0x7652 (a row): VRAM address at, map cell c, the
// offset within the cell's 32-byte tile entry; they wrap within the plane.
uint16_t GroundTile(uint32_t c, uint16_t off) { return rd16(0x4ADF0 + uint16_t(((rd16(c) & 0x1FF) << 5) + off)); }
uint16_t OverlayTile(uint32_t c, uint16_t off) {
    uint16_t o = rd8(c) & 0xFE;
    if (U8{0xFFC198} && o >= 0x67) o = 0;
    return rd16(0x4ADF0 + uint16_t((o << 4) + off));
}

using TileFn = uint16_t (*)(uint32_t, uint16_t);

void Column(TileFn tile, uint16_t off, uint16_t at, uint32_t base, uint32_t c) {
    int16_t n = 0x1F;
    int16_t first = int16_t(int16_t(base + 0xFFE - at) >> 7);
    int16_t k = first;
    for (;;) {
        for (; k >= 0; k--) {
            display::vram_w16(at, tile(c, off));
            at = uint16_t(at + 0x80);
            off += 8;
            if (off >= 0x20) {
                off -= 0x20;
                c += 0x100;
            }
        }
        n = int16_t(n - first - 1);
        if (n < 0) return;
        k = n;
        first = 0x40;
        at = uint16_t(base + (at & 0x7E));
    }
}

void Row(TileFn tile, uint16_t off, uint16_t at, uint32_t c) {
    uint16_t start = at;
    int16_t n = 0x3F;
    int16_t first = int16_t((int16_t(((at + 0x80) & 0xFF80) - at) >> 1) - 1);
    int16_t k = first;
    for (;;) {
        for (; k >= 0; k--) {
            display::vram_w16(at, tile(c, off));
            at = uint16_t(at + 2);
            off += 2;
            if (!(off & 7)) {
                off -= 8;
                c += 4;
            }
        }
        n = int16_t(n - first - 2);
        if (n < 0) return;
        k = n;
        first = 0x78;
        at = start & 0xFF80;
    }
}

// 0x7700: the VRAM address of the view's top-left tile on a plane.
uint32_t Origin(uint32_t base) {
    return base + uint16_t(((U16{0xFFE3BE} >> 2) & 0x7E) + ((U16{0xFFE3C0} << 4) & 0xF80));
}

// The map cell entry and tile offset for a tile column and a pixel row.
uint32_t CellAt(int16_t tx, int16_t py, uint16_t& off) {
    off = uint16_t(((tx & 3) * 2) + (py & 0x18));
    return 0xFF7D9C + uint32_t(int32_t(py >> 5) * 0x100) + uint32_t(int32_t(int16_t(tx & 0xFFFC)));
}

// 0x771C, 0x7774: the columns uncovered on the right or the left, one row
// above the view down.
void ColumnAt(TileFn tile, uint32_t base, int16_t tx, int16_t dcol) {
    uint32_t o = Origin(base);
    uint16_t col = o & 0x7E;
    uint32_t top = base + (uint16_t((o ^ col) - 0x80) & 0xFFF);
    uint16_t at = uint16_t(((col + dcol) & 0x7E) + top);
    uint16_t off;
    uint32_t c = CellAt(tx, int16_t(U16{0xFFE3C0} - 8), off);
    Column(tile, off, at, base, c);
}

// 0x7820, 0x7878: the row uncovered below or above, one tile left of the
// view across.
void RowAt(TileFn tile, uint32_t base, uint16_t drow, int16_t py) {
    uint32_t o = Origin(base);
    uint16_t col = o & 0x7E;
    uint32_t rowbase = base + (uint16_t((o ^ col) + drow) & 0xFFF);
    uint16_t at = uint16_t(((col - 2) & 0x7E) + rowbase);
    uint16_t off;
    uint32_t c = CellAt(int16_t((U16{0xFFE3BE} >> 3) - 1), py, off);
    Row(tile, off, at, c);
}

void ScrollStep(int16_t dx, int16_t dy) {
    wr16(0xFFE4C8, uint16_t(dx));
    wr16(0xFFE4CA, uint16_t(dy));
    U16{0xFFE3EC} += uint16_t(dx);
    U16{0xFFE3BE} = U16{0xFFE3EC};
    U16{0xFFE3EE} += uint16_t(dy);
    U16{0xFFE3C0} = U16{0xFFE3EE};
    uint32_t a = plane_a_base, b = plane_b_base;
    if (dx) {
        if (dx > 0) {
            int16_t tx = int16_t((U16{0xFFE3BE} >> 3) + 0x28);
            ColumnAt(GroundTile, b, tx, 0x50);
            ColumnAt(OverlayTile, a, tx, 0x50);
        } else {
            int16_t tx = int16_t(int16_t(U16{0xFFE3BE} - 8) >> 3);
            ColumnAt(GroundTile, b, tx, -2);
            ColumnAt(GroundTile, b, int16_t(U16{0xFFE3BE} >> 3), 0);
            ColumnAt(OverlayTile, a, tx, -2);
            ColumnAt(OverlayTile, a, int16_t(U16{0xFFE3BE} >> 3), 0);
        }
        uint16_t h = uint16_t(-U16{0xFFE3BE}) & 0x3FF;
        U32{0xFFE3B6} = uint32_t(h) << 16 | h;
    }
    if (dy) {
        if (dy > 0) {
            RowAt(GroundTile, b, 0xE00, int16_t(U16{0xFFE3C0} + 0xE0));
            RowAt(OverlayTile, a, 0xE00, int16_t(U16{0xFFE3C0} + 0xE0));
        } else {
            RowAt(GroundTile, b, 0, int16_t(U16{0xFFE3C0}));
            RowAt(OverlayTile, a, 0, int16_t(U16{0xFFE3C0}));
        }
        uint16_t v = U16{0xFFE3C0} & 0x3FF;
        U32{0xFFE3BA} = uint32_t(v) << 16 | v;
    }
}

} // namespace

// 0x79CE, 0x78D0: the view scrolls by up to 7 pixels a step (the rest in a
// second step), the uncovered columns and rows drawn.
void ScrollView(int16_t dx, int16_t dy) {
    auto clamp = [](int16_t v) { return int16_t(v > 7 ? 7 : v < -7 ? -7 : v); };
    ScrollStep(clamp(dx), clamp(dy));
    auto rest = [](int16_t v) {
        if (v < 0) return int16_t(v + 7 > 0 ? 0 : v + 7);
        return int16_t(v - 7 < 0 ? 0 : v - 7);
    };
    ScrollStep(rest(dx), rest(dy));
}

// 0xFA2: the scroll words into the hscroll table and VSRAM.
void UploadScroll() {
    display::vram_w16(hscroll_base, U16{0xFFE3B6});
    display::vram_w16(uint16_t(hscroll_base + 2), U16{0xFFE3B8});
    display::vsram_w16(0, U16{0xFFE3BA});
    display::vsram_w16(1, U16{0xFFE3BC});
}

// 0x5A7E: both views over rect, scrolled there, the planes cleared.
void SetMapBounds(uint32_t rect) {
    wr8(0xFFE3C8, 0);
    wr8(0xFFE3F6, 0);
    U16{0xFFE3C2} = plane_a_base;
    U16{0xFFE3C4} = 0xE3B6;
    U16{0xFFE3F2} = 0xE3B8;
    U16{0xFFE3C6} = 0xE3BA;
    U16{0xFFE3F4} = 0xE3BC;
    U16{0xFFE3F0} = plane_b_base;
    InitView(0xFFE3BE, rect);
    wr32(0xFFE3BE + 0x18, 0xFF7D9C);
    wr8(0xFFE3BE + 0x18, 0xFF);
    InitView(0xFFE3EC, rect);
    wr32(0xFFE3EC + 0x18, 0xFF7D9C);
    wr8(0xFFE3EC + 0x18, 0);
    NarrowViewLimits();
    FlushDma();
    U16{0xFFE3BA} = U16{0xFFE3EE};
    U16{0xFFE3BC} = U16{0xFFE3EE};
    U16{0xFFE3B6} = uint16_t(0x400 - U16{0xFFE3EC});
    U16{0xFFE3B8} = U16{0xFFE3B6};
    UploadScroll();
    for (int i = 0; i < 0x800; i++) display::vram_w16(uint16_t(plane_a_base + i * 2), 0);
    for (int i = 0; i < 0x800; i++) display::vram_w16(uint16_t(plane_b_base + i * 2), 0);
}

// 0x5EE2, 0x5EE6: a map cell's 4x4 tiles into the nametables when it is in
// view: the ground (tile word & 0x1FF) on plane B, the overlay (bits
// 9-15, spice/fog) on plane A, both from the 32-byte entries at 0x4ADF0;
// the lower half only above the panel. 0 when out of view.
uint16_t RedrawCell(uint16_t cell) {
    cell &= 0x3FFF;
    int16_t col = int16_t(((cell & 0x3F) << 5) - (U16{0xFFE3EC} & 0xFFE0));
    if (col < 0 || col > int16_t(screen_width)) return 0;
    col = int16_t(col >> 2);
    int16_t py = int16_t((cell & 0xFC0) >> 1);
    bool lower = int16_t(py - U16{0xFFE3C0}) < 0xD8;
    py = int16_t(py - (U16{0xFFE3EE} & 0xFFE0));
    if (int16_t(py + 7) < 0) return 0;
    if (py > 0xE0) return 0;
    uint16_t row = uint16_t(py << 4);
    col = int16_t(col + ((uint16_t(-U16{0xFFE3B6}) & 0x1E0) >> 2));
    if (col >= 0x80) col = int16_t(col - 0x80);
    row = uint16_t((row + ((U16{0xFFE3BA} & 0x1E0) << 4)) & 0xFFC);
    uint32_t c = 0xFF7D9C + cell * 4u;
    uint16_t over = rd8(c) & 0xFE;
    uint16_t ground = uint16_t((rd16(c) & 0x1FF) << 5);
    if (U8{0xFFC198} && over >= 0x67) over = 0;
    uint32_t g = 0x4ADF0 + ground, o = 0x4ADF0 + uint32_t(over << 4);
    uint16_t at = uint16_t(row + col);
    auto put = [&](uint16_t base, uint32_t src) {
        for (int r = 0; r < (lower ? 4 : 2); r++)
            for (int k = 0; k < 4; k++)
                display::vram_w16(uint16_t(base + at + r * 0x80 + k * 2), rd16(src + r * 8u + k * 2u));
    };
    put(plane_b_base, g);
    put(plane_a_base, o);
    return 1;
}

} // namespace game
