// The build menu (0x287DC): a full-screen grid of 3 x 6 items (0xFFBF8E,
// 0x80 empty) for a construction yard (structures, 0xFFC614 1) or a
// factory (units), the item under the cursor (0xFFBF8A column, 0xFFBF8C
// row) shown large with its cost, health and damage, and the yard's
// minimap. 0xFFC61C the item list (0xFFC618 its upgrade entry, 0xFFBFA0
// the item it replaces), 0xFFBFB4/0xFFBFB5 repair and upgrade allowed.
// Sprites in their own list at 0xFFF5B0 (pool 0xFFF3B0).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr uint32_t kMenuList = 0xFFF5B0;
constexpr uint32_t kGrid = 0xFFBF8E;
constexpr U8 menu_units{0xFFC614};

// Writes words to VRAM as the original's data port writes (autoincrement 2).
void VramLong(uint16_t at, uint32_t v) {
    display::vram_w16(at, uint16_t(v >> 16));
    display::vram_w16(uint16_t(at + 2), uint16_t(v));
}

// 0x7E22, 0x808E: the minimap sprite shown / hidden.
void ShowMinimap() {
    if (uint16_t n = U16{0xFFBFBA}) U8{Node(n) + 7} &= 0x7F;
}
void HideMinimap() {
    if (uint16_t n = U16{0xFFBFBA}) U8{Node(n) + 7} |= 0x80;
}

// 0x7E34: the price tag sprite for a structure layout (-1 hides it).
void ShowSizeSprite(int16_t layout) {
    uint16_t n = U16{0xFFBFAA};
    if (!n) return;
    if (layout < 0) {
        U8{Node(n) + 7} |= 0x80;
        return;
    }
    wr32(Node(n) + 8, rd32(0x7E56 + uint16_t(layout << 2)));
    U8{Node(n) + 7} &= 0x7F;
}

// 0x8876: every menu sprite but the size and cost ones shown.
void ShowMenuSprites() {
    for (uint16_t w = U16{0xFFF5B4}; w; w = rd16(Node(w) + 0xE)) {
        if (w == U16{0xFFBFAA} || w == U16{0xFFBFBA}) continue;
        U8{Node(w) + 7} &= 0x7F;
    }
}

// 0x7E92: the yard's minimap (0xFFC768: rows of nibble cells) drawn as
// eight tiles at VRAM 0x8020 through a colour table (0x7E72 by kind); its
// sprite in the house's palette line.
void DrawMinimap(uint16_t kind) {
    FlushDma();
    uint32_t table = 0x7E72 + uint16_t(kind << 4);
    uint32_t src = 0xFFC768;
    uint16_t pal = (rd16(src) & 0xF) == 2 ? 0x2000 : 0x4000;
    uint16_t at = 0x8020;
    for (int r = 0; r < 4; r++, src += 0x1C) {
        for (int k = 0; k < 2; k++) {
            uint16_t w = rd16(src);
            src += 2;
            uint32_t row = 0;
            for (int n = 0; n < 4; n++) {
                uint8_t c = rd8(table + ((w >> 12) & 0xF));
                w = uint16_t(w << 4);
                row = row << 8 | uint8_t(c << 4 | c);
            }
            for (int i = 0; i < 8; i++, at = uint16_t(at + 4)) VramLong(at, row);
        }
    }
    uint32_t n = AllocSprite(0x4A2DE, 0xC0, 0x4A2E4, kMenuList);
    U16{0xFFBFBA} = uint16_t(n);
    U16{n + 6} |= pal;
    ShowMenuSprites();
}

// 0x7F4E, 0x889A: a menu frame: the music counter by the frames passed,
// then VBlank and the menu's sprite table.
void MenuFrame() {
    uint32_t now = frame_counter;
    uint32_t then = U32{0xFFBFBC};
    U32{0xFFBFBC} = now;
    U16{0xFFC574} -= uint16_t(now - then);
    WaitVBlank();
    BuildSpriteTable(U16{0xFFF5B4});
}

// 0x7F72: the item under the cursor.
int16_t CurrentItem() {
    uint16_t c = U16{0xFFBF8A}, r = U16{0xFFBF8C};
    return int8_t(rd8(kGrid + uint16_t(r * 3 + c)));
}

// 0x7F90: three flickering lights in the window art (random delays).
void AnimateLights() {
    if (int16_t(--U16{0xFFBFAC}) < 0) {
        U16{0xFFBFAC} = uint16_t(int8_t(RandomRange(7, 0x1B)));
        uint32_t src = 0x7FCC + uint16_t((frame_counter & 3) << 4);
        QueueDma(2, src, uint32_t(uint16_t(window_base + 0x9E)) << 16 | 8);
    }
    if (int16_t(--U16{0xFFBFAE}) < 0) {
        U16{0xFFBFAE} = uint16_t(int8_t(RandomRange(4, 0xB)));
        QueueDma(2, 0x8048 + uint16_t((frame_counter & 1) << 1), uint32_t(uint16_t(window_base + 0x1AC)) << 16 | 1);
    }
    if (int16_t(--U16{0xFFBFB0}) < 0) {
        U16{0xFFBFB0} = uint16_t(int8_t(RandomRange(7, 0x11)));
        QueueDma(2, 0x8088 + uint16_t((frame_counter & 1) << 1), uint32_t(uint16_t(window_base + 0x3C4)) << 16 | 1);
    }
}

