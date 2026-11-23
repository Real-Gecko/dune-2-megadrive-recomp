// The in-game VBlank hooks: the free map cursor (0x6D10) and the placement
// cursor (0x6092), both doing the frame's video work first (0x6716). The
// cursor record at 0xFFBF12: +0 x, +2 y (pixels on screen, 32 per cell),
// +8/+A and +C/+E its limits, +0x18 (0xFFBF2A) the button latched, +0x20
// (0xFFBF32) negative while a scripted scroll runs, +0x22/+0x24 the view
// scroll this frame, +0x26/+0x28 the scripted scroll left. 0xFFBF3C/3E
// fractions of x and y, 0xFFBF40 the cursor speed.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kCursor = 0xFFBF12;
constexpr U16 cur_x{0xFFBF12};
constexpr U16 cur_y{0xFFBF14};
constexpr U16 scroll_dx{0xFFBF34};
constexpr U16 scroll_dy{0xFFBF36};
constexpr U16 view_x{0xFFE3BE};
constexpr U16 view_y{0xFFE3C0};

// 0x6716: the sprite table, the scroll words, and the cursor's and its
// shadow's frames (0xFFBF04/0xFFBF08, uploaded a frame after they are set;
// bit 1 of 0xFFBF0C/0xFFBF0D shows the sprite then).
void FrameVideo() {
    SpriteTableHook();
    UploadScroll();
    if (uint32_t src = U32{0xFFBF04}) {
        if (!(U8{0xFFBF0C} & 1)) {
            U8{0xFFBF0C} |= 1;
        } else {
            if (U8{0xFFBF0C} & 2) {
                U8{0xFFBF0C} &= uint8_t(~2);
                if (uint16_t n = U16{0xFFBF18}) U8{Node(n) + 7} &= 0x7F;
            }
            for (int i = 0; i < 0x80; i++) display::vram_w16(uint16_t(0xF780 + i * 2), rd16((src & 0xFFFFFE) + i * 2u));
            U32{0xFFBF04} = 0;
            U8{0xFFBF0C} &= uint8_t(~1);
        }
    }
    if (uint32_t src = U32{0xFFBF08}) {
        if (!(U8{0xFFBF0D} & 1)) {
            U8{0xFFBF0D} |= 1;
        } else {
            if (U8{0xFFBF0D} & 2) {
                U8{0xFFBF0D} &= uint8_t(~2);
                if (uint16_t n = U16{0xFFBF60}) U8{Node(n) + 7} &= 0x7F;
            }
            for (int i = 0; i < 0x80; i++) display::vram_w16(uint16_t(0xF880 + i * 2), rd16((src & 0xFFFFFE) + i * 2u));
            U32{0xFFBF08} = 0;
            U8{0xFFBF0D} &= uint8_t(~1);
            wr8(0xFFBF7E, 0);
        }
    }
}

// The tick counters: 0xFFC58C counts down, 0xFFC590 the frame, 0xFFC594
// and 0xFFC598 (the game tick) count while 0xFFC59C / 0xFFC59D are set.
void CountTicks() {
    if (S32{0xFFC58C} > 0) U32{0xFFC58C} -= 1;
    U32{0xFFC590} = frame_counter;
    if (U8{0xFFC59C}) U32{0xFFC594} += 1;
    if (U8{0xFFC59D}) U32{0xFFC598} += 1;
}

// 0x5E2A: a screen shake (0xFFBEA4 the view to return to, 0xFFBEA8 frames
// left) jitters the view by up to 2 pixels; 1 while it runs.
bool Shake() {
    if (!U32{0xFFBEA4}) return false;
    if (!(rd8(0xFFE41B) & 0x20) && S16{0xFFBEA8} > 0) {
        auto jitter = [](U16 view, U16 home) {
            int16_t r = int16_t(int8_t(RandomRange(0, 4)));
            int16_t d = int16_t(r - 2 + view - home);
            return uint16_t(d > 4 || d < -4 ? -r : r);
        };
        scroll_dx = jitter(view_x, U16{0xFFBEA4});
        scroll_dy = jitter(view_y, U16{0xFFBEA6});
        U16{0xFFBEA8} -= 1;
        return true;
    }
    scroll_dx = uint16_t(U16{0xFFBEA4} - view_x);
    scroll_dy = uint16_t(U16{0xFFBEA6} - view_y);
    U32{0xFFBEA4} = 0;
    U16{0xFFBEA8} = 0;
    return true;
}

