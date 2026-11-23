// Sprite objects (0x1000-0x14B0): 16-byte nodes in RAM, linked by word
// addresses, turned into the sprite attribute table each frame.
//
// Node: +0 position pointer (y word, x word in 1/8 pixels; byte +4 the sort
// layer), +4 blink counter, +5 blink rate, +6 attribute word whose high
// bits (priority, palette, flips) apply to every piece, +7 flags (bit 7
// hidden, 6 screen coordinates, 5 blinking, 4 blink phase, 3 blink toggles,
// 2 offset by the owner's facing, 1 per-band sprite limit), +8 frame
// pointer, +0xC previous, +0xE next.
// Frame: word piece count - 1, then 12-byte pieces: width, height, y offset,
// size << 8, attribute word, x offset.
// List header: +0 pool, +2 free list, +4 active list.
#include "game.hpp"

namespace game {

namespace {

// Node links are movea.w: sign-extended, so RAM nodes are 0xFFFFxxxx.
uint32_t node(uint16_t w) { return uint32_t(int32_t(int16_t(w))); }
U16 prev(uint32_t n) { return U16{n + 0xC}; }
U16 next(uint32_t n) { return U16{n + 0xE}; }
U8 flags(uint32_t n) { return U8{n + 7}; }

// 0x12F6: sort key, layer << 16 | (y + first piece y offset).
uint32_t SortKey(uint32_t n) {
    uint32_t pos = rd32(n);
    uint32_t k = uint32_t(rd8(pos + 4)) << 16 | rd16(pos);
    return (k & 0xFFFF0000) | uint16_t(k + rd16(rd32(n + 8) + 2 + 4));
}

} // namespace

// 0x13B0
void StopBlink(uint32_t n) { flags(n) &= uint8_t(~0x20); }

// 0x13B8: blinks at a rate (0 keeps the node's rate).
void StartBlink(uint32_t n, uint8_t rate) {
    if (rate) wr8(n + 5, rate);
    if (rd8(n + 5)) flags(n) |= 0x20;
}

// 0x13F4, 0x43EA: moves a node along the active list until the keys run from
// high to low.
void SortSprite(uint32_t n, uint32_t list) {
    if (!rd32(n)) return;
    int32_t key = int32_t(SortKey(n));
    for (;;) {
        md::int_mask = 0;
        uint16_t p = prev(n), q = next(n);
        int32_t kp = p ? int32_t(SortKey(node(p))) : 0x3FFFFFFF;
        uint16_t kq_low = 0;
        int32_t kq = int32_t(0x80000000u);
        if (q) {
            kq = int32_t(SortKey(node(q)));
            kq_low = uint16_t(kq);
        }
        if (key <= kq) { // after its next
            uint16_t qn = next(node(q));
            next(n) = qn;
            if (qn) prev(node(qn)) = uint16_t(n);
            prev(n) = q;
            if (q) {
                next(node(q)) = uint16_t(n);
                prev(node(q)) = p;
            }
            if (p) next(node(p)) = q;
            continue;
        }
        if (key <= kp) break;
        // before its previous
        uint16_t pp = prev(node(p));
        prev(n) = pp;
        if (pp) next(node(pp)) = uint16_t(n);
        next(n) = p;
        if (p) {
            next(node(p)) = q;
            prev(node(p)) = uint16_t(n);
        }
        if (q != kq_low) prev(node(q)) = p; // the original's null test compares with the key
    }
    uint32_t a = node(U16{list + 4});
    while (uint16_t p = prev(a)) a = node(p);
    wr16(list + 4, uint16_t(a));
}

// 0x1000: takes a node from the free list and shows a frame at a position;
// 0 when none is free (the original returns the caller's a2).
uint32_t AllocSprite(uint32_t pos, uint32_t attr, uint32_t frame, uint32_t list) {
    uint16_t f = U16{list + 2};
    if (!f) return 0;
    uint32_t n = node(f);
    wr16(list + 2, next(n));
    wr32(n, pos);
    wr32(n + 4, attr);
    wr32(n + 8, frame);
    if (uint8_t rate = rd8(n + 5)) StartBlink(n, rate);
    else StopBlink(n);
    uint16_t head = U16{list + 4};
    prev(n) = head;
    if (!head) {
        next(n) = 0;
        wr16(list + 4, uint16_t(n));
        md::int_mask = 0;
        return n;
    }
    uint32_t h = node(head);
    next(n) = next(h);
    next(h) = uint16_t(n);
    if (uint16_t q = next(n)) prev(node(q)) = uint16_t(n);
    md::int_mask = 0;
    SortSprite(n, list);
    return n;
}

// 0x1360: unlinks a node from the active list into the free list.
void FreeSprite(uint32_t n, uint32_t list) {
    uint16_t p = prev(n), q = next(n);
    if (p) next(node(p)) = q;
    else wr16(list + 4, next(n));
    if (q) prev(node(q)) = p;
    prev(n) = 0;
    next(n) = U16{list + 2};
    if (uint16_t f = next(n)) prev(node(f)) = uint16_t(n);
    md::int_mask = 0;
    wr16(list + 2, uint16_t(n));
    wr32(n, 0);
}

// 0x1088, 0x431C: writes the sprite attribute table copy at kSpriteTable from the
// active list, starting at node first. Sprites are counted in 32-line bands;
// in crowded bands nodes with flag 1 alternate between frames.
void BuildSpriteTable(uint16_t first) {
    U8 building{0xFFF3AE};
    building = 0xFF;
    constexpr uint32_t cnt = 0xFFF700, mode = 0xFFF708, cells = 0xFFF710, cell_limit = 0xFFF718,
                       cnt_limit = 0xFFF720;
    for (int i = 0; i < 8; i++) {
        wr8(cnt + i, 0);
        wr8(cells + i, 0);
    }
    uint32_t out = kSpriteTable;
    wr32(out, 0);
    wr32(out + 4, 0);
    uint16_t count = 1;
    bool full = false;
    for (uint16_t w = first; w && !full; w = next(node(w))) {
        uint32_t n = node(w);
        uint8_t f = flags(n);
        if (f & 0x80) continue;
        if (f & 0x20) { // blinking: count down, then switch phase
            U8 counter{n + 4};
            bool skip;
            if (!(f & 0x10)) {
                if (counter) { counter--; skip = false; }
                else {
                    counter = rd8(n + 5);
                    skip = true;
                    if (f & 8) flags(n) |= 0x10;
                }
            } else {
                if (counter) { counter--; skip = true; }
                else {
                    counter = rd8(n + 5);
                    skip = false;
                    if (f & 8) flags(n) &= uint8_t(~0x10);
                }
            }
            if (skip) continue;
        }
        f = flags(n);
        uint32_t pos = rd32(n);
        int16_t x = int16_t(rd16(pos)), y = int16_t(rd16(pos + 2));
        if (!(f & 0x40)) {
            int16_t wy = x, wx = y; // world position: y at +0, x at +2
            x = int16_t((uint16_t(wx) >> 3) - U16{0xFFE3BE});
            y = int16_t((uint16_t(wy) >> 3) - U16{0xFFE3C0});
            if (f & 4) {
                uint32_t t = 0x1164 + (rd8(pos + 0x68) & 7) * 4; // the owner's facing
                x = int16_t(x + int16_t(rd16(t)));
                y = int16_t(y + int16_t(rd16(t + 2)));
            }
        }
        x = int16_t(x + 0x80);
        y = int16_t(y + 0x80);
        uint32_t piece = rd32(n + 8);
        uint16_t pieces = rd16(piece);
        piece += 2;
        for (int k = 0; k <= pieces; k++, piece += 12) {
            int16_t py = int16_t(y + int16_t(rd16(piece + 4))), ph = int16_t(rd16(piece + 2));
            int16_t px = int16_t(x + int16_t(rd16(piece + 10))), pw = int16_t(rd16(piece));
            if (px > 0x1BF || py > 0x15F || int16_t(px + pw) < 0x80 || int16_t(py + ph) < 0x80) continue;
            int16_t band = int16_t(py - 0x80);
            if (band >= -0x1F) {
                band = int16_t(band >> 5);
                if (band < 7) {
                    int b = band + 1;
                    U8{cnt + b}++;
                    U8{cells + b} += uint8_t(uint16_t(pw) >> 3);
                    if ((flags(n) & 2) && rd8(mode + b)) {
                        int8_t c = int8_t(rd8(cnt + b)), cl = int8_t(rd8(cnt_limit + b));
                        int8_t e = int8_t(rd8(cells + b)), el = int8_t(rd8(cell_limit + b));
                        if (int8_t(rd8(mode + b)) > 0) {
                            if (c <= cl || e <= el) continue;
                        } else if (c > cl && e > el) {
                            if (int8_t(cl * 2) >= c || int8_t(el * 2) >= e) continue;
                        }
                    }
                }
            }
            wr16(out, uint16_t(py));
            wr16(out + 2, uint16_t(rd16(piece + 6) + count));
            uint16_t pa = rd16(piece + 8);
            uint16_t a = uint16_t((pa & 0x9FFF) ^ (rd16(n + 6) & 0xF800));
            if (pa & 0x6000) a = uint16_t((a & 0x9FFF) | pa);
            wr16(out + 4, a);
            wr16(out + 6, uint16_t(px));
            if (++count > 0x50) { full = true; break; }
            out += 8;
        }
    }
    if (count >= 2) {
        U16 link{kSpriteTable + (count - 2) * 8u + 2};
        link &= 0xFF00;
    }
    uint8_t m = (vblank_count & 1) ? 0xE1 : 0x1F;
    for (int i = 7; i >= 0; i--) {
        wr8(mode + i, 0);
        if (int8_t(rd8(cells + i)) > 0x20 || int8_t(rd8(cnt + i)) > 0x10) {
            wr8(cell_limit + i, uint8_t(rd8(cells + i) - 0x20));
            wr8(cnt_limit + i, uint8_t(rd8(cnt + i) - 0x10));
            wr8(mode + i, m);
        }
    }
}

// 0x9510: builds a sprite of class cls (table at 0x60E8C: frame pointer,
// descriptor) into the object at obj, uploading its graphics when the frame
// pointer is flagged negative. Stores the allocated node at obj+6.
uint32_t CreateSprite(uint32_t obj, uint16_t cls, uint16_t orient) {
    uint32_t base = 0x60E8C + cls * 8u;
    uint32_t first = rd32(base);
    if (!first) return 0;
    if (int32_t(first) < 0) {
        uint32_t desc = rd32(base + 4);
        uint16_t hi = uint16_t(rd16(desc + 0xA) << 5);
        uint16_t len = rd16(0x94F0 + uint16_t(rd8(desc + 8) * 2));
        QueueDma(2, first, uint32_t(hi) << 16 | len);
    }
    uint32_t a2 = base + 4;
    uint8_t type = rd8(a2);
    wr8(obj + 4, type);
    uint32_t attr = 0;
    if (type != 0x0B) attr = rd16(0x9588 + uint16_t((orient & 7) * 2));
    if (int8_t(type) >= 0) attr |= 2;
    uint32_t n = AllocSprite(obj, attr, rd32(a2), 0xFFF3A8);
    wr16(obj + 6, uint16_t(n));
    return n;
}

// 0x9598: a node shows class cls (uploading its graphics when flagged)
// with flips (0-3) and the house's palette (0x9588) unless type 0xB.
void SetSpriteClass(uint16_t n, int16_t cls, uint16_t flip, uint16_t house) {
    if (!n) return;
    uint32_t a = Node(n), t = 0x60E8C + int32_t(cls) * 8;
    uint32_t desc = rd32(t + 4);
    if (int32_t(desc) <= 0) return;
    wr32(a + 8, desc);
    uint32_t first = rd32(t);
    if (int32_t(first) < 0) {
        uint16_t hi = uint16_t(rd16(desc + 0xA) << 5);
        uint16_t len = rd16(0x9610 + uint16_t(rd8(desc + 8) * 2));
        QueueDma(2, first, uint32_t(hi) << 16 | len);
    }
    uint16_t attr = uint16_t((flip & 3) << 11);
    if (rd8(t + 4) != 0xB) attr |= rd16(0x9588 + (house & 7) * 2u);
    wr16(a + 6, uint16_t((rd16(a + 6) & 0x87FF) | attr));
}

// 0x2EBFC, 0x2EC02: a tank's frame from body and turret directions (the
// tables at 0x2EC7A and 0x2F2FA, 8x8 frames of 26 bytes), house palette
// from 0x2EC5E.
void SetTankFrame(uint32_t table, uint16_t n, uint16_t body, uint16_t turret, uint16_t house) {
    if (!n) return;
    uint32_t a = Node(n);
    uint16_t d = uint16_t(((body + 0x10) & 0xE0) >> 1);
    uint16_t t = uint16_t(((turret + 0x10) & 0xE0) >> 1);
    wr32(a + 8, table + uint16_t(d * 13 + t + (t >> 1) + (t >> 3)));
    wr16(a + 6, uint16_t((rd16(a + 6) & 0x87FF) | rd16(0x2EC5E + house * 2u)));
}

// 0x967C: a node shows one of the frames at 0x641EE by direction and layer.
void SetDirFrame(uint16_t n, uint16_t facing, int16_t layer) {
    if (!n) return;
    uint32_t a = Node(n);
    uint16_t i = uint16_t((((facing + 0x10) & 0xE0) >> 3) + (layer << 5));
    wr32(a + 8, rd32(0x641EE + int16_t(i)));
    U8{a + 7} &= 0x7F;
}

// 0xA708: frame i of the four at 0xA742 with flips.
void SetFrame4(uint16_t n, uint16_t i, uint16_t flip) {
    if (!n) return;
    uint32_t a = Node(n);
    wr32(a + 8, rd32(0xA742 + (i & 3) * 4u));
    wr16(a + 6, uint16_t((rd16(a + 6) & 0xE7FF) | (flip & 3) << 11));
}

// 0xA6B2: the graphics of frame i of the four at 0xA6F8 uploaded to tile
// 0x3FE, with flips.
void UploadFrame4(uint16_t n, uint16_t i, uint16_t flip) {
    if (!n) return;
    uint32_t a = Node(n);
    QueueDma(2, rd32(0xA6F8 + (i & 3) * 4u), 0x7FC00100);
    wr16(a + 6, uint16_t((rd16(a + 6) & 0xE7FF) | (flip & 3) << 11));
}

} // namespace game
