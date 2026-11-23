// The player's commands: the A/B/Start buttons on the map select units and
// structures, give orders, place structures and fire special weapons.
// RAM: 0xFFC578 the selected structure, 0xFFC57C the player's last selected
// structure, 0xFFC25C the selected unit, 0xFFDC4C a special weapon armed.
// Object word +6: 0x8000 highlighted, 0x2000 a structure ready to place,
// 0x80 a special weapon ready (palace) or an order blinking (units).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U16 game_mode{0xFFC264};
constexpr U32 selected_struct{0xFFC578};
constexpr U32 last_struct{0xFFC57C};
constexpr U32 selected_unit{0xFFC25C};
constexpr U16 special_armed{0xFFDC4C};

int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
bool Mine(uint32_t o) { return HouseOf(o) == int16_t(player_house); }
uint32_t UnitInfo(uint32_t u) { return rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4); }
uint32_t StructInfo(int16_t type) { return rd32(0x6B954 + type * 4); }
void Highlight(uint32_t o) { U16{o + 6} |= 0x8000; }
void Unhighlight(uint32_t o) { U16{o + 6} &= 0x7FFF; }

// A palace with its special weapon ready to arm.
bool SpecialReady(uint32_t s) {
    return rd8(s + 2) == 2 && (rd16(s + 6) & 0x80) && !rd16(s + 0x56) && !(rd16(s + 6) & 0x100);
}

// 0x4594: a cell not in the middle of being revealed (the fog step in map
// byte 0 against 0xFFC224, the last 16 steps by the table at 0x45D0).
uint16_t CellVisible(uint16_t cell) {
    uint16_t d = (rd8(rd32(0x4AA24) + uint16_t(cell << 2)) >> 1) & 0x7F;
    if (!d) return 1;
    int16_t t = int16_t(U16{0xFFC224});
    if (int16_t(d) > t || int16_t(d) < int16_t(t - 0xF)) return 1;
    return rd8(0x45D0 + d - (t - 0xF));
}

// 0xA752: an object's sprite (pos + 6) blinks at a rate for some frames
// (pos + 5), with a sound repeated per blink.
void BlinkObject(uint32_t pos, uint16_t rate, uint16_t frames, uint16_t sfx) {
    if (!pos) return;
    wr8(pos + 5, uint8_t(frames));
    uint32_t n = Node(rd16(pos + 6));
    wr8(n + 4, uint8_t(rate));
    wr8(n + 5, uint8_t(rate));
    U8{n + 7} |= 0x20;
    wr8(n, uint8_t(sfx));
    PlaySoundEffect(sfx);
}


// 0x27398: a thick spice cell next to a spice cell, else the cell.
uint16_t ThickSpiceNear(uint16_t cell) {
    uint32_t t = 0xC7F18;
    uint16_t terr = TerrainAt(cell);
    for (int i = 0; i < 8; i++, t += 4) {
        uint16_t c = uint16_t(cell + rd16(t) + rd16(t + 2));
        if (CellInMap(c) && terr == 8 && TerrainAt(c) == 9) return c;
    }
    return cell;
}

// 0x293C8: the upgrade level of a structure about to be placed by
// scenario; a light factory (3) becomes a heavy factory (4) from scenario 4.
void InitUpgradeLevel(uint32_t s) {
    uint16_t scen = U16{0xFFC050};
    if (rd8(s + 2) == 3) {
        switch (scen) {
        case 3: wr8(s + 0x54, 1); break;
        case 7:
        case 8:
            if (player_house == 1) U8{s + 0x54} += 1;
            [[fallthrough]];
        case 6:
            if (player_house != 2) U8{s + 0x54} += 1;
            [[fallthrough]];
        case 5: U8{s + 0x54} += 1; [[fallthrough]];
        case 4: {
            uint32_t h = rd32(0x4A9FA + HouseOf(s) * 4);
            U8{h + 3} -= 1;
            wr8(s + 2, 4);
            U8{h + 4} += 1;
            break;
        }
        }
        wr16(s + 0x52, UpgradeItem(s));
    }
    if (rd8(s + 2) == 5) {
        if ((scen == 7 || scen == 8) && player_house) U8{s + 0x54} += 1;
        wr16(s + 0x52, UpgradeItem(s));
    }
    if (rd8(s + 2) == 8) wr8(s + 0x54, (scen == 7 || scen == 8) ? 1 : 0);
}



