// The C-callable library wrappers at 0x420C-0x4A04 (stack arguments).
#include "game.hpp"

namespace game {

// 0x420C: steps one shown colour (line, index) a level per component toward
// target, straight in CRAM; true once it is there.
bool FadeColor(uint16_t line, uint16_t index, uint16_t target) {
    int i = (line * 16 + index) & 63;
    uint16_t c = display::cram[i] & 0xEEE;
    md::int_mask = 0;
    if (target == c) return true;
    auto step = [&](uint16_t mask, uint16_t one) {
        uint16_t want = target & mask, have = c & mask;
        if (want == have) return;
        have = want < have ? uint16_t(have - one) : uint16_t(have + one);
        c = uint16_t((c & (0xEEE & ~mask)) | have);
    };
    step(0xE00, 0x200);
    step(0xE0, 0x20);
    step(0xE, 2);
    display::cram_w16(i, c);
    return target == c;
}

// 0x43FE: whether (px, py) lies within 0x100 of (x, y) (16-bit compares);
// the value is x - 0x100 in the low word (so 0 for x = 0x100 even inside).
uint32_t NearPoint(int16_t x, int16_t y, int16_t px, int16_t py) {
    int16_t x0 = int16_t(x - 0x100), y0 = int16_t(y - 0x100), x1 = int16_t(x + 0x100), y1 = int16_t(y + 0x100);
    if (px < x0 || py < y0 || px > x1 || py > y1) return 0;
    return (uint32_t(int32_t(x)) & 0xFFFF0000) | uint16_t(x0);
}

// 0x4434: plane size codes (0 32 cells, 1 64, 3 128).
void SetPlaneSize(uint16_t vcode, uint16_t hcode) {
    static const int cells[4] = {32, 64, 32, 128};
    display::cfg.plane_h = cells[vcode & 3];
    display::cfg.plane_w = cells[hcode & 3];
}

// 0x444C, 0x446A, 0x4488, 0x44A6: table addresses; return the old one.
uint16_t SetPlaneA(uint16_t a) {
    uint16_t old = plane_a_base;
    plane_a_base = a;
    display::cfg.plane_a = a & 0xE000;
    return old;
}
uint16_t SetPlaneB(uint16_t a) {
    uint16_t old = plane_b_base;
    plane_b_base = a;
    display::cfg.plane_b = a & 0xE000;
    return old;
}
uint16_t SetHScrollTable(uint16_t a) {
    uint16_t old = hscroll_base;
    hscroll_base = a;
    display::cfg.hscroll = a & 0xFC00;
    return old;
}
uint16_t SetSpriteTable(uint16_t a) {
    uint16_t old = sprite_base;
    sprite_base = a;
    display::cfg.sprites = a & 0xFE00;
    return old;
}

// 0x44C4: count colours straight into CRAM from (line, index).
void WriteCram(uint16_t line, uint16_t index, uint16_t count, uint32_t src) {
    int i = line * 16 + index;
    for (uint16_t k = 0; k < count; k++) display::cram_w16(i + k, rd16(src + k * 2u));
    md::int_mask = 0;
}

// 0x4504: VBlank hook that shows the sprite table copy.
void SpriteTableHook() {
    uint16_t dest = sprite_base;
    for (int i = 0; i < 0x140; i++) display::vram_w16(uint16_t(dest + i * 2), rd16(kSpriteTable + i * 2u));
}

// 0x4616
uint32_t InstallSpriteTableHook() {
    uint32_t old = vblank_hook;
    vblank_hook = 0x4504;
    md::int_mask = 0;
    return old;
}

// 0x4796
uint32_t SetVBlankHook(uint32_t hook) {
    uint32_t old = vblank_hook;
    vblank_hook = hook;
    md::int_mask = 0;
    return old;
}

// 0x4600: clears 0xAFF8 bytes from 0xFF1000.
void ClearMapRam() {
    for (uint32_t a = 0xFF1000; a < 0xFF1000 + 0x15DF * 8; a += 4) wr32(a, 0);
}

// 0x45E2: clears 0xA0 bytes at the two RAM tables the ROM longs 0x4A9FA and
// 0x4AA0E point to.
void ClearUnitLists() {
    uint32_t a = rd32(0x4A9FA), b = rd32(0x4AA0E);
    for (int i = 0; i < 0x14 * 8; i += 4) {
        wr32(a + i, 0);
        wr32(b + i, 0);
    }
}

constexpr U16 last_vblank{0xFFE008};
constexpr U16 frame_delta{0xFFFFFE};

// 0x4668: VBlanks since the last call (waits for one if none), counted off
// two game timers.
void UpdateFrameDelta() {
    if (last_vblank == vblank_count) WaitVBlank();
    uint16_t d = uint16_t(vblank_count - last_vblank);
    frame_delta = d;
    U16{0xFFC574} -= d;
    if (S16{0xFFBF88} > 0) U16{0xFFBF88} -= d;
    last_vblank = vblank_count;
}

// 0x4750
void ResetFrameDelta() {
    FlushDma();
    WaitVBlank();
    vblank_count = 0;
    last_vblank = 0;
    frame_delta = 0;
}

// 0x4768
void SyncFrameDelta() { last_vblank = vblank_count; }

// 0x47D6
uint16_t FrameDelta() { return frame_delta; }

// 0x471A: waits a VBlank, reads the pads; Start/A/C/B newly pressed (pad
// state then cleared so held buttons count again).
uint16_t ReadButtons() {
    WaitVBlank();
    ReadPads();
    uint16_t b = pad1 & 0xF0;
    wr16(0xFFE41A, 0);
    return b;
}

// 0x472E: the palette buffer from what CRAM shows.
void ReadShownPalette() {
    for (int i = 0; i < 64; i++) wr16(kPalette + i * 2u, display::cram[i]);
}

// 0x4772: palette line bits of a sprite node from a table of 5.
void SetSpritePalette(uint16_t node, uint16_t n) {
    U16 attr{uint32_t(int32_t(int16_t(node))) + 6};
    attr &= 0x9FFF;
    attr |= rd16(0x478C + n * 2u);
}

// 0x4366, 0x43DA
void HideSprite(uint16_t node) {
    if (node) U8{uint32_t(int32_t(int16_t(node))) + 7} |= 0x80;
}
void ShowSprite(uint16_t node) {
    if (node) U8{uint32_t(int32_t(int16_t(node))) + 7} &= 0x7F;
}

// 0x43A6: 320 pixels wide flag; off switches the screen to 256.
void SetScreenWidth(uint16_t wide) {
    if (wide) {
        screen_width = 0x140;
        return;
    }
    screen_width = 0x100;
    display::cfg.width = 256;
}

// 0x43C6
uint16_t SetFadeDelay(uint16_t n) {
    uint16_t old = fade_delay;
    fade_delay = n;
    return old;
}

// 0x4346, 0x4356
void DisplayOff() {
    display::cfg.enabled = false;
    vdp_mode2 = 0x8124;
}
void DisplayOn() {
    display::cfg.enabled = true;
    vdp_mode2 = 0x8164;
}

// 0x4324, 0x4334: backdrop colour 0, then the full-palette fades.
void FadeIn(uint32_t pal) {
    display::cfg.backdrop = 0;
    FadeInAll(pal);
}
void FadeOutScreen() {
    display::cfg.backdrop = 0;
    FadeOutAll();
}

// 0x46A8: the value of a named entry of the table at 0x52C34 (name pointer,
// value; 0 ends), 0 if absent.
uint32_t FindNamed(uint32_t name) {
    for (uint32_t t = 0x52C34;; t += 8) {
        uint32_t s = rd32(t);
        if (!s) return 0;
        uint32_t a = name;
        for (;;) {
            uint8_t c = rd8(a++);
            if (c != rd8(s++)) break;
            if (!c) return rd32(t + 4);
        }
    }
}

// 0x46DC: in resource file n of the table at 0x52C2C, the data of the chunk
// with an id (chunks: id, length, data padded to even); its id when absent
// is the negative end marker.
uint32_t FindChunk(int16_t file, uint32_t id) {
    uint32_t a = rd32(0x52C2C + int32_t(file) * 8 + 4) + 0xC;
    for (;;) {
        uint32_t c = a;
        a += 8;
        uint32_t cid = rd32(c);
        if (int32_t(cid) < 0) return cid;
        if (cid == id) return a;
        a += (rd32(c + 4) + 1) & 0xFFFFFFFE;
    }
}

// 0x47AA: a position plus one of four random offsets of 0x10.
uint32_t RandomCorner(uint32_t pos) { return rd32(0x47BE + RandomRange(0, 3) * 4u) + pos; }

// 0x49C8: n * 64 frames.
void WaitLong(uint16_t n) { WaitFrames(uint16_t(n << 6)); }

// 0x49D4, 0x49EC: one-shot flags at 0xFFC59B: read and clear, read and set.
uint8_t TakeFlag(uint16_t i) {
    U8 f{0xFFC59Bu + i};
    uint8_t v = f;
    f = 0;
    return v;
}
uint8_t TestAndSetFlag(uint16_t i) {
    U8 f{0xFFC59Bu + i};
    uint8_t v = f;
    f = 0xFF;
    return v;
}

// 0x439E
void SetPadRepeatMask(uint16_t m) { pad_repeat_mask = uint8_t(m); }

// 0x4340
uint32_t FrameCounter() { return frame_counter; }

// 0x431C: the sprite table from the game's sprite list.
void BuildGameSprites() { BuildSpriteTable(U16{0xFFF3AC}); }

// 0x4A24: all colours fade in toward pal, or out when pal is 0.
void FadeAll(uint32_t pal) {
    if (pal) FadeInAll(pal);
    else FadeOutAll();
}

} // namespace game
