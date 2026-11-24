// The mentat screens (0x1F346-0x20146 C side, 0x23F60-0x264F6 asm side):
// house emblem assembled from pieces, the mentat's animated face and
// scrolling text, the Yes/No (Proceed/Repeat) prompt, the closing
// emblem animation.
//
// RAM: 0xFFA500 four flying pieces of 0x24 bytes (+0 x 16.16, +4 x2 16.16,
// +8 y 16.16, +0xC y2 16.16, +0x10 acceleration, +0x14 speed, +0x18 flags:
// bit 0 held, 1 in use, 2 shown; +0x1A tile base, +0x1C and +0x20 piece
// lists); 0xFFA62C the screen's own sprite table, shown by its VBlank hook;
// 0xFFB3B6 a palette; 0xFFF3B2 the mask of emblem pieces still drawn.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kPieces = 0xFFA500;
constexpr uint32_t kMentatSprites = 0xFFA62C;
constexpr uint32_t kMentatPalette = 0xFFB3B6;
constexpr U16 mentat_timer{0xFFB3B0};
constexpr U16 mentat_step{0xFFB3B2};
constexpr U16 mentat_frozen{0xFFB3B4}; // the hook leaves the pieces alone
constexpr U16 emblem_mask{0xFFF3B2};
constexpr U32 emblem_pos{0xFFF3B6};
constexpr U16 emblem_skip{0xFFF3BA}; // the piece not flown in
constexpr U16 scroll_x{0xFFF3BE};
constexpr U16 scroll_y{0xFFF3C0};
constexpr U16 player_house{0xFFC274};

U8 piece_flags(uint32_t p) { return U8{p + 0x18}; }

uint32_t house_index() { return uint32_t(int16_t(player_house)) * 4; }

// 0x23F60 / 0x24282: draws the pieces of 10 emblem parts (piece lists at
// lists[i]: x, y, size, then (dx, dy, size) until a zero long) whose bit
// 9 - i is set in mask, from 4bpp tiles at gfx into a 33 x 14 tile image at
// 0xFF0000 (types >= 2: the second image at +0x39C0; type 1 or 3 recolours).
void BlitTiles(int16_t x, int16_t y, uint32_t size_type, uint16_t tile, uint32_t gfx) {
    uint32_t dst = 0xFF0000;
    uint16_t type = uint16_t(size_type >> 16);
    if (int16_t(type) > 1) {
        dst += 0x39C0;
        type = uint16_t(type - 2);
    }
    uint16_t size = uint16_t(size_type);
    uint16_t row = uint16_t(int16_t(y - 0x30) >> 3);
    dst += uint32_t(row) * 0x420 + ((uint32_t(int32_t(int16_t(x - 0x18))) & 0x1F8) << 2);
    int cols = rd16(0x24042 + size * 2u), rows = rd16(0x24062 + size * 2u);
    uint32_t src_off = uint32_t(uint16_t(tile << 5));
    uint32_t col = dst;
    for (int c = 0; c <= cols; c++, col += 0x20) {
        uint32_t d = col;
        for (int r = 0; r <= rows; r++) {
            uint32_t s = gfx + uint16_t(src_off);
            for (int line = 0; line < 8; line++, s += 4, d += 4) {
                uint32_t px = rd32(s);
                if (type) {
                    uint32_t m = 0;
                    for (int b = 24; b >= 0; b -= 8) m |= uint32_t(rd8(0x24082 + (px >> b & 0xFF))) << b;
                    px = m;
                }
                uint32_t mask = 0;
                for (int b = 24; b >= 0; b -= 8) mask |= uint32_t(rd8(0x24182 + (px >> b & 0xFF))) << b;
                wr32(d, (rd32(d) & mask) | px);
            }
            d += 0x400;
            src_off += 0x20;
        }
    }
}

void DrawEmblemParts(uint32_t lists, uint32_t gfx, uint32_t types, uint16_t mask) {
    uint16_t tile = 0;
    for (int i = 9; i >= 0; i--) {
        uint32_t p = rd32(lists);
        lists += 4;
        int16_t x = int16_t(rd16(p)), y = int16_t(rd16(p + 2));
        uint16_t size = rd16(p + 4);
        uint16_t type = rd16(types);
        types += 2;
        p += 6;
        for (;;) {
            if (mask >> (i & 31) & 1) BlitTiles(x, y, uint32_t(type) << 16 | size, tile, gfx);
            tile = uint16_t(tile + rd16(0x96304 + uint16_t(size * 2)));
            if (!rd32(p)) break;
            x = int16_t(x + rd16(p));
            y = int16_t(y + rd16(p + 2));
            size = rd16(p + 4);
            p += 6;
        }
    }
}

// 0x2435E / 0x24A76: the same, as one byte per pixel into a 0x70 wide
// picture at 0xFF7380 (only colour 0 transparent), from an origin.
void BlitPixels(int16_t x, int16_t y, uint16_t size, uint16_t tile, uint32_t gfx) {
    uint32_t dst = 0xFF7380 + uint32_t(uint16_t(y - 0x30)) * 0x70 + (uint32_t(int32_t(int16_t(x - 0x18))) & 0x1F8);
    int cols = rd16(0x24042 + size * 2u), rows = rd16(0x24062 + size * 2u);
    uint32_t src_off = uint32_t(uint16_t(tile << 5));
    for (int c = 0; c <= cols; c++, dst += 8) {
        uint32_t d = dst;
        for (int r = 0; r <= rows; r++) {
            uint32_t s = gfx + uint16_t(src_off);
            for (int line = 0; line < 8; line++, s += 4) {
                uint32_t px = rd32(s);
                for (int k = 0; k < 8; k++, d++) {
                    px = px << 4 | px >> 28;
                    if (px & 0xF) wr8(d, uint8_t(px & 0xF));
                }
                d += 0x68;
            }
            src_off += 0x20;
        }
    }
}

