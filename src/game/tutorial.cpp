// The tutorial (0x3067C): chapters of a script (0x418DC) that play
// animated pictures streamed into planes A and B, a pointer sprite and
// two-line captions. Start ends it, A, B or C skip a wait.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kSprites = 0xFFF5B0; // list header (pool at 0xFFF3B0)
constexpr uint32_t kPointer = 0xFFD70C; // x, y; node at +4, shape at +6
constexpr uint32_t kCaption = 0xFFDCAC; // x, y; node at +4
constexpr uint32_t kFont = 0x42D02;     // 8x8 tiles, 32 bytes each

void ShowSprites() {
    BuildSpriteTable(U16{kSprites});
    md::int_mask = 0;
}

// 0x13138
void ClearHook() {
    vblank_hook = 0;
    md::int_mask = 0;
}

// 0x130F4: the sprite pool and the pointer's tiles.
void InitPointer() {
    InitSpritePool(0xFFF3B0, kSprites);
    LoadTileList(0x13876, 0x5960);
    wr16(kPointer + 4, 0);
    wr8(kPointer + 6, 0xFF);
}

// 0x13124
void InstallHook() {
    vblank_hook = 0x13148;
    md::int_mask = 0;
}

// 0x1314E: the pointer at (x, y) in a shape from the table at 0x1321A.
void ShowPointer(uint16_t shape, uint16_t x, uint16_t y) {
    if (rd8(kPointer + 6) != shape) {
        wr8(kPointer + 6, uint8_t(shape));
        if (uint16_t n = rd16(kPointer + 4)) FreeSprite(Node(n), kSprites);
        uint32_t n = AllocSprite(kPointer, 0, rd32(0x1321A + int16_t(shape << 3) + 4), kSprites);
        wr16(kPointer + 4, uint16_t(n));
        U8{n + 7} |= 0x40;
    }
    wr16(kPointer, x);
    wr16(kPointer + 2, y);
    ShowSprites();
}

// 0x131C4
void RemovePointer() {
    InitSpritePool(0xFFF3B0, kSprites);
    md::int_mask = 0;
    wr16(kPointer, 0xFFFF);
    wr16(kPointer + 2, 0xFFFF);
    wr16(kPointer + 4, 0);
    ClearHook();
    WaitVBlank();
    SpriteTableHook();
}

// 0x131FC
void MovePointer(uint16_t x, uint16_t y) {
    wr16(kPointer, x);
    wr16(kPointer + 2, y);
    BuildSpriteTable(U16{kSprites});
}

// 0x41A72, 0x41AA4: the pointer slides 3 pixels a frame for 32 frames.
void SlidePointer(int16_t dy) {
    for (int i = 0; i < 0x20; i++) {
        wr16(kPointer + 2, uint16_t(rd16(kPointer + 2) + dy));
        ShowSprites();
        WaitVBlank();
    }
}

// 0x42ABA, 0x42A56: the caption tiles cleared, then its frame drawn.
void ClearCaption() {
    QueueDma(0x301, 0xFFFFFFFF, 0xCEC0u << 16 | 0x800);
    QueueDma(2, kFont + 0x120, 0xCE00u << 16 | 0x10);
    QueueDma(2, kFont + 0x160, 0xCE20u << 16 | 0x10);
    for (uint16_t at = 0xCE40; at < 0xCEC0; at = uint16_t(at + 0x20)) QueueDma(2, kFont + 0x140, uint32_t(at) << 16 | 0x10);
}

// 0x42A06: the text as tiles, the second line (after '\n') from 0xD2C0.
void DrawCaption(uint32_t s) {
    uint16_t at = 0xCEC0;
    while (uint8_t c = rd8(s++)) {
        if (c == '\n') {
            at = 0xD2C0;
            continue;
        }
        QueueDma(2, kFont + (rd8(0x42AD0 + c) << 5), uint32_t(at) << 16 | 0x10);
        at = uint16_t(at + 0x20);
    }
}

// 0x42984: the caption sprite at (x, y).
void PlaceCaption(uint16_t x, uint16_t y) {
    if (!rd16(kCaption + 4)) {
        uint32_t n = AllocSprite(kCaption, 0, 0x42B50, kSprites);
        wr16(kCaption + 4, uint16_t(n));
        U8{n + 7} |= 0x40;
    }
    wr16(kCaption, x);
    wr16(kCaption + 2, y);
    ShowSprites();
}

