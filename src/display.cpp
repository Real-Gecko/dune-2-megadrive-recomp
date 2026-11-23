#include "display.hpp"

#include <algorithm>
#include <cstring>

namespace display {

uint8_t vram[0x10000];
uint16_t cram[64];
uint16_t vsram[40];
Config cfg;

void reset() {
    std::memset(vram, 0, sizeof vram);
    std::memset(cram, 0, sizeof cram);
    std::memset(vsram, 0, sizeof vsram);
    cfg = Config{};
}

void vram_w16(uint16_t a, uint16_t v) {
    a &= 0xFFFE;
    vram[a] = uint8_t(v >> 8);
    vram[a + 1] = uint8_t(v);
}

uint16_t vram_r16(uint16_t a) {
    a &= 0xFFFE;
    return uint16_t(vram[a] << 8 | vram[a + 1]);
}

void vram_copy(uint16_t dest, const uint8_t* src, uint32_t bytes, int step) {
    for (uint32_t i = 0; i + 1 < bytes; i += 2) {
        vram_w16(dest, uint16_t(src[i] << 8 | src[i + 1]));
        dest = uint16_t(dest + step);
    }
}

// The fill writes the word once, then its high byte at each following step.
void vram_fill(uint16_t dest, uint16_t v, uint32_t bytes, int step) {
    vram_w16(dest, v);
    for (uint32_t i = 0; i < bytes; i++) {
        vram[dest ^ 1] = uint8_t(v >> 8);
        dest = uint16_t(dest + step);
    }
}

void vram_move(uint16_t dest, uint16_t src, uint32_t bytes, int step) {
    for (uint32_t i = 0; i < bytes; i++) {
        vram[dest ^ 1] = vram[src ^ 1];
        src = uint16_t(src + 1);
        dest = uint16_t(dest + step);
    }
}

void cram_w16(int i, uint16_t v) { cram[i & 63] = v & 0xEEE; }
void vsram_w16(int i, uint16_t v) {
    if (i >= 0 && i < 40) vsram[i] = v & 0x7FF;
}

uint32_t color(uint16_t c) {
    static const uint8_t lv[8] = {0, 0x34, 0x57, 0x74, 0x90, 0xAC, 0xCE, 0xFF};
    return 0xFF000000u | uint32_t(lv[(c >> 1) & 7]) << 16 | uint32_t(lv[(c >> 5) & 7]) << 8 | lv[(c >> 9) & 7];
}

namespace {

constexpr int H = 224;

// Pixel of a tile entry at (x, y) inside its 8x8 cell: 0 transparent, else
// palette line * 16 + colour.
inline int tile_pixel(uint16_t e, int x, int y) {
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint8_t b = vram[((e & 0x7FF) * 32 + y * 4 + (x >> 1)) & 0xFFFF];
    int c = x & 1 ? b & 15 : b >> 4;
    return c ? ((e >> 13) & 3) * 16 + c : 0;
}

// One scanline of a scrolled plane: colour (bits 0-5) | priority (bit 6).
void plane_line(uint8_t* out, uint16_t nt, int line, bool is_a, int w) {
    int pw = cfg.plane_w, ph = cfg.plane_h;
    uint16_t hs_addr = cfg.hscroll;
    if (cfg.hmode == 2) hs_addr = uint16_t(hs_addr + (line & ~7) * 4);
    else if (cfg.hmode == 3) hs_addr = uint16_t(hs_addr + line * 4);
    int hs = vram_r16(uint16_t(hs_addr + (is_a ? 0 : 2))) & 0x3FF;
    for (int x = 0; x < w; x++) {
        int vs;
        if (cfg.vcolumns) vs = vsram[((x >> 4) * 2 + (is_a ? 0 : 1)) % 40];
        else vs = vsram[is_a ? 0 : 1];
        int px = (x - hs) & (pw * 8 - 1);
        int py = (line + vs) & (ph * 8 - 1);
        uint16_t e = vram_r16(uint16_t(nt + ((py >> 3) * pw + (px >> 3)) * 2));
        int c = tile_pixel(e, px & 7, py & 7);
        out[x] = uint8_t(c | (e & 0x8000 ? 0x40 : 0));
    }
}

bool in_window(int line, int x) {
    int wx = (cfg.win_h & 0x1F) * 16, wy = (cfg.win_v & 0x1F) * 8;
    bool in_x = (cfg.win_h & 0x80) ? x >= wx : x < wx;
    bool in_y = (cfg.win_v & 0x80) ? line >= wy : line < wy;
    return in_x || in_y;
}

} // namespace

void compose(uint32_t* out, int pitch) {
    int w = cfg.width;
    uint32_t cmap[64];
    for (int i = 0; i < 64; i++) cmap[i] = color(cram[i]);
    if (!cfg.enabled) {
        for (int y = 0; y < H; y++) std::fill(out + y * pitch, out + y * pitch + w, cmap[cfg.backdrop & 63]);
        return;
    }
    static uint8_t a[320], b[320], s[320];
    int max_sprites = w == 320 ? 80 : 64, line_sprites = w == 320 ? 20 : 16, line_px = w;
    for (int line = 0; line < H; line++) {
        plane_line(b, cfg.plane_b, line, false, w);
        plane_line(a, cfg.plane_a, line, true, w);
        int wn = w == 320 ? 64 : 32;
        uint16_t wbase = w == 320 ? cfg.window & 0xF000 : cfg.window & 0xF800;
        for (int x = 0; x < w; x++) {
            if (!in_window(line, x)) continue;
            uint16_t e = vram_r16(uint16_t(wbase + ((line >> 3) * wn + (x >> 3)) * 2));
            a[x] = uint8_t(tile_pixel(e, x & 7, line & 7) | (e & 0x8000 ? 0x40 : 0));
        }
        // Sprites in link order; the first opaque pixel wins.
        std::memset(s, 0, sizeof s);
        int link = 0, count = 0, n_on_line = 0, px_on_line = 0;
        bool seen_nonzero_x = false, masked = false;
        do {
            uint16_t base = uint16_t(cfg.sprites + link * 8);
            int y = (vram_r16(base) & 0x3FF) - 128;
            int size = vram[base + 2], next = vram[base + 3] & 0x7F;
            uint16_t e = vram_r16(uint16_t(base + 4));
            int rx = vram_r16(uint16_t(base + 6)) & 0x1FF, x0 = rx - 128;
            int sw = ((size >> 2) & 3) + 1, sh = (size & 3) + 1;
            if (line >= y && line < y + sh * 8) {
                if (++n_on_line > line_sprites) break;
                if (rx == 0) {
                    if (seen_nonzero_x) masked = true;
                } else {
                    seen_nonzero_x = true;
                }
                int ty = line - y;
                if (e & 0x1000) ty = sh * 8 - 1 - ty;
                for (int cx = 0; cx < sw * 8; cx++) {
                    if (px_on_line++ >= line_px) break;
                    if (masked) continue;
                    int x = x0 + cx;
                    if (x < 0 || x >= w || (s[x] & 63)) continue;
                    int tx = e & 0x0800 ? sw * 8 - 1 - cx : cx;
                    uint16_t te = uint16_t((e & 0xE000) | ((e + (tx >> 3) * sh + (ty >> 3)) & 0x7FF));
                    int c = tile_pixel(te, tx & 7, ty & 7);
                    if (c) s[x] = uint8_t(c | (e & 0x8000 ? 0x40 : 0));
                }
            }
            link = next;
        } while (link && ++count < max_sprites);
        uint32_t* o = out + line * pitch;
        for (int x = 0; x < w; x++) {
            int c = cfg.backdrop & 63;
            int layers[3] = {b[x], a[x], s[x]};
            for (int pri = 0; pri < 2; pri++)
                for (int l : layers)
                    if ((l & 63) && ((l >> 6) & 1) == pri) c = l & 63;
            o[x] = cmap[c];
        }
    }
}

} // namespace display
