// The options screen (0x20DA6) and the password screen (0x215C0), from the
// front end and from Start in a mission. Text is drawn as tiles into VRAM
// 0x8000 on, which the screen's map shows; the cursor sprite's position is
// at 0xFFDBF0 (node 0xFFDBF6), in the list at 0xFFF5B0.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kList = 0xFFF5B0;
constexpr uint32_t kCursor = 0xFFDBF0;
constexpr S16 cursor_x{0xFFDBF0};
constexpr S16 cursor_y{0xFFDBF2};
constexpr U16 cursor_node{0xFFDBF6};
constexpr S16 slide_y{0xFFDBF8}; // pixels left to move, 4 a frame
constexpr S16 slide_x{0xFFDBFA};
constexpr U16 item{0xFFDBFC};
constexpr U8 at_end{0xFFDBFE}; // password cursor on the end mark
constexpr U8 shown_end{0xFFDBFF};
constexpr uint32_t kYesNo = 0xFFDCAC; // the confirm box sprite: x, y, node at +4
constexpr U8 music_on{0xFFDC50};
constexpr U8 sfx_on{0xFFDC51};
constexpr U8 radar_on{0xFFBF7C};
constexpr U16 player_house{0xFFC274};
constexpr uint32_t kSteps = 0x21CFC; // option row heights

int32_t Ext(int16_t v) { return v; }

void CursorFrame() {
    BuildSpriteTable(U16{kList});
    WaitVBlank();
    SpriteTableHook();
}

void NewCursor(uint32_t frame) {
    uint32_t n = AllocSprite(kCursor, 0, frame, kList);
    cursor_node = uint16_t(n);
    U8{n + 7} |= 0x40;
}

// 0x22194: string s as tiles at VRAM address at, one per character, from
// the font at 0x50C34 (light) or 0x51C34.
void DrawText(uint32_t s, uint16_t at, uint16_t light) {
    uint32_t font = light ? 0x50C34 : 0x51C34;
    for (uint8_t c; (c = rd8(s++)) != 0; at = uint16_t(at + 0x20))
        QueueDma(2, font + c * 32u, uint32_t(at) << 16 | 0x10);
}

// 0x22160, 0x221F0: the text tiles cleared, the backdrop sprite at
// 0xFFD70C.
void InitTextScreen(uint32_t frame) {
    QueueDma(0x301, 0, 0x80000900);
    uint32_t n = AllocSprite(0xFFD70C, 0, frame, kList);
    wr16(0xFFD710, uint16_t(n));
    U8{n + 7} |= 0x40;
    wr16(0xFFD70C, 0);
    wr16(0xFFD70E, 0);
}

// 0x21B94
void OptionsInit() {
    InitSpritePool(0xFFF3B0, kList);
    InitTextScreen(0x22530);
    NewCursor(0x6567E);
    QueueDma(2, 0x655FE, 0xF7800040);
    slide_y = 0;
    cursor_x = 0xC;
    item = 0;
    cursor_y = 0x24;
}

// 0x21D0A
void OptionsFrame() {
    if (int16_t d = slide_y) {
        if (d > 0) {
            d = int16_t(d - 4);
            cursor_y += 4;
        } else {
            d = int16_t(d + 4);
            cursor_y -= 4;
        }
        slide_y = d;
    }
    CursorFrame();
}

// 0x21C1A: Up/Down start the cursor sliding to the next row (wrapping);
// -1 for those or while sliding, else the row under the cursor. In a
// mission the last two rows (restart, pick another house) are there too.
uint16_t OptionsKey(uint16_t key, uint16_t ingame) {
    if (slide_y) return 0xFFFF;
    uint8_t k = uint8_t(key);
    if (k == 'U') {
        if (cursor_y > 0x24) {
            item = uint16_t(item - 1);
            slide_y = int16_t(-rd16(kSteps + item * 2u));
        } else if (!ingame) {
            cursor_y = 0x84;
            item = 5;
        } else {
            cursor_y = 0xA4;
            item = 7;
        }
        return 0xFFFF;
    }
    if (k == 'D') {
        if (cursor_y >= (ingame ? 0xA4 : 0x84)) {
            cursor_y = 0x24;
            item = 0;
        } else {
            slide_y = int16_t(rd16(kSteps + item * 2u));
            item = uint16_t(item + 1);
        }
        return 0xFFFF;
    }
    return item;
}

