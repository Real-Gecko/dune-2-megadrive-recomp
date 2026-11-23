// The end of a mission: the map scene with an ornithopter flying over
// (0x26952 won, 0x26A9A lost).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr uint32_t kOrni = 0xFFD306; // x, y; +4 flags; velocity at +6

// 0x4C72: the map cursor hidden.
void HideMapCursor() {
    if (uint16_t n = U16{0xFFBF18}) {
        U8{0xFFBF0C} &= uint8_t(~2);
        U8{Node(n) + 7} |= 0x80;
    }
}

// 0xB576: an ornithopter in the house's colours, from one of four
// directions.
void LaunchOrnithopter(uint16_t house) {
    uint16_t r = uint16_t(RandomRange(0, 3) << 2);
    wr32(kOrni + 6, rd32(0xB5CC + r));
    wr32(kOrni, rd32(0xB5DC + r));
    wr16(kOrni + 4, 0x1000);
    AllocSprite(kOrni, rd16(0xB5EC + house * 2u) | 0x40, rd32(0xB5BC + r), kSpriteList);
}

// 0x26984: the selection dropped, the radar off, off-screen infantry
// (types 0 and 1) removed; three seconds of the map running with the
// ornithopter and music track.
void EndScene(uint16_t house, uint16_t track) {
    U32{0xFFC578} = 0;
    U32{0xFFC25C} = 0;
    UpdatePanel();
    uint8_t radar = U8{0xFFBF7C};
    U8{0xFFBF7C} = 0;
    UpdateRadarFrame();
    HideMapCursor();
    DropSelection();
    InstallSpriteTableHook();
    for (int i = 0; i < 0x66; i++) {
        uint32_t u = rd32(0x4A85A + i * 4u);
        uint16_t f = rd16(u + 4);
        if (!(f & 2) || (f & 4) || OnScreen(rd32(u + 0xA))) continue;
        if (rd8(u + 2) == 1 || (rd8(u + 2) == 0 && rd8(u + 3) == 0xFF)) FreeUnit(u);
    }
    bool playing = false;
    LaunchOrnithopter(house);
    SetVBlankHook(0xB544);
    int16_t t = 0xB4;
    do {
        UpdateFrameDelta();
        GameTick();
        StructureTick();
        UpdateEffects();
        UpdateCellEffects();
        if (!playing && t < 0xFF) {
            PlayMusicTrack(track);
            playing = true;
        }
        t = int16_t(t - FrameDelta());
    } while (t > 0);
    FadeOutScreen();
    DisplayOff();
    U8{0xFFBF7C} = radar;
    UpdateRadarFrame();
}

// 0x9D0A: picture n (0 lost, 1 won) from the table at 0x9D84 (tiles for
// VRAM 0xB000, sprite frame, palette): its sprite at (0x80, 0xF0), its
// palette as line 3.
void ShowEndPicture(uint16_t n) {
    InitSpritePool(0xFFE6A8, kSpriteList);
    uint32_t e = 0x9D84 + n * 12u;
    QueueDma(2, rd32(e), 0xB00001C0);
    U32{0xFFC752} = 0x8000F0;
    uint32_t node = AllocSprite(0xFFC752, 0, rd32(e + 4), kSpriteList);
    U8{node + 7} |= 0x40;
    U8{0xFFC757} = 0;
    uint32_t pal = rd32(e + 8);
    QueueDma(0x102, pal, 0x600010);
    for (int i = 0; i < 16; i++) wr16(kPalette + 0x60 + i * 2u, rd16(pal + 0x60 + i * 2u));
}

// 0xA632: the picture rises a pixel every third call, up to y 0xB0.
void RaiseEndPicture() {
    U8{0xFFC757} -= 1;
    if (int8_t(U8{0xFFC757}) >= 0) return;
    uint32_t v = U32{0xFFC752};
    uint16_t y = uint16_t(v - 1);
    if (int16_t(y) < 0xB0) return;
    U32{0xFFC752} = (v & 0xFFFF0000) | y;
    U8{0xFFC757} = 2;
    BuildSpriteTable(U16{0xFFF3AC});
    SpriteTableHook();
}