void DrawPartsPixels(int16_t x0, int16_t y0, uint32_t lists, uint32_t gfx) {
    uint16_t mask = emblem_mask, tile = 0;
    for (int i = 9; i >= 0; i--) {
        int16_t x = x0, y = y0;
        uint32_t p = rd32(lists) + 4;
        lists += 4;
        uint16_t size = rd16(p);
        p += 2;
        for (;;) {
            if (mask >> (i & 31) & 1) BlitPixels(x, y, size, tile, gfx);
            tile = uint16_t(tile + rd16(0x96304 + uint16_t(size * 2)));
            if (!rd32(p)) break;
            x = int16_t(x + rd16(p));
            y = int16_t(y + rd16(p + 2));
            size = rd16(p + 4);
            p += 6;
        }
    }
}

// 0x24AF2: scales the 0xFF7380 picture into tiles at dst (rows of 0x420
// bytes): step 16.16, tiles_y rows of tiles_x tiles. (One of the eight
// column steps adds only the whole part, as the original does.)
void ScalePicture(uint32_t dst, uint32_t step, uint16_t tiles_y, uint16_t tiles_x) {
    step = step << 16 | step >> 16;
    auto add = [&](uint32_t& acc) {
        uint64_t s = uint64_t(acc) + step;
        acc = uint32_t(s);
        acc = (acc & 0xFFFF0000) | uint16_t(uint16_t(acc) + uint16_t(s >> 32));
    };
    auto add_word = [&](uint32_t& acc) {
        uint32_t s = uint32_t(uint16_t(acc)) + uint16_t(step);
        acc = (acc & 0xFFFF0000) | uint16_t(s);
        acc = (acc & 0xFFFF0000) | uint16_t(uint16_t(acc) + uint16_t(s >> 16));
    };
    uint32_t src = 0xFF7380, sx = 0, sy = 0;
    for (int ty = int16_t(tiles_y - 1); ty >= 0; ty--) {
        for (int line = 0; line < 8; line++) {
            uint32_t d = dst;
            for (int tx = 0; tx < tiles_x; tx++) {
                for (int b = 0; b < 4; b++) {
                    uint8_t v = uint8_t(rd8(src + int16_t(sx)) << 4);
                    add(sx);
                    v = uint8_t(v + rd8(src + int16_t(sx)));
                    wr8(d++, v);
                    if (b == 2) add_word(sx);
                    else add(sx);
                }
                d += 0x1C;
            }
            add(sy);
            src = 0xFF7380 + uint32_t(uint16_t(sy)) * 0x70;
            sx = 0;
            dst += 4;
        }
        dst += 0x400;
    }
}

// 0x23FA4: uploads both emblem images to VRAM tile 1 and lays them out at
// (6, 9) on plane B (priority) and plane A (palette 1).
void ShowEmblem() {
    QueueDma(2, 0xFF0000, 0x20u << 16 | 0x39C0);
    FlushDma();
    auto layout = [](uint16_t at, uint16_t e) {
        for (int r = 0; r <= 0xD; r++, at = uint16_t(at + 0x80))
            for (int c = 0; c <= 0x20; c++) display::vram_w16(uint16_t(at + c * 2), e++);
    };
    layout(0xC48C, 0x8001);
    layout(0xE48C, 0xA1CF);
}

// 0x2442A: the screen's display setup.
void MentatDisplay() {
    ClearVram();
    ClearVsram();
    ClearPaletteRange(0, 0x40);
    ClearCram();
    display::cfg.win_h = 0;
    display::cfg.win_v = 0;
    display::cfg.plane_w = 64;
    display::cfg.plane_h = 32;
    display::cfg.width = 320;
    display::cfg.sprites = 0xC000;
    display::cfg.hscroll = 0xA800;
    display::cfg.backdrop = 0;
    WaitVBlank();
    display::vsram_w16(0, 0x18);
    display::vsram_w16(1, 0x18);
}

// 0x24488: back to the game's display setup.
void RestoreDisplay() {
    ClearVram();
    ClearVsram();
    ClearPaletteRange(0, 0x40);
    ClearCram();
    display::cfg.win_h = 0;
    display::cfg.win_v = 0;
    display::cfg.plane_w = 64;
    display::cfg.plane_h = 32;
    display::cfg.width = 320;
    display::cfg.sprites = 0xF000;
    display::cfg.hscroll = 0xF400;
    display::cfg.backdrop = 0;
}

// 0x244CC: starts emblem part n flying (part 7 only in the third slot);
// the piece, or 0 when no slot is free.
uint32_t StartPiece(uint16_t n, uint32_t types) {
    uint32_t p = kPieces, base = 0x244C4;
    if (n == 7) {
        p += 0x48;
        base += 4;
        if (piece_flags(p) & 2) return 0;
    } else {
        int i = 0;
        while (piece_flags(p) & 2) {
            if (++i == 4) return 0;
            p += 0x24;
            base += 2;
        }
    }
    uint16_t pal = rd16(0x95CF0 + uint16_t(rd16(types + n * 2u) * 2));
    uint32_t k = n * 4u;
    wr16(p + 0x1A, uint16_t(rd16(base) + pal));
    uint32_t a = rd32(0x962DC + k);
    uint32_t off_a = rd32(0x9D11C + k), off_b = rd32(0x9D144 + k);
    wr32(p + 0x1C, a);
    wr32(p, uint32_t(rd16(a)) << 16);
    wr32(p + 8, uint32_t(rd16(a + 2)) << 16);
    uint32_t b = rd32(0x96158 + k);
    wr32(p + 0x20, b);
    wr32(p + 4, uint32_t(rd16(b)) << 16);
    wr32(p + 0xC, uint32_t(rd16(b + 2)) << 16);
    wr32(p + 0x10, 0x4000);
    wr32(p + 0x14, 0);
    U8 f = piece_flags(p);
    f = uint8_t((f | 3) & ~4);
    uint16_t t = rd16(p + 0x1A);
    QueueDma(2, 0x96324 + int16_t(off_a), uint32_t(uint16_t(t << 5)) << 16 | 0x2B0);
    uint16_t len = (t & 0x1FFF) == 0x541 ? 0x600 : 0x510;
    QueueDma(2, 0x986E6 + int16_t(off_b), uint32_t(uint16_t((t + 0x2B) << 5)) << 16 | len);
    return p;
}

