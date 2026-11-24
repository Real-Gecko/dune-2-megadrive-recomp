// The asm library at 0x200-0x1900: DMA queue, palette fades, decompression,
// random numbers, pads, hardware init and VBlank.
#include "game.hpp"

#include "../assets.hpp"

#include <cstring>

namespace game {

// 0x38E: queues a DMA. cmd: high byte the kind (0 VRAM, 1 CRAM, 2 VSRAM from
// 68000 memory, 3 VRAM fill, 4 VRAM copy), low byte the address step.
// dest_len: destination << 16 | length in words. Transfers from 68000
// memory that cross a 128 KB boundary are split, as the DMA needs.
void QueueDma(uint16_t cmd, uint32_t src, uint32_t dest_len) {
    uint8_t saved = dma_enable;
    uint16_t next = dma_write;
    while (uint16_t(dest_len)) {
        dma_enable = 0;
        uint16_t at = dma_write;
        next = uint16_t(at + 10);
        if (next >= 0x280) next = 0;
        if (next == dma_read) { // queue full: let VBlank drain it
            dma_enable = 0xFF;
            WaitVBlank();
            continue;
        }
        uint32_t e = kDmaQueue + at;
        uint32_t s = src & 0xFFFFFF;
        wr16(e, cmd);
        wr32(e + 2, s);
        wr32(e + 6, dest_len);
        if (cmd >= 0x300) break;
        uint16_t bytes = uint16_t(dest_len << 1);
        uint32_t end = s + bytes;
        if ((end >> 16) == (s >> 16)) break;
        uint16_t first = uint16_t(bytes - uint16_t(end)); // bytes before the boundary
        src += int16_t(first); // adda.w
        dest_len = uint32_t(uint16_t((dest_len >> 16) + first)) << 16 | uint16_t(uint16_t(end) >> 1);
        wr16(e + 8, uint16_t(first >> 1));
        dma_write = next;
    }
    dma_write = next;
    dma_enable = saved;
}

// 0x82E: runs the queued DMAs from VBlank. With the display on, at most
// 0x1188 bytes go per VBlank; an entry over the limit is done in part and
// its remainder stays queued.
void RunDmaQueue() {
    const uint16_t limit = 0x1188;
    bool limited = vdp_mode2 & 0x40, stop = false;
    uint16_t total = 0;
    uint16_t r = dma_read;
    while (r != dma_write) {
        uint32_t e = kDmaQueue + r;
        uint16_t cmd = rd16(e), dest = rd16(e + 6), len = rd16(e + 8);
        uint32_t src = rd32(e + 2);
        int kind = cmd >> 8, step = cmd & 0xFF;
        if (kind <= 2) {
            total = uint16_t(total + len * 2);
            if (limited && total > limit) {
                uint16_t over = uint16_t(total - limit), part = uint16_t(len * 2 - over);
                if (!part) break;
                wr32(e + 2, src + part);
                wr16(e + 6, uint16_t(dest + part));
                wr16(e + 8, uint16_t(over >> 1));
                len = uint16_t(part >> 1);
                stop = true;
            }
            src &= 0xFFFFFE;
            for (uint16_t i = 0; i < len; i++) {
                uint16_t w = rd16(src + i * 2u);
                if (kind == 0) display::vram_w16(dest, w);
                else if (kind == 1) display::cram_w16((dest >> 1) & 63, w);
                else display::vsram_w16((dest >> 1) & 63, w);
                dest = uint16_t(dest + step);
            }
        } else {
            total = uint16_t(total + len);
            if (limited && total > limit) {
                uint16_t over = uint16_t(total - limit);
                len = uint16_t(len - over);
                if (!len) break;
                if (kind == 4) wr32(e + 2, src + len);
                wr16(e + 6, uint16_t(dest + len));
                wr16(e + 8, over);
                stop = true;
            }
            if (kind == 3) display::vram_fill(dest, uint16_t(src), len, step);
            else display::vram_move(dest, uint16_t(src), len, step);
        }
        if (stop) break;
        r = uint16_t(r + 10);
        if (r >= 0x280) r = 0;
    }
    dma_read = r;
}

// 0xFD4, 0x43F6
void WaitVBlank() {
    uint16_t v = vblank_count;
    while (vblank_count == v) host::frame();
}

// 0xFE4, 0x43FA: waits until VBlank has run every queued DMA.
void FlushDma() {
    dma_enable = 0xFF;
    while (dma_write != dma_read) host::frame();
    dma_read = 0;
    dma_write = 0;
}

// 0x7E0
void WaitFrames(uint16_t n) {
    while (n--) WaitVBlank();
}

// 0x41C
void ClearCram() { std::memset(display::cram, 0, sizeof display::cram); }
// 0x4E8, 0x4308
void ClearVram() { std::memset(display::vram, 0, sizeof display::vram); }
// 0x536
void ClearVsram() { std::memset(display::vsram, 0, sizeof display::vsram); }

// 0x496: clears work RAM, then fills the stack area (0xF3B0 up) with 0xFF.
void ClearRam() {
    std::memset(md::ram, 0, 0xFFDC);
    std::memset(md::ram + 0xF3B0, 0xFF, 0xFFF6 - 0xF3B0);
}

// 0xF56, 0x43D2: shows 64 colours (copied to the palette buffer first).
void UploadPalette(uint32_t pal) {
    if ((pal & 0xFFFFFF) != (kPalette & 0xFFFFFF))
        for (int i = 0; i < 32; i++) wr32(kPalette + i * 4, rd32(pal + i * 4));
    QueueDma(0x102, kPalette, 0x40);
}

// 0x47A: blacks out count colours from first.
void ClearPaletteRange(uint16_t first, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) wr16(kPalette + (first + i) * 2u, 0);
    UploadPalette(kPalette);
}

