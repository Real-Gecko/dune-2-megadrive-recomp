// Port addition, not in the ROM: the battle drawn wider than the console's
// screen (host::kWideW x kWideH). The ground and overlay come straight from
// the map cells (as RedrawCell puts them into the planes) and the sprites
// from the sprite list (as BuildSpriteTable places them, without its
// screen culling and sprite limits). The game's 320 x 224 view sits in the
// middle (host::screen().ox/oy); its scroll limits are narrowed so the wide
// view stays on the map. The panel sprites keep to the right edge, the
// radar to the bottom right corner.
#include "game.hpp"

#include <algorithm>
#include <cstring>

namespace game {

namespace {

constexpr int kW = host::kWideW, kH = host::kWideH;
constexpr int OX = (kW - 320) / 2, OY = (kH - 224) / 2;
constexpr uint32_t kMap = 0xFF7D9C;

// A tile entry's pixel at (x, y) inside its 8x8 cell: 0 transparent, else
// palette line * 16 + colour.
inline int TilePixel(uint16_t e, int x, int y) {
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint8_t b = display::vram[((e & 0x7FF) * 32 + y * 4 + (x >> 1)) & 0xFFFF];
    int c = x & 1 ? b & 15 : b >> 4;
    return c ? ((e >> 13) & 3) * 16 + c : 0;
}

uint8_t sprites[kW * kH];

void DrawSprites(int vx, int vy) {
    std::memset(sprites, 0, sizeof sprites);
    for (uint16_t w = U16{0xFFF3AC}; w; w = rd16(Node(w) + 0xE)) {
        uint32_t n = Node(w);
        uint8_t f = rd8(n + 7);
        if ((f & 0x80) || ((f & 0x20) && (f & 0x10))) continue;
        uint32_t pos = rd32(n);
        int x = int16_t(rd16(pos)), y = int16_t(rd16(pos + 2));
        if (!(f & 0x40)) {
            int wy = x, wx = y;
            x = (uint16_t(wx) >> 3) - vx;
            y = (uint16_t(wy) >> 3) - vy;
            if (f & 4) {
                uint32_t t = 0x1164 + (rd8(pos + 0x68) & 7) * 4;
                x += int16_t(rd16(t));
                y += int16_t(rd16(t + 2));
            }
        } else if (pos == 0xFFBF4C || pos == 0xFFBF76) { // the radar
            x += kW - 320;
            y += kH - 224;
        } else if (pos != 0xFFBF12 && pos != 0xFFD306 && x >= 0xC0) { // the panel (not the cursor or the end scene's ornithopter)
            x += kW - 320;
        } else {
            x += OX;
            y += OY;
        }
        uint32_t piece = rd32(n + 8);
        uint16_t pieces = rd16(piece);
        piece += 2;
        for (int k = 0; k <= pieces; k++, piece += 12) {
            int py = y + int16_t(rd16(piece + 4)), px = x + int16_t(rd16(piece + 10));
            int size = rd16(piece + 6) >> 8;
            int sw = ((size >> 2) & 3) + 1, sh = (size & 3) + 1;
            if (px >= kW || py >= kH || px + sw * 8 <= 0 || py + sh * 8 <= 0) continue;
            uint16_t pa = rd16(piece + 8);
            uint16_t e = uint16_t((pa & 0x9FFF) ^ (rd16(n + 6) & 0xF800));
            if (pa & 0x6000) e = uint16_t((e & 0x9FFF) | pa);
            for (int ty = 0; ty < sh * 8; ty++) {
                int sy = py + ty;
                if (sy < 0 || sy >= kH) continue;
                int fy = e & 0x1000 ? sh * 8 - 1 - ty : ty;
                uint8_t* row = sprites + sy * kW;
                for (int cx = 0; cx < sw * 8; cx++) {
                    int sx = px + cx;
                    if (sx < 0 || sx >= kW || (row[sx] & 63)) continue;
                    int tx = e & 0x0800 ? sw * 8 - 1 - cx : cx;
                    uint16_t te = uint16_t((e & 0xE000) | ((e + (tx >> 3) * sh + (fy >> 3)) & 0x7FF));
                    if (int c = TilePixel(te, tx & 7, fy & 7)) row[sx] = uint8_t(c | (e & 0x8000 ? 0x40 : 0));
                }
            }
        }
    }
}

// One plane's tile entry at a map pixel (ground on plane B, the overlay on
// plane A), 0 off the map.
inline uint16_t GroundAt(uint32_t c, int wx, int wy) {
    return rd16(0x4ADF0 + uint32_t(((rd16(c) & 0x1FF) << 5) + ((wy >> 3) & 3) * 8 + ((wx >> 3) & 3) * 2));
}
inline uint16_t OverlayAt(uint32_t c, int wx, int wy) {
    uint16_t o = rd8(c) & 0xFE;
    if (U8{0xFFC198} && o >= 0x67) o = 0;
    return rd16(0x4ADF0 + uint32_t((o << 4) + ((wy >> 3) & 3) * 8 + ((wx >> 3) & 3) * 2));
}

} // namespace

bool DrawWide(uint32_t* px, int pitch) {
    if (vblank_hook != 0x6D10 && vblank_hook != 0x6092 && vblank_hook != 0xB544) return false; // 0xB544: the end scene
    if (display::cfg.win_h || display::cfg.win_v || display::cfg.width != 320) return false;
    uint32_t cmap[64];
    for (int i = 0; i < 64; i++) cmap[i] = display::color(display::cram[i]);
    uint32_t back = cmap[display::cfg.backdrop & 63];
    if (!display::cfg.enabled) {
        for (int y = 0; y < kH; y++) std::fill(px + y * pitch, px + y * pitch + kW, back);
        return true;
    }
    int vx = int16_t(U16{0xFFE3BE}) - OX, vy = int16_t(U16{0xFFE3C0}) - OY;
    DrawSprites(vx, vy);
    uint8_t a[kW + 8], b[kW + 8];
    for (int y = 0; y < kH; y++) {
        int wy = vy + y;
        bool row_in = wy >= 0 && wy < 64 * 32;
        for (int x = 0, wx = vx; x < kW; x += 8 - (wx & 7), wx = vx + x) {
            int n = std::min(8 - (wx & 7), kW - x);
            uint16_t ge = 0, oe = 0;
            if (row_in && wx >= 0 && wx < 64 * 32) {
                uint32_t c = kMap + uint32_t(((wy >> 5) << 6) + (wx >> 5)) * 4;
                ge = GroundAt(c, wx, wy);
                oe = OverlayAt(c, wx, wy);
            }
            for (int k = 0; k < n; k++) {
                int tx = (wx + k) & 7, ty = wy & 7;
                b[x + k] = ge ? uint8_t(TilePixel(ge, tx, ty) | (ge & 0x8000 ? 0x40 : 0)) : 0;
                a[x + k] = oe ? uint8_t(TilePixel(oe, tx, ty) | (oe & 0x8000 ? 0x40 : 0)) : 0;
            }
        }
        const uint8_t* s = sprites + y * kW;
        uint32_t* o = px + y * pitch;
        for (int x = 0; x < kW; x++) {
            int c = display::cfg.backdrop & 63;
            int layers[3] = {b[x], a[x], s[x]};
            for (int pri = 0; pri < 2; pri++)
                for (int l : layers)
                    if ((l & 63) && ((l >> 6) & 1) == pri) c = l & 63;
            o[x] = cmap[c];
        }
    }
    return true;
}

// After SetMapBounds: both views' scroll limits narrowed by the wide
// view's margins (a map narrower than the wide view keeps its middle).
void NarrowViewLimits() {
    if (!host::opt.wide) return;
    for (uint32_t v : {0xFFE3BEu, 0xFFE3ECu}) {
        auto narrow = [&](uint32_t pos, uint32_t lo, uint32_t hi, int m) {
            int l = int16_t(rd16(lo)) + m, h = int16_t(rd16(hi)) - m;
            if (l > h) l = h = (l + h) / 2;
            wr16(lo, uint16_t(l));
            wr16(hi, uint16_t(h));
            wr16(pos, uint16_t(std::clamp(int(int16_t(rd16(pos))), l, h)));
        };
        narrow(v, v + 0x14, v + 0x16, OX);
        narrow(v + 2, v + 0x10, v + 0x12, OY);
    }
}

} // namespace game