// 0x5984: a scripted scroll by up to 16 pixels a frame.
void ScriptedScroll() {
    auto step = [](int16_t v) { return int16_t(v > 0x10 ? 0x10 : v < -0x10 ? -0x10 : v); };
    int16_t dx = S16{kCursor + 0x26}, dy = S16{kCursor + 0x28};
    if (!dx && !dy) {
        wr16(kCursor + 0x20, 0);
        return;
    }
    dx = step(dx);
    dy = step(dy);
    scroll_dx = uint16_t(dx);
    U16{kCursor + 0x26} -= uint16_t(dx);
    scroll_dy = uint16_t(dy);
    U16{kCursor + 0x28} -= uint16_t(dy);
}

// The view follows a cursor coordinate outside [lo, hi] (16.16 steps of
// 1-3 pixels kept in a fraction), kept inside the map's scroll limits.
void Follow(U16 pos, U16 frac, U16 delta, U16 view, int16_t lo, int16_t hi, U16 vmin, U16 vmax) {
    int16_t p = int16_t(pos);
    int16_t d = 0;
    if (p < lo || p > hi) {
        int32_t v = int32_t(int16_t(p < lo ? p - lo : p - hi)) << 12;
        if (v < 0x10000 && v > -0x10000 && v != 0) v = v > 0 ? 0x10000 : -0x10000;
        if (v > 0x2C000) v = 0x30000;
        if (v < -0x2C000) v = -0x30000;
        uint32_t w = (uint32_t(uint16_t(p)) << 16 | frac) - uint32_t(v);
        frac = uint16_t(w);
        pos = uint16_t(w >> 16);
        d = int16_t(p - int16_t(pos));
    }
    delta = uint16_t(d);
    int16_t t = int16_t(d + view - vmin);
    if (t <= 0) {
        delta -= uint16_t(t);
        pos += uint16_t(t);
        return;
    }
    t = int16_t(int16_t(delta) + view - (vmax - 1));
    if (t >= 0) {
        delta -= uint16_t(t);
        pos += uint16_t(t);
    }
}

// The cursor moves by the pad (accelerating to 3 pixels a frame); with C
// held the view scrolls instead. A, B and Start latch their key.
// Returns true when the view was scrolled directly.
bool MoveCursor() {
    U8{0xFFE41E} &= 0xDF;
    uint16_t pad = uint16_t(ReadPads());
    int32_t dx = 0, dy = 0;
    int32_t up = int32_t(U32{0xFFBF40}) + 0x1000;
    if (uint32_t(up) >= 0x30000) up = 0x30000;
    U32{0xFFBF40} = uint32_t(up);
    int32_t down = -up;
    if (!(pad & 0xF) || !(pad & 0x20)) U16{0xFFBF4A} = 0x3C;
    if (pad & 0x20) {
        up = 0xE0000;
        down = int32_t(0xFFF20000);
        if (U16{0xFFBF4A}) {
            U16{0xFFBF4A} -= 1;
            up = 0x70000;
            down = int32_t(0xFFF90000);
        }
    }
    switch (pad & 0xF) {
    case 0: U32{0xFFBF40} = 0x10000; goto cell;
    case 1: dy = down; break;
    case 2: dy = up; break;
    case 4: dx = down; break;
    case 5: dx = down; dy = down; break;
    case 6: dx = down; dy = up; break;
    case 8: dx = up; break;
    case 9: dx = up; dy = down; break;
    case 10: dx = up; dy = up; break;
    default: return false;
    }
    if (pad & 0x20) {
        auto axis = [](U16 pos, U16 delta, U16 view, int16_t step, U16 vmin, U16 vmax, int16_t lo, int16_t hi) {
            int16_t v = int16_t(view + step);
            if (v < int16_t(vmin)) {
                pos += uint16_t(v - int16_t(vmin));
                if (int16_t(pos - lo) < 0) pos = uint16_t(lo);
                v = int16_t(vmin);
            }
            if (v >= int16_t(vmax)) {
                pos += uint16_t(v - int16_t(vmax));
                if (int16_t(pos) >= hi) pos = uint16_t(hi);
                v = int16_t(vmax - 1);
            }
            delta = uint16_t(v - view);
        };
        axis(cur_x, scroll_dx, view_x, int16_t(dx >> 16), U16{0xFFE3D2}, U16{0xFFE3D4}, 0x18, 0x128);
        axis(cur_y, scroll_dy, view_y, int16_t(dy >> 16), U16{0xFFE3CE}, U16{0xFFE3D0}, 0x18, 0xC8);
        return true;
    }
    {
        auto axis = [](U16 pos, U16 frac, U16 delta, int32_t step, int16_t lo, int16_t hi) {
            uint32_t f = uint32_t(frac) + uint16_t(step);
            frac = uint16_t(f);
            int16_t p = int16_t(int16_t(step >> 16) + int16_t(pos) + int16_t(f >> 16));
            int16_t d = int16_t(p - int16_t(pos));
            if (p < lo) {
                p = lo;
                delta = uint16_t(d);
            }
            if (p >= hi) {
                p = hi;
                delta = uint16_t(d);
            }
            return p;
        };
        int16_t x = axis(cur_x, U16{0xFFBF3C}, scroll_dx, dx, S16{kCursor + 8}, S16{kCursor + 0xC});
        int16_t y = axis(cur_y, U16{0xFFBF3E}, scroll_dy, dy, S16{kCursor + 0xA}, S16{kCursor + 0xE});
        cur_x = uint16_t(x);
        cur_y = uint16_t(y);
    }
cell: {
    uint32_t v = rd32(kCursor) + rd32(0xFFE3EC);
    U16{0xFFC240} = uint16_t(((v >> 16) >> 5) | ((v & 0x7E0) << 1));
}
    if (pad & 0x80) {
        wr8(kCursor + 0x18, 'S');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x40) {
        wr8(kCursor + 0x18, 'A');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x10) {
        wr8(kCursor + 0x18, 'B');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x20) {
        wr8(kCursor + 0x18, 'C');
    }
    return false;
}