// The palette DMA and fade_delay frames of a fade step; false when the fade
// hook asked to stop (it returns with carry clear; hooks called here return
// the carry flag as their value).
static bool FadeStep(uint16_t first, uint16_t count, bool hook_can_stop) {
    QueueDma(0x102, kPalette + first * 2u, uint32_t(first * 2) << 16 | count);
    uint16_t n = uint16_t(fade_delay - 1);
    do {
        if (uint32_t h = fade_hook) {
            uint32_t carry = call(h);
            if (hook_can_stop && !carry) return false;
        }
        WaitVBlank();
    } while (n-- != 0);
    return true;
}

// Steps a component (mask) of c one level toward t; up only when up_only.
static uint16_t step_toward(uint16_t c, uint16_t t, uint16_t mask, uint16_t one, bool up_only) {
    uint16_t a = c & mask, b = t & mask;
    if (a == b) return a;
    if (up_only || a < b) return uint16_t(a + one);
    return uint16_t(a - one);
}

// 0x57A: fades count colours from black up to pal, first colour first.
void FadeTo(uint32_t pal, uint16_t first, uint16_t count) {
    FlushDma();
    uint32_t buf = kPalette + first * 2u, target = pal + first * 2u;
    for (uint16_t i = 0; i < count; i++) wr16(buf + i * 2u, 0);
    for (;;) {
        uint16_t same = 0;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t t = rd16(target + i * 2u), c = rd16(buf + i * 2u);
            if (t == c) same++;
            else c = uint16_t(step_toward(c, t, 0xE, 2, true) | step_toward(c, t, 0xE0, 0x20, true) |
                              step_toward(c, t, 0xE00, 0x200, true));
            wr16(buf + i * 2u, c);
        }
        FadeStep(first, count, false);
        if (same == count) return;
    }
}

// 0x628: fades count colours from first to black, starting from what is
// shown. (It reads 33 colours back whatever count is.)
void FadeOut(uint16_t first, uint16_t count) {
    FlushDma();
    uint32_t buf = kPalette + first * 2u;
    for (int i = 0; i <= 0x20; i++) wr16(buf + i * 2u, display::cram[(first + i) & 63]);
    for (;;) {
        uint16_t black = 0;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t c = rd16(buf + i * 2u);
            if (!c) black++;
            else {
                uint16_t g = c & 0xE0, b = c & 0xE00, r = c & 0xE;
                if (g) g = uint16_t(g - 0x20);
                if (b) b = uint16_t(b - 0x200);
                if (r) r = uint16_t(r - 2);
                c = uint16_t(r | g | b);
            }
            wr16(buf + i * 2u, c);
        }
        FadeStep(first, count, false);
        if (black == count) return;
    }
}

// 0x6C8, 0x430C: fades count colours from what the buffer holds toward pal; false
// when the fade hook stopped it.
bool FadeToward(uint32_t pal, uint16_t first, uint16_t count) {
    FlushDma();
    uint32_t buf = kPalette + first * 2u, target = pal + first * 2u;
    for (;;) {
        uint16_t same = 0;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t t = rd16(target + i * 2u), c = rd16(buf + i * 2u);
            if (t == c) same++;
            else c = uint16_t(step_toward(c, t, 0xE, 2, false) | step_toward(c, t, 0xE0, 0x20, false) |
                              step_toward(c, t, 0xE00, 0x200, false));
            wr16(buf + i * 2u, c);
        }
        if (!FadeStep(first, count, true)) return false;
        if (same == count) return true;
    }
}

// 0xA8A: display on, all 64 colours fade toward pal.
void FadeInAll(uint32_t pal) {
    FlushDma();
    display::cfg.enabled = true;
    vdp_mode2 = 0x8164;
    FadeToward(pal, 0, 0x40);
}