// 0x28DFA: the item list sorted by display order (type +0x20; the upgrade
// entry last), packed into the grid from the third slot (0x80 empty) after
// the repair (-2 allowed / -4 not) and upgrade (-3 / -5) buttons.
void SortGrid(uint32_t g) {
    for (int pass = 0; pass < 0x11; pass++) {
        for (int i = 0; i < 0x11; i++) {
            int16_t a = int8_t(rd8(g + i)), b = int8_t(rd8(g + i + 1));
            int16_t ka = 0x270F, kb = 0x270F;
            uint16_t up = menu_units ? 0x13 : 0x1B;
            uint32_t tbl = menu_units ? 0x6B954 : 0x6C5BC;
            if (a >= 0) ka = a == int16_t(up) ? 0x7F : int8_t(rd8(rd32(tbl + uint16_t(a << 2)) + 0x20));
            if (b >= 0) kb = b == int16_t(up) ? 0x7F : int8_t(rd8(rd32(tbl + uint16_t(b << 2)) + 0x20));
            if (ka > kb) {
                wr8(g + i, uint8_t(b));
                wr8(g + i + 1, uint8_t(a));
            }
        }
    }
    for (int k = 0, d = 0x11; k <= 0xE; k++, d--) {
        int16_t v = int8_t(rd8(g + 0xE - k));
        wr8(g + d, uint8_t(v < 0 ? 0x80 : v));
    }
    wr8(g, 0xFF);
    wr8(g + 1, rd8(0xFFBFB4) ? 0xFE : 0xFC);
    wr8(g + 2, rd8(0xFFBFB5) ? 0xFD : 0xFB);
}

// 0x81CC: the grid from the item list (the upgrade entry's slot taken by
// the item it stands for, saved in 0xFFBFA0).
void FillGrid() {
    U32{0xFFBF8A} = 0;
    for (int i = 0; i < 0x12; i++) wr8(kGrid + i, 0xFF);
    if (uint32_t list = U32{0xFFC61C}) {
        uint16_t up = rd8(0xFFC618);
        int8_t was = int8_t(rd8(list + up));
        wr8(0xFFBFA0, uint8_t(was));
        if (was >= 0) wr8(list + up, uint8_t(up));
        uint32_t out = kGrid;
        for (uint16_t i = 0; i <= up; i++) {
            int8_t v = int8_t(rd8(list + i));
            if (v >= 0) wr8(out++, uint8_t(v));
        }
    }
    SortGrid(kGrid);
}

// 0x827A: the grid's icons (frames from 0x70CA4 structures / 0x6E840
// units; slot positions and frames at 0x4A152).
void DrawIcons() {
    FlushDma();
    uint32_t slot = 0x4A152;
    uint16_t attr = rd16(0x8276 + menu_units * 2u);
    uint32_t frames = rd32(0x826E + menu_units * 4u);
    for (int i = 0; i < 0x12; i++, slot += 8) {
        int16_t item = int8_t(rd8(kGrid + i));
        if (uint8_t(item) == 0x80) break;
        uint32_t frame = rd32(slot + 4);
        uint32_t n = AllocSprite(slot, item == 0xC ? 0x2000 : attr, frame, kMenuList);
        wr8(n + 7, 0x40);
        uint16_t dest = uint16_t(rd16(frame + 0xA) << 5);
        QueueDma(2, rd32(frames + uint32_t(int32_t(int16_t(item * 4)))), uint32_t(dest) << 16 | 0xC0);
        FlushDma();
    }
}

// 0x821E: the three button sprites (repair, upgrade, done) from 0x4A51C,
// in the structure or unit version.
void DrawButtons() {
    uint32_t b = 0x4A51C;
    for (int i = 0; i < 3; i++, b += 0x10) {
        uint32_t n = AllocSprite(b, 0x40, rd32(b + 4), kMenuList);
        uint32_t frame = rd32(Node(uint16_t(n)) + 8);
        uint16_t dest = uint16_t(rd16(frame + 0xA) << 5);
        QueueDma(2, menu_units ? rd32(b + 0xC) : rd32(b + 8), uint32_t(dest) << 16 | 0x40);
    }
}

// 0x276EC: the text lines for an item into 0xFFC5B0 (name, cost, health,
// damage; or the repair / upgrade captions).
uint32_t ItemText(int16_t item, uint16_t structures) {
    md::Frame frame(0x38);
    uint32_t up = frame.base + 0xE, dmg = frame.base;
    for (int i = 0; i < 0xE; i++) {
        wr8(up + i, rd8(0x276BE + i));
        wr8(dmg + i, rd8(0x276CC + i));
    }
    uint32_t out = 0xFFC5B0;
    if (item >= 0) {
        uint32_t name;
        int16_t cost, damage, health;
        if (item == int8_t(rd8(0xFFBFA1))) {
            if (structures) {
                uint32_t i = rd32(0x6B954 + uint16_t(item << 2));
                name = rd32(i + 2);
                cost = int16_t(rd16(i + 0x16));
                damage = int16_t(-rd16(i + 0x3A));
                health = int16_t(rd16(i + 0x10));
                ShowSizeSprite(int16_t(rd16(i + 0x3C)));
            } else {
                uint32_t i = rd32(0x6C5BC + uint16_t(item << 2));
                name = rd32(i + 2);
                cost = int16_t(rd16(i + 0x16));
                damage = int16_t(rd16(i + 0x54));
                health = int16_t(rd16(i + 0x10));
                ShowSizeSprite(-1);
            }
        } else {
            name = up;
            uint32_t i = rd32(0x6B954 + int16_t(int8_t(rd8(U32{0xFFC578} + 2))) * 4);
            cost = int16_t(rd16(i + 0x16));
            if (cost < 0) cost = int16_t(cost + 1);
            cost = int16_t(cost >> 1);
            health = damage = 0;
        }
        MemSet(out, 0x20, 0xC);
        int16_t len = int16_t(StrLen(name));
        if (len > 0xC) len = 0xC;
        int32_t pad = 0xC - len;
        if (pad < 0) pad++;
        MemCpy(out + uint32_t(pad >> 1), name, len);
        Sprintf(out + 0xC, 0x276DA, {L(uint32_t(int32_t(cost)))});
        Sprintf(out + 0x14, 0x276E0, {L(uint32_t(int32_t(damage)))});
        Sprintf(out + 0x1C, 0x276E6, {L(uint32_t(int32_t(health)))});
        HideMinimap();
        return out;
    }
    ShowSizeSprite(-1);
    MemSet(out, 0x20, 0xC);
    if (item == -2 || item == -4) {
        MemCpy(out, dmg, 0xC);
        ShowMinimap();
    } else {
        HideMinimap();
    }
    MemSet(out + 0xC, 0x20, 8);
    MemSet(out + 0x14, 0x20, 8);
    MemSet(out + 0x1C, 0x20, 9);
    return out;
}