// 0x245F2: the screen's sprite table: the emblem frame, then the shown
// flying pieces, last slot first.
void BuildMentatSprites() {
    uint32_t out = kMentatSprites;
    uint32_t pos = emblem_pos + 0x800080;
    wr16(out, uint16_t(pos));
    wr16(out + 6, uint16_t(pos >> 16));
    wr16(out + 2, 0xA01);
    wr16(out + 4, 0xC700);
    out += 8;
    uint8_t link = 2;
    auto emit = [&](uint16_t y, uint8_t size, uint16_t attr, uint16_t x) {
        wr16(out, y);
        wr8(out + 2, size);
        wr8(out + 3, link);
        wr16(out + 4, attr);
        wr16(out + 6, x);
        out += 8;
    };
    auto pieces = [&](uint32_t list, uint16_t attr, uint16_t x, uint16_t y) {
        list += 4;
        uint16_t size = rd16(list);
        list += 2;
        emit(y, uint8_t(size), attr, x);
        for (;;) {
            link++;
            attr = uint16_t(attr + rd16(0x96304 + uint16_t(size * 2)));
            if (!rd32(list)) return;
            x = uint16_t(x + rd16(list));
            y = uint16_t(y + rd16(list + 2));
            size = rd16(list + 4);
            list += 6;
            emit(y, uint8_t(size), attr, x);
        }
    };
    for (uint32_t p = kPieces + 3 * 0x24, i = 0; i < 4; i++, p -= 0x24) {
        uint8_t f = piece_flags(p);
        if (!(f & 2) || !(f & 4)) continue;
        pieces(rd32(p + 0x20), rd16(p + 0x1A) & 0x1FFF, uint16_t(rd16(p + 4) + 0x80), uint16_t(rd16(p + 0xC) + 0x80));
        pieces(rd32(p + 0x1C), uint16_t(rd16(p + 0x1A) + 0x2B), uint16_t(rd16(p) + 0x80), uint16_t(rd16(p + 8) + 0x80));
    }
    wr32(out, 0);
    wr32(out + 4, 0);
}

// 0x246E4: pieces let go fall down and are done once off the screen.
void MovePieces() {
    for (uint32_t p = kPieces, i = 0; i < 4; i++, p += 0x24) {
        uint8_t f = piece_flags(p);
        if (!(f & 2) || (f & 1)) continue;
        wr32(p + 0x14, rd32(p + 0x14) + rd32(p + 0x10));
        uint32_t v = rd32(p + 0x14);
        wr32(p + 8, rd32(p + 8) + v);
        wr32(p + 0xC, rd32(p + 0xC) + v);
        if (int16_t(rd16(p + 8)) >= 0xE0) piece_flags(p) = 0;
    }
}

void ShowMentatSprites() {
    for (int i = 0; i < 0x140; i++) display::vram_w16(uint16_t(0xC000 + i * 2), rd16(kMentatSprites + i * 2u));
    display::cfg.enabled = true;
}

// One step of the fade down to black on a palette in RAM.
void Darken(uint32_t pal, int n) {
    for (int i = 0; i < n; i++) {
        uint16_t c = rd16(pal + i * 2u), d = c & 0xE00;
        if (d) d = uint16_t(d - 0x200);
        if (uint16_t g = c & 0xE0) d = uint16_t(d + g - 0x20);
        if (uint16_t r = c & 0xE) d = uint16_t(d + r - 2);
        wr16(pal + i * 2u, d);
    }
}

// 0x24952: redraws the emblem with the parts still in emblem_mask.
void RedrawEmblem(uint16_t mode) {
    for (uint32_t a = 0xFF0000; a < 0xFF0000 + 0x39C * 0x20; a += 4) wr32(a, 0);
    DrawEmblemParts(0x96158, 0x96324, 0x95D00, emblem_mask);
    uint32_t types = rd32(0x95F6C + house_index()) + mode * 0x14u;
    DrawEmblemParts(0x962DC, 0x986E6, types, emblem_mask);
    QueueDma(2, 0xFF0000, 0x20u << 16 | 0x39C0);
}

// 0x249F4: the parts fly off one by one, except emblem_skip.
void FlyPiecesOff(uint16_t mode) {
    WaitFrames(0x65);
    uint32_t types = rd32(0x95F6C + house_index()) + mode * 0x14u;
    uint16_t skip = emblem_skip;
    for (int16_t i = 9; i >= 0; i--) {
        uint32_t p;
        for (;;) {
            if (i == 7) WaitFrames(0x15);
            if (uint16_t(i) == skip) {
                p = 0;
                break;
            }
            if ((p = StartPiece(uint16_t(i), types))) break;
            host::frame(); // the VBlank hook frees slots as pieces leave
        }
        if (!p) continue;
        FlushDma();
        piece_flags(p) |= 4;
        emblem_mask &= uint16_t(~(1u << ((9 - i) & 31)));
        RedrawEmblem(mode);
        FlushDma();
        piece_flags(p) &= uint8_t(~1);
    }
}

