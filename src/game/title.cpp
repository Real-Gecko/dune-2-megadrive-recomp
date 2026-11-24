// Title screen (0x17418): logo scroll, "PRESENT", the house intros and the
// START GAME / OPTIONS / TUTORIAL menu (0x17A20-0x17F2E).
#include "game.hpp"

#include <algorithm>
#include <cstdio>

namespace game {

// 0x17B6C, 0x17BA4: horizontal scroll of plane B / plane A (whole screen).
void SetPlaneBScroll(uint16_t v) { display::vram_w16(uint16_t(hscroll_base + 2), v & 0x7FF); md::int_mask = 0; }
void SetPlaneAScroll(uint16_t v) { display::vram_w16(hscroll_base, v & 0x7FF); md::int_mask = 0; }

namespace {

// Title cursor / intro sprite: x, y words, node at +4, frame index at +6,
// house at +7.
constexpr uint32_t kCursor = 0xFFD70C;
constexpr uint32_t kTitleSprites = 0xFFF5B0; // their list header
constexpr S32 scroll_pos{0xFFD6FC};          // plane B scroll, 16.16
constexpr S32 scroll_target{0xFFD700};
constexpr S32 scroll_speed{0xFFD704};
constexpr U16 scroll_slowing{0xFFD708};
constexpr U8 scroll_running{0xFFD70A};
constexpr U8 scroll_repeat{0xFFD70B};
constexpr U8 music_on{0xFFDC50};
constexpr U8 sfx_on{0xFFDC51};
constexpr U16 player_house{0xFFC274};

U16 cursor_node{kCursor + 4};

// 0x17D52: clears plane A (64 x 64 cells of it).
void ClearPlaneA() {
    WaitVBlank();
    for (int i = 0; i < 0x2000; i += 2) display::vram_w16(uint16_t(plane_a_base + i), 0);
    md::int_mask = 0;
}

// 0x17BDA: plane B scrolls left until it stops at target.
void StartTitleScroll(uint16_t target) {
    scroll_running = 1;
    scroll_repeat = 1;
    scroll_target = int32_t(uint32_t(target) << 16);
    scroll_pos = -1;
    scroll_slowing = 0;
    scroll_speed = 0x8000;
    vblank_hook = 0x17C36;
    md::int_mask = 0;
}

// 0x17C26
void StopVBlankHook() {
    vblank_hook = 0;
    md::int_mask = 0;
}

// 0x17D1A: 2 when a button was pressed, 1 once the scroll stopped, else 0.
uint16_t TitleScrollState() {
    WaitVBlank();
    if ((ReadPads() & 0xF0) || host::press_pending()) return 2;
    return scroll_pos <= scroll_target ? 1 : 0;
}

// 0x17D9C: an empty sprite list, a second's wait, the intro tiles.
void PrepareHouseIntros() {
    InitSpritePool(0xFFF3B0, kTitleSprites);
    WaitFrames(0x3D);
    LoadTileList(0x17F2E, 0x6200);
}

// 0x17E86: shows the sprites; three frames, stopped by a button (nonzero).
uint16_t ShowSpritesWait() {
    BuildSpriteTable(U16{kTitleSprites});
    for (int i = 0; i < 3; i++) {
        WaitVBlank();
        if (uint16_t b = ReadPads() & 0xF0) return b;
        if (host::press_pending()) return 1;
    }
    SpriteTableHook();
    return 0;
}

// 0x17EC8: the next frame of the house intro sprite.
void NextIntroFrame() {
    if (uint16_t n = cursor_node) FreeSprite(uint32_t(int32_t(int16_t(n))), kTitleSprites);
    uint32_t frames = rd32(0x1957A + rd8(kCursor + 7) * 4u);
    uint8_t f = uint8_t(rd8(kCursor + 6) + 1);
    wr8(kCursor + 6, f);
    uint32_t node = AllocSprite(kCursor, 0, rd32(frames + f * 4u), kTitleSprites);
    cursor_node = uint16_t(node);
    U8{node + 7} |= 0x40;
    wr16(kCursor, 0x28);
    wr16(kCursor + 2, 0x28);
}

// 0x17DC8: plays house n's intro script (0 end, 1 x y: show and wait three
// frames, 2 next frame); true when a button stopped it.
bool HouseIntro(uint16_t house) {
    wr8(kCursor + 7, uint8_t(house));
    wr8(kCursor + 6, 0);
    uint32_t script = rd32(0x1963A + house * 4u);
    uint32_t frame = rd32(rd32(0x1957A + house * 4u));
    uint32_t node = AllocSprite(kCursor, 0, frame, kTitleSprites);
    cursor_node = uint16_t(node);
    U8{node + 7} |= 0x40;
    for (;;) {
        uint16_t op = rd16(script);
        script += 2;
        if (!op) break;
        if (op == 1) {
            wr16(kCursor, rd16(script));
            wr16(kCursor + 2, rd16(script + 2));
            script += 4;
            if (!ShowSpritesWait()) continue;
            uint16_t n = cursor_node;
            if (!n) return false;
            FreeSprite(uint32_t(int32_t(int16_t(n))), kTitleSprites);
            cursor_node = 0;
            BuildSpriteTable(0);
            SpriteTableHook();
            return true;
        }
        if (op == 2) NextIntroFrame();
    }
    if (uint16_t n = cursor_node) {
        FreeSprite(uint32_t(int32_t(int16_t(n))), kTitleSprites);
        cursor_node = 0;
    }
    return false;
}

// 0x17A20: the menu cursor at (0x78, 0xA0).
void CreateMenuCursor() {
    InitSpritePool(0xFFF3B0, kTitleSprites);
    uint32_t node = AllocSprite(kCursor, 0, 0x17B5E, kTitleSprites);
    cursor_node = uint16_t(node);
    U8{node + 7} |= 0x40;
    wr16(kCursor, 0x78);
    wr16(kCursor + 2, 0xA0);
}

// 0x17A5E
void HideMenuCursor() {
    uint16_t n = cursor_node;
    if (!n) return;
    U8{uint32_t(int32_t(int16_t(n))) + 7} |= 0x80;
    BuildSpriteTable(U16{kTitleSprites});
    WaitVBlank();
    SpriteTableHook();
}

// 0x17A8C
void ShowMenuCursor() {
    BuildSpriteTable(U16{kTitleSprites});
    WaitVBlank();
    SpriteTableHook();
}

// Port: 4 with the LOAD GAME item (saves exist), else 3.
uint16_t menu_rows = 3;

// 0x17A9E: Up/Down move the cursor over the three items; a button returns
// the item ('A' 'B' 'C', port: 'L' LOAD GAME), else 0.
uint16_t MenuCursorKey(uint16_t key) {
    U16 y{kCursor + 2};
    uint16_t last = uint16_t(0xA0 + (menu_rows - 1) * 8);
    uint8_t k = uint8_t(key);
    if (k == 'U' || k == 'D') {
        if (k == 'U') y = int16_t(y) > 0xA0 ? uint16_t(y - 8) : last;
        else y = int16_t(y) < int16_t(last) ? uint16_t(y + 8) : uint16_t(0xA0);
        PlaySfx(0xB);
        return 0;
    }
    if (k == 'A' || k == 'B' || k == 'C' || k == 'S') {
        if (y == 0xA0) return 'A';
        if (y == 0xA8) return 'B';
        if (y == 0xB0) return 'C';
        if (y == 0xB8) return 'L';
    }
    return 0;
}

// Port: frames waited; true when a key, mouse or pad button skipped the
// intro.
bool IntroWait(uint16_t n) {
    while (n--) {
        WaitVBlank();
        if (host::press_pending()) return true;
    }
    return false;
}

// Port: the menu row under the mouse (0 to rows - 1), else -1.
int MouseRow(int rows) {
    const host::Mouse& m = host::mouse();
    if (!m.inside || m.x < 112 || m.x > 296 || m.y < 160 || m.y >= 160 + rows * 8) return -1;
    return (m.y - 160) / 8;
}

// Port: a text line (padded to 22 characters) on the title's menu row.
void PrintRow(int row, const char* text) {
    md::Frame f(24);
    int i = 0;
    for (; text[i] && i < 22; i++) wr8(f.base + i, uint8_t(text[i]));
    for (; i < 22; i++) wr8(f.base + i, ' ');
    wr8(f.base + 22, 0);
    PrintPlaneA(f.base, uint16_t(0xA50 + row * 0x80), 0, 3);
}

void PrintMenu() {
    PrintRow(0, "START GAME");
    PrintRow(1, "OPTIONS");
    PrintRow(2, "TUTORIAL");
    PrintRow(3, menu_rows > 3 ? "LOAD GAME" : "");
    for (int r = 4; r < 7; r++) PrintRow(r, "");
}

// Port: the saved missions, seven rows at a time; Up/Down (or the mouse)
// pick one, A (Enter) or a click loads it as its password would, B (Esc,
// a right click) goes back. True when a game was loaded.
bool LoadMenu() {
    static const char* const kHouse[3] = {"HARKONNEN", "ATREIDES", "ORDOS"};
    std::vector<Save> saves = ReadSaves();
    int n = int(saves.size()), rows = std::min(n, 7), at = 0, top = 0;
    auto draw = [&]() {
        for (int r = 0; r < 7; r++) {
            char line[32] = "";
            if (r < rows) {
                const Save& s = saves[size_t(top + r)];
                std::snprintf(line, sizeof line, "%-10s MISSION %d", kHouse[s.house], s.scenario);
            }
            PrintRow(r, line);
        }
    };
    draw();
    // No held button repeats here: the A (Enter) that opened the list
    // must not pick from it or close it.
    uint8_t saved_mask = pad_repeat_mask;
    pad_repeat_mask = 0xFF;
    for (;;) {
        uint16_t key = GetKey();
        if (host::take_escape()) key = 'B';
        bool click = key == 'A' && (host::mouse_pad() & 0x40);
        int row = MouseRow(rows);
        int to = at;
        if (row >= 0 && (host::mouse().active || click)) to = top + row;
        if (key == 'U' && at > 0) to = at - 1;
        if (key == 'D' && at < n - 1) to = at + 1;
        if (to != at) {
            at = to;
            PlaySfx(0xB);
            if (at < top) top = at;
            if (at >= top + rows) top = at - rows + 1;
            draw();
        }
        wr16(kCursor + 2, uint16_t(0xA0 + (at - top) * 8));
        ShowMenuCursor();
        WaitVBlank();
        if ((key == 'A' || key == 'C' || key == 'S') && !(click && row < 0)) {
            pad_repeat_mask = saved_mask;
            player_house = saves[size_t(at)].house;
            U16{0xFFC04C} = saves[size_t(at)].scenario;
            U16{0xFFC050} = uint16_t(saves[size_t(at)].scenario - 1);
            U16{0xFFC024} = 1; // as a password: the game loaded
            return true;
        }
        if (key == 'B') {
            pad_repeat_mask = saved_mask;
            PlaySfx(0xB);
            PrintMenu();
            wr16(kCursor + 2, 0xB8);
            return false;
        }
    }
}

// Brightens colour (3, 0xD) to white, pulsing (3, 0xE) meanwhile.
void PulseIn() {
    while (!FadeColor(3, 0xD, 0xEEE)) {
        FadeColor(3, 0xE, 0x666);
        WaitFrames(3);
    }
}

} // namespace

// 0x17C36: VBlank hook scrolling plane B: speeds up to 3 pixels a frame,
// wraps once past -0x140 (or, the second time, slows down to the target).
void TitleScrollHook() {
    int32_t speed = scroll_speed, pos = scroll_pos;
    if (scroll_running && int16_t(uint32_t(pos) >> 16) < -0x140) {
        if (!scroll_repeat) {
            scroll_running = 0;
            scroll_slowing = 0xFFFF;
            speed = 0x58800;
            scroll_speed = speed;
        } else {
            pos = -1;
            scroll_pos = pos;
        }
    }
    SetPlaneBScroll(uint16_t(uint32_t(pos) >> 16));
    if (!scroll_slowing) {
        speed += 0x800;
        pos -= speed >= 0x30000 ? 0x30000 : speed;
    } else {
        speed -= 0x800;
        if (speed >= 0x30000) pos -= 0x30000;
        else if (speed <= 0x8000) pos -= 0x8000;
        else pos -= speed;
        if (pos <= scroll_target) {
            speed = 0x8000;
            pos = scroll_target;
        }
    }
    scroll_speed = speed;
    scroll_pos = pos;
}

// 0x4D4A: the first pressed button as a letter: Start 'S', A, B, C, Up 'U',
// Down 'D', Left 'L', Right 'R'; 0 for none.
uint16_t GetKey() {
    uint32_t p = ReadPads();
    uint16_t k = uint16_t(p);
    if (p & 0x80) k = 'S';
    else if (p & 0x40) k = 'A';
    else if (p & 0x10) k = 'B';
    else if (p & 0x20) k = 'C';
    else if (p & 1) k = 'U';
    else if (p & 2) k = 'D';
    else if (p & 4) k = 'L';
    else if (p & 8) k = 'R';
    wr8(0xFFBF2B, uint8_t(k));
    return k;
}

// 0x17418: the title. intro: show the logo scroll and house intros first.
// Returns 0 start game, 1 options, 2 tutorial, 3 attract demo (timeout).
uint16_t TitleScreen(uint16_t intro) {
    uint16_t key = 0;
    FadeOutScreen();
    uint32_t old_hook = SetVBlankHook(0);
    DisplayOff();
    SetFadeDelay(3);
    LoadTitleSoundBanks();
    uint16_t old_b = SetPlaneB(0xC000);
    uint16_t old_a = SetPlaneA(0xE000);
    uint16_t old_h = SetHScrollTable(0xB800);
    uint16_t old_s = SetSpriteTable(0xB000);
    SetScreenWidth(1);
    SetWindowPos(0, 0);
    SetPlaneSize(0, 3);
    ClearVram();
    uint32_t pal = LoadScreen(12);
    uint16_t result = 1;
    if (music_on) PlayMusic(0);
    if (intro) {
        FadeIn(pal);
        StartTitleScroll(0xFD40);
        if (IntroWait(200)) {
            result = 0;
            goto done;
        }
        SetPlaneAScroll(0);
        LoadScreen(13);
        FadeToward(0xA6E18, 0x10, 0x30);
        if (IntroWait(300)) {
            result = 0;
            goto done;
        }
        FadeToward(0xFFC28C, 0x10, 0x30);
        ClearPlaneA();
        LoadFont();
        PrintPlaneA(0x173F4, 0x10, 0x1C, 3); // PRESENT
        PulseIn();
        for (int i = 0; i < 200; i++)
            if (ReadButtons() || host::press_pending()) {
                result = 0;
                goto done;
            }
        while (!FadeColor(3, 0xD, 0)) {
            FadeColor(3, 0xE, 0);
            if (IntroWait(3)) {
                result = 0;
                goto done;
            }
        }
        ClearPlaneA();
        scroll_repeat = 0;
        uint16_t s;
        do s = TitleScrollState();
        while (!s);
        StopVBlankHook();
        if (s == 2) {
            result = 0;
            goto done;
        }
        if (result) {
            WriteCram(1, 0, 0x30, 0xA6E98);
            PrepareHouseIntros();
            static const uint16_t order[3] = {1, 0, 2};
            for (uint16_t h : order) {
                if (music_on) PlayMusic(10);
                if (HouseIntro(h)) {
                    GemsStopSeq(10);
                    result = 0;
                    WriteCram(1, 0, 0x30, 0xFFC28C);
                    goto done;
                }
            }
            WriteCram(1, 0, 0x30, 0xFFC28C);
            for (int i = 0; i < 180; i++)
                if (ReadButtons() || host::press_pending()) {
                    result = 0;
                    break;
                }
        }
    }
done:
    if (!result) {
        StopVBlankHook();
        SetPlaneBScroll(0xFD40);
    }
    SetPlaneAScroll(0xFE00);
    LoadScreen(13);
    LoadFont();
    if (!intro) {
        SetPlaneBScroll(0xFD40);
        FadeIn(pal);
    }
    SetFadeDelay(1);
    FadeToward(0xA6E18, 0x10, 0x30);
    if (intro && result) WaitFrames(200);
    pad_repeat_mask = 0x7F;
    SetColor(3, 0xD, 0);
    SetColor(3, 0xE, 0);
    PrintPlaneA(0x173FC, 0xA50, 0, 3); // START GAME
    PrintPlaneA(0x17407, 0xAD0, 0, 3); // OPTIONS
    PrintPlaneA(0x1740F, 0xB50, 0, 3); // TUTORIAL
    menu_rows = ReadSaves().empty() ? 3 : 4;
    if (menu_rows > 3) PrintRow(3, "LOAD GAME");
    PulseIn();
    // Port: the press that skipped the intro is not taken as a menu key.
    if (host::press_pending()) ReadPads();
    host::clear_press();
    CreateMenuCursor();
    SetColor(2, 0xD, 0x666);
    SetColor(2, 0xF, 0x666);
    bool bright = true;
    uint16_t blink = 0, idle = 900;
    while (!key) {
        key = GetKey();
        // Port: the mouse points at an item; a click picks the one under it.
        bool click = key == 'A' && (host::mouse_pad() & 0x40);
        int row = MouseRow(menu_rows);
        if (row >= 0 && (host::mouse().active || click)) wr16(kCursor + 2, uint16_t(0xA0 + row * 8));
        if (click && row < 0) key = 0;
        if (key == 'D' || key == 'U') idle = 900;
        if (int16_t(--idle) <= 0) key = 'D';
        else key = MenuCursorKey(key);
        switch (key) {
        case 'A': result = 0; break;
        case 'B': result = 1; break;
        case 'C': result = 2; break;
        case 'L':
            if (LoadMenu()) result = 0;
            else key = 0;
            idle = 900;
            break;
        case 'D': result = 3; break;
        default: key = 0;
        }
        ShowMenuCursor();
        WaitVBlank();
        if (blink > 2) {
            blink = 0;
            if (bright) {
                if (FadeColor(2, 0xD, 0xEEE)) bright = false;
            } else if (FadeColor(2, 0xD, 0x666)) {
                bright = true;
            }
        }
        blink++;
    }
    if (sfx_on) PlayMusic(0xC);
    HideMenuCursor();
    pad_repeat_mask = 0xF0;
    // The original started sequence 0x29 here (if result || intro), but the
    // title bank has only 13 sequences: the Z80 chewed sample data, silent,
    // until LoadGameSoundBanks reset it. Skipped.
    GemsStopSeq(0);
    FadeOutScreen();
    DisplayOff();
    ClearVram();
    SetSpriteTable(old_s);
    SetPlaneB(old_b);
    SetPlaneA(old_a);
    SetHScrollTable(old_h);
    SetVBlankHook(old_hook);
    DisplayOn();
    WaitFrames(15);
    LoadGameSoundBanks();
    return result;
}

} // namespace game