// 0xC7E7C, 0xC7E46: frame n & 3 of the window's animation (tables 0xC5DFC
// won, 0xC5044 lost).
void ShowWindowFrame(uint32_t table, uint16_t n) {
    Decompress(rd32(table + (n & 3) * 4u), 0xFF0000);
    QueueDma(2, 0xFF0000, uint32_t(window_base) << 16 | 0x700);
}

// 0x128AE, 0x12996: screen, picture and window animation until a button.
void EndBriefing(uint16_t screen, uint16_t picture, uint32_t frames) {
    uint16_t frame = 1, tick = 0;
    FadeOutScreen();
    uint32_t old_hook = SetVBlankHook(0);
    DisplayOff();
    SetScreenWidth(1);
    SetWindowPos(0x14, 0x1E);
    SetWindowBase(0xD000);
    ClearVram();
    FadeIn(LoadScreen(screen));
    ShowEndPicture(picture);
    for (;;) {
        RaiseEndPicture();
        if (tick >= 0xB) {
            ShowWindowFrame(frames, frame);
            frame = (frame + 1) & 3;
            tick = 0;
        }
        tick++;
        uint16_t k = GetKey();
        if (k == 'A' || k == 'B' || k == 'C' || k == 'S') break;
        WaitVBlank();
    }
    FadeOutScreen();
    SetWindowPos(0, 0);
    ResetWindowBase();
    SetVBlankHook(old_hook);
}

} // namespace

void WinBriefing() { EndBriefing(0xB, 1, 0xC5DFC); }
void LoseBriefing() { EndBriefing(9, 0, 0xC5044); }

// 0xB544: VBlank hook of the end scene: game time runs, the ornithopter
// moves.
void EndSceneHook() {
    SpriteTableHook();
    U32{0xFFC594} += 1;
    U32{0xFFC598} += 1;
    U16{kOrni} += rd16(kOrni + 6);
    U16{kOrni + 2} += rd16(kOrni + 8);
    BuildSpriteTable(U16{0xFFF3AC});
}

// 0x26952
void WinScreen() {
    uint16_t h = player_house, track = 0;
    if (h == 0) track = 6;
    else if (h == 1) track = 7;
    else if (h == 2) track = 5;
    EndScene(h, track);
}

// 0x26A9A: before the last mission the ornithopter is the house that
// attacks there (0xFFC27C), which passes on when it has no structures but
// walls, slabs and the like.
void LoseScreen() {
    uint16_t track = 1; // the caller's d3
    if (U16{0xFFC050} == 7) {
        bool any = false;
        int16_t enemy = int16_t(U16{0xFFC27C});
        for (int i = 0; i < 0x49; i++) {
            uint32_t s = rd32(0x4A71E + i * 4u);
            if (int16_t(int8_t(rd8(s + 8))) != enemy) continue;
            uint8_t t = rd8(s + 2);
            if (t == 0 || t == 1 || t == 0xE || t == 0xF || t == 0x10) continue;
            any = true;
            break;
        }
        if (!any) {
            switch (U16{0xFFC27C}) {
            case 0:
                if (player_house == 2) U16{0xFFC27C} = 1;
                break;
            case 1:
                if (player_house == 0) U16{0xFFC27C} = 2;
                break;
            case 2:
                if (player_house == 1) U16{0xFFC27C} = 0;
                break;
            }
        }
        switch (player_house) {
        case 0: track = 3; break;
        case 1: track = 4; break;
        case 2: track = 2; break;
        }
    }
    EndScene(U16{0xFFC27C}, track);
}