// 0x8306: an item shown large (portrait tiles into VRAM 0x7000 and a
// 6 x 7 map in the window), its palette (returned) and its four text
// lines as tiles of the font in the window at 0x8442's places.
uint32_t ShowItem(int16_t item, uint16_t load_palette) {
    if (item <= -4) item = int16_t(item + 2);
    int16_t text_item = item;
    uint16_t structures = menu_units;
    uint16_t pic = uint16_t(item);
    if (item < 0) {
        structures = 1;
        if (!player_house && item == 0xB) pic = 8;
        pic = rd8(0xFFBF85);
        if (int8_t(uint8_t(item) + 2) < 0) {
            pic = rd8(0xFFBFB3);
            structures = menu_units;
        }
    }
    uint32_t pics = rd32(0x82FE + structures * 4u);
    uint32_t p = rd32(pics + uint16_t(pic << 2));
    uint32_t map = p + 0x20;
    uint32_t packed = map + 0xA8;
    FlushDma();
    uint16_t size = rd16(packed);
    Decompress(packed + 2, 0xFF0000);
    QueueDma(2, 0xFF0000, uint32_t(0x7000) << 16 | uint16_t(size >> 1));
    uint32_t tiles = 0xFF0000 + size;
    for (int i = 0; i < 0x2A; i++) {
        uint32_t v = (rd32(map + i * 4u) + 0x3800380) & 0x9FFF9FFF;
        wr32(tiles + i * 4u, v);
    }
    uint16_t at = uint16_t(window_base + 0x2A8);
    for (int r = 0; r < 7; r++, at = uint16_t(at + 0x80), tiles += 0x18) QueueDma(2, tiles, uint32_t(at) << 16 | 0xC);
    if (load_palette) QueueDma(0x102, p, 0x10);
    uint32_t text = ItemText(text_item, structures);
    for (int i = 0; i < 0x12; i++) {
        uint16_t pos = uint16_t(window_base + rd16(0x8442 + i * 2u));
        uint8_t first = rd8(text++), second = rd8(text++);
        VramLong(pos, uint32_t(0x2400 | first) << 16 | uint32_t(0x2400 | second));
    }
    return p;
}

// 0x80A0: the menu's palette (0xA6C98, buttons 0xA69D8) into pal, its
// sprites, grid, icons and the first item; the sprite table hook.
void OpenMenu(uint32_t pal) {
    FlushDma();
    for (int i = 0; i < 0x20; i++) wr32(pal + i * 4u, rd32(0xA6C98 + i * 4u));
    for (int i = 0; i < 8; i++) wr32(pal + 0x40 + i * 4u, rd32(0xA69D8 + i * 4u));
    InitSpritePool(0xFFF3B0, kMenuList);
    AllocSprite(0x6A80E, 0x40, 0x6A7EE, kMenuList);
    wr8(0xFFBF84, 0xFF);
    U32{0xFFBF80} = 0x200030;
    U16{0xFFBF86} = uint16_t(AllocSprite(0xFFBF80, 0x40, 0x4A144, kMenuList));
    QueueDma(2, 0x49FC4, 0x7C0000C0);
    U16{0xFFBFAA} = uint16_t(AllocSprite(0x8838, 0x40, 0x8638, kMenuList));
    QueueDma(2, 0x8638, 0x5D800100);
    FillGrid();
    DrawIcons();
    wr8(0xFFBFA1, rd8(kGrid));
    uint32_t p = ShowItem(int8_t(rd8(kGrid)), 0);
    for (int i = 0; i < 8; i++) wr32(pal + i * 4u, rd32(p + i * 4u));
    uint16_t len = rd16(0x4F6C6);
    QueueDma(2, 0x4F6C8, uint32_t(0x8000) << 16 | uint16_t(len >> 1));
    DrawButtons();
    U16{0xFFBFA8} = 0;
    U16{0xFFBFAC} = 0;
    U16{0xFFBFAE} = 0;
    U16{0xFFBFB0} = 0;
    U32{0xFFBFBC} = frame_counter;
    vblank_hook = 0x4504;
}

// 0x8466: the pad moves the cursor a slot (sliding there over frames);
// the blinking frame of the selection redrawn each call.
void MoveMenuCursor() {
    uint16_t pad = rd8(0xFFE41A) & 0xF;
    int16_t c = int16_t(U16{0xFFBF8A}), r = int16_t(U16{0xFFBF8C});
    int way = -1;
    switch (pad) {
    case 4:
        if (--c >= 0) way = 0;
        break;
    case 8:
        if (++c < 3) way = 1;
        break;
    case 1:
    case 5:
    case 9:
        if (--r >= 0) way = 2;
        break;
    case 2:
    case 6:
    case 10:
        if (++r < 6) way = 3;
        break;
    default: break;
    }
    if (way < 0) {
        AnimateLights();
    } else {
        uint16_t slot = uint16_t(r * 3 + c);
        uint8_t item = rd8(kGrid + slot);
        if (item != 0x80) {
            U16{0xFFBF8A} = uint16_t(c);
            U16{0xFFBF8C} = uint16_t(r);
            wr8(0xFFBFA1, item);
            int16_t shown = int8_t(item);
            if (item == rd8(0xFFC618)) shown = int8_t(rd8(0xFFBFA0));
            ShowItem(shown, 1);
            uint32_t pos = rd32(Node(U16{0xFFBF86}));
            int16_t tx = int16_t(rd16(0x4A152 + slot * 8u)), ty = int16_t(rd16(0x4A154 + slot * 8u));
            for (;;) {
                int16_t x = int16_t(rd16(pos)), y = int16_t(rd16(pos + 2));
                bool more;
                switch (way) {
                case 0: x = int16_t(x - 4); more = x >= tx; break;
                case 1: x = int16_t(x + 4); more = x < tx; break;
                case 2: y = int16_t(y - 3); more = y > ty; break;
                default: y = int16_t(y + 3); more = y < ty; break;
                }
                if (!more) break;
                wr16(pos, uint16_t(x));
                wr16(pos + 2, uint16_t(y));
                MenuFrame();
                wr8(0xFFBF2B, 0xFF);
                AnimateLights();
            }
            wr16(pos, uint16_t(tx));
            wr16(pos + 2, uint16_t(ty));
        }
        uint32_t n = Node(U16{0xFFBFBA});
        U8{n + 7} |= 0x80;
        if (U32{0xFFBF80} == rd32(0x4A15A)) U8{n + 7} &= 0x7F;
    }
    U16{0xFFBFA8} += 1;
    uint16_t k = (U16{0xFFBFA8} & 0x1F) >> 3;
    uint8_t colour = rd8(0x85E0 + k);
    colour = uint8_t(colour << 4 | colour);
    for (int i = 0; i < 0x180; i++) {
        uint8_t b = rd8(0x49FC4 + i);
        wr8(0xFF0000 + i, b ? colour : 0);
    }
    QueueDma(2, 0xFF0000, 0x7C0000C0);
}