// Port: the cursor's y on a row.
int16_t RowY(uint16_t row) {
    int16_t y = 0x24;
    for (uint16_t i = 0; i < row; i++) y = int16_t(y + rd16(kSteps + i * 2u));
    return y;
}

// Port: the row under the mouse (16 pixels from the cursor's y), else -1.
int OptionsMouseRow(uint16_t ingame) {
    const host::Mouse& m = host::mouse();
    if (!m.inside || m.x < 8 || m.x >= 312) return -1;
    for (uint16_t row = 0; row <= (ingame ? 7 : 5); row++)
        if (m.y >= RowY(row) && m.y < RowY(row) + 16) return row;
    return -1;
}

// 0x2208E: colours first..last of a palette line rotated down one, in CRAM.
void CycleColors(uint16_t line, uint16_t first, uint16_t last) {
    int i = line * 16 + first;
    uint16_t keep = display::cram[i & 63] & 0xEEE;
    for (int k = 0; k < last - first; k++, i++) display::cram_w16(i & 63, display::cram[(i + 1) & 63] & 0xEEE);
    display::cram_w16(i & 63, keep);
    md::int_mask = 0;
}

// 0x22222: the whole palette at half brightness.
void DimPalette() {
    WaitVBlank();
    for (int i = 0; i < 64; i++) {
        uint16_t c = rd16(kPalette + i * 2u);
        c = uint16_t(((c & 0xE00) >> 1 | (c & 0xE0) >> 1 | (c & 0xE) >> 1) & 0xEEE);
        wr16(kPalette + i * 2u, c);
        display::cram_w16(i, c);
    }
    md::int_mask = 0;
}

// 0x22294: the YES / NO box, its cursor on YES.
void ShowConfirm() {
    FreeSprite(Node(cursor_node), kList);
    NewCursor(0x22776);
    cursor_x = 0x70;
    cursor_y = 0x83;
    QueueDma(2, 0x6424E, 0xF7800040);
    uint32_t n = AllocSprite(kYesNo, 0, 0x22622, kList);
    wr16(kYesNo + 4, uint16_t(n));
    U8{n + 7} |= 0x40;
    wr16(kYesNo, 0x64);
    wr16(kYesNo + 2, 0x70);
    // 0x223EC: the box's frame tiles.
    uint16_t at = 0x8900;
    for (uint32_t t : {0x120u, 0x160u, 0x140u, 0x140u, 0x140u, 0x140u}) {
        QueueDma(2, 0x42D02 + t, uint32_t(at) << 16 | 0x10);
        at = uint16_t(at + 0x20);
    }
    CursorFrame();
}

// 0x22450: Left/Right slide the cursor between YES and NO; a button
// answers 'A' (yes) or 'B' (no), else 0.
uint16_t ConfirmKey(uint16_t key) {
    if (slide_x) return 0;
    uint8_t k = uint8_t(key);
    if (k == 'L') {
        if (cursor_x > 0x70) slide_x = -0x44;
    } else if (k == 'R') {
        if (cursor_x < 0xB4) slide_x = 0x44;
    } else if (k == 'A' || k == 'B' || k == 'C') {
        PlaySfx(0x28);
        return cursor_x == 0x70 ? 'A' : 'B';
    }
    return 0;
}

// 0x224E2
void ConfirmFrame() {
    if (int16_t d = slide_x) {
        if (d > 0) {
            d = int16_t(d - 4);
            cursor_x += 4;
        } else {
            d = int16_t(d + 4);
            cursor_x -= 4;
        }
        slide_x = d;
        if (!d) PlaySfx(0x19);
    }
    CursorFrame();
}

// 0x22332: the box removed; restore: the options cursor back on its row.
void EndConfirm(uint16_t restore) {
    if (uint16_t n = U16{kYesNo + 4}) {
        FreeSprite(Node(n), kList);
        U16{kYesNo + 4} = 0;
    }
    FreeSprite(Node(cursor_node), kList);
    if (restore) {
        NewCursor(0x6567E);
        QueueDma(2, 0x655FE, 0xF7800040);
        slide_y = 0;
        cursor_x = 0xC;
        int16_t y = 0x24;
        for (uint16_t i = item; i; i--) y = int16_t(y + rd16(kSteps + (i - 1) * 2u));
        cursor_y = y;
    }
    CursorFrame();
}