namespace {

// Writes to VRAM as the original's data port writes (autoincrement 2).
struct VramWriter {
    uint16_t at;
    void put(uint16_t v) {
        display::vram_w16(at, v);
        at = uint16_t(at + 2);
    }
};

// 0x26C68: the score backdrop: tiles 0xA2E80 (seven chunks) and one of
// three plane B maps by scenario.
void LoadScoreBackdrop(uint16_t scen) {
    int16_t k = int16_t(scen);
    do k = int16_t(k - 3);
    while (k >= 0);
    k = int16_t(k + 3);
    uint32_t map = rd32(0x26C5C + k * 4u);
    uint16_t at = 0;
    for (int i = 0; i < 7; i++, at = uint16_t(at + 0x1000)) {
        Decompress(rd32(0xA2E80 + i * 4u), 0xFF0000);
        QueueDma(2, 0xFF0000, uint32_t(at) << 16 | 0x800);
        FlushDma();
    }
    Decompress(map, 0xFF0000);
    QueueDma(2, 0xFF0000, 0xC0000700);
    FlushDma();
}

// 0x26D3C: v in digits (tiles 0x4530 + digit) ending at the last cell
// from at; leading zeros only with pad.
void DrawNumber(uint32_t v, uint16_t at, uint16_t digits, bool pad) {
    uint16_t a = uint16_t(at + (digits - 1) * 2);
    display::vram_w16(a, uint16_t(0x4530 + v % 10));
    for (int n = int16_t(digits - 2); n >= 0; n--) {
        a = uint16_t(a - 2);
        v = (v / 10) & 0xFFFF;
        if (!pad && !v) break;
        display::vram_w16(a, uint16_t(0x4530 + v % 10));
    }
}

// 0x26DB2: n characters of s as tiles 0x4500 + character.
void DrawChars(uint32_t s, uint16_t n, uint16_t at) {
    VramWriter w{at};
    for (uint16_t i = 0; i < n; i++) w.put(uint16_t(0x4500 | rd8(s + i)));
}

// 0x26CE8: game time (frames) as hours and minutes.
void SplitTime(uint32_t t, uint32_t& hours, uint32_t& minutes) {
    uint32_t per_min = U8{0xFFE000} ? 0xBB8 : 0xE10;
    hours = uint32_t(int32_t(int16_t(uint16_t(t / per_min)))) / 0x3C;
    hours = uint32_t(int32_t(int16_t(uint16_t(hours))));
    uint32_t d = uint16_t(hours * 0x3C) * per_min;
    minutes = uint32_t(int32_t(int16_t(uint16_t((t - d) / per_min))));
}

// 0x26E3A: the score, the time and the rank (17 characters from 0xA68E2
// by the thresholds at 0xA68D2; the last one after the last scenario won).
void DrawScoreTop(uint32_t time, uint16_t score, uint16_t scen, uint16_t won) {
    QueueDma(2, 0x50C34, 0xA0000800);
    FlushDma();
    Decompress(0xA5D8E, 0xFF0000);
    QueueDma(2, 0xFF0000, 0xEE000700);
    FlushDma();
    DrawNumber(score, 0xEF92, 5, false);
    uint32_t hours, minutes;
    SplitTime(time, hours, minutes);
    DrawNumber(hours, 0xEFBC, 2, false);
    DrawNumber(minutes, 0xEFC2, 2, true);
    uint32_t rank = 0xA68E2;
    if (won && scen == 9) {
        rank = 0xA696A;
    } else {
        for (uint32_t t = 0xA68D2; score >= rd16(t); t += 2) rank += 0x11;
    }
    DrawChars(rank, 0x11, 0xF198);
}

// 0x27026: plane A scrolls up a screen, speeding up then slowing down.
void ScrollScoreUp() {
    uint32_t speed = 0x10000, pos = 0;
    auto show = [&] {
        WaitVBlank();
        display::vsram_w16(0, uint16_t(pos >> 16));
    };
    do {
        show();
        speed += 0x2000;
        pos += int32_t(speed) >= 0x100000 ? 0x100000 : speed;
    } while (int32_t(pos) < 0x700000);
    do {
        show();
        speed -= 0x2000;
        pos += int32_t(speed) <= 0x10000 ? 0x10000 : speed;
    } while (int32_t(pos) < 0xE00000);
    pos = 0xE00000;
    show();
}

// 0x270EE: the count-up step for a pair of values: one bar cell (8 steps)
// per 0xA8 of the larger (capped at 65000).
uint16_t CountStep(uint16_t a, uint16_t b) {
    if (a >= 0xFDE8) a = 0xFDE8;
    if (b >= 0xFDE8) b = 0xFDE8;
    if (b >= a) a = b;
    if (a <= 0xA8) return 1;
    return uint16_t(a / 0xA8 + 1);
}

// 0x26DEE: a bar len pixels long: full cells 0x17 + attr, the end 0x18 +
// attr + len & 7.
void DrawBar(uint16_t len, uint16_t at, uint16_t attr) {
    VramWriter w{at};
    for (int16_t n = int16_t(int16_t(len) >> 3); n > 0; n--) w.put(uint16_t(0x17 + attr));
    w.put(uint16_t(attr + 0x18 + (len & 7)));
}

// 0x26F98: a bar and its number counting up to value.
void CountUp(uint16_t value, uint16_t at, uint16_t attr, uint16_t step) {
    uint16_t len = 0, shown = 0;
    U16{0xFFDC48} = 0;
    do {
        WaitVBlank();
        DrawBar(len++, at, attr);
        DrawNumber(shown, uint16_t(at + 0x2C), 5, false);
        U16{0xFFDC48} += 1;
        if (!(U16{0xFFDC48} & 3)) PlaySoundEffect(0x34);
        shown = uint16_t(shown + step);
    } while (value > shown);
    DrawNumber(value, uint16_t(at + 0x2C), 5, false);
    for (int i = 0; i < 11; i++) WaitVBlank();
}

// 0x27120: the lower screen's map (VRAM 0xEE00) copied to the top, plane A
// scrolled back.
void CopyScoreDown() {
    for (int i = 0; i < 0x700; i++) wr16(0xFF0000 + i * 2u, display::vram_r16(uint16_t(0xEE00 + i * 2)));
    QueueDma(2, 0xFF0000, 0xE0000700);
    FlushDma();
    WaitVBlank();
    display::vsram_w16(0, 0);
}

// 0x26F0A: the next scenario's password and the house name.
void DrawPassword(uint16_t scen) {
    Decompress(0xA5E64, 0xFF0000);
    QueueDma(2, 0xFF0000, 0xEE000700);
    FlushDma();
    uint16_t i = uint16_t(uint16_t(scen - 1) * 0x18 + uint16_t(player_house << 3));
    DrawChars(rd32(0x88124 + i), 10, 0xF41E);
    DrawChars(0xA697C + uint16_t(player_house * 9), 9, 0xF220);
    DrawNumber(scen, 0xF32C, 1, false);
}

void WaitButton() {
    do WaitVBlank();
    while (!(ReadPads() & 0xF0));
}

// 0x11904: the column that centres a credits line (0xFF escapes take no
// room).
uint16_t CreditColumn(uint32_t s) {
    uint32_t p = s, skip = s;
    for (;;) {
        uint8_t c = rd8(p++);
        if (!c || c == '\n') break;
        if (c == 0xFF) {
            p++;
            skip += 2;
        }
    }
    return uint16_t(uint16_t(0x28 - uint16_t(p - skip - 1)) >> 1);
}

// 0x1188C: a credits line on plane A's row; 0xFF n switches to palette
// line n (kept at 0xFF0000).
void DrawCreditLine(uint16_t row, uint32_t s) {
    uint16_t col = CreditColumn(s);
    for (;;) {
        uint8_t c = rd8(s++);
        if (!c || c == '\n') break;
        if (c == 0xFF) {
            wr8(0xFF0000, rd8(s++));
            continue;
        }
        uint8_t line = rd8(0xFF0000);
        uint16_t attr = uint16_t(line >> 3 | line << 13);
        display::vram_w16(uint16_t(plane_a_base + row * 0x80 + col * 2), uint16_t((c & 0x7F) | 0x480 | attr));
        col++;
    }
}

// 0x117B8: the credits (0x11930) scroll up a pixel every three frames,
// a line every eight pixels; at their end the closing text (0x1193E)
// stays and a button stops them.
void Credits() {
    FlushDma();
    bool ended = false;
    uint16_t scroll = 0, row = 0x1C;
    uint32_t s = 0x11930;
    wr8(0xFF0000, 3);
    for (;;) {
        for (int i = 0; i < 0x28; i++) display::vram_w16(uint16_t(plane_a_base + row * 0x80 + i * 2), 0);
        DrawCreditLine(row, s);
        uint8_t c;
        do c = rd8(s++);
        while (c && c != '\n');
        if (!c) {
            ended = true;
            s = 0x1193E;
            continue;
        }
        row = (row + 1) & 0x1F;
        for (int px = 0; px < 8; px++) {
            for (int f = 0; f < 3; f++) {
                WaitVBlank();
                if (ended && GetKey()) return;
            }
            display::vsram_w16(0, ++scroll);
        }
    }
}

} // namespace