// Port: a mouse click on another slot than the cursor's moves the cursor
// there (the item shown, the A dropped); true when it did or the click
// missed the grid (slots of 32 x 24 pixels from 0x4A152).
bool ClickMenuSlot() {
    if (!(host::mouse_pad() & 0x40)) return false;
    const host::Mouse& m = host::mouse();
    int c = (m.x - 32) >> 5, r = (m.y - 48) / 24;
    if (m.x < 32 || m.y < 48 || c > 2 || r > 5) return true;
    uint16_t slot = uint16_t(r * 3 + c);
    uint8_t item = rd8(kGrid + slot);
    if (item == 0x80) return true;
    if (c == U16{0xFFBF8A} && r == U16{0xFFBF8C}) return false;
    U16{0xFFBF8A} = uint16_t(c);
    U16{0xFFBF8C} = uint16_t(r);
    wr8(0xFFBFA1, item);
    int16_t shown = int8_t(item);
    if (item == rd8(0xFFC618)) shown = int8_t(rd8(0xFFBFA0));
    ShowItem(shown, 1);
    uint32_t pos = rd32(Node(U16{0xFFBF86}));
    wr16(pos, rd16(0x4A152 + slot * 8u));
    wr16(pos + 2, rd16(0x4A154 + slot * 8u));
    uint32_t n = Node(U16{0xFFBFBA});
    U8{n + 7} |= 0x80;
    if (U32{0xFFBF80} == rd32(0x4A15A)) U8{n + 7} &= 0x7F;
    PlaySfx(0x19);
    return true;
}

} // namespace

// 0x287DC: the menu until A picks an item or B/C leave; the choice (an
// item, -2/-4 repair, -3/-5 upgrade, -1 none).
int16_t BuildMenu() {
    uint32_t t0 = FrameCounter();
    FadeOutScreen();
    DisplayOff();
    uint32_t old_hook = SetVBlankHook(0);
    ClearWindowPlane();
    SetWindowPos(0x14, 0x1E);
    SetScreenWidth(1);
    uint32_t pal = LoadScreen(5);
    OpenMenu(pal);
    MenuFrame();
    DrawMinimap(0);
    DisplayOn();
    UploadPalette(pal);
    SetPadRepeatMask(0xF0);
    PlaySfx(0x2C);
    U16{0xFFC574} += uint16_t(t0 - FrameCounter());
    int16_t choice = 0;
    for (;;) {
        GetKey();
        bool esc = host::take_escape();
        if (esc) wr8(0xFFBF2B, 'B');
        uint8_t key = rd8(0xFFBF2B);
        if (key == 'A' && ClickMenuSlot()) {
            wr8(0xFFBF2B, 0);
        } else if (key == 'A') {
            choice = CurrentItem();
            bool bad = (choice == -4 && !rd8(0xFFBFB4)) || (choice == -5 && !rd8(0xFFBFB5));
            if (!bad && rd8(U32{0xFFC578} + 2) == 5 && (choice == 0 || choice == 1) && !U16{0xFFC728}) bad = true;
            if (bad) {
                PlaySoundEffect(0x2F);
                wr8(0xFFBF2B, 0);
            }
        } else if (key == 'B' && (esc || (host::mouse_pad() & 0x10))) {
            choice = -1; // port: a right click or Esc leaves as the done button
        } else if (key == 'B' || key == 'C') {
            wr8(0xFFBF2B, 0);
        } else {
            MoveMenuCursor();
            if (rd8(0xFFBF2B) == 0xFF) PlaySfx(0x19);
            wr8(0xFFBF2B, 0);
        }
        if (S16{0xFFC574} < 0) PlayMusicTrack(uint16_t(RandomRange(0, 4) + 8));
        UpdateHouse(player_house, 0);
        AnimateLights();
        MenuFrame();
        if (rd8(0xFFBF2B)) break;
    }
    PlaySoundEffect(0x26);
    t0 = FrameCounter();
    FadeOutScreen();
    DisplayOff();
    SetWindowPos(0, 0);
    U8{0xFFBF7F} = 1;
    SetVBlankHook(old_hook);
    LoadGameTiles();
    // The original passes a register left from its caller: the structure.
    if (U32{0xFFC260}) UpdateUnitSprite(U32{0xFFC578});
    if (uint32_t s = U32{0xFFC578}) {
        if (rd8(s + 2) == 8 && (choice == 1 || choice == 0xE || choice == 0)) HideBuildMarker();
    }
    U8{0xFFBF7F} = 0;
    wr8(0xFFBF2B, 0);
    FadeIn(LoadScreen(4));
    SyncFrameDelta();
    U16{0xFFC574} += uint16_t(t0 - FrameCounter());
    return choice;
}