// 0x21A5C: ARE YOU SURE? over the dimmed screen; true for yes.
bool Confirm() {
    uint16_t blink = 0;
    bool yes = false, done = false, bright = false;
    DimPalette();
    WriteCram(1, 8, 8, 0x88294);
    SetColor(3, 1, 0x666);
    DrawText(0x21A32, 0x89C0, 0);
    DrawText(0x21A40, 0x8B60, 0);
    DrawText(0x21A4E, 0x8D00, 0);
    ShowConfirm();
    while (!done) {
        uint16_t key = GetKey();
        // Port: the mouse points at YES (x 100-151) or NO (152-207); a
        // click elsewhere does nothing.
        const host::Mouse& m = host::mouse();
        bool click = key == 'A' && (host::mouse_pad() & 0x40);
        int at = !m.inside || m.y < 0x7F || m.y >= 0x93 || m.x < 100 || m.x >= 208 ? -1 : m.x >= 152;
        if (at >= 0 && !slide_x && (m.active || click) && cursor_x != (at ? 0xB4 : 0x70)) {
            cursor_x = at ? 0xB4 : 0x70;
            PlaySfx(0x19);
        }
        if (click && at < 0) key = 0;
        uint16_t k = ConfirmKey(key);
        if (host::take_escape()) k = 'B'; // port: Esc answers no
        if (k == 'A') yes = done = true;
        else if (k == 'B') {
            yes = false;
            done = true;
        }
        if (blink > 6) {
            blink = 0;
            if (bright) {
                if (FadeColor(3, 1, 0xEEE)) bright = false;
            } else if (FadeColor(3, 1, 0x666)) {
                bright = true;
            }
        }
        blink++;
        ConfirmFrame();
    }
    EndConfirm(!yes);
    if (!yes) UploadPalette(0x88214);
    return yes;
}

// 0x21C00: the map cursor's shape set again.
void RestoreCursorShape() {
    uint32_t old = U32{0xFFBF62};
    U32{0xFFBF62} = 0xA0A0A0A0;
    SetCursorShape(int16_t(old >> 16), int16_t(old));
}

// 0x21D44
void PasswordInit() {
    InitSpritePool(0xFFF3B0, kList);
    InitTextScreen(0x22790);
    NewCursor(0x2275C);
    QueueDma(2, 0x6424E, 0xF7800040);
    cursor_x = 0x54;
    cursor_y = 0x34;
    slide_y = 0;
    slide_x = 0;
    at_end = 0;
    shown_end = 0;
}

// 0x21EB8: the cursor snaps onto the end mark (and changes shape there).
void PasswordFrame() {
    if (cursor_x >= 0xD4 && cursor_y >= 0x54) {
        cursor_x = 0xD4;
        cursor_y = 0x54;
        at_end = 1;
    } else {
        at_end = 0;
    }
    if (at_end != shown_end) {
        int16_t x = cursor_x, y = cursor_y;
        FreeSprite(Node(cursor_node), kList);
        NewCursor(at_end ? 0x22776 : 0x2275C);
        shown_end = at_end;
        cursor_x = x;
        cursor_y = y;
    }
    CursorFrame();
}

// 0x21F82: the character under the cursor (3 rows of 10 at 0x21FCA:
// letters, '<' '>' and the end mark '!'); 0 while sliding.
uint8_t CharAtCursor() {
    if (slide_y || slide_x) return 0;
    uint16_t row = uint16_t(uint16_t(cursor_y - 0x34) >> 4) * 10;
    uint16_t col = uint16_t(cursor_x - 0x54) >> 4;
    return rd8(0x21FCA + int16_t(uint16_t(row + col)));
}

// 0x21FE8
void CursorToEnd() {
    cursor_x = 0xD4;
    cursor_y = 0x54;
    slide_x = 0;
    slide_y = 0;
}