void Finish() {
    U32{0xFFBF34} = 0;
    BuildSpriteTable(U16{0xFFF3AC});
    PlaceRadarFrame();
}

// The placement cursor's shape size (0x64F6: width, height words by the
// shape number at the start of its frame).
uint32_t ShapeSize() {
    uint32_t frame = rd32(Node(U16{kCursor + 6}) + 8);
    return 0x64F6 + uint16_t(rd16(frame) << 2);
}

// The placement cursor's pad step: like the free cursor's, but released
// between cells it goes on to the next 32-pixel line (0xFFBF48 the last
// direction), and a direction moving off a line keeps the other axis
// aligned. Returns true when the view was scrolled directly.
bool MovePlacementCursor() {
    U8{0xFFE41E} &= 0xDF;
    uint16_t pad = uint16_t(ReadPads());
    uint16_t dir = pad;
    for (;;) {
        uint16_t flags = dir;
        int32_t dx = 0, dy = 0;
        int32_t up = int32_t(U32{0xFFBF40}) + 0x1000;
        if (uint32_t(up) >= 0x30000) up = 0x30000;
        U32{0xFFBF40} = uint32_t(up);
        int32_t down = -up;
        if (!(flags & 0xF) || !(flags & 0x20)) U16{0xFFBF4A} = 0x3C;
        if (flags & 0x20) {
            up = 0xE0000;
            down = int32_t(0xFFF20000);
            if (U16{0xFFBF4A}) {
                U16{0xFFBF4A} -= 1;
                up = 0x70000;
                down = int32_t(0xFFF90000);
            }
        }
        auto align_x = [&]() {
            if (!((view_x + cur_x) & 0x1F)) return;
            uint16_t d = U16{0xFFBF48} & 0xC;
            flags |= d;
            dx = 0x10000;
            if (!(d & 8)) {
                flags |= 4;
                dx = -0x10000;
            }
        };
        auto align_y = [&]() {
            if (!((view_y + cur_y) & 0x1F)) return;
            uint16_t d = U16{0xFFBF48} & 3;
            flags |= d;
            dy = 0x10000;
            if (!(d & 2)) {
                flags |= 1;
                dy = -0x10000;
            }
        };
        switch (dir & 0xF) {
        case 0: {
            U32{0xFFBF40} = 0x10000;
            uint16_t last = U16{0xFFBF48};
            if (!last) goto cell;
            uint16_t d = 0;
            if ((view_x + cur_x) & 0x1F) d = last & 0xC;
            if ((view_y + cur_y) & 0x1F) d |= last & 3;
            if (d) {
                dir = d;
                continue;
            }
            goto cell;
        }
        case 1: dy = down; align_x(); break;
        case 2: dy = up; align_x(); break;
        case 4: dx = down; align_y(); break;
        case 8: dx = up; align_y(); break;
        case 5: dy = down; dx = down; break;
        case 6: dx = down; dy = up; break;
        case 9: dy = down; dx = up; break;
        case 10: dx = up; dy = up; break;
        default: return false;
        }
        U16{0xFFBF48} = flags;
        if (flags & 0x20) {
            uint32_t sz = ShapeSize();
            int16_t vx = int16_t(view_x + int16_t(dx >> 16));
            if (vx < S16{0xFFE3D2}) {
                cur_x += uint16_t(vx - S16{0xFFE3D2});
                if (int16_t(S16{kCursor + 8} - S16{kCursor}) >= 0) cur_x = U16{kCursor + 8};
                vx = S16{0xFFE3D2};
            }
            if (vx >= S16{0xFFE3D4}) {
                cur_x += uint16_t(vx - S16{0xFFE3D4});
                int16_t lim = int16_t(0x13F - int16_t(rd16(sz) * 2));
                if (lim < S16{kCursor}) cur_x = uint16_t(lim);
                vx = int16_t(S16{0xFFE3D4} - 1);
            }
            scroll_dx = uint16_t(vx - view_x);
            int16_t vy = int16_t(view_y + int16_t(dy >> 16));
            if (vy < S16{0xFFE3CE}) {
                cur_y += uint16_t(vy - S16{0xFFE3CE});
                if (int16_t(S16{kCursor + 0xA} - S16{kCursor + 2}) >= 0) cur_y = U16{kCursor + 0xA};
                vy = S16{0xFFE3CE};
            }
            if (vy >= S16{0xFFE3D0}) {
                cur_y += uint16_t(vy - S16{0xFFE3D0});
                int16_t lim = int16_t(0xDF - int16_t(rd16(sz + 2) * 2));
                if (lim < S16{kCursor + 2}) cur_y = uint16_t(lim);
                vy = int16_t(S16{0xFFE3D0} - 1);
            }
            scroll_dy = uint16_t(vy - view_y);
            uint32_t v = rd32(kCursor) + rd32(0xFFE3EC) + 0x100010;
            U16{0xFFC240} = uint16_t(((v >> 16) >> 5) | ((v & 0x7E0) << 1));
            return true;
        }
        {
            auto axis = [](U16 pos, U16 frac, U16 delta, int32_t step, int16_t lo) {
                uint32_t f = uint32_t(frac) + uint16_t(step);
                frac = uint16_t(f);
                int16_t p = int16_t(int16_t(step >> 16) + int16_t(pos) + int16_t(f >> 16));
                int16_t d = int16_t(p - int16_t(pos));
                if (p < lo) {
                    p = lo;
                    delta = uint16_t(d);
                }
                return p;
            };
            int16_t x = axis(cur_x, U16{0xFFBF3C}, scroll_dx, dx, S16{kCursor + 8});
            int16_t y = axis(cur_y, U16{0xFFBF3E}, scroll_dy, dy, S16{kCursor + 0xA});
            cur_x = uint16_t(x);
            cur_y = uint16_t(y);
        }
        break;
    }
cell: {
    uint32_t v = rd32(kCursor) + rd32(0xFFE3EC) + 0x100010;
    U16{0xFFC240} = uint16_t(((v >> 16) >> 5) | ((v & 0x7E0) << 1));
}
    if (pad & 0x80) {
        wr8(kCursor + 0x18, 'S');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x40) {
        wr8(kCursor + 0x18, 'A');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x10) {
        wr8(kCursor + 0x18, 'B');
        U16{0xFFC02C} = 1;
    } else if (pad & 0x20) {
        wr8(kCursor + 0x18, 'C');
    }
    return false;
}