// Button A on the map; final when the selection is left as it is.
uint16_t PressA(uint16_t cell, bool& final) {
    final = false;
    uint16_t done = 0;
    U16{0xFFC244} = cell;
    uint32_t s = StructureAt(cell);
    uint32_t u = selected_unit;
    uint32_t entered = 0;
    if (u && Mine(u)) {
        entered = s;
        s = 0;
    }
    if (uint32_t cur = selected_struct; cur && Mine(cur)) {
        if (s != cur && rd8(cur + 2) == 2 && (rd16(cur + 6) & 0x80) && special_armed) s = 0;
        if (s && s != cur && rd8(cur + 2) == 8 && (rd16(cur + 6) & 0x2000)) s = 0;
    }
    if (s) {
        U16{0xFFC248} = cell;
        if (u) {
            U16{u + 6} &= 0x5FFF;
            selected_unit = 0;
            DropSelection();
        }
        if (s != selected_struct) {
            if (CellVisible(cell)) {
                if (selected_struct) Unhighlight(selected_struct);
                selected_struct = s;
                Highlight(s);
                PlaySoundEffect(0x26);
                if (Mine(s)) {
                    last_struct = s;
                    if (rd8(s + 2) == 8 && !(rd16(s + 6) & 0x6000)) {
                        uint32_t h = rd32(0x4A9FA + HouseOf(s) * 4);
                        if (rd16(s + 0x52) == 0xB && rd8(h + 0xB)) wr16(s + 0x52, 1);
                        if (rd16(s + 0x52) == 2 && rd8(h + 2)) wr16(s + 0x52, 1);
                    }
                }
            }
        } else if (rd16(s + 6) & 0x8000) {
            if (SpecialReady(s) && !special_armed) {
                special_armed = 1;
                ClearMark(s, 1);
            } else {
                StructureMenu();
            }
        } else {
            PlaySoundEffect(0x26);
            Highlight(s);
            UpdatePanel();
        }
        return done;
    }

    if (uint32_t t = selected_struct) {
        if (rd16(t + 6) & 0x8000) {
            if (rd8(t + 2) == 8) {
                if (rd16(t + 6) & 0x2000) {
                    U16{0xFFC170} = rd16(t + 0x52);
                    U16{0xFFC180} = uint16_t(CanPlace(cell, U16{0xFFC170}));
                    final = true;
                    if (!U16{0xFFC180}) {
                        PlaySoundEffect(0x2F);
                        return done;
                    }
                    U16{0xFFC248} = cell;
                    uint32_t p = StructureGet(uint16_t(int8_t(rd8(t + 3))));
                    U32{0xFFC174} = p;
                    U16{0xFFC178} = rd16(p);
                    U16{0xFFC188} = cell;
                    wr8(t + 3, 0xFF);
                    if (game_mode != 2) SetGameMode(2);
                    U8{t + 6} &= uint8_t(~0x40);
                    U16{0xFFC030} = 0x26;
                    done = 1;
                    PlaySoundEffect(0x14);
                    InitUpgradeLevel(U32{0xFFC174});
                    wr32(0xFFBF0E, 0x100010);
                    SetCursorShape(0, -1);
                    p = U32{0xFFC174};
                    if (rd8(p + 2) == 0xB || rd8(p + 2) == 2) wr16(t + 0x52, 1);
                    return done;
                }
            } else if (SpecialReady(t) && special_armed) {
                U16{0xFFC1A8} = cell;
                LaunchSpecial(t);
                if (!rd8(t + 8)) OrderSpecial(cell);
                special_armed = 0;
                U16{0xFFC1A8} = 0;
                wr8(0xFFBF2B, 0);
                wr8(0xFFBF2A, 0);
                U8{t + 7} &= 0x7F;
            }
        } else if (StructureAt(cell) == t) {
            Highlight(t);
        }
    }

    U16{0xFFC248} = cell;
    if (u && (rd16(u + 6) & 0x8000)) {
        uint32_t t = CellVisible(cell) ? UnitAt(cell) : 0;
        if (!Mine(u) || rd8(u + 2) == 6) {
            if (t == u) return done;
            U16{u + 6} &= 0x5FFF;
            selected_unit = t;
            if (t && CellVisible(cell)) {
                PlaySoundEffect(0x26);
                Highlight(t);
                if (Mine(t) && rd8(u + 2) != 6) U8{t + 6} |= 0x20;
            }
            return done;
        }
        U32{0xFFC18C} = u;
        uint16_t terr = TerrainAt(cell);
        if (t && t != u && Mine(t)) t = 0;
        uint16_t order;
        uint8_t type = rd8(u + 2);
        if (type == 0x10) {
            order = 1;
            if (terr == 8 || terr == 9) {
                order = 5;
                U8{u + 7} &= uint8_t(~0x20);
                if (ThickSpiceNear(cell) != cell) U8{u + 6} |= 4;
            }
            goto ack;
        }
        if (type == 0x11) {
            if (t != u) {
                order = 1;
                goto stop_blink;
            }
            order = 7;
            if (rd16(u + 6) & 0x80) goto stop_blink;
            {
                uint32_t c = rd32(0x4AA24) + int16_t(cell) * 4;
                wr8(c + 3, 0);
                if (CanPlace(cell, 8)) {
                    U8{u + 7} |= 0x80;
                    BlinkObject(u + 0xA, 8, 0xFF, 0x2E);
                } else {
                    PlaySoundEffect(0x2F);
                }
                wr8(c + 3, uint8_t(rd16(u) + 1));
            }
            goto ack;
        }
        order = 3;
        if (t == u) goto launcher;
        order = 1;
        if (t || entered || terr == 0xE) {
            order = 0;
            if (terr == 0xE) U8{u + 7} |= 0x20;
        }
        if (entered) {
            if (CanEnter(u, entered)) order = 1;
            else if (Mine(entered) && rd8(entered + 2) == 0xD) order = 3;
        } else if (t) {
            if (Mine(t)) {
                if (rd8(t + 0x54) == 1) {
                    SetUnitAction(u, 1);
                    UnitMoveTo(u, rd16(t + 0x5C));
                } else {
                    SetUnitAction(u, int16_t(order));
                    UnitAttack(u, MakeTarget(rd16(t), 1));
                    U8{t + 6} |= 8;
                    BlinkObject(t + 0xA, 6, 0x18, 0x24);
                }
                selected_unit = 0;
                DropSelection();
                final = true;
                return done;
            }
            U8{t + 6} |= 8;
            BlinkObject(t + 0xA, 6, 0x18, 0x24);
        } else {
            if (terr == 0xB) order = 0;
            PlaySoundEffect(0x24);
        }
    launcher:
        if (rd8(u + 2) == 0xB) {
            if (rd16(u + 6) & 0x80) goto stop_blink;
            if (order == 3) {
                U8{u + 7} |= 0x80;
                BlinkObject(u + 0xA, 8, 0xFF, 0x2E);
            }
        }
        goto ack;
    stop_blink:
        U8{u + 7} &= 0x7F;
        wr8(u + 0xF, 0);
        TickBlink(u + 0xA);
    ack:
        PlaySoundEffect(rd16(UnitInfo(u) + 0x3E) ? 0x12 : 0x10);
        U16{0xFFC190} = order;
        if (game_mode != 1) SetGameMode(1);
        done = 1;
        U16{u + 6} &= 0x5FFF;
        selected_unit = 0;
        DropSelection();
        if (selected_struct) Highlight(selected_struct);
        return done;
    }

    uint32_t v = UnitAt(SnapToUnit(cell));
    if (v) {
        if (CellVisible(PosCell(rd32(v + 0xA)))) {
            HidePanelIcon();
            DropSelection();
            wr8(0xFFBF7E, 1);
            PlaySoundEffect(0x26);
            Highlight(v);
            if (Mine(v) && rd8(v + 2) != 6) {
                U8{v + 6} |= 0x20;
                PlaySoundEffect(rd16(UnitInfo(v) + 0x3E) ? 0xF : 0x11);
            }
        } else {
            v = 0;
        }
    }
    selected_unit = v;
    if (selected_struct && v) Unhighlight(selected_struct);
    return done;
}