// 0x21DB8: the cursor moved a cell (Left/Right wrap through the rows);
// true when it moved.
bool MovePasswordCursor(uint16_t key) {
    uint8_t k = uint8_t(key);
    if (k == 'U') {
        if (cursor_y <= 0x34) return false;
        cursor_y -= 0x10;
    } else if (k == 'D') {
        if (cursor_y >= 0x54) return false;
        cursor_y += 0x10;
    } else if (k == 'L') {
        if (cursor_x > 0x54) cursor_x -= 0x10;
        else {
            cursor_x = 0xE4;
            int16_t y = int16_t(cursor_y - 0x10);
            cursor_y = y < 0x34 ? int16_t(0x54) : y;
        }
    } else if (k == 'R') {
        if (cursor_y >= 0x54 && cursor_x >= 0xD4) {
            cursor_x = 0x54;
            cursor_y = 0x34;
        } else if (cursor_x >= 0xE4) {
            cursor_x = 0x54;
            int16_t y = int16_t(cursor_y + 0x10);
            if (y > 0x54) return false;
            cursor_y = y;
        } else {
            cursor_x += 0x10;
        }
    } else {
        return false;
    }
    return true;
}

// 0x21902: password s looked up in the table at 0x88124 (string, kind,
// scenario): kinds 0-2 start that house at the scenario; 3 scenario 10
// (when a house is set up); the rest are cheats. True to start a game.
bool CheckPassword(uint32_t s) {
    if (!s) return false;
    uint16_t i = 0;
    bool found = false;
    while (rd32(0x88124 + i * 8u) && !found) {
        if (!StrCmp(rd32(0x88124 + i * 8u), s)) found = true;
        else i++;
    }
    if (!found) return false;
    uint32_t e = 0x88124 + i * 8u;
    switch (rd16(e + 4)) {
    case 0:
    case 1:
    case 2:
        player_house = rd16(e + 4);
        U16{0xFFC04C} = rd16(e + 6);
        U16{0xFFC050} = uint16_t(U16{0xFFC04C} - 1);
        return true;
    case 3:
        U16{0xFFC04C} = 10;
        U16{0xFFC050} = 9;
        return U32{0xFFC278} != 0;
    case 6: U8{0xFFC198} ^= 1; break;
    case 7:
        if (uint32_t h = U32{0xFFC278}) {
            wr32(h + 0x12, 25000);
            U32{0xFFC054} = 25000;
        }
        break;
    case 9: U8{0xFFC019} ^= 1; break;
    case 10: DrawText(0x218F6, 0x8000, 1); break; // the version
    }
    return false;
}

} // namespace