// The view follows the placement cursor (thresholds by the shape's size;
// the far edge keeps the whole shape on the map).
void FollowShape(U16 pos, U16 frac, U16 delta, U16 view, int16_t lo, int16_t span, U16 vmin, U16 vmax, int16_t size,
                 int16_t edge) {
    int16_t p = int16_t(pos);
    int16_t d = 0;
    if (p < lo || p > lo + span) {
        int32_t v = int32_t(int16_t(p < lo ? p - lo : p - (lo + span))) << 12;
        if (v < 0x10000 && v > -0x10000 && v != 0) v = v > 0 ? 0x10000 : -0x10000;
        if (v > 0x2C000) v = 0x30000;
        if (v < -0x2C000) v = -0x30000;
        uint32_t w = (uint32_t(uint16_t(p)) << 16 | frac) - uint32_t(v);
        frac = uint16_t(w);
        pos = uint16_t(w >> 16);
        d = int16_t(p - int16_t(pos));
    }
    delta = uint16_t(d);
    int16_t t = int16_t(d + view - vmin);
    if (t <= 0) {
        delta -= uint16_t(t);
        pos += uint16_t(t);
        return;
    }
    t = int16_t(int16_t(delta) + view - (vmax - 1));
    if (t < 0) return;
    delta -= uint16_t(t);
    pos += uint16_t(t);
    t = int16_t(int16_t(delta) + view + int16_t(pos) + 2 * size - (vmax + edge));
    if (t >= 0) pos -= uint16_t(t);
}

} // namespace