// Button B: a structure builds its item (or a palace disarms); a unit
// stops blinking or is deselected.
void PressB() {
    uint32_t s = selected_struct;
    uint32_t u = selected_unit;
    if (s && !u) {
        if (!Mine(s)) {
            Unhighlight(s);
            s = last_struct;
            selected_struct = s;
            if (s) {
                PlaySoundEffect(0x26);
                Highlight(s);
            }
        } else if (rd16(StructInfo(int8_t(rd8(s + 2))) + 0xC) & 2) {
            uint16_t f = rd16(s + 6);
            if (!(f & 0x8000)) {
            } else if (f & 0x6000) {
                if (f & 0x2000) {
                    if (rd8(s + 2) == 8) {
                        Unhighlight(s);
                        selected_struct = 0;
                    }
                    if (!selected_struct) return;
                }
            } else if (!(rd16(s + 4) & 0x2000) && !(f & 2)) {
                if ((rd8(s + 2) == 8 && (rd16(s + 0x52) == 1 || rd16(s + 0x52) == 0xE)) || !U16{0xFFC724}) {
                    if (StructureBuild(s, int16_t(rd16(s + 0x52)))) {
                        U8{s + 6} |= 0x40;
                        U16{0xFFC030} = 0x29;
                        PlaySoundEffect(0xD);
                    } else {
                        ShowBuildMarker();
                        PlaySoundEffect(0x2F);
                    }
                } else {
                    PlaySoundEffect(0x2F);
                }
            }
        } else if (rd8(s + 2) == 2 && (rd16(s + 6) & 0x80)) {
            special_armed = 0;
        }
    }
    if (!u) return;
    if (rd16(u + 6) & 0x80) {
        wr8(u + 0xF, 0);
        TickBlink(u + 0xA);
        return;
    }
    Unhighlight(u);
    selected_unit = 0;
    DropSelection();
    if (!s) return;
    if (!Mine(s)) {
        s = last_struct;
        selected_struct = s;
    }
    if (s) Highlight(s);
}