namespace {

// The starport menu: a 3 x 4 grid at 0xFFBFCC (0xFF empty; 0x1C send order,
// 0x1D/0x1F repair allowed/not, 0x1E/0x20 stop repair allowed/not, then the
// unit types in stock), cursor 0xFFBFC8 column / 0xFFBFCA row, its sprite
// 0xFFBFC6 at 0xFFBFC0. 0xFFC2D8: 9 offers of 12 bytes (+0 type, +2 ordered,
// +3 left, +4 price, +8 info); 0xFFBFF4 the order's total.
constexpr uint32_t kOffers = 0xFFC2D8;
constexpr uint32_t kPortGrid = 0xFFBFCC;

// 0x27334: a price, 60-160% of cost.
int16_t RandomPrice(uint16_t cost) {
    int32_t r = int32_t(RandomRange(0, 0x32));
    r += int32_t(RandomRange(0, 0x32)) + 0x3C;
    return int16_t(int32_t(uint32_t(r) * cost) / 100);
}

// 0x8F14: the grid (buttons, then each type with stock 0xFFC2A0 >= 0) and
// the offers from it; prices drawn again when 0xFFBF88 ran out.
void FillPortGrid() {
    U32{0xFFBFC8} = 0;
    for (int i = 0; i < 0x12; i++) wr8(kPortGrid + i, 0xFF);
    wr32(kPortGrid, 0x1C1D1EFF);
    if (!rd8(0xFFBFB4)) wr8(kPortGrid + 1, 0x1F);
    if (!rd8(0xFFBFB5)) wr8(kPortGrid + 2, 0x20);
    uint32_t out = kPortGrid + 3;
    for (uint16_t t = 0; t < 0x1C; t++)
        if (int16_t(rd16(0xFFC2A0 + t * 2u)) >= 0) wr8(out++, uint8_t(t));
    int16_t timer = int16_t(U16{0xFFBF88});
    uint32_t e = kOffers;
    for (int i = 0; i < 10; i++) {
        uint8_t t = rd8(kPortGrid + 3 + i);
        if (int8_t(t) < 0) {
            wr32(e + 2, 0xFFFFFFFF);
            wr32(e + 4, 0xFFFFFFFF);
            wr32(e + 8, 0xFFFFFFFF);
            continue;
        }
        wr16(e, t);
        wr8(e + 2, 0);
        wr8(e + 3, rd8(0xFFC2A1 + t * 2u));
        uint32_t info = rd32(0x6C5BC + t * 4u);
        wr32(e + 8, info);
        if (timer <= 0) wr16(e + 4, uint16_t(RandomPrice(rd16(info + 0x16))));
        wr16(e + 6, 0);
        e += 0xC;
    }
    if (timer <= 0) U16{0xFFBF88} = 0x4650;
}

// 0x8FEC: the grid's icons (slots at 0x4A576, frames 0x70CA4).
void DrawPortIcons() {
    uint32_t slot = 0x4A576;
    for (int i = 0; i < 0xC; i++, slot += 8) {
        uint8_t t = rd8(kPortGrid + i);
        if (t == 0xFF) break;
        uint32_t n = AllocSprite(slot, 0x40, rd32(slot + 4), kMenuList);
        U16{n + 6} |= 0x6000;
        wr16(0xFFBFDE + i * 2u, uint16_t(n));
        uint16_t dest = uint16_t(rd16(rd32(slot + 4) + 0xA) << 5);
        QueueDma(2, rd32(0x70CA4 + t * 4u), uint32_t(dest) << 16 | 0xC0);
    }
}

// 0x27940: the text lines for an item into 0xFFC5B0: name (or out of
// stock), price, damage, health; for the buttons blank, the repair one
// with the damage caption and the minimap.
uint32_t PortItemText(uint16_t item) {
    uint32_t out = 0xFFC5B0;
    if (item < 0x1C) {
        int16_t left = 0, price = 0;
        for (int i = 0; i < 9; i++) {
            uint32_t e = kOffers + i * 0xCu;
            if (rd16(e) == item) {
                left = int8_t(rd8(e + 3));
                price = int16_t(rd16(e + 4));
            }
        }
        uint32_t info = rd32(0x6C5BC + item * 4u);
        uint32_t name = left ? rd32(info + 2) : 0x27904;
        MemSet(out, 0x20, 0xC);
        int16_t len = int16_t(StrLen(name));
        if (len > 0xC) len = 0xC;
        int32_t pad = 0xC - len;
        if (pad < 0) pad++;
        StrNCpy(out + uint32_t(pad >> 1), name, len);
        Sprintf(out + 0xC, 0x2792E, {L(uint32_t(int32_t(price)))});
        Sprintf(out + 0x14, 0x27934, {L(uint32_t(int32_t(int16_t(rd16(info + 0x54)))))});
        Sprintf(out + 0x1C, 0x2793A, {L(uint32_t(int32_t(int16_t(rd16(info + 0x10)))))});
        HideMinimap();
        return out;
    }
    MemSet(out, 0x20, 0xC);
    if (item == 0x1D || item == 0x1F) {
        StrNCpy(out, 0x27920, 0xC);
        ShowMinimap();
    } else {
        HideMinimap();
    }
    MemSet(out + 0xC, 0x20, 8);
    MemSet(out + 0x14, 0x20, 8);
    MemSet(out + 0x1C, 0x20, 9);
    return out;
}

// 0x9058, 0x905E: an item shown large (pictures 0x8F1D0) with its text;
// its palette returned.
uint32_t ShowPortItem(uint16_t item) {
    uint32_t p = rd32(0x8F1D0 + uint16_t(item << 2));
    uint32_t map = p + 0x20;
    uint32_t packed = map + 0xA8;
    uint16_t size = rd16(packed);
    Decompress(packed + 2, 0xFF0000);
    QueueDma(2, 0xFF0000, uint32_t(0x7000) << 16 | uint16_t(size >> 1));
    uint32_t tiles = 0xFF0000 + size;
    for (int i = 0; i < 0x2A; i++) wr32(tiles + i * 4u, (rd32(map + i * 4u) + 0x3800380) & 0x9FFF9FFF);
    uint16_t at = uint16_t(window_base + 0x2A8);
    for (int r = 0; r < 7; r++, at = uint16_t(at + 0x80), tiles += 0x18) QueueDma(2, tiles, uint32_t(at) << 16 | 0xC);
    QueueDma(0x102, p, 0x10);
    uint32_t text = PortItemText(item);
    for (int i = 0; i < 0x12; i++) {
        uint16_t pos = uint16_t(window_base + rd16(0x914C + i * 2u));
        uint8_t first = rd8(text++), second = rd8(text++);
        VramLong(pos, uint32_t(0x2400 | first) << 16 | uint32_t(0x2400 | second));
    }
    return p;
}

// 0x8DD8: the menu's palette (0xA6C98, buttons 0xA6C58) into pal, its
// sprites (cursor, icons, buttons, the two animated figures), the first
// item; the sprite table hook.
void OpenStarport(uint32_t pal) {
    FlushDma();
    U16{0xFFBFF4} = 0;
    for (int i = 0; i < 0x20; i++) wr32(pal + i * 4u, rd32(0xA6C98 + i * 4u));
    for (int i = 0; i < 8; i++) wr32(pal + 0x40 + i * 4u, rd32(0xA6C58 + i * 4u));
    InitSpritePool(0xFFF3B0, kMenuList);
    AllocSprite(0x6A80E, 0x40, 0x6A7EE, kMenuList);
    wr8(0xFFBFC4, 0xFF);
    U32{0xFFBFC0} = 0x200030;
    U16{0xFFBFC6} = uint16_t(AllocSprite(0xFFBFC0, 0x40, 0x4A144, kMenuList));
    QueueDma(2, 0x49FC4, 0x7C0000C0);
    FillPortGrid();
    DrawPortIcons();
    uint32_t p = ShowPortItem(uint16_t(int8_t(rd8(kPortGrid))));
    for (int i = 0; i < 8; i++) wr32(pal + i * 4u, rd32(p + i * 4u));
    uint16_t len = rd16(0x4F6C6);
    QueueDma(2, 0x4F6C8, uint32_t(0x8000) << 16 | uint16_t(len >> 1));
    DrawButtons();
    U16{0xFFBFF6} = uint16_t(AllocSprite(0xFFBFFE, 0x40, 0x4A696, kMenuList));
    U16{0xFFBFF8} = 0xB;
    U32{0xFFBFFA} = 0;
    U16{0xFFC004} = 0;
    U16{0xFFC006} = 0;
    U16{0xFFC010} = uint16_t(AllocSprite(0xFFC00A, 0x40, 0x4A70C, kMenuList));
    U32{0xFFBFBC} = frame_counter;
    vblank_hook = 0x4504;
}

// 0x88DA: the window's animations: a figure turning (0x894A frames), two
// blinking lights, and a figure walking along 0x8B0C in a random palette.
void AnimatePort() {
    if (int16_t(--U16{0xFFBFFA}) < 0) {
        U16{0xFFBFFA} = U16{0xFFBFF8};
        if (int16_t(--U16{0xFFBFFC}) < 0) {
            U16{0xFFBFFC} = 9;
            U16{0xFFBFF8} = RandomRange(7, 0x11);
            wr8(0xFFC003, uint8_t(frame_counter & 1));
        }
        uint16_t k = U16{0xFFBFFC};
        if (int8_t(rd8(0xFFC003)) <= 0) k = uint16_t(9 - k);
        U32{0xFFBFFE} = rd32(0x894A + k * 8u);
        wr32(Node(U16{0xFFBFF6}) + 8, rd32(0x894E + k * 8u));
    }
    if (int16_t(--U16{0xFFC004}) < 0) {
        U16{0xFFC004} = uint16_t(int8_t(RandomRange(7, 0x1B)));
        QueueDma(2, 0x89D6 + uint16_t((frame_counter & 3) << 3), uint32_t(uint16_t(window_base + 0x842)) << 16 | 4);
    }
    if (int16_t(--U16{0xFFC006}) < 0) {
        U16{0xFFC006} = uint16_t(int8_t(RandomRange(4, 0xB)));
        QueueDma(2, 0x8A36 + uint16_t((frame_counter & 0xF) * 6), uint32_t(uint16_t(window_base + 0x9A)) << 16 | 3);
    }
    bool turn;
    if (U16{0xFFC00E}) turn = int16_t(--U16{0xFFC014}) < 0;
    else turn = int16_t(++U16{0xFFC014}) > 0xB3;
    if (turn) {
        uint16_t pal = uint16_t(RandomRange(0, 3) << 11);
        U16 attr{Node(U16{0xFFC010}) + 6};
        attr = uint16_t((attr & 0xE7FF) | pal);
        U16{0xFFC014} = 0;
        U16{0xFFC00E} = Random() & 1;
        if (U16{0xFFC00E}) U16{0xFFC014} = 0xB3;
    }
    uint16_t k = U16{0xFFC014};
    U16{0xFFC00C} = rd16(0x8B0C + k * 4u);
    U16{0xFFC00A} = rd16(0x8B0E + k * 4u);
}

// 0x9170: the pad moves the cursor a slot (sliding there over frames),
// else the animations; the blinking selection frame redrawn, a frame shown.
void MovePortCursor() {
    uint16_t pad = rd8(0xFFE41A) & 0xF;
    int16_t c = int16_t(U16{0xFFBFC8}), r = int16_t(U16{0xFFBFCA});
    int way = -1;
    switch (pad) {
    case 4:
        if (--c >= 0) way = 0;
        break;
    case 8:
        if (++c < 3) way = 1;
        break;
    case 1:
    case 5:
    case 9:
        if (--r >= 0) way = 2;
        break;
    case 2:
    case 6:
    case 10:
        if (++r < 4) way = 3;
        break;
    default: break;
    }
    if (way < 0) {
        AnimatePort();
    } else {
        uint16_t slot = uint16_t(r * 3 + c);
        uint8_t item = rd8(kPortGrid + slot);
        if (item != 0xFF) {
            U16{0xFFBFC8} = uint16_t(c);
            U16{0xFFBFCA} = uint16_t(r);
            ShowPortItem(uint16_t(int8_t(item)));
            uint32_t pos = rd32(Node(U16{0xFFBFC6}));
            int16_t tx = int16_t(rd16(0x4A576 + slot * 8u)), ty = int16_t(rd16(0x4A578 + slot * 8u));
            for (;;) {
                int16_t x = int16_t(rd16(pos)), y = int16_t(rd16(pos + 2));
                bool more;
                switch (way) {
                case 0: x = int16_t(x - 4); more = x >= tx; break;
                case 1: x = int16_t(x + 4); more = x < tx; break;
                case 2: y = int16_t(y - 3); more = y > ty; break;
                default: y = int16_t(y + 3); more = y < ty; break;
                }
                if (!more) break;
                wr16(pos, uint16_t(x));
                wr16(pos + 2, uint16_t(y));
                MenuFrame();
                wr8(0xFFBF2B, 0xFF);
                AnimatePort();
            }
            wr16(pos, uint16_t(tx));
            wr16(pos + 2, uint16_t(ty));
        }
    }
    U16{0xFFC008} += 1;
    uint16_t k = (U16{0xFFC008} & 0x1F) >> 3;
    uint8_t colour = rd8(0x92B8 + k);
    colour = uint8_t(colour << 4 | colour);
    for (int i = 0; i < 0x180; i++) wr8(0xFF0000 + i, rd8(0x49FC4 + i) ? colour : 0);
    QueueDma(2, 0xFF0000, 0x7C0000C0);
    MenuFrame();
}

} // namespace