// 0x20DA6: rows music on/off, effects on/off, radar, music test, effect
// test, password, and in a mission restart / pick another house. Returns 0
// (Start: back), 1 restart, 2 another house, 3 password.
uint16_t OptionsScreen(uint16_t ingame) {
    uint16_t at_row = 0, track = 0, effect = 0, result = 0, ticks = 0, key;
    int16_t sound = -1, music = -1;
    auto click = [&](int16_t id) {
        if (sound != -1) GemsStopSeq(uint32_t(Ext(sound)));
        PlaySfx(uint32_t(Ext(id)));
        sound = id;
    };
    auto stop_music = [&] {
        if (music != -1) {
            GemsStopSeq(uint32_t(Ext(music)));
            music = -1;
        }
    };
    FadeOutScreen();
    uint32_t old_hook = SetVBlankHook(0);
    DisplayOff();
    ClearWindowPlane();
    SetWindowPos(0x14, 0x1E);
    SetScreenWidth(1);
    OptionsInit();
    LoadScreen(0xE);
    DrawText(rd32(0x87F44 + track * 8u), 0x8120, 1);
    DrawText(rd32(0x87FDC + effect * 8u), 0x8300, 1);
    DrawText(music_on ? 0x20D53 : 0x20D57, 0x8000, 1);
    DrawText(sfx_on ? 0x20D5B : 0x20D5F, 0x8060, 1);
    DrawText(radar_on ? 0x20D63 : 0x20D67, 0x80C0, 1);
    if (ingame) {
        DrawText(0x20D6B, 0x84E0, 1); // Restart mission
        DrawText(0x20D7B, 0x86C0, 1); // Pick another house
    }
    OptionsFrame();
    FadeIn(0x88214);
    GemsStopSeq(uint32_t(Ext(S16{0xFFDC52})));
    StopSeq29();
    PlaySoundEffect(6);
    SetPadRepeatMask(0xFF);
    do {
        key = GetKey();
        if (host::take_escape()) key = 'S'; // port: Esc closes it too
        // Port: the mouse points at a row; a click acts on the row under
        // it, elsewhere it does nothing.
        bool mouse_click = key == 'A' && (host::mouse_pad() & 0x40);
        int mouse_row = OptionsMouseRow(ingame);
        if (mouse_row >= 0 && !slide_y && (host::mouse().active || mouse_click)) {
            item = uint16_t(mouse_row);
            cursor_y = RowY(item);
        }
        if (mouse_click && mouse_row < 0) key = 0;
        uint16_t row = OptionsKey(key, ingame);
        if (row != 0xFFFF && row != at_row) {
            click(0x19);
            at_row = row;
        }
        bool abc = key == 'A' || key == 'B' || key == 'C';
        bool lr = key == 'L' || key == 'R';
        if (key && row != 0xFFFF && int16_t(row) >= 0 && row <= 7) switch (row) {
            case 0:
                if (!abc && !lr) break;
                click(0x28);
                if (music_on) {
                    music_on = 0;
                    stop_music();
                } else {
                    music_on = 1;
                }
                DrawText(music_on ? 0x20D8E : 0x20D92, 0x8000, 1);
                break;
            case 1:
                if (!abc && !lr) break;
                click(0x28);
                sfx_on = sfx_on ? 0 : 1;
                DrawText(sfx_on ? 0x20D96 : 0x20D9A, 0x8060, 1);
                break;
            case 2:
                if (!abc && !lr) break;
                if (radar_on) {
                    radar_on = 0;
                } else {
                    radar_on = 1;
                    if (U16{0xFFBF58}) { // the radar's sprite
                        radar_on = 2;
                        UpdateRadarStatus(player_house);
                    }
                }
                UpdateRadarFrame();
                DrawText(radar_on ? 0x20D9E : 0x20DA2, 0x80C0, 1);
                click(0x28);
                break;
            case 3:
                if (key == 'L' || key == 'R') {
                    if (key == 'L') {
                        if (track) track--;
                        else
                            while (rd32(0x87F4C + track * 8u)) track++;
                    } else {
                        track = rd32(0x87F4C + track * 8u) ? uint16_t(track + 1) : 0;
                    }
                    DrawText(rd32(0x87F44 + track * 8u), 0x8120, 1);
                    click(0x28);
                } else if (abc) {
                    if (sound != -1) {
                        GemsStopSeq(uint32_t(Ext(sound)));
                        sound = -1;
                    }
                    if (music_on) {
                        GemsStopSeq(uint32_t(Ext(music)));
                        music = int16_t(rd32(0x87F48 + track * 8u));
                        GemsStartSeq(uint32_t(Ext(music)));
                    }
                }
                break;
            case 4:
                if (key == 'L' || key == 'R') {
                    if (key == 'L') {
                        if (effect) effect--;
                        else
                            while (rd32(0x87FE4 + effect * 8u)) effect++;
                    } else {
                        effect = rd32(0x87FE4 + effect * 8u) ? uint16_t(effect + 1) : 0;
                    }
                    DrawText(rd32(0x87FDC + effect * 8u), 0x8300, 1);
                    click(0x28);
                } else if (abc) {
                    stop_music();
                    sound = int16_t(rd32(0x87FE0 + effect * 8u));
                    PlaySfx(uint32_t(Ext(sound)));
                }
                break;
            case 5:
                if (!abc) break;
                click(0x28);
                result = 3;
                key = 'S';
                break;
            case 6:
            case 7:
                if (!abc) break;
                click(0x28);
                if (Confirm()) {
                    result = row == 6 ? 1 : 2;
                    key = 'S';
                }
                break;
            }
        OptionsFrame();
        if (ticks & 8) {
            CycleColors(1, 8, 0xD);
            ticks = 0;
        }
        ticks++;
    } while (key != 'S');
    if (music != -1) GemsStopSeq(uint32_t(Ext(music)));
    FadeOutScreen();
    DisplayOff();
    SetPadRepeatMask(0xF0);
    SetWindowPos(0, 0);
    U8{0xFFBF7F} = 1;
    SetVBlankHook(old_hook);
    if (!result && ingame) {
        LoadGameTiles();
        if (uint32_t u = U32{0xFFC260}) UpdateUnitSprite(u);
        if (radar_on) {
            RefreshRadar();
            radar_on = 1;
        }
        RestoreCursorShape();
        U8{0xFFBF7F} = 0;
        FadeIn(LoadScreen(4));
        SyncFrameDelta();
        if (music_on) PlayMusicTrack(uint16_t(RandomRange(0, 4) + 8));
    }
    wr8(0xFFBF2A, 0);
    wr8(0xFFBF2B, 0);
    U8{0xFFBF7F} = 0;
    return result;
}