// 0x12734: the house's ending picture and music, then the credits.
void Finale(uint16_t house) {
    FadeOutScreen();
    SetVBlankHook(0);
    DisplayOff();
    display::vsram_w16(0, 0); // 0x200FA, 0x20120
    display::vsram_w16(1, 0);
    SetScreenWidth(1);
    SetWindowPos(0, 0);
    SetPlaneSize(0, 1);
    ResetWindowBase();
    ClearVram();
    LoadFont();
    FadeIn(LoadScreen(8));
    PlayMusicTrack(0x21);
    for (int i = 0; i < 0x96; i++) WaitVBlank();
    SetFadeDelay(7);
    PlayMusic(0x18);
    FadeToward(0x6C700, 1, 9);
    FadeToward(0x6C714 + uint16_t(house * 20), 1, 9);
    SetColor(2, 0xD, 0xEE);
    SetColor(2, 0xE, 0x66);
    SetColor(2, 0xF, 0);
    SetColor(3, 0xD, 0xEEE);
    SetColor(3, 0xE, 0x666);
    SetColor(3, 0xF, 0);
    for (int i = 0; i < 0x32; i++) WaitVBlank();
    Credits();
    FadeOutScreen();
}

// 0x271E2: the score screen of a scenario (1 up): score, time and rank,
// then three rows of player / enemy counts with bars; after a win (but the
// last) the next password.
void ScoreScreen(uint16_t scen, uint16_t score, uint32_t time, uint16_t a, uint16_t b, uint16_t kills, uint16_t losses,
                 uint16_t c, uint16_t d, uint16_t won) {
    uint16_t saved_delay = fade_delay;
    uint8_t saved_mask = pad_repeat_mask;
    pad_repeat_mask = 0;
    vblank_hook = 0;
    md::int_mask = 0;
    InitSpritePool(0xFFE6A8, kSpriteList);
    fade_delay = 3;
    FadeOutAll();
    // 0x27192
    ClearVram();
    ClearVsram();
    ClearPaletteRange(0, 0x40);
    ClearCram();
    SetWindowPos(0, 0);
    SetPlaneSize(1, 1);
    display::cfg.backdrop = 0;
    LoadScoreBackdrop(scen);
    FadeInAll(0xA5D0E);
    DrawScoreTop(time, score, scen, won);
    ScrollScoreUp();
    uint16_t step = CountStep(a, b);
    CountUp(a, 0xF494, 0x500, step);
    CountUp(b, 0xF514, 0x2500, step);
    step = CountStep(kills, losses);
    CountUp(kills, 0xF714, 0x500, step);
    CountUp(losses, 0xF794, 0x2500, step);
    step = CountStep(c, d);
    CountUp(c, 0xF994, 0x500, step);
    CountUp(d, 0xFA14, 0x2500, step);
    CopyScoreDown();
    WaitButton();
    if (won && int16_t(scen) < 9) {
        DrawPassword(scen);
        ScrollScoreUp();
        WaitButton();
    }
    StartSeq29();
    uint16_t delay = fade_delay;
    fade_delay = 3;
    FadeOutAll();
    fade_delay = delay;
    StopSeq29();
    SetPlaneSize(0, 1);
    pad_repeat_mask = saved_mask;
    fade_delay = saved_delay;
}

} // namespace game