// 0x28A18: the starport for structure s: A on a unit orders one (paid at
// once) and leaves, on send order leaves, on repair / stop repair sets it
// and leaves.
void StarportMenu(uint32_t s) {
    uint32_t t0 = FrameCounter();
    FadeOutScreen();
    DisplayOff();
    uint32_t old_hook = SetVBlankHook(0);
    ClearWindowPlane();
    SetWindowPos(0x14, 0x1E);
    SetScreenWidth(1);
    uint32_t pal = LoadScreen(6);
    OpenStarport(pal);
    MenuFrame();
    DrawMinimap(1);
    DisplayOn();
    UploadPalette(pal);
    SetPadRepeatMask(0xF0);
    PlaySfx(0x2C);
    U16{0xFFC574} += uint16_t(t0 - FrameCounter());
    for (;;) {
        GetKey();
        if (host::take_escape()) break; // port: Esc leaves as DONE
        uint8_t key = rd8(0xFFBF2B);
        if (key == 'A') {
            uint16_t item = rd8(kPortGrid + U16{0xFFBFCA} * 3u + U16{0xFFBFC8});
            uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
            switch (item) {
            case 0x1C: break;
            case 0x1D:
                if (int16_t(rd16(s + 0x12)) < int16_t(rd16(info + 0x10))) {
                    U8{s + 4} |= 0x20;
                    SetRepairMark(s, rd16(info + 0x3C));
                }
                break;
            case 0x1E:
                U8{s + 4} &= uint8_t(~0x20);
                ClearMark(s, 2);
                break;
            case 0x1F:
            case 0x20:
                PlaySoundEffect(0x2F);
                wr8(0xFFBF2B, 0);
                break;
            default:
                if ((rd16(s + 6) & 0x100) || U32{0xFFC260}) {
                    PlaySoundEffect(0x2F);
                    wr8(0xFFBF2B, 0);
                    break;
                }
                for (int i = 0; i < 9; i++) {
                    uint32_t e = kOffers + i * 0xCu;
                    if (rd16(e) != item) continue;
                    uint32_t h = U32{0xFFC278};
                    uint32_t price = uint32_t(int32_t(int16_t(rd16(e + 4))));
                    if (price > rd32(h + 0x12) || int8_t(rd8(e + 3)) <= 0 || (item == 1 && !U16{0xFFC728})) {
                        PlaySoundEffect(0x2F);
                        wr8(0xFFBF2B, 0);
                        break;
                    }
                    U8{e + 2} += 1;
                    U8{e + 3} -= 1;
                    if (price > U32{0xFFC058}) U32{0xFFC058} = price;
                    U32{0xFFC058} -= price;
                    UpdateHouse(player_house, 3);
                    U32{U32{0xFFC278} + 0x12} -= price;
                    U16{0xFFBFF4} += uint16_t(price);
                    if (!rd8(e + 3)) ShowPortItem(item);
                    StructureBuild(s, 1);
                    break;
                }
                break;
            }
        } else if (key == 'B' || key == 'C') {
            wr8(0xFFBF2B, 0);
        } else {
            MovePortCursor();
            if (rd8(0xFFBF2B) == 0xFF) PlaySfx(0x19);
            wr8(0xFFBF2B, 0);
        }
        if (S16{0xFFC574} < 0) PlayMusicTrack(uint16_t(RandomRange(0, 4) + 8));
        UpdateHouse(player_house, 0);
        AnimatePort();
        MenuFrame();
        if (rd8(0xFFBF2B)) break;
    }
    PlaySoundEffect(0x26);
    t0 = FrameCounter();
    FadeOutScreen();
    DisplayOff();
    SetWindowPos(0, 0);
    U8{0xFFBF7F} = 1;
    SetVBlankHook(old_hook);
    LoadGameTiles();
    if (U32{0xFFC260}) UpdateUnitSprite(U32{0xFFC260});
    U8{0xFFBF7F} = 0;
    wr8(0xFFBF2B, 0);
    SideMenu();
    FadeIn(LoadScreen(4));
    SyncFrameDelta();
    U16{0xFFC574} += uint16_t(t0 - FrameCounter());
}