// 0xA8C
void FadeOutAll() { FadeOut(0, 0x40); }

// 0x7D2: count tiles from src to a VRAM address.
void UploadTiles(uint32_t src, uint16_t vram, uint16_t count) {
    QueueDma(2, src, uint32_t(vram) << 16 | uint16_t(count << 4));
}

// 0x7F0: a map of w x h entries (size = w << 16 | h) at cell pos (x << 16 | y)
// of a 64 cell wide nametable.
void BlitMap(uint32_t src, uint16_t nt, uint32_t pos, uint32_t size) {
    uint16_t at = uint16_t(nt + uint16_t((pos >> 16) + uint16_t(pos << 6)) * 2);
    uint16_t w = uint16_t(size >> 16);
    for (uint16_t row = uint16_t(size); row > 0; row--) {
        QueueDma(2, src, uint32_t(at) << 16 | w);
        src += w * 2u;
        at = uint16_t(at + 0x80);
    }
}

// 0xC32: Westwood LCW decompression from src to dest (the data files hold
// the ROM's packed data unpacked).
uint16_t Decompress(uint32_t src, uint32_t dest) {
    if (const std::vector<uint8_t>* u = assets::unpacked(src)) {
        for (uint8_t b : *u) wr8(dest++, b);
        return uint16_t(u->size());
    }
    uint32_t start = dest;
    auto copy = [&](uint32_t from, uint16_t n) {
        do wr8(dest++, rd8(from++));
        while (--n);
    };
    for (;;) {
        uint8_t c = rd8(src++);
        if (!(c & 0x80)) { // relative back reference
            uint16_t off = uint16_t((c & 0xF) << 8 | rd8(src++));
            copy(dest - off, uint16_t((c >> 4) + 3));
        } else if (!(c & 0x40)) {
            if (c == 0x80) return uint16_t(dest - start);
            copy(src, c & 0x3F); // literal bytes
            src += c & 0x3F;
        } else if (c == 0xFE) { // fill
            uint16_t n = uint16_t(rd8(src + 1) << 8 | rd8(src));
            uint8_t v = rd8(src + 2);
            src += 3;
            do wr8(dest++, v);
            while (--n);
        } else if (c == 0xFF) { // long absolute copy
            uint16_t n = uint16_t(rd8(src + 1) << 8 | rd8(src));
            uint16_t off = uint16_t(rd8(src + 3) << 8 | rd8(src + 2));
            src += 4;
            copy(start + off, n);
        } else { // absolute copy
            uint16_t off = uint16_t(rd8(src + 1) << 8 | rd8(src));
            src += 2;
            copy(start + off, uint16_t((c & 0x3F) + 3));
        }
    }
}

// 0xCD8, 0x438E: a list of tile sets (word count, bit 15 = compressed; long source;
// 0 ends) uploaded one after another from a VRAM address.
void LoadTileList(uint32_t list, uint16_t vram) {
    for (;;) {
        uint16_t n = rd16(list);
        list += 2;
        if (!n) return;
        uint32_t src = rd32(list);
        list += 4;
        bool packed = n & 0x8000;
        if (packed) {
            FlushDma();
            n &= 0x7FFF;
            Decompress(src, 0xFF0000);
            src = 0xFF0000;
        }
        UploadTiles(src, vram, n);
        vram = uint16_t(vram + (n << 5));
        if (packed) FlushDma();
    }
}

// 0xD20: decompresses to 0xFF0000 and uploads bytes to a VRAM address.
void UnpackTiles(uint32_t src, uint16_t vram, uint16_t bytes) {
    FlushDma();
    Decompress(src, 0xFF0000);
    QueueDma(2, 0xFF0000, uint32_t(vram) << 16 | uint16_t(bytes >> 1));
    FlushDma();
}

// 0xD54. (The ROM checksum test at 0x292C passes on a good dump.)
void InitHardware() {
    ClearRam();
    display::cfg.enabled = false;
    vdp_mode2 = 0x8124;
    ClearVram();
    ClearVsram();
    ClearCram();
    display::cfg.backdrop = 0;
    display::cfg.hmode = 0;
    display::cfg.vcolumns = false;
    display::cfg.plane_w = 64;
    display::cfg.plane_h = 32;
    screen_width = 0x140;
    display::cfg.width = 320;
    display::cfg.win_h = 0;
    display::cfg.win_v = 0;
    vdp_autoinc = 0x8F02;
    display::cfg.plane_a = 0xE000;
    plane_a_base = 0xE000;
    display::cfg.plane_b = 0xC000;
    plane_b_base = 0xC000;
    display::cfg.window = 0x6000;
    window_base = 0x6000;
    display::cfg.sprites = 0xF000;
    sprite_base = 0xF000;
    display::cfg.hscroll = 0xF400;
    hscroll_base = 0xF400;
    random_seed = 0x349F285A;
    fade_delay = 8;
    GemsInit(0x1792, 0x1792, 0x1792, 0x1792);
}