// 0x6092: the placement cursor, as the free one but snapping to cells and
// keeping the structure's shape on the map. The scroll words are not
// cleared after.
void PlacementCursorHook() {
    FrameVideo();
    U8{0xFFBF69} = uint8_t((U8{0xFFBF69} + 1) & 7);
    if (!U8{0xFFBF69}) {
        U8{0xFFBF68} &= 7;
        uint8_t f = U8{0xFFBF68};
        U8{0xFFBF68} += 1;
        uint32_t src = 0x6910 + f * 0x80u;
        for (int i = 0; i < 0x40; i++) display::vram_w16(uint16_t(0x57C0 + i * 2), rd16(src + i * 2u));
    }
    CountTicks();
    if (!Shake() && !U8{0xFFBF7F} && !MovePlacementCursor()) {
        uint32_t sz = ShapeSize();
        int16_t w = int16_t(rd16(sz)), h = int16_t(rd16(sz + 2));
        FollowShape(cur_x, U16{0xFFBF3C}, scroll_dx, view_x, int16_t(0x78 - w), 0x50, U16{0xFFE3D2}, U16{0xFFE3D4}, w, 0x13F);
        FollowShape(cur_y, U16{0xFFBF3E}, scroll_dy, view_y, int16_t(0x52 - h), 0x3C, U16{0xFFE3CE}, U16{0xFFE3D0}, h, 0xDF);
    }
    ScrollView(int16_t(scroll_dx), int16_t(scroll_dy));
    BuildSpriteTable(U16{0xFFF3AC});
    PlaceRadarFrame();
}

// 0x6D10: the free cursor: video, the animated tiles (eight frames of
// 0x6910 into VRAM 0x57C0 every eighth frame), tick counters, then shake,
// scripted scroll or the pad, the view following the cursor.
void FreeCursorHook() {
    FrameVideo();
    U8{0xFFBF69} = uint8_t((U8{0xFFBF69} + 1) & 7);
    if (!U8{0xFFBF69}) {
        U8{0xFFBF68} &= 7;
        uint8_t f = U8{0xFFBF68};
        U8{0xFFBF68} += 1;
        uint32_t src = 0x6910 + f * 0x80u;
        for (int i = 0; i < 0x40; i++) display::vram_w16(uint16_t(0x57C0 + i * 2), rd16(src + i * 2u));
    }
    CountTicks();
    if (!Shake()) {
        if (U8{0xFFBF7F}) {
            Finish();
            return;
        }
        if (S16{kCursor + 0x20} < 0) {
            ScriptedScroll();
        } else if (MoveCursor()) {
            ScrollView(int16_t(scroll_dx), int16_t(scroll_dy));
            Finish();
            return;
        }
        Follow(cur_x, U16{0xFFBF3C}, scroll_dx, view_x, 0x78, 0xC8, U16{0xFFE3D2}, U16{0xFFE3D4});
        Follow(cur_y, U16{0xFFBF3E}, scroll_dy, view_y, 0x52, 0x8E, U16{0xFFE3CE}, U16{0xFFE3D0});
    }
    ScrollView(int16_t(scroll_dx), int16_t(scroll_dy));
    Finish();
}

} // namespace game