namespace {

// 0x28D7E: the map cursor after the menu: placement for a yard's ready
// structure, else free.
void CursorAfterMenu() {
    uint32_t s = U32{0xFFC578};
    if (s && rd8(s + 2) == 8 && (rd16(s + 6) & 0x2000)) {
        UsePlacementCursor();
        uint16_t k = rd16(rd32(0x6B954 + uint16_t(rd16(s + 0x52) << 2)) + 0x3C);
        SetCursorLimits(0, uint32_t(rd16(0xC7EFC + uint16_t(k << 2))) << 16 | rd16(0xC7EFE + uint16_t(k << 2)));
        return;
    }
    UseFreeCursor();
    SetCursorLimits(0x180018, 0x12800C8);
}

// The repair (-2) and stop-repair (-3) buttons.
void RepairChoice(uint32_t s, int16_t choice, uint16_t max, bool cancel) {
    if (choice == -2 && max > rd16(s + 0x12)) {
        U8{s + 4} |= 0x20;
        SetRepairMark(s, rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x3C));
    }
    if (choice == -3) {
        if (cancel) CancelBuild(s);
        U8{s + 4} &= uint8_t(~0x20);
        ClearMark(s, 2);
    }
}

// An upgrade picked: the build stopped, the upgrade started (+0x55 100%).
void StartUpgrade(uint32_t s) {
    U8{s + 7} |= 2;
    wr8(s + 0x55, 100);
    StructureBuild(s, -3);
    wr8(s + 0x50, rd8(0xFFBFA0));
    PlaySoundEffect(0xC);
}

} // namespace