// No button: the cursor's shape for what is selected.
void CursorIdle() {
    int16_t a = 0, b = 0;
    uint32_t s = selected_struct;
    uint32_t u = selected_unit;
    if (s && !u && Mine(s) && (rd16(s + 6) & 0x8000)) {
        if (rd8(s + 2) == 8 && (rd16(s + 6) & 0x2000)) {
            uint16_t k = rd16(StructInfo(int16_t(rd16(s + 0x52))) + 0x3C);
            SetCursorLimits(0, uint32_t(rd16(0xC7EFC + k * 4u)) << 16 | rd16(0xC7EFE + k * 4u));
            if (!CanPlace(U16{0xFFC240}, rd16(s + 0x52))) k += 7;
            a = int16_t(rd16(0xC7F3E + uint16_t(k * 2)));
            UsePlacementCursor();
            if (S16{0xFFC258} >= 0) a = int16_t(-(U16{0xFFC258} + 1));
            else b = a;
            DropSelection();
            SetCursorShape(a, b);
            return;
        }
        UseFreeCursor();
        SetCursorLimits(0x180018, 0x12800C8);
        b = int16_t(rd16(0xC7F5A + rd16(StructInfo(int8_t(rd8(s + 2))) + 0x3C) * 2u));
        if (rd8(s + 2) == 2 && (rd16(s + 6) & 0x80) && special_armed && !(rd16(s + 6) & 0x100)) a = 5;
        DropSelection();
        wr32(0xFFBF5A, rd32(s + 0xA));
        SetCursorShape(a, b);
        ShowCursorShadow();
        return;
    }
    UseFreeCursor();
    SetCursorLimits(0x180018, 0x12800C8);
    if (u) {
        b = 6;
        if (!(rd16(u + 6) & 0x8000)) return;
        if (Mine(u) && rd8(u + 2) != 6) a = 5;
        else b = 0x1D;
        DropSelection();
        SetCursorShape(a, b);
        wr32(0xFFBF5A, rd32(u + 0xA));
        ShowCursorShadow();
        return;
    }
    DropSelection();
    if (s) {
        Highlight(s);
        b = int16_t(rd16(0xC7F5A + rd16(StructInfo(int8_t(rd8(s + 2))) + 0x3C) * 2u));
        SetCursorShape(a, b);
        wr32(0xFFBF5A, rd32(s + 0xA));
        ShowCursorShadow();
        return;
    }
    DropSelection();
    SetCursorShape(a, -1);
}

} // namespace

// 0x4D1A: the free map cursor (VBlank hook 0x6D10).
void UseFreeCursor() {
    if (vblank_hook == 0x6D10) return;
    wr16(0xFFBF32, 0);
    wr32(0xFFBF34, 0);
    wr32(0xFFBF38, 0);
    wr8(0xFFBF2A, 0);
    vblank_hook = 0x6D10;
}