// 0x25970: the mentat's face tiles and the screen's own sprite table.
void LoadMentatFace(uint16_t extra_frame) {
    uint32_t h = house_index();
    Decompress(rd32(0x25934 + h), 0xFF0000);
    QueueDma(2, 0xFF0000, 0xD000u << 16 | 0x7E0);
    QueueDma(2, 0xFF0FC0, 0xF000u << 16 | 0x7E0);
    QueueDma(2, 0xFF1F80, 0xA820u << 16 | 0x480);
    FlushDma();
    mentat_frozen = 1;
    for (int i = 0; i <= 0x51; i++) wr32(kMentatSprites + i * 4u, rd32(0x257EC + i * 4u));
    if (extra_frame) {
        wr16(0xFFA76C + 6, uint16_t(rd16(0xFFF3B6) + 0x97));
        wr16(0xFFA76C, uint16_t(rd16(0xFFF3B8) + 0x80));
    }
    uint32_t a = 0xFFA674;
    uint16_t dy = rd16(0x25958 + h + 2), dx = rd16(0x25958 + h);
    for (int k = 0; k < 2; k++) {
        U16{a + k * 8u} += dy;
        U16{a + k * 8u + 6} += dx;
    }
    a += 0x10;
    dy = rd16(0x25964 + h + 2);
    dx = rd16(0x25964 + h);
    for (int k = 0; k < 4; k++) {
        U16{a + k * 8u} += dy;
        U16{a + k * 8u + 6} += dx;
    }
    md::int_mask = 0;
    Decompress(0x506CA, 0xFF0000);
    QueueDma(2, 0xFF0000, 0x8C00u << 16 | 0x800);
    FlushDma();
}

// 0x25B90 / 0x25B02: a line of mentat text on plane A (four rows cleared
// first) at (x, y + 3); carriage returns go down a row.
void PrintMentatText(uint32_t s, uint16_t x, uint16_t y) {
    uint16_t line = uint16_t(x * 2 + (y + 3) * 0x80 + 0xE000), at = line;
    for (int i = 0; i < 0x100; i++) display::vram_w16(uint16_t(at + i * 2), 0);
    for (;;) {
        uint8_t c = rd8(s++);
        if (!c) break;
        if (c == '\r') {
            line = uint16_t(line + 0x80);
            at = line;
            continue;
        }
        display::vram_w16(at, uint16_t(c + 0xC460));
        at = uint16_t(at + 2);
    }
    md::int_mask = 0;
}

// 0x25A98: a frame of the eyes (part 0) or the mouth (part 1).
void SetFaceFrame(uint16_t, uint16_t part, uint16_t frame) {
    if (part == 0) QueueDma(2, 0xFF1000 + uint16_t(frame * 0x240), 0xB120u << 16 | 0x120);
    else if (part == 1) QueueDma(2, 0xFF1B40 + uint16_t(frame * 0x460), 0xB360u << 16 | 0x230);
}

// 0x20146: frame limit check of the face animation table at 0x2023A;
// 0 when frame + 1 is past the limit.
uint16_t FaceFrameLimit(uint16_t house, uint16_t part, uint16_t frame) {
    uint16_t n = rd16(0x2023A + uint16_t(house * 0x12) + uint16_t(part * 6));
    if (n && int16_t(frame + 1) > int16_t(n)) return 0;
    return n;
}

// Face state (at 0xFF3E80): +0 eyes, +2 next eyes, +4 mouth, +6 head,
// +8 eye time, +0xC mouth time, +0x10 head time.
// 0x1F626: animates the face; talking: 1 while text shows.
void AnimateFace(uint16_t house, uint32_t face, uint16_t talking) {
    uint32_t now = FrameCounter();
    if (now > rd32(face + 0x10)) {
        if (rd32(face + 0x10)) {
            int16_t f = int16_t(rd16(face + 6));
            if (FaceFrameLimit(house, 2, uint16_t((f < 0 ? -f : f) + 1))) f = int16_t(f + 1);
            else f = int16_t(1 - f);
            wr16(face + 6, uint16_t(f));
            SetFaceFrame(house, 2, uint16_t(f < 0 ? -f : f));
        }
        uint32_t t = 0;
        if (house == 0) t = FrameCounter() + 0x4650;
        else if (house == 1) t = FrameCounter() + uint32_t(int16_t(RandomRange(1, 3))) * 60;
        else if (house == 2) {
            if (rd16(face + 6)) t = FrameCounter() + 6;
            else t = FrameCounter() + uint32_t(int16_t(RandomRange(0xA, 0x19))) * 60;
        }
        if (house <= 2) wr32(face + 0x10, t);
    }
    if (talking == 1) {
        if (FrameCounter() > rd32(face + 0xC)) {
            uint16_t m = RandomRange(0, 4);
            wr16(face + 4, m);
            SetFaceFrame(house, 1, m);
            int16_t lo = 0, hi = 0;
            switch (m) {
            case 0: lo = 7, hi = 0x1E; break;
            case 1: case 2: case 3: lo = 6, hi = 0xA; break;
            case 4: lo = 5, hi = 6; break;
            }
            wr32(face + 0xC, FrameCounter() + uint32_t(int32_t(int8_t(RandomRange(uint8_t(lo), uint8_t(hi))))));
        }
    } else if (rd16(face + 4)) {
        wr16(face + 4, 0);
        wr32(face + 0xC, 0);
        SetFaceFrame(house, 1, 0);
    }
    if (FrameCounter() <= rd32(face + 8)) return;
    U16 eyes{face}, next{face + 2};
    int8_t e = 0;
    if (next) {
        eyes = next;
        next = 0;
        if (eyes == 4) wr32(face + 8, FrameCounter() + uint32_t(int8_t(RandomRange(0xC, 0x1E))));
        else wr32(face + 8, FrameCounter() + uint32_t(int8_t(RandomRange(0x14, 0xB4))));
    } else {
        bool blink = false;
        if (talking == 0) {
            e = int8_t(RandomRange(0, 7));
            if (e > 5) e = 1;
            else blink = e == 5;
        } else if (talking == 1) {
            if (eyes) e = 0;
            else {
                e = int8_t(RandomRange(0, 0x11));
                if (e > 9) e = 0;
                else blink = e >= 5;
            }
        } else {
            e = int8_t(RandomRange(0, 0xF));
            if (e > 0xA) e = 2;
            else blink = e >= 5;
        }
        if (blink) e = 4;
        if ((e == 2 && eyes == 1) || (e == 1 && eyes == 2)) {
            next = uint16_t(int16_t(e));
            eyes = 0;
            wr32(face + 8, FrameCounter() + uint32_t(int8_t(RandomRange(1, 5))));
        } else if (uint32_t(int32_t(e)) != eyes && (e == 4 || eyes == 4)) {
            next = uint16_t(int16_t(e));
            eyes = 3;
            wr32(face + 8, FrameCounter());
        } else {
            eyes = uint16_t(int16_t(e));
            if (e == 4) wr32(face + 8, FrameCounter() + uint32_t(int8_t(RandomRange(6, 0x3C))));
            else wr32(face + 8, FrameCounter() + uint32_t(int8_t(RandomRange(0xF, 0xB4))));
        }
    }
    SetFaceFrame(house, 0, eyes);
}