// 0xFA2C: the structure types a house has standing (bit per type; a
// type 4 counts as type 3 too).
uint32_t OwnedTypes(uint16_t house) {
    uint32_t bits = 0;
    for (uint32_t s = FirstListedStructure(house == player_house ? 0 : 1); s; s = NextListedStructure())
        if (!(rd16(s + 4) & 4)) bits |= 1u << int8_t(rd8(s + 2));
    if (bits & 0x10) bits |= 8;
    return bits;
}

// 0x28F3C: A on the player's selected structure: its menu (a yard's
// structures, a factory's units, a starport's order, others repair only)
// and what was picked started.
void StructureMenu() {
    uint32_t s = U32{0xFFC578};
    if (!s || int8_t(rd8(s + 8)) != int16_t(player_house)) return;
    uint8_t type = rd8(s + 2);
    uint16_t max = rd16(rd32(0x6B954 + int16_t(int8_t(type)) * 4) + 0x10);
    menu_units = 1;
    wr8(0xFFBF85, type);
    wr8(0xFFBFB2, type);
    wr8(0xFFBFB4, max > rd16(s + 0x12) ? 1 : 0);
    wr8(0xFFBFB3, type);
    wr8(0xFFBFB5, (rd16(s + 4) & 0x2000) ? 1 : 0);
    wr32(U32{0xFFC278} + 0xE, OwnedTypes(player_house));
    StructureBuild(s, -1);
    switch (type) {
    case 8: {
        U32{0xFFC61C} = 0xFFC63C;
        wr8(0xFFC618, 0x13);
        wr8(0xFFBFB3, uint8_t(rd16(s + 0x52)));
        if (rd16(s + 6) & 0x6002) wr8(0xFFBFB5, 1);
        int16_t c = BuildMenu();
        if (c >= 0) {
            U8{s + 4} &= uint8_t(~0x20);
            ClearMark(s, 2);
            if (c < 0x13) {
                if (int8_t(rd8(s + 3)) < 0 || c != int16_t(rd16(s + 0x52))) {
                    if (StructureBuild(s, c)) {
                        U8{s + 6} |= 0x40;
                        U8{s + 6} &= uint8_t(~0x20);
                        U16{0xFFC030} = 0x29;
                        PlaySoundEffect(0xD);
                    } else {
                        PlaySoundEffect(0x2F);
                    }
                    U8{s + 6} &= uint8_t(~0x10);
                }
                ShowIcon(int16_t(rd16(rd32(0x6B954 + uint16_t(c << 2)) + 0x14)));
            } else if (!(rd16(s + 6) & 2)) {
                CancelBuild(s);
                U16{s + 6} &= 0x9FFF;
                U8{s + 4} &= uint8_t(~0x20);
                ClearMark(s, 2);
                StartUpgrade(s);
            }
        } else {
            RepairChoice(s, c, max, true);
            if (c == -3) U16{s + 6} &= 0x9FFF;
        }
        CursorAfterMenu();
        break;
    }
    case 3:
    case 4:
    case 5:
    case 7:
    case 0xA: {
        U32{0xFFC61C} = 0xFFC620;
        wr8(0xFFC618, 0x1B);
        menu_units = 0;
        wr8(0xFFBFB3, uint8_t(rd16(s + 0x52)));
        if (rd16(s + 6) & 0x4002) wr8(0xFFBFB5, 1);
        int16_t c = BuildMenu();
        if (c >= 0) {
            U8{s + 4} &= uint8_t(~0x20);
            ClearMark(s, 2);
            if (c < 0x1B) {
                if (StructureBuild(s, c)) {
                    U8{s + 6} |= 0x40;
                    U16{0xFFC030} = 0x29;
                    PlaySoundEffect(0xD);
                } else {
                    PlaySoundEffect(0x2F);
                }
                ShowIcon(int16_t(rd16(rd32(0x6C5BC + uint16_t(c << 2)) + 0x14)));
                U8{s + 6} &= uint8_t(~0x10);
            } else {
                CancelBuild(s);
                U8{s + 6} &= uint8_t(~0x40);
                U8{s + 4} &= uint8_t(~0x20);
                ClearMark(s, 2);
                StartUpgrade(s);
            }
        } else {
            RepairChoice(s, c, max, true);
            if (c == -3) U8{s + 6} &= uint8_t(~0x40);
        }
        CursorAfterMenu();
        break;
    }
    case 9:
    case 0xC:
    case 0xD:
    case 0xF:
    case 0x10:
    case 0x11:
    case 0x12:
        HidePanelIcon();
        U32{0xFFC61C} = 0;
        RepairChoice(s, BuildMenu(), max, false);
        CursorAfterMenu();
        break;
    case 0xB:
        menu_units = 0;
        StarportMenu(s);
        CursorAfterMenu();
        HidePanelIcon();
        break;
    case 2: {
        U32{0xFFC61C} = 0;
        int16_t c = BuildMenu();
        if (c > 0) {
            if (!rd16(s + 0x4E)) U8{s + 7} |= 0x80;
        } else {
            RepairChoice(s, c, max, false);
        }
        break;
    }
    default: HidePanelIcon(); break;
    }
    wr8(0xFFBF2A, 0);
    wr8(0xFFBF2B, 0);
}

} // namespace game