// 0x4CC2: the placement cursor (VBlank hook 0x6092), snapped to 32 pixels.
void UsePlacementCursor() {
    if (vblank_hook == 0x6092) return;
    uint32_t o = rd32(0xFFE3BE);
    wr32(0xFFBF12, ((rd32(0xFFBF12) + o) & 0xFFE0FFE0) - o);
    wr16(0xFFBF32, 0);
    wr32(0xFFBF34, 0);
    wr32(0xFFBF38, 0);
    wr16(0xFFBF26, 0);
    wr8(0xFFBF2A, 0);
    vblank_hook = 0x6092;
}

// 0x556E: how a unit may enter a structure: 2 already headed in, 1 it may,
// 0 not.
uint16_t CanEnter(uint32_t u, uint32_t s) {
    if (!u || !s) return 0;
    uint32_t si = rd32(0x6B954 + rd8(s + 2) * 4u);
    uint32_t ui = rd32(0x6C5BC + rd8(u + 2) * 4u);
    uint16_t as_unit = rd16(u) | 0x4000;
    uint16_t as_struct = rd16(s) | 0x8000;
    uint8_t house = rd8(u + 0x5F) ? 2 : rd8(u + 8);
    if (house == rd8(s + 8)) {
        if (!((1u << rd8(u + 2)) & rd32(si + 0x34))) return 0;
        if (rd16(s + 0x2A) == as_unit) return 2;
        if (rd8(s + 3) == 0xFF) return 1;
        return 0;
    }
    if (rd8(u + 2) == 6 && rd16(u + 0x5C) == as_struct) return 2;
    if (rd16(ui + 0x3E) || !(rd16(si + 0xC) & 0x80)) return 0;
    return rd16(u + 0x5C) == as_struct ? 2 : 1;
}

// 0x9A32, 0x9A36, 0x9A3A: clears a bit of a structure's marks (0xFFC868 by
// index); when it was set, its marker sprite (+0x10) is freed.
void ClearMark(uint32_t s, uint8_t bit) {
    U8 m{0xFFC868u + rd16(s)};
    bool was = m & (1u << bit);
    m &= uint8_t(~(1u << bit));
    if (!was) return;
    uint16_t n = rd16(s + 0x10);
    if (!n) return;
    wr16(s + 0x10, 0);
    FreeSprite(Node(n), kSpriteList);
}

// 0xA78A: counts a blink down by the frame delta; at the end it stops.
uint16_t TickBlink(uint32_t pos) {
    if (!pos) return 0;
    int16_t d = int16_t(rd8(pos + 5) - U16{0xFFFFFE});
    uint32_t n = Node(rd16(pos + 6));
    if (d <= 0) {
        wr16(n + 4, 0);
        U8{n + 7} &= uint8_t(~0x20);
        wr8(pos + 5, 0);
        return 0;
    }
    wr8(pos + 5, uint8_t(d));
    if (int8_t(rd8(n + 4)) > 0) return uint16_t(d);
    if (uint8_t id = rd8(n)) PlaySoundEffect(id);
    return 1;
}

// 0x4DE8: the cursor's shadow shown (deferred while its frame uploads).
void ShowCursorShadow() {
    uint16_t n = U16{0xFFBF60};
    if (!n) return;
    if (U32{0xFFBF08}) U8{0xFFBF0D} |= 2;
    else U8{Node(n) + 7} &= 0x7F;
}

// 0x27B50: the latched button (0xFFBF2B) acts on the map cursor's cell; 1
// when an order or a placement was started.
uint16_t SideMenu() {
    uint16_t cell = U16{0xFFC240};
    switch (rd8(0xFFBF2B)) {
    case 'A': {
        bool final;
        uint16_t done = PressA(cell, final);
        if (final || StructureAt(cell) || selected_unit) return done;
        if (uint32_t s = selected_struct) {
            if (Mine(s)) {
                Highlight(s);
            } else {
                selected_struct = last_struct;
                if (last_struct) Highlight(last_struct);
            }
        }
        return done;
    }
    case 'B': PressB(); return 0;
    case 'S':
        do {
            U16{0xFFC14C} = OptionsScreen(1);
            if (U16{0xFFC14C} == 3 && PasswordScreen()) U16{0xFFC14C} = 1;
        } while (U16{0xFFC14C} == 3);
        return 0;
    default: CursorIdle(); return 0;
    }
}

// 0x27386: the yard's ready flags cleared once its structure is placed.
void StructurePlaced() {
    if (uint32_t s = selected_struct) U16{s + 6} &= 0x9FFF;
}

} // namespace game