// 0x1FBBA: breaks text into sentences (each ends in . ! ? or a line end
// and is NUL-terminated) of lines up to width, wrapping at spaces; the
// sentence count.
uint16_t SplitSentences(uint32_t src, uint32_t dst, uint16_t width) {
    uint16_t n = 0;
    auto end_char = [](uint8_t c) { return c == '.' || c == '!' || c == '?' || c == 0 || c == '\r'; };
    while (rd8(src)) {
        uint16_t col = 0;
        n++;
        while (!end_char(rd8(src)) && col < width) {
            wr8(dst++, rd8(src++));
            col++;
            if (rd8(src) == '\n' || rd8(src) == '\r') col = 0;
        }
        if (col >= width) {
            while (rd8(src) != ' ') {
                col--;
                src--;
                dst--;
            }
            src++;
            wr8(dst++, '\r');
            n--;
        }
        if (end_char(rd8(src))) {
            wr8(dst++, rd8(src++));
            wr8(dst++, 0);
        }
    }
    return n;
}

// 0x1F9DA: the mentat talks through text one sentence at a time (A/B/C
// skip ahead) while the face animates.
void MentatTalk(uint16_t house, uint32_t face, uint32_t text) {
    uint16_t total = SplitSentences(text, 0xFF0000, 0x24), left = total, shown = 0;
    uint32_t s = 0xFF0000, until = 0, len4 = 0;
    uint16_t state = 1, talking = 2;
    uint32_t next_tick = FrameCounter() + 0x1E;
    bool done = false, last = false;
    while (!done) {
        uint16_t key = GetKey();
        if (key != 'A' && key != 'B' && key != 'C') key = 0;
        switch (state) {
        case 0:
            if (!key) break;
            if (last) {
                state = 5;
                break;
            }
            state = 1;
            [[fallthrough]];
        case 1:
            if (key) {
                if (last) {
                    state = 5;
                    break;
                }
                shown = total;
            } else if (FrameCounter() > next_tick) {
                next_tick = FrameCounter() + 0xF;
                shown++;
            }
            if (int16_t(shown) >= int16_t(total)) {
                state = 2;
                shown = total;
            }
            break;
        case 2:
            state = 3;
            key = 1;
            [[fallthrough]];
        case 3:
            if (talking == 2 && FrameCounter() > until) key = 1;
            if (key && last) {
                state = 4;
                talking = 0;
                break;
            }
            if (key) {
                if (left-- == 0) break;
                PrintMentatText(s, 2, 2);
                talking = 1;
                len4 = StrLen(s) << 2;
                until = FrameCounter() + len4;
                if (left) {
                    while (rd8(s++)) {}
                } else {
                    last = true;
                }
            } else if (talking == 1 && FrameCounter() > until) {
                talking = 2;
                if (!last) until += len4 + (int32_t(len4) >> 1);
            }
            break;
        case 4:
            if (last) state = 5;
            break;
        case 5: done = true; break;
        }
        AnimateFace(house, face, talking);
        host::frame(); // the original spins on the frame counter
    }
}

} // namespace

// 0x24728: VBlank hook of the mentat screen: cycles colours 0x28-0x2B,
// shows the sprite table, moves the pieces.
void MentatHook() {
    if (int16_t(--mentat_timer) < 0) {
        mentat_timer = 7;
        for (int i = 0; i < 4; i++) {
            display::cram_w16(0x28 + i, rd16(0xA2DCE + mentat_step));
            mentat_step = (mentat_step + 2) & 6;
        }
        mentat_step = (mentat_step + 2) & 6;
    }
    ShowMentatSprites();
    if (!mentat_frozen) {
        MovePieces();
        BuildMentatSprites();
    }
}

// 0x24816: VBlank hook of the closing animation: scrolls to (scroll_x,
// scroll_y) and fades the palette every 5 frames.
void MentatFadeHook() {
    display::vsram_w16(0, scroll_y);
    display::vram_w16(0xA800, scroll_x);
    if (--mentat_timer) return;
    mentat_timer = 5;
    Darken(kMentatPalette, 0x40);
    for (int i = 0; i < 0x40; i++) display::cram_w16(i, rd16(kMentatPalette + i * 2u));
    display::cfg.enabled = true;
}