// 0x4295E
void ShowCaption(uint32_t s, uint16_t x, uint16_t y) {
    ClearCaption();
    DrawCaption(s);
    PlaceCaption(x, y);
}

// 0x429D2
void HideCaption() {
    if (uint16_t n = rd16(kCaption + 4)) {
        FreeSprite(Node(n), kSprites);
        wr16(kCaption + 4, 0);
        ShowSprites();
    }
}

// 0x418AC: waits n frames; -1 if Start is pressed, 0 at once for A, B or C.
uint32_t WaitOrSkip(uint16_t n) {
    uint16_t i = uint16_t(n - 1);
    do {
        uint32_t p = ReadPads();
        if (p & 0x80) return 0xFFFFFFFF;
        if (p & 0x70) return 0;
        WaitVBlank();
    } while (i-- != 0);
    return 0;
}

// Picture state: per plane (A at +0, B at +0xA) the source (0 none), the
// read offset into its unpacked frames and the map buffer.

// 0x4166A: picture n from the table at 0x416E2 (tiles, plane A and plane B
// frame data), unpacked to 0xFF1C00 and 0xFF7400.
void LoadPicture(uint32_t pic, uint16_t n) {
    uint32_t e = 0x416E2 + n * 12u;
    if (uint32_t t = rd32(e)) LoadTileList(t, 0);
    wr32(pic, rd32(e + 4));
    wr32(pic + 0xA, rd32(e + 8));
    wr32(pic + 6, 0xFF0000);
    wr32(pic + 0x10, 0xFF0E00);
    wr16(pic + 4, 0);
    wr16(pic + 0xE, 0);
    if (uint32_t s = rd32(pic)) Decompress(s, 0xFF1C00);
    if (uint32_t s = rd32(pic + 0xA)) Decompress(s, 0xFF7400);
}

// 0x417E8: one frame of run-length map changes into a 40-cell-wide window
// of a 64-cell map. Returns the bytes used, or -1 at the end.
uint32_t DecodeFrame(uint32_t src, uint32_t dst) {
    uint16_t col = 0;
    uint16_t len = uint16_t(rd8(src) << 8 | rd8(src + 1));
    if (len == 0xFFFF) return 0xFFFFFFFF;
    src += 2;
    uint16_t left = len;
    auto put = [&](uint16_t v) {
        wr16(dst, v);
        dst += 2;
        if (int16_t(++col) >= 0x28) {
            dst += 0x30;
            col = 0;
        }
    };
    auto word = [&]() {
        uint16_t v = uint16_t(rd8(src) << 8 | rd8(src + 1));
        src += 2;
        return v;
    };
    do {
        uint8_t c = rd8(src++);
        if (c == 0x80) { // skip cells
            uint8_t n = rd8(src++);
            left--;
            dst += n * 2u;
            col = uint16_t(col + n);
            if (int16_t(col) >= 0x28) {
                uint16_t rows = uint16_t(uint32_t(int32_t(int16_t(col))) / 0x28);
                dst += 0x30u * rows;
                col = uint16_t(uint32_t(int32_t(int16_t(col))) % 0x28);
            }
        } else if (c > 0x80) { // literal cells
            uint16_t n = c & 0x7F;
            left = uint16_t(left - n * 2);
            uint16_t k = uint16_t(n - 1);
            do put(word());
            while (k-- != 0);
        } else { // one cell repeated
            uint16_t v = word();
            left = uint16_t(left - 2);
            uint16_t k = uint16_t(c - 1);
            do put(v);
            while (k-- != 0);
        }
    } while (--left);
    return uint16_t(len + 2);
}

// 0x41772: the next frame of a plane, shown when show is set; -1 after the
// last.
uint32_t PictureFrame(uint32_t pic, uint16_t plane, uint16_t show) {
    uint32_t src = plane ? 0xFF7400 : 0xFF1C00;
    uint32_t p = pic + plane * 10u;
    if (!rd32(p)) return 0;
    src += int16_t(rd16(p + 4));
    uint32_t map = rd32(p + 6);
    uint32_t n = DecodeFrame(src, map);
    if (uint16_t(n) == 0xFFFF) return n;
    wr16(p + 4, uint16_t(rd16(p + 4) + n));
    if (show) QueueDma(2, map, uint32_t(rd16(plane ? 0xFFE024 : 0xFFE022)) << 16 | 0x700);
    return 0;
}