// 0xE42, 0x439A
uint8_t Random() {
    uint8_t b1 = rd8(0xFFE017), b2 = rd8(0xFFE018), b3 = rd8(0xFFE019);
    uint8_t d = uint8_t(b3 >> 2);
    int x = (b3 >> 1) & 1;
    uint8_t n1 = uint8_t(b1 << 1 | x);
    x = b1 >> 7;
    uint8_t n2 = uint8_t(b2 << 1 | x);
    x = (b2 >> 7) ^ 1;
    d = uint8_t(d - b3 - x);
    uint8_t n3 = uint8_t((d & 1) << 7 | b3 >> 1);
    wr8(0xFFE017, n1);
    wr8(0xFFE018, n2);
    wr8(0xFFE019, n3);
    return uint8_t(n3 ^ n2);
}

// 0xC0C, 0x4382: a random number from lo to hi.
uint8_t RandomRange(uint8_t lo, uint8_t hi) {
    uint8_t r = Random(), span = uint8_t(hi - lo + 1), mask = 0xFF;
    while (span <= r) {
        mask >>= 1;
        r &= mask;
        if (!r) break;
    }
    return uint8_t(r + lo);
}

// 0xEC0: pad state: held, and pressed = held minus the repeat-masked buttons
// that were already held. Returns pad 2 pressed << 16 | pad 1 pressed.
uint32_t ReadPads() {
    if (pad_hooked) {
        uint32_t v = 0;
        if (uint32_t h = pad_hook) {
            v = call(h);
            wr32(0xFFE41A, v);
        }
        return v;
    }
    auto one = [](U8 pressed, U8 held, uint8_t now) {
        uint8_t keep = uint8_t(~(held & pad_repeat_mask));
        held = now;
        pressed = uint8_t(now & keep);
    };
    one(pad1, pad1_held, host::pad());
    one(pad2, pad2_held, 0);
    return uint32_t(uint8_t(pad2)) << 16 | uint8_t(pad1);
}

// 0x131E, 0x4376: links the 16-byte sprite objects of a pool (next at +0xE, previous
// at +0xC) into the free list of a list header.
void InitSpritePool(uint32_t pool, uint32_t list) {
    wr16(list, uint16_t(pool));
    wr16(list + 2, uint16_t(pool));
    wr32(list + 4, 0);
    wr16(pool + 0xC, 0);
    uint32_t a = pool, b = pool + 0x10;
    for (;;) {
        wr32(a, 0);
        wr32(a + 4, 0);
        wr32(a + 8, 0);
        wr16(a + 0xE, uint16_t(b));
        wr16(b + 0xC, uint16_t(a));
        a = b;
        b = a + 0x10;
        if (uint16_t(b) == uint16_t(list)) break;
    }
    wr32(a, 0);
    wr32(a + 4, 0);
    wr32(a + 8, 0);
    wr16(a + 0xE, 0);
}

// 0x1864: the logo on plane A with its shimmering colours 2-12.
void BootLogo() {
    wr8(0xFFF3A0, 0xFF); // st.b 8(a0) with a0 left at the last sprite object
    LoadTileList(0x18CE, 0);
    UnpackTiles(0x1D2A, plane_a_base, 0x1000);
    UploadPalette(0x1DA4);
    for (int pass = 0; pass < 3; pass++)
        for (int16_t off = 0x28;; off = int16_t(off - 2)) {
            WaitFrames(3);
            if (host::press_pending()) return; // port: skips to the title menu
            if (off < 0) break;
            for (int i = 0; i < 11; i++) wr16(kPalette + 4 + i * 2u, rd16(0x18D6 + off + i * 2u));
            UploadPalette(kPalette);
        }
}

// 0x17F4
void VBlank() {
    int mask = md::int_mask;
    md::int_mask = 7;
    if (dma_enable) RunDmaQueue();
    if (uint32_t h = vblank_hook) call(h);
    vblank_count++;
    frame_counter++;
    if (vblank_timer) vblank_timer--;
    int8_t n = int8_t(rd8(kSoundQueue));
    if (n > 0) {
        int left = n & 3, i = 0;
        do {
            uint8_t s = rd8(kSoundQueue + 1 + i++);
            if (s >= 0x14 && s <= 0x3C) GemsStartSeq(s);
        } while (--left > 0);
    }
    wr32(kSoundQueue, 0);
    md::int_mask = mask;
}

} // namespace game