// 0x25ECE (setup) and 0x260EC (continue from 0x26008): the mentat
// screen of a house in a mode (0-8) talking through text.
static void MentatRun(uint16_t mode, uint32_t text, uint16_t extra_frame, bool setup) {
    uint16_t saved_delay = fade_delay;
    uint8_t saved_mask = pad_repeat_mask;
    pad_repeat_mask = 0;
    emblem_mask = 0xFFFF;
    uint32_t h = house_index();
    if (setup) {
        InitSpritePool(0xFFE6A8, kSpriteList);
        fade_delay = 3;
        FadeOutAll();
        vblank_hook = 0;
        ClearVram();
        MentatDisplay();
        for (uint32_t a = 0xFF0000; a < 0xFF0000 + 0x2EE0 * 4; a += 4) wr32(a, 0);
        DrawEmblemParts(0x96158, 0x96324, 0x95D00, emblem_mask);
        uint32_t types = rd32(0x95F6C + h) + mode * 0x14u;
        DrawEmblemParts(0x962DC, 0x986E6, types, emblem_mask);
        ShowEmblem();
        Decompress(0x9CF28, 0xFF0000);
        uint32_t e = rd32(0x96050 + h) + mode * 8u;
        emblem_pos = rd32(e);
        uint16_t off = rd16(e + 4);
        emblem_skip = rd16(e + 6);
        QueueDma(2, 0xFF0000 + off, 0xE000u << 16 | 0x90);
        FlushDma();
        BuildMentatSprites();
        QueueDma(2, kMentatSprites, 0xC000u << 16 | 0x140);
        FlushDma();
        LoadMentatFace(extra_frame);
        FlushDma();
    }
    Decompress(rd32(0x25940 + h), 0xFF1000);
    Decompress(rd32(0x2594C + h), 0xFF1B40);
    vblank_hook = 0x24728;
    md::int_mask = 0;
    uint32_t src = mode == 8 ? 0xA2DFE : 0xA2D7E;
    for (int i = 0; i < 16; i++) wr32(kMentatPalette + i * 4u, rd32(src + i * 4u));
    for (int i = 0; i < 32; i++) wr16(kMentatPalette + i * 2u, uint16_t((rd16(kMentatPalette + i * 2u) >> 1) & 0x666));
    src = 0xA2C3E + uint16_t(player_house * 0x80);
    for (int i = 0; i < 16; i++) wr32(kMentatPalette + 0x40 + i * 4u, rd32(src + i * 4u));
    FadeInAll(kMentatPalette);
    for (int i = 0; i <= 0x32; i++) wr32(0xFF3E80 + i * 4u, 0);
    pad_repeat_mask = 0xF0;
    MentatTalk(player_house, 0xFF3E80, text);
    pad_repeat_mask = saved_mask;
    fade_delay = saved_delay;
}

void MentatStart(uint16_t mode, uint32_t text, uint16_t extra_frame) {
    mentat_frozen = 0;
    mentat_step = 0;
    mentat_timer = 0;
    MentatRun(mode, text, extra_frame, true);
}

void MentatContinue(uint16_t mode, uint32_t text, uint16_t extra_frame) {
    MentatRun(mode, text, extra_frame, false);
}

// 0x25BB8: the two-choice prompt (yes/no, or proceed/repeat when
// proceed): Up/Down move, a button picks; 0 for the first choice.
uint16_t MentatChoice(uint16_t proceed) {
    wr16(kMentatSprites, 0xA8);
    wr16(kMentatSprites + 8, 0xA8);
    wr16(kMentatSprites + 0x10, 0xA8);
    for (int i = 0; i < 6; i++) {
        U16{0xFFA674 + i * 8u} -= 0x80;
        U16{0xFFA674 + i * 8u + 6} -= 0x80;
    }
    FlushDma();
    Decompress(0xA27FC, 0xFF0000);
    QueueDma(2, 0xFF0000, 0xB120u << 16 | 0x140);
    FlushDma();
    Decompress(proceed ? 0xA2818 : 0xA2A44, 0xFF0000);
    QueueDma(2, 0xFF0000, 0xB3A0u << 16 | 0x280);
    FlushDma();
    for (int i = 0; i < 9; i++) {
        U16{kMentatSprites + i * 8u + 6} += 0x140;
        U16{kMentatSprites + i * 8u} += 0x80;
    }
    U16 sel{kMentatSprites};
    for (;;) {
        WaitVBlank();
        uint32_t b = ReadPads();
        // Port: the mouse points at a choice (rows at y 0xA8 and 0xC0, x
        // 0xC1-0x110), moving the mark there; a click picks the one under
        // it, elsewhere nothing.
        const host::Mouse& m = host::mouse();
        int row = !m.inside || m.x < 0xC1 || m.x > 0x110 || m.y < 0xA4 || m.y >= 0xD4 ? -1 : m.y >= 0xBC;
        bool click = (b & 0x40) && (host::mouse_pad() & 0x40);
        if (click && row < 0) continue;
        bool point = row >= 0 && (m.active || click) && sel != (row ? 0x140 : 0x128);
        if ((b & 0x70) && !point) break;
        int16_t d;
        if (point) d = row ? 2 : -2;
        else if ((b & 1) && sel != 0x128) d = -2;
        else if ((b & 2) && sel != 0x140) d = 2;
        else continue;
        for (int i = 0; i < 12; i++) {
            sel += uint16_t(d);
            U16{kMentatSprites + 8} += uint16_t(d);
            U16{kMentatSprites + 0x10} += uint16_t(d);
            WaitVBlank();
        }
        PlaySfx(0x19);
        if (click) break;
    }
    PlaySfx(0x28);
    uint16_t choice = sel == 0x128 ? 0 : 1;
    for (int i = 0; i < 9; i++) {
        U16{kMentatSprites + i * 8u + 6} -= 0x140;
        U16{kMentatSprites + i * 8u} -= 0x80;
    }
    for (int i = 0; i < 0x3C0; i++) display::vram_w16(uint16_t(0xB120 + i * 2), 0);
    md::int_mask = 0;
    for (int i = 0; i < 6; i++) {
        U16{0xFFA674 + i * 8u} += 0x80;
        U16{0xFFA674 + i * 8u + 6} += 0x80;
    }
    return choice;
}