// 0x418DC: runs a chapter (table at 0x41916) until its end or an op that
// stops it (op table at 0x41936). Returns nonzero when stopped.
uint32_t RunChapter(uint32_t pic, uint16_t chapter) {
    uint32_t pc = rd32(0x41916 + chapter * 4u);
    if (!pc) return 0;
    uint32_t mark = 0;
    uint16_t count = 0, result = 0;
    auto w = [&]() {
        uint16_t v = rd16(pc);
        pc += 2;
        return v;
    };
    auto l = [&]() {
        uint32_t v = rd32(pc);
        pc += 4;
        return v;
    };
    for (;;) {
        uint16_t op = w();
        if (!op) return 0;
        uint32_t stop = 0;
        switch (op >> 1) {
        case 1: stop = WaitOrSkip(w()); break;
        case 2: LoadPicture(pic, w()); break;
        case 3: result = uint16_t(PictureFrame(pic, w(), 0xFFFF)); break;
        case 4: mark = pc; break;
        case 5:
            if (w() != result) pc = mark;
            break;
        case 6: pc = rd32(pc); break;
        case 7: FadeOutScreen(); break;
        case 8: FadeIn(l()); break;
        case 9:
            count = w();
            mark = pc;
            break;
        case 10:
            if (--count) pc = mark;
            break;
        case 11: {
            uint16_t plane = rd16(pc), k = uint16_t(rd16(pc + 2) - 1);
            uint32_t r;
            do r = PictureFrame(pic, plane, 0);
            while (k-- != 0);
            pc += 4;
            result = uint16_t(r);
            break;
        }
        case 12: PlaySfx(w()); break;
        case 13:
            wr16(pic + 4, 0);
            wr16(pic + 0xE, 0);
            break;
        case 14: wr16(pic + 4, 0); break;
        case 15: wr16(pic + 0xE, 0); break;
        case 16:
            InitPointer();
            InstallHook();
            break;
        case 17: {
            uint16_t y = w(), x = w(), shape = w();
            ShowPointer(shape, x, y);
            break;
        }
        case 18: RemovePointer(); break;
        case 19: {
            uint16_t y = w(), x = w();
            MovePointer(x, y);
            break;
        }
        case 20: SlidePointer(-3); break;
        case 21: SlidePointer(3); break;
        case 22: UploadPalette(l()); break;
        case 23: WaitVBlank(); break;
        case 24: {
            ShowCaption(pc, 0x1C, 0x20);
            while (rd8(pc++)) {}
            pc = (pc + 1) & ~1u;
            break;
        }
        case 25: HideCaption(); break;
        case 26: {
            FlushDma();
            uint16_t at = plane_b_base;
            for (int i = 0; i < 0x1C; i++, at = uint16_t(at + 0x80)) QueueDma(0x301, 0, uint32_t(at) << 16 | 0x50);
            break;
        }
        default: untranslated(0x41936 + op);
        }
        if (stop) return stop;
    }
}

} // namespace

// 0x3067C: chapters 0, 1, 3, 5, 6 and 7 with the screen set up for them;
// nonzero when Start ended it.
uint16_t Tutorial() {
    md::Frame pic(0x14);
    LoadGameSoundBanks();
    uint16_t delay = SetFadeDelay(5);
    uint32_t hook = SetVBlankHook(0);
    FadeOutScreen();
    ClearVram();
    uint16_t hscroll = SetHScrollTable(0xB800);
    uint16_t sprites = SetSpriteTable(0xBC00);
    uint16_t a = SetPlaneA(0xC000);
    uint16_t b = SetPlaneB(0xE000);
    SetScreenWidth(1);
    SetWindowPos(0, 0);
    SetPlaneSize(0, 1);
    SetPlaneAScroll(0);
    SetPlaneBScroll(0);
    InitPointer();
    wr16(kCaption + 4, 0);
    InstallHook();
    if (U8{0xFFDC50}) PlayMusic(5);
    uint16_t r = 0;
    for (uint16_t chapter : {0, 1, 3, 5, 6, 7})
        if (!r) r = uint16_t(RunChapter(pic.base, chapter));
    SetPlaneA(a);
    SetPlaneB(b);
    SetHScrollTable(hscroll);
    SetSpriteTable(sprites);
    SetVBlankHook(hook);
    SetFadeDelay(delay);
    return r;
}

} // namespace game