// 0x215C0: a password of up to ten characters picked from a grid; 1 when
// it starts a game (house and scenario set), 0 for Start.
uint16_t PasswordScreen() {
    md::Frame f(11);
    uint32_t buf = f.base;
    for (int i = 0; i < 11; i++) wr8(buf + i, 0);
    uint16_t len = 0, repeat = 0, prev = 0, blink = 0, result = 0;
    bool done = false, bright = false;
    FadeOutScreen();
    uint32_t old_hook = SetVBlankHook(0);
    DisplayOff();
    SetWindowPos(0x14, 0x1E);
    SetScreenWidth(1);
    PasswordInit();
    DrawText(0x215BA, 0x8000, 1); // "_"
    PasswordFrame();
    FadeIn(LoadScreen(10));
    SetPadRepeatMask(0xF0);
    // Port: letters and Backspace typed on the keyboard enter as if picked
    // from the grid; Esc leaves.
    host::clear_chars();
    while (!done) {
        uint16_t key = GetKey();
        if (host::take_escape()) key = 'S';
        int typed = host::take_char();
        if (typed) key = 'T';
        switch (key) {
        case 'T':
        case 'A':
        case 'B':
        case 'C': {
            uint8_t c = key == 'T' ? uint8_t(typed == '\b' ? '<' : typed) : CharAtCursor();
            if (!c) break;
            if (c == '!') {
                if (CheckPassword(buf)) {
                    done = true;
                    result = 1;
                } else {
                    PlaySfx(RandomRange(0, 1) ? 0x4C : 0x1A);
                }
                break;
            }
            if (len >= 10 && c != '<') {
                PlaySfx(0x1A);
                break;
            }
            if (c == '<') {
                if (len) {
                    PlaySfx(0x28);
                    if (!rd8(buf + len)) DrawText(0x215BC, uint16_t(0x8000 + len * 32), 1);
                    len--;
                } else {
                    PlaySfx(0x1A);
                }
            } else if (c == '>') {
                if (rd8(buf + len)) {
                    PlaySfx(0x28);
                    len++;
                } else {
                    PlaySfx(0x1A);
                }
            } else {
                PlaySfx(0x28);
                wr8(buf + len++, c);
                if (len == 10) CursorToEnd();
            }
            DrawText(buf, 0x8000, 1);
            DrawText(0x215BE, uint16_t(0x8000 + len * 32), 1);
            break;
        }
        case 'S': done = true; break;
        case 'U':
        case 'D':
        case 'L':
        case 'R':
            if (repeat) repeat--;
            else if (MovePasswordCursor(key)) {
                PlaySfx(0x19);
                repeat = prev == key ? 8 : 0x19;
            }
            break;
        default: repeat = 0;
        }
        prev = key;
        PasswordFrame();
        if (blink > 6) {
            blink = 0;
            if (bright) {
                if (FadeColor(3, 1, 0xEEE)) bright = false;
            } else if (FadeColor(3, 1, 0x666)) {
                bright = true;
            }
        }
        blink++;
    }
    if (result == 1) {
        PlaySfx(RandomRange(0, 1) ? 0x43 : 0x44);
        for (int i = 0; i < 50; i++) WaitVBlank();
    }
    FadeOutScreen();
    SetVBlankHook(old_hook);
    SetPadRepeatMask(0xF0);
    return result;
}

} // namespace game