// 0x264CA: fades out and restores the game's display.
void MentatClose() {
    uint16_t saved = fade_delay;
    fade_delay = 3;
    FadeOutAll();
    vblank_hook = 0;
    md::int_mask = 0;
    RestoreDisplay();
    fade_delay = saved;
}

// 0x25DB2: the face and text slide away while colours 0x30-0x3F fade.
void MentatSlideAway() {
    for (int i = 0; i < 0x180; i++) display::vram_w16(uint16_t(0xE180 + i * 2), 0);
    for (int i = 0; i < 16; i++) wr16(kMentatPalette + i * 2u, display::cram[0x30 + i]);
    md::int_mask = 0;
    uint16_t scroll = 0;
    for (int16_t t = 0x17; t >= 0; t--) {
        WaitVBlank();
        for (int i = 0; i < 16; i++) display::cram_w16(0x30 + i, rd16(kMentatPalette + i * 2u));
        scroll--;
        display::vram_w16(0xA800, scroll);
        display::vram_w16(0xA802, scroll);
        uint32_t a = 0xFFA67A;
        for (int i = 0; i <= 0x1E; i++, a += 8) U16{a} -= 4;
        U16{a} -= 1;
        if (t < 9) Darken(kMentatPalette, 16);
    }
}

// 0x256AE: the emblem spins down in eight scaled steps.
static void SpinEmblem() {
    mentat_step = 1;
    uint32_t t = 0x24B8E + uint16_t(emblem_skip * 0xFC) + 4;
    ScalePicture(0xFF39C0, rd32(t), rd16(t + 6), rd16(t + 4));
    t += 8;
    for (int i = 0; i < 8; i++) {
        QueueDma(2, 0xFF39C0, 0x39E0u << 16 | 0x1CE0);
        t += 4;
        ScalePicture(0xFF0000, rd32(t), rd16(t + 6), rd16(t + 4));
        t += 8;
        FlushDma();
        scroll_x = uint16_t(scroll_x - rd16(t - 0xC));
        scroll_y = uint16_t(scroll_y - rd16(t - 0xA));
        WaitVBlank();
        for (int r = 0; r <= 0xD; r++)
            for (int c = 0; c <= 0x20; c++)
                display::vram_w16(uint16_t(0xE48C + r * 0x80 + c * 2), uint16_t(0x81CF + r * 0x21 + c));
        QueueDma(2, 0xFF0000, 0x20u << 16 | 0x1CE0);
        t += 4;
        ScalePicture(0xFF39C0, rd32(t), rd16(t + 6), rd16(t + 4));
        t += 8;
        FlushDma();
        scroll_x = uint16_t(scroll_x - rd16(t - 0xC));
        scroll_y = uint16_t(scroll_y - rd16(t - 0xA));
        WaitVBlank();
        for (int r = 0; r <= 0xD; r++)
            for (int c = 0; c <= 0x20; c++)
                display::vram_w16(uint16_t(0xE48C + r * 0x80 + c * 2), uint16_t(0x8001 + r * 0x21 + c));
    }
}

// 0x2610C: after the briefing: the parts fly off, the last one zooms
// into the screen while it fades.
void MentatOutro(uint16_t mode) {
    uint16_t saved_delay = fade_delay;
    uint8_t saved_mask = pad_repeat_mask;
    pad_repeat_mask = 0;
    emblem_mask = 0xFFFF;
    mentat_frozen = 0;
    for (int i = 0; i <= 0x23; i++) wr32(kPieces + i * 4u, 0);
    BuildMentatSprites();
    QueueDma(2, kMentatSprites, 0xC000u << 16 | 0x140);
    FlushDma();
    FadeInAll(mode == 8 ? 0xA2DFE : 0xA2D7E);
    FlushDma();
    FlyPiecesOff(mode);
    WaitFrames(0x33);
    uint32_t p;
    for (;;) {
        WaitVBlank();
        p = StartPiece(emblem_skip, 0); // (the original passes its a3 along)
        if (p) break;
    }
    FlushDma();
    piece_flags(p) |= 4;
    for (uint32_t a = 0xFF0000; a < 0xFF0000 + 0x39C * 0x20; a += 4) wr32(a, 0);
    WaitFrames(3);
    QueueDma(2, 0xFF0000, 0x20u << 16 | 0x39C0);
    FlushDma();
    for (int i = 0; i <= 0x7FF; i++) display::vram_w16(uint16_t(0xE000 + i * 2), 0);
    for (int i = 0; i <= 0x73F; i++) display::vram_w16(uint16_t(0xC180 + i * 2), 0);
    md::int_mask = 0;
    uint32_t a = rd32(0x962DC + emblem_skip * 4u);
    int16_t sx = int16_t(rd16(a) - 0x30), sy = int16_t(0x48 - rd16(a + 2));
    scroll_x = uint16_t(sx);
    scroll_y = uint16_t(sy);
    WaitFrames(3);
    display::vsram_w16(0, uint16_t(sy));
    display::vram_w16(0xA800, uint16_t(sx));
    uint32_t types = rd32(0x95F6C + house_index()) + mode * 0x14u + emblem_skip * 2u;
    if (uint16_t ty = rd16(types)) {
        uint32_t pal = 0xA2D7E + rd16(0x95CF8 + ty * 2u);
        for (int i = 0; i < 6; i++) display::cram_w16(1 + i, rd16(pal + i * 2u));
    }
    for (int r = 0; r <= 0xD; r++)
        for (int c = 0; c <= 0x20; c++)
            display::vram_w16(uint16_t(0xE48C + r * 0x80 + c * 2), uint16_t(0x8001 + r * 0x21 + c));
    for (uint32_t b = 0xFF7380; b < 0xFF7380 + 0x640 * 4; b += 4) wr32(b, 0);
    uint32_t pa = rd32(0x962DC + emblem_skip * 4u);
    uint32_t d3 = rd32(pa);
    DrawPartsPixels(0x18, 0x30, 0x962DC, 0x986E6);
    uint32_t pb = rd32(0x96158 + emblem_skip * 4u);
    int16_t x0 = int16_t(0x18 - int16_t(d3 >> 16) + int16_t(rd16(pb)));
    int16_t y0 = int16_t(0x30 - int16_t(d3) + int16_t(rd16(pb + 2)));
    DrawPartsPixels(x0, y0, 0x96158, 0x96324);
    ScalePicture(0xFF0000, 0x10000, 0xA, 0xE);
    QueueDma(2, 0xFF0000, 0x20u << 16 | 0x1CE0);
    FlushDma();
    WaitVBlank();
    mentat_frozen = 1;
    for (int i = 0; i <= 0x9F; i++) wr32(kMentatSprites + i * 4u, 0);
    WaitVBlank();
    vblank_hook = 0;
    md::int_mask = 0;
    for (int i = 0; i < 32; i++) wr32(kMentatPalette + i * 4u, uint32_t(display::cram[i * 2]) << 16 | display::cram[i * 2 + 1]);
    mentat_timer = 0x28;
    mentat_frozen = 1;
    vblank_hook = 0x24816;
    md::int_mask = 0;
    StartSeq29();
    GemsSeqCommand3(7, 2, 1);
    SpinEmblem();
    GemsStopSeq(7);
    vblank_hook = 0;
    md::int_mask = 0;
    RestoreDisplay();
    pad_repeat_mask = saved_mask;
    fade_delay = saved_delay;
}

constexpr U8 house_confirmed{0xFFC194};

// 0x1F346: the mentat asks whether to fight for a house; true for yes.
uint16_t ConfirmHouse(uint16_t house) {
    StartSeq29();
    FadeOutScreen();
    PlayMusicTrack(house == 1 ? 0x19 : house == 2 ? 0x1A : 0x18);
    StopSeq29();
    player_house = house;
    MentatStart(0, rd32(0x87D7C + house * 4u), 0);
    uint16_t no = MentatChoice(0);
    PlaySoundEffect(0x26);
    MentatClose();
    return no ? 0 : 1;
}

// 0x1F3EA: the briefing of a scenario (1 up), repeatable, then the outro.
void Briefing(uint16_t house, uint16_t scenario) {
    if (!house_confirmed) {
        StartSeq29();
        FadeOutScreen();
        PlayMusicTrack(house == 1 ? 0x19 : house == 2 ? 0x1A : 0x18);
        StopSeq29();
    }
    house_confirmed = 0;
    uint32_t texts = rd32(0x87F38 + house * 4u) + scenario * 16u;
    MentatStart(uint16_t(scenario - 1), rd32(texts - 16), 1);
    SetPadRepeatMask(0xF0);
    while (MentatChoice(1) == 1) MentatContinue(uint16_t(scenario - 1), rd32(texts - 4), 1);
    // Port: a key, mouse or pad button press skips the outro; its end state
    // (sequences stopped, the game's display, the DMA queue empty) is set
    // straight away.
    uint8_t saved_mask = pad_repeat_mask;
    uint16_t saved_delay = fade_delay;
    host::begin_skippable(true);
    try {
        StartSeq29();
        PlaySoundEffect(0x26);
        WaitFrames(60);
        PlayMusicTrack(0x1D);
        StopSeq29();
        WaitFrames(100);
        MentatSlideAway();
        MentatOutro(uint16_t(scenario - 1));
    } catch (const host::Skip&) {
        StopSeq29();
        GemsStopSeq(7);
        dma_read = dma_write;
        vblank_hook = 0;
        md::int_mask = 0;
        RestoreDisplay();
        pad_repeat_mask = saved_mask;
        fade_delay = saved_delay;
    }
    host::begin_skippable(false);
}

// 0x1F516, 0x1F5A4: the mentat's word on a won or lost scenario (0 up),
// until a button.
void WinMap(uint16_t house, uint16_t scen) {
    uint32_t texts = rd32(0x87F38 + house * 4u);
    StartSeq29();
    FadeOutScreen();
    StopSeq29();
    MentatStart(uint16_t(scen + 1), rd32(texts + 4 + scen * 16u), 0);
    for (;;) {
        uint16_t k = GetKey();
        WaitVBlank();
        if (k == 'A' || k == 'B' || k == 'C') break;
    }
    MentatClose();
}

void LoseMap(uint16_t house, uint16_t scen) {
    uint32_t texts = rd32(0x87F38 + house * 4u);
    StartSeq29();
    FadeOutScreen();
    StopSeq29();
    MentatStart(scen, rd32(texts + 8 + scen * 16u), 0);
    for (;;) {
        WaitVBlank();
        uint16_t k = GetKey();
        if (k == 'A' || k == 'B' || k == 'C') break;
    }
    MentatClose();
}

} // namespace game
