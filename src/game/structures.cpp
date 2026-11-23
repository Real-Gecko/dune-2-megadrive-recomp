// Structures (slots listed at 0x4A71E, info at 0x6B954): +0 index, +2
// type, +4 flags (bit 0 used, 2 not yet placed, 12 on the map), +8 house,
// +0xA position, +0x12 hit points, +0x5C state.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kMap = 0xFF7D9C;

} // namespace

// The cell of a position (+0xA long: y, x words in 1/256 cells).
uint16_t PosCell(uint32_t pos) { return uint16_t(((pos >> 24) << 6) + ((pos >> 8) & 0xFF)); }

// 0xEF26: a placed structure claims its cells (layout at 0x6B740, counts at
// 0x6B82E): house and index into the map, the ground under it cleared.
void OccupyCells(uint32_t s) {
    if (!s || !(rd16(s + 4) & 1) || (rd16(s + 4) & 4)) return;
    uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
    uint16_t layout = rd16(info + 0x3C);
    uint32_t offs = 0x6B740 + uint16_t(layout * 18);
    uint16_t n = rd16(0x6B82E + layout * 2u);
    for (int16_t i = 0; i < int16_t(n); i++) {
        uint16_t cell = uint16_t(rd16(offs + i * 2u) + PosCell(rd32(s + 0xA)));
        uint32_t c = kMap + int16_t(cell) * 4;
        wr8(c + 2, uint8_t((rd8(c + 2) & 0xF8) | rd8(s + 8)));
        U8{c + 2} |= 0x20;
        wr8(c + 3, uint8_t(rd16(s) + 1));
        uint16_t t = (rd16(c) >> 9) & 0x7F;
        if (t == 0x21 || t < 0x14) U16{c} &= 0x1FF;
    }
    U8{s + 4} |= 0x10;
}

// 0xFA12: a structure's state, then its cells claimed.
void SetStructureState(uint32_t s, uint16_t state) {
    if (!s) return;
    wr16(s + 0x5C, state);
    OccupyCells(s);
}

namespace {

uint32_t SInfo(uint8_t type) { return rd32(0x6B954 + int16_t(int8_t(type)) * 4); }
int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t LayoutCells(uint16_t layout) { return 0x6B740 + uint16_t(layout * 18); }
uint16_t LayoutCount(uint16_t layout) { return rd16(0x6B82E + layout * 2u); }

// 0xB90A: a structure slot: slabs and walls have fixed ones (0x48, 0x47,
// 0x46), others the first free (index -1) or the given one.
uint32_t AllocStructure(uint16_t index, uint16_t type) {
    if (!U32{0xFFD4B4}) return 0;
    int16_t i = int16_t(index);
    uint32_t s = 0;
    if (type == 0 || type == 1 || type == 0xE) {
        i = type == 0 ? 0x48 : type == 1 ? 0x47 : 0x46;
        s = U32{0xFFD4B4} + i * 0x62u;
    } else if (i == -1) {
        for (i = 0;;) {
            uint32_t c = rd32(0x4A71E + i * 4u);
            if (!(rd16(c + 4) & 1)) {
                s = c;
                break;
            }
            if (++i >= 0x46) break;
        }
    } else {
        s = rd32(0x4A71E + int32_t(i) * 4);
        if (rd16(s + 4) & 1) s = 0;
    }
    if (!s) return 0;
    MemSet(s, 0, 0x62);
    wr16(s, uint16_t(i));
    wr8(s + 2, uint8_t(type));
    wr16(s + 4, 3);
    wr16(s + 6, 0);
    wr16(s + 0x14, 0);
    wr8(s + 3, 0xFF);
    wr8(s + 0xF, type == 0xF || type == 0x10 ? 0x7F : 0xFF);
    wr32(0xFFD31C + U16{0xFFD318} * 4u, s);
    U16{0xFFD318} += 1;
    return s;
}

// 0x204E8: a radar blip for types flagged at 0x204D4 (allied list
// 0xFFDBDC, enemy 0xFFDBD8); returns the type when not flagged.
uint16_t AddStructureBlip(uint32_t s) {
    uint8_t type = rd8(s + 2);
    if (!rd8(0x204D4 + type)) return type;
    U16{0xFFBEA2} -= 1;
    uint32_t b = U32{0xFFDBD0};
    uint32_t n = Node(rd16(b + 4));
    wr16(n + 2, 0);
    U32{0xFFDBD0} = n;
    wr32(b + 2, 0);
    wr16(b, rd16(s));
    U32 list{Allied(uint16_t(HouseOf(s)), U16{0xFFC274}) ? 0xFFDBDCu : 0xFFDBD8u};
    uint32_t head = list;
    wr16(b + 4, uint16_t(head));
    if (uint16_t(head)) wr16(head + 2, uint16_t(b));
    list = b;
    return uint16_t(b);
}

void SetGround(uint16_t cell, uint16_t tile, int16_t house) {
    uint32_t c = 0xFF7D9C + int16_t(cell) * 4;
    wr16(c, uint16_t((rd16(c) & 0xFE00) + (tile & 0x1FF)));
    wr8(c + 2, uint8_t((rd8(c + 2) & 0xF8) | uint8_t(house)));
}

void ClearOverlay(uint16_t cell) { U16{uint32_t(0xFF7D9C + int16_t(cell) * 4)} &= 0x1FF; }

} // namespace

// The position (cell centre) of a cell.
uint32_t CellPos(uint16_t cell) {
    return (((uint32_t(cell & 0xFC0) << 10) + (cell & 0x3F)) << 8) + 0x800080;
}

// 0xE6D0: puts a structure on the map at a cell (1 placed). Walls and
// slabs become ground and free their record; a negative check result
// places it damaged.
uint16_t PlaceStructure(uint32_t s, uint16_t cell) {
    if (!s || cell == 0xFFFF) return 0;
    uint8_t type = rd8(s + 2);
    int16_t house = HouseOf(s);
    bool player = house == int16_t(U16{0xFFC274});
    uint32_t info = SInfo(type);
    uint16_t layout = rd16(info + 0x3C);
    if (type == 0xE) {
        if (!CanPlace(cell, 0xE)) return 0;
        SetGround(cell, uint16_t(U16{0xFFC234} + 1), house);
        if (player) RevealAround(CellPos(cell), 1);
        if (IsSpiceCell(cell)) ClearOverlay(cell);
        MarkCell(cell);
        RedrawUnitsAt(CellPos(cell));
        ConnectWalls(cell, 1);
        RedrawCell(cell);
        FreeStructure(s);
        return 1;
    }
    int16_t ok = int16_t(player ? CanPlace(cell, type) : CanPlaceAI(cell, type));
    if (!ok && !U16{0xFFC148}) return 0;
    uint32_t offs = LayoutCells(layout);
    uint16_t n = LayoutCount(layout);
    if (type == 0 || type == 1) {
        uint16_t placed = 0;
        for (uint16_t i = 0; i < n; i++) {
            uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
            if (!CanPlace(c, 0)) continue;
            SetGround(c, U16{0xFFC230}, house);
            if (player) RevealAround(CellPos(c), 1);
            if (IsSpiceCell(c)) ClearOverlay(c);
            MarkCell(c);
            RedrawUnitsAt(CellPos(c));
            UpdateCellSprite(c, 0, 0);
            placed = 1;
        }
        if (type == 1)
            for (uint16_t i = 0; i < n; i++) {
                uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
                if (!CanPlace(c, 0)) continue;
                SetGround(c, U16{0xFFC230}, house);
                if (player) {
                    RevealAround(CellPos(c), 1);
                    ClearOverlay(c);
                }
                MarkCell(c);
                RedrawUnitsAt(CellPos(c));
                UpdateCellSprite(c, 0, 0);
                placed = 1;
            }
        if (placed) FreeStructure(s);
        return placed;
    }
    U8{s + 5} &= uint8_t(~4);
    wr32(s + 0xA, CellPos(cell) & 0xFF00FF00);
    wr16(s + 0x4E, 0);
    wr16(s + 0x12, rd16(info + 0x10));
    if (player) U8{s + 9} |= 0xFF;
    else U8{s + 9} |= uint8_t(1u << house);
    if (ok < 0) {
        uint16_t part = PartOf(LayoutCount(layout), uint16_t(-ok));
        U16{s + 0x12} -= ScaleHealth(uint16_t(int16_t(rd16(info + 0x10)) >> 1), part);
        U8{s + 4} |= 4;
    } else {
        wr16(s + 0x12, rd16(info + 0x10));
    }
    wr16(s + 0x5E, rd16(info + 0x10));
    ScriptReset(s + 0x16, 0xFFC1C8);
    wr16(s + 0x22, 0);
    wr16(s + 0x2A, 0);
    wr16(s + 0x14, 0);
    ScriptReset(s + 0x16, rd32(s + 0x1A));
    ScriptStart(s + 0x16, int16_t(int8_t(type)));
    for (uint16_t i = 0; i < n; i++) {
        uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
        CrushUnit(UnitAt(c));
        MarkCell(c);
        RedrawUnitsAt(CellPos(c));
    }
    if (type == 9) U16{HouseGet(uint16_t(house)) + 0x1E} += 1;
    if (!U16{0xFFC148}) UpdatePower(house);
    if (type == 2) {
        if (house == 0 || house == 4) wr16(s + 0x52, 0x12);
        else if (house == 1) wr16(s + 0x52, 3);
        else if (house == 2) wr16(s + 0x52, 6);
    }
    for (uint16_t i = 0; i < n; i++) {
        uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
        MarkCell(c);
        RedrawUnitsAt(CellPos(c));
    }
    DrawStructureTiles(rd16(info + 0x3E), 0, PosCell(rd32(s + 0xA)), layout, uint16_t(house));
    RevealStructure(s);
    OccupyCells(s);
    U32{HouseGet(uint16_t(house)) + 0xE} |= 1u << type;
    return 1;
}

// 0xE584: a structure of a type for a house, placed at cell unless -1.
uint32_t CreateStructure(uint16_t index, uint16_t type, uint16_t house, uint16_t cell) {
    uint32_t info = SInfo(uint8_t(type));
    uint32_t s = AllocStructure(index, type);
    if (!s) return 0;
    U8{rd32(0x4A9FA + int16_t(house) * 4) + type} += 1;
    wr8(s + 8, uint8_t(house));
    wr16(s + 0x4C, house);
    U8{s + 5} |= 4;
    wr32(s + 0xA, 0);
    wr8(s + 3, 0xFF);
    wr16(s + 0x5C, 0xFFFF);
    wr16(s + 0x60, AddStructureBlip(s));
    uint32_t t = rd32(0x4AA2C);
    if (type == 0xF) wr16(s + 0x4E, rd16(t + 2 + rd16(t + 0x2E) * 2u));
    if (type == 0x10) wr16(s + 0x4E, rd16(t + 2 + rd16(t + 0x30) * 2u));
    wr16(s + 0x12, rd16(info + 0x10));
    wr16(s + 0x5E, rd16(info + 0x10));
    if (house == 0 && type == 3) wr8(s + 0x54, 1);
    if (rd16(info + 0xC) & 2) wr8(s + 0x55, HasUpgrade(s) ? 0x64 : 0);
    wr16(s + 0x52, 0xFFFF);
    StructureBuild(s, -2);
    wr16(s + 0x56, 0);
    if (house != U16{0xFFC274}) {
        AiStructureSetup(s);
        wr8(s + 0x55, 0);
    }
    if (cell != 0xFFFF && !PlaceStructure(s, cell)) {
        FreeStructure(s);
        s = 0;
    }
    return s;
}

// 0x10BA6: a structure can be upgraded now (its level's requirement at
// info +0x60 reached by the scenario), with house exceptions.
uint16_t HasUpgrade(uint32_t s) {
    if (!s) return 0;
    uint8_t type = rd8(s + 2);
    uint32_t info = SInfo(type);
    int16_t house = int16_t(rd16(s + 0x4C)), scen = int16_t(U16{0xFFC050});
    uint8_t level = rd8(s + 0x54);
    if (house == 0 && type == 5) return 0;
    if (house == 2 && type == 4) {
        if (scen == 5) return 1;
        if (scen > 5) return 0;
        if (level == 1 && scen < int16_t(rd16(info + 0x64))) return 0;
    }
    int16_t need = int16_t(rd16(info + 0x60 + level * 2u));
    if (need && need <= scen + 1) {
        if (type == 8) {
            uint32_t mask = rd32(rd32(0x6B994) + 0x1C);
            if ((rd32(HouseGet(uint16_t(house)) + 0xE) & mask) != mask) return 0;
        }
        if (type == 0xA && house == 1 && level == 1) return 0;
        if (type == 4 && house == 2 && scen >= 7) return 0;
        return 1;
    }
    return type == 7 && house == 0 && level == 0 && scen >= 3;
}

// 0x10A9C: the upgrade level computer houses start a structure with.
void AiStructureSetup(uint32_t s) {
    int16_t scen = int16_t(U16{0xFFC050}), house = int16_t(rd16(s + 0x4C));
    U8 level{s + 0x54};
    switch (int8_t(rd8(s + 2))) {
    case 3: if (scen >= 2) level = 1; break;
    case 4:
        if (scen == 3) level = 1;
        else if (scen == 4) level = 2;
        else if (scen >= 5 && scen <= 8) level = 3;
        break;
    case 5: if (scen >= 6 && house) level = 1; break;
    case 7:
        if (scen >= 5 && house == 2) level = 1;
        if (scen >= 3 && house == 0) level = 1;
        break;
    case 8: if (scen >= 5) level = 1; break;
    case 10: if (scen) level = 1; break;
    case 6: case 9: default: level = 0; break;
    }
}

// 0xFA88: a factory's production: -3 stops, -1 lists what it can build into
// 0xFFC620 (0xFFC63C for the construction yard; 0xFF where not), -2 also
// picks the first; the starport (type 0xB) item 1 delivers its order;
// otherwise starts building item. 1 when something started.
uint16_t StructureBuild(uint32_t s, int16_t item) {
    if (!s) return 0;
    uint8_t type = rd8(s + 2);
    int16_t house = HouseOf(s);
    uint32_t info = SInfo(type), h = HouseGet(uint16_t(house));
    if (!(rd16(info + 0xC) & 2)) return 0;
    if (item == -3) {
        wr16(s + 0x5A, 0);
        return 0;
    }
    if (item == -1 || item == -2) {
        int16_t up = 0;
        if (type != 0xB && rd16(s + 0x4C) != 4 && HasUpgrade(s) &&
            int16_t(rd16(info + 0x10)) >= int16_t(rd16(s + 0x12))) {
            up = int16_t(int16_t(rd16(info + 0x16)) / 2);
            if (type == 5 && !rd8(s + 8)) up = 0;
        }
        uint32_t mask = BuildableMask(s);
        if (!mask) {
            wr16(s + 0x52, 0);
            return 0;
        }
        bool yard = type == 8;
        wr8(0xFFC344, yard ? 1 : 0);
        if (yard || type != 0xB) {
            uint32_t list = yard ? 0xFFC63C : 0xFFC620;
            int n = yard ? 0x13 : 0x1B;
            for (int i = 0; i < n; i++) {
                if (mask & (1u << i)) {
                    wr8(list + i, uint8_t(i));
                    if (item == -2) {
                        wr16(s + 0x52, uint16_t(i));
                        return 0;
                    }
                } else {
                    wr8(list + i, 0xFF);
                }
            }
            wr8(list + n, 0xFF);
            if (up && (rd32(h + 0xE) & (1u << 0x12))) {
                if (yard) {
                    wr8(0xFFC64F, 0x10);
                } else {
                    uint16_t d = UpgradeItem(s);
                    if (rd8(0xFFC620 + int16_t(d)) == 0xFF) wr8(0xFFC63B, uint8_t(d));
                }
            }
        }
        if (item != -1) wr16(s + 0x52, uint16_t(item));
        return 0;
    }
    if (type == 0xB) {
        if (item != 1) return 1;
        for (int16_t i = 0; i < 9; i++) {
            uint32_t e = 0xFFC2D8 + i * 12;
            if (int8_t(rd8(e + 2)) <= 0) continue;
            uint16_t ut = rd16(e);
            U16{0xFFC148} += 1;
            uint32_t u = CreateUnitAt(0xFFFF, ut, uint16_t(house), 0xFFFF, 0);
            U16{0xFFC148} -= 1;
            if (u) {
                wr16(s + 0x5C, 1);
                U16{0xFFC17C} = rd16(s);
                if (!rd16(h + 0x2E)) wr16(h + 0x2E, rd16(0x6C75A + uint16_t(rd16(h) * 30) + 0xE));
                wr8(u + 3, uint8_t(rd16(h + 0x30)));
                wr16(h + 0x30, rd16(u));
                if (U16 left{0xFFC2A0u + ut * 2u}; left) left = uint16_t(left - 1);
                U8{e + 2} -= 1;
                if (rd8(e + 2)) i--;
                U8{u + 6} |= 2;
                U16{0xFFC348} += 1;
            } else {
                int16_t cost = int16_t(house == int16_t(U16{0xFFC274}) ? rd16(e + 4)
                                                                        : rd16(rd32(0x6C5BC + ut * 4u) + 0x16));
                U32{h + 0x12} += uint32_t(int32_t(cost));
            }
        }
        return 1;
    }
    bool refresh;
    if (item != int16_t(rd16(s + 0x52)) && type == 8) refresh = true;
    else if (type == 8 || (rd16(s + 6) & 0x2000) || item == int16_t(rd16(s + 0x52))) refresh = rd16(s + 6) & 2;
    else refresh = true;
    if (refresh) CancelBuild(s);
    if (rd8(s + 3) != 0xFF || item == -1) return 0;
    uint32_t it, made;
    if (type != 8) {
        it = rd32(0x6C5BC + uint16_t(item) * 4u);
        made = CreateUnitAt(0xFFFF, uint16_t(item), uint16_t(house), 0xFFFF, 0);
    } else {
        it = SInfo(uint8_t(item));
        made = CreateStructure(0xFFFF, uint16_t(item), uint16_t(house), 0xFFFF);
    }
    U8{s + 4} &= uint8_t(~0x40);
    if (!made) return 0;
    wr8(s + 3, uint8_t(rd16(made)));
    wr16(s + 0x52, uint16_t(item));
    wr16(s + 0x56, uint16_t(rd16(it + 0x18) << 8));
    wr16(s + 0x5A, 0);
    SetStructureState(s, 1);
    return 1;
}

// 0xFE5C: the items a factory offers, as a mask (bit = unit or structure
// type) gated by the scenario, upgrade level (+0x54), house and the house's
// structures (+0xE); also marks each in 0xFFC620/0xFFC63C (1 offered, 0xFF
// after an upgrade).
uint32_t BuildableMask(uint32_t s) {
    if (!s) return 0;
    uint8_t type = rd8(s + 2);
    int16_t house = HouseOf(s), owner = int16_t(rd16(s + 0x4C));
    int16_t scen = int16_t(U16{0xFFC050}), player = int16_t(U16{0xFFC274});
    uint16_t level = rd8(s + 0x54);
    uint32_t have = rd32(HouseGet(uint16_t(house)) + 0xE);
    uint32_t m = 0;
    auto list = [&](uint32_t mask) {
        for (int i = 0; i < 0x1B; i++) wr8(0xFFC620 + i, (mask & (1u << i)) ? 1 : 0xFF);
        return mask;
    };
    auto light_tail = [&]() {
        switch (scen) {
        case 1:
            break;
        case 2: case 3: case 4: case 5: case 6: case 7: case 8:
            if (level >= 1) m |= 1u << 0xF;
            break;
        default: goto done;
        }
        if (owner == 1 || owner == 4) m |= 1u << 0xD;
        else if (owner == 2) m |= 1u << 0xE;
    done:
        if ((have & 0x40000) != 0x40000) m &= 0xFFFDF97F;
        return list(m);
    };
    switch (type) {
    case 3:
        return light_tail();
    case 4:
        if (scen >= 3 && scen <= 8) {
            if (scen >= 6) {
                m |= 1u << 0xA;
                if (have & 0x20) {
                    switch (owner) {
                    case 0: m |= 1u << 0xB; break;
                    case 1: m |= 1u << 0xC; break;
                    case 2: m |= 1u << 8; break;
                    case 4:
                        if (player == 0) m |= 1u << 0xC;
                        else if (player == 1) m |= 1u << 0xB;
                        else if (player == 2) m |= 1u << 0xC | 1u << 0xB;
                        break;
                    }
                }
            }
            if (scen >= 5 && level >= 3) m |= 1u << 0xA;
            if (scen >= 4 && level >= 2 && owner != 2) m |= 1u << 7;
            if (level >= 1) m |= 1u << 0x11;
            m |= 1u << 0x10 | 1u << 9;
        }
        m |= 1u << 0xF;
        return light_tail();
    case 5:
        if (scen >= 4 && scen <= 8) {
            if (scen >= 6 && level) m |= 2;
            m |= 1;
        }
        return list(m);
    case 7: case 10: {
        for (int i = 0; i < 0x1B; i++) wr8(0xFFC620 + i, 0);
        uint32_t info = SInfo(type);
        for (int i = 0; i < 10; i++) {
            int16_t ut = int16_t(rd16(info + 0x4C + i * 2u));
            if (ut == -1) continue;
            if (ut == 0xD && owner == 2) ut = 0xE;
            uint32_t ui = rd32(0x6C5BC + ut * 4);
            int16_t need = int16_t(int8_t(rd8(ui + 0x21)));
            uint32_t pre = rd32(ui + 0x1C);
            if ((have & pre) != pre) continue;
            if (!((1u << owner) & rd8(ui + 0x32))) continue;
            if (house == 2) {
                if (type == 0xA && ut == 5) need = 9;
                else if (type == 7 && ut == 2) need = 0;
            }
            if (need <= int16_t(level)) {
                wr8(0xFFC620 + i, 1);
                m |= 1u << ut;
            } else if (rd8(s + 0x55) && need <= int16_t(level + 1)) {
                wr8(0xFFC620 + i, 0xFF);
            }
        }
        return m;
    }
    case 8: {
        uint32_t info = rd32(0x6B954);
        for (int i = 0; i < 0x13; i++, info += 0x66) {
            uint32_t e = 0xFFC63C + i;
            wr8(e, 0);
            int16_t need_scen = int16_t(rd16(info + 0x1A));
            uint32_t pre = rd32(info + 0x1C);
            if (i == 7 && house == 0 && scen >= 1) {
                pre &= ~(1u << 10);
                need_scen = 2;
            }
            if ((have & pre) != pre && house == player) continue;
            if (house != 0 && i == 3) need_scen = 2;
            if (i == 4 || i == 6 || (i == 7 && player == 2)) continue;
            if (scen < need_scen - 1) continue;
            if (!((1u << house) & rd8(info + 0x32))) continue;
            int16_t need = int16_t(int8_t(rd8(info + 0x21)));
            if (need <= int16_t(level) || house != player) {
                wr8(e, 1);
                m |= 1u << i;
            } else if (rd8(s + 0x55) && need <= int16_t(level + 1)) {
                wr8(e, 0xFF);
            }
        }
        m |= 2;
        if (rd16(s + 6) & 0x100) return m & 0x4002;
        if (!(m & 0x804)) return m;
        uint32_t count = rd32(0x4A9FA + house * 4u);
        if (rd8(count + 2)) m &= ~4u;
        if (rd8(count + 0xB)) m &= ~(1u << 0xB);
        return m;
    }
    case 11:
        return 0xFFFFFFFF;
    default:
        return 0;
    }
}

// 0xF100: whether the player may put a structure of a type at cell (for
// slabs, type 1, how many cells need one in 0xFFC258). Returns 1, minus
// the number of cells off concrete, or 0. Must touch own concrete or an
// own structure unless loading a scenario.
int16_t CanPlace(uint16_t cell, uint16_t type) {
    uint32_t info = SInfo(uint8_t(type));
    uint16_t layout = rd16(info + 0x3C);
    uint32_t offs = LayoutCells(layout);
    uint16_t n = LayoutCount(layout);
    int16_t player = int16_t(U16{0xFFC274});
    U16 need{0xFFC258};
    need = type == 1 ? 0 : 0xFFFF;
    uint16_t own_slab = 0, off_slab = 0;
    uint8_t slab_cell[4] = {};
    bool ok = true;
    auto owner = [](uint16_t c) { return int16_t(rd8(0xFF7D9C + int16_t(c) * 4 + 2) & 7); };
    auto terrain = [](uint16_t t) { return 0x6CCB2 + uint16_t(t * 28); };
    for (uint16_t i = 0; i < n; i++) {
        uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
        uint16_t t = TerrainAt(c);
        if (!CellInMap(c)) {
            if (type == 0) {
                need = 0xF;
                return 0;
            }
            ok = false;
            break;
        }
        if (rd16(info + 0xC) & 8) {
            if (type == 1) {
                slab_cell[i] = 0;
                if (rd8(0xFF7D9C + int16_t(c) * 4 + 3) && t != 0xC) {
                    need = uint16_t(need + int8_t(rd8(0x6BBC6 + i)));
                } else if (t == 4 || t == 5 || t == 0xD) {
                    slab_cell[i] = 1;
                } else {
                    if ((t == 0xA || t == 0xB || t == 0xC) && owner(c) == player) own_slab = 1;
                    off_slab++;
                    need = uint16_t(need + int8_t(rd8(0x6BBC6 + i)));
                }
            } else if (!rd16(terrain(t) + 0x10) && !U16{0xFFC148}) {
                ok = false;
                break;
            }
        } else {
            if (!rd16(terrain(t) + 0xC) && !U16{0xFFC148}) {
                ok = false;
                break;
            }
            if (t == 0xA || t == 0xB || t == 0xC) {
                if (owner(c) != player) ok = false;
            } else {
                off_slab++;
            }
        }
        if (type != 1 && UnitOnCell(c)) {
            ok = false;
            break;
        }
    }
    if (!U16{0xFFC148} && ok && type != 8) {
        ok = false;
        auto slab_near = [&](uint16_t k_off) {
            for (int k = 0; k < 4; k++)
                if (slab_cell[k] && rd8(0x6BBCA + k * 12 + k_off)) return true;
            return false;
        };
        for (uint16_t d = 0; d < 16; d++) {
            uint16_t off = rd16(0x6B83C + (layout * 16u + d) * 2);
            if (!off) {
                for (uint16_t j = 0; j < n; j++) {
                    uint16_t c = uint16_t(cell + rd16(offs + j * 2u));
                    if (TerrainAt(c) == 0xA && owner(c) == player) {
                        ok = true;
                        break;
                    }
                }
                break;
            }
            uint16_t c = uint16_t(cell + off);
            if (uint32_t st = StructureAt(c)) {
                if (HouseOf(st) != player) continue;
            } else {
                uint16_t t = TerrainAt(c);
                if ((t != 0xA && t != 0xB) || owner(c) != player) continue;
            }
            if (type != 1) {
                ok = true;
                break;
            }
            if (slab_near(d)) ok = true;
        }
        if (type == 1 && own_slab && int16_t(need) < 0xF) ok = true;
    }
    if (ok && int16_t(need) < 0xF) return off_slab ? int16_t(-off_slab) : 1;
    if (type == 1) need = 0xF;
    return 0;
}

// 0x2073A: a structure's radar blip back to the free list.
void FreeStructureBlip(uint16_t b) {
    if (int16_t(b) >= 0) return;
    uint32_t n = Node(b);
    U16{0xFFBEA2} += 1;
    uint32_t s = rd32(0x4A71E + rd16(n) * 4u);
    wr16(n, 0);
    bool allied = Allied(uint16_t(HouseOf(s)), U16{0xFFC274});
    uint16_t prev = rd16(n + 2), next = rd16(n + 4);
    if (!prev) {
        wr32(rd32(0x20732 + (allied ? 4 : 0)), Node(next));
        if (next) wr16(Node(next) + 2, 0);
    } else {
        wr16(Node(prev) + 4, next);
        if (next) wr16(Node(next) + 2, prev);
        wr16(n + 2, 0);
    }
    uint32_t f = U32{0xFFDBD0};
    wr16(n + 4, uint16_t(f));
    if (uint16_t(f)) wr16(f + 2, uint16_t(n));
    U32{0xFFDBD0} = n;
}

// 0xDE08: the flag cell of a structure (0x6BA04) loses its overlay.
void ClearFlagCell(uint32_t s) {
    uint32_t info = SInfo(rd8(s + 2));
    uint16_t c = uint16_t(PosCell(rd32(s + 0xA)) + rd16(0x6BA04 + rd16(info + 0x3C) * 2u));
    uint32_t a = 0xFF7D9C + int16_t(c) * 4;
    int16_t t = int16_t((rd16(a) >> 9) & 0x7F);
    if ((rd16(s + 6) & 0x10) && t > 0 && int32_t(t) < int32_t(U16{0xFFC224})) {
        U16{a} &= 0x1FF;
        RedrawCell(c);
    }
}

// 0xBA56: a structure record freed.
void FreeStructure(uint32_t s) {
    ScriptReset(s + 0x16, 0xFFC1C8);
    U8{rd32(0x4A9FA + HouseOf(s) * 4) + int16_t(int8_t(rd8(s + 2)))} -= 1;
    ClearFlagCell(s);
    FreeStructureBlip(rd16(s + 0x60));
    wr16(s + 0x60, 0);
    wr16(s + 4, 0);
    U16 count{0xFFD318};
    for (int16_t i = 0; i < int16_t(count); i++) {
        uint32_t at = 0xFFD31C + i * 4u;
        if (rd32(at) != s) continue;
        count = uint16_t(count - 1);
        if (i < int16_t(count)) MemCpy(at, at + 4, (int16_t(count) - i) * 4);
    }
}

// 0xF4C2: a computer house's placement check: inside the map on buildable
// ground; 1, or minus the cells off concrete.
int16_t CanPlaceAI(uint16_t cell, uint16_t type) {
    uint32_t info = SInfo(uint8_t(type));
    uint16_t layout = rd16(info + 0x3C);
    uint32_t offs = LayoutCells(layout);
    uint16_t n = LayoutCount(layout), off = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t c = uint16_t(cell + rd16(offs + i * 2u));
        uint16_t t = TerrainAt(c);
        if (!CellInMap(c)) return 0;
        uint32_t tr = 0x6CCB2 + uint16_t(t * 28);
        if (rd16(info + 0xC) & 8) {
            if (!rd16(tr + 0x10) && !U16{0xFFC148}) return 0;
        } else {
            if (!rd16(tr + 0xC) && !U16{0xFFC148}) return 0;
            if (t != 0xA) off++;
        }
    }
    return off ? int16_t(-off) : 1;
}

// 0x10870: the player's structure uncovers its sight radius (info +0x12)
// around its centre (0x6B938).
void RevealStructure(uint32_t s) {
    if (!s || HouseOf(s) != int16_t(U16{0xFFC274})) return;
    uint32_t info = SInfo(rd8(s + 2));
    RevealAround(rd32(s + 0xA) + rd32(0x6B938 + rd16(info + 0x3C) * 4u), rd16(info + 0x12));
}

// 0xF5A2: a structure destroyed: its marks gone, state 1 (+0x22), its
// script set to the death routine; what it held dies with it (a yard's
// structure in turn). The house loses the credits its storage held beyond
// what is left; a computer house is refunded the cost (half again above
// level 7); a windtrap (type 9) is counted off.
void StructureDestroyed(uint32_t s) {
    if (!s) return;
    ClearMarks(s);
    wr16(s + 0x22, 1);
    U16{s + 4} &= 0xDFFD;
    wr16(s + 0x14, 0);
    ScriptReset(s + 0x16, 0xFFC1C8);
    ScriptStart(s + 0x16, int16_t(int8_t(rd8(s + 2))));
    PlaySoundEffect(0x2C);
    int8_t held = int8_t(rd8(s + 3));
    wr8(s + 3, 0xFF);
    if (held != -1) {
        if (rd8(s + 2) == 8) {
            StructureDestroyed(StructureGet(uint16_t(held)));
            wr8(s + 3, 0xFF);
        } else {
            while (held != -1) {
                uint32_t u = UnitGet(uint16_t(held));
                held = int8_t(rd8(u + 3));
                CrushUnit(u);
            }
        }
    }
    int16_t house = int8_t(rd8(s + 8));
    uint32_t h = HouseGet(uint16_t(house));
    uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
    uint16_t lost = ScaleHealth(rd16(info + 0x38), PartOf(uint16_t(rd32(h + 0x16)), uint16_t(rd32(h + 0x12))));
    uint32_t take = lost < rd32(h + 0x12) ? lost : uint16_t(rd32(h + 0x12));
    U32{h + 0x12} -= take;
    if (house != int16_t(U16{0xFFC274})) {
        uint16_t cost = rd16(info + 0x16);
        if (S16{0xFFC050} > 7) cost = uint16_t(cost + (cost >> 1));
        U32{HouseGet(uint16_t(house)) + 0x12} += cost;
    }
    if (rd8(s + 2) == 9) U16{HouseGet(uint16_t(house)) + 0x1E} -= 1;
}

// 0x10CE0: units heading for or attacking a structure let go of it. (The
// original's link and team-target tests compare a sign-extended target
// with a zero-extended one, so never match for structures.)
void ForgetStructure(uint32_t s) {
    if (!s) return;
    uint16_t t = MakeTarget(rd16(s), 2);
    ReleaseTarget(s);
    for (uint32_t u = FirstUnit(0, 0xFFFF, 0xFFFF); u; u = NextUnit(0)) {
        if (rd16(u + 0x5C) == t) wr16(u + 0x5C, 0);
        if (rd16(u + 0x5A) == t) wr16(u + 0x5A, 0);
    }
    for (uint32_t m = FirstTeam(0, 0xFFFF); m; m = NextTeam(0)) {
    }
}

// 0xF8DA: a structure takes damage; destroyed at none left (1): the
// destroyed/lost counts and score (0xFFC05E) by side, the player's
// selection dropped, the house's voice (the player's own by house, else
// 0x15) and every target on it released.
uint16_t DamageStructure(uint32_t s, uint16_t damage, uint16_t) {
    if (!s || !damage || rd16(s + 0x22) == 1) return 0;
    int16_t house = int8_t(rd8(s + 8));
    if (U16{0xFFC018} && house == int16_t(U16{0xFFC274})) return 0;
    U16{s + 0x12} -= damage;
    if (S16{s + 0x12} > 0) return 0;
    wr16(s + 0x12, 0);
    int16_t pts = int16_t(int16_t(rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x16)) / 100);
    if (pts <= 1) pts = 1;
    if (Allied(uint16_t(house), U16{0xFFC274})) {
        U16{0xFFC09A} += 1;
        if (pts > S16{0xFFC05E}) pts = S16{0xFFC05E};
        U16{0xFFC05E} -= uint16_t(pts);
    } else {
        U16{0xFFC09C} += 1;
        U16{0xFFC05E} += uint16_t(pts);
    }
    if (house == int16_t(U16{0xFFC274}) && s == U32{0xFFC578}) U32{0xFFC578} = 0;
    StructureDestroyed(s);
    if (house == int16_t(U16{0xFFC274})) {
        uint8_t h = rd8(s + 8);
        PlayVoice(h == 0 ? 0x16 : h == 1 ? 0x17 : h == 2 ? 0x18 : 0xFFFF);
    } else {
        PlayVoice(0x15);
    }
    ForgetStructure(s);
    return 1;
}

// 0x4880A: a unit arrives in a structure. A friendly one takes it in (a
// repair facility charges by the damage and heals it, a refinery shows it
// docked). An enemy saboteur blows up; another unit captures a structure
// below a quarter health (what it held lost, a refinery's or repair
// facility's units changing sides), else damages it by twice its health
// (at most half the structure's) and is lost.
void EnterStructure(uint32_t u, uint32_t s) {
    if (!u || !s) return;
    if (u == U32{0xFFC25C}) {
        DropSelection();
        U32{0xFFC25C} = 0;
    }
    uint32_t si = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
    if (!(rd16(u + 4) & 2) || !rd16(s + 0x12)) {
        CrushUnit(u);
        return;
    }
    U8{u + 9} |= rd8(s + 9);
    uint32_t pos = rd32(u + 0xA);
    LiftUnit(u);
    if (rd8(u + 2) == 0x10) wr16(s + 0x4E, rd16(u + 0x5A));
    auto house_of = [](uint32_t o) { return uint16_t(int16_t(int8_t(rd8(o + 8)))); };
    if (Allied(house_of(s), house_of(u))) {
        SetStructureState(s, (rd16(si + 0xC) & 0x10) ? 2 : 1);
        if (rd8(s + 2) == 0xD) {
            uint32_t ui = rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4);
            uint16_t max = rd16(ui + 0x10);
            uint16_t cost = ScaleHealth(uint16_t(rd16(ui + 0x18) << 6), PartOf(max, uint16_t(max - rd16(u + 0x12))));
            wr16(s + 0x56, cost > 1 ? cost : 1);
            wr16(u + 0x5A, rd16(s + 0x56));
            wr16(u + 0x12, max);
            U8{u + 5} &= uint8_t(~8);
            wr8(u + 0x73, 0);
        }
        if (rd8(s + 2) == 0xC) {
            wr8(u + 0x68, 0);
            wr8(u + 0x69, 0);
            wr8(u + 0x6A, 0x80);
            UpdateUnitSprite(u);
            wr32(u + 0xA, rd32(s + 0xA) + 0x1800280);
            ShowSprite(rd16(u + 0x10));
        } else {
            wr32(u + 0xA, rd32(s + 0xA) + 0x1000100);
        }
        wr8(u + 3, rd8(s + 3));
        wr8(s + 3, uint8_t(rd16(u)));
        return;
    }
    if (rd8(u + 2) == 6) {
        SpawnEffect(4, pos, 0x1F4, 0);
        FreeUnit(u);
        return;
    }
    if ((int16_t(rd16(si + 0x10)) >> 2) > int16_t(rd16(s + 0x12))) {
        if (rd8(s + 3) != 0xFF) {
            uint16_t held = uint16_t(int16_t(int8_t(rd8(s + 3))));
            bool clear = true;
            switch (rd8(s + 2)) {
            case 8: FreeStructure(StructureGet(held)); break;
            case 0xC:
            case 0xD:
                for (uint32_t v = UnitGet(held); v; v = UnitGet(uint16_t(int16_t(int8_t(rd8(v + 3)))))) {
                    U8{rd32(0x4AA0E + uint32_t(int32_t(int8_t(rd8(v + 8))) * 4)) + int8_t(rd8(v + 2))} -= 1;
                    FreeUnitBlip(rd16(v + 0x4C));
                    wr8(v + 8, rd8(u + 8));
                    wr8(v + 0x74, rd8(u + 8));
                    wr16(v + 0x4C, AddBlip(v));
                    SetSpritePalette(rd16(v + 0x10), house_of(v));
                    U8{rd32(0x4AA0E + uint32_t(int32_t(int8_t(rd8(u + 8))) * 4)) + int8_t(rd8(v + 2))} += 1;
                }
                clear = false;
                break;
            default: FreeUnit(UnitGet(held)); break;
            }
            if (clear) {
                wr8(s + 3, 0xFF);
                U16{s + 6} &= 0x9FFF;
            }
        }
        ForgetStructure(s);
        FreeStructureBlip(rd16(s + 0x60));
        uint8_t type = rd8(s + 2);
        uint32_t bit = 1u << int8_t(type);
        if (!--U8{rd32(0x4A9FA + uint32_t(int32_t(int8_t(rd8(s + 8))) * 4)) + int8_t(type)})
            U32{HouseGet(house_of(s)) + 0xE} &= ~bit;
        wr8(s + 8, rd8(u + 8));
        U32{HouseGet(house_of(u)) + 0xE} |= bit;
        U8{s + 4} &= uint8_t(~0x20);
        if (house_of(s) == U16{0xFFC274}) wr8(s + 9, 0xFF);
        else U8{s + 9} |= uint8_t(1u << house_of(s));
        switch (type) {
        case 3: break;
        case 4: wr16(s + 0x52, rd16(s + 0x4C) == 1 ? 0xD : rd16(s + 0x4C) == 2 ? 0xE : 0xF); break;
        case 5: wr16(s + 0x52, 0); break;
        case 8:
            wr16(s + 0x52, 1);
            wr16(s + 0x4C, house_of(s));
            break;
        default: wr16(s + 0x4C, house_of(s)); break;
        }
        wr16(s + 0x60, AddStructureBlip(s));
        U8{rd32(0x4A9FA + uint32_t(int32_t(int8_t(rd8(s + 8))) * 4)) + int8_t(type)} += 1;
        UpdatePower(int16_t(house_of(s)));
        U8{s + 7} &= uint8_t(~0x10);
        RevealAround(rd32(s + 0xA), rd16(si + 0x12));
        OccupyCells(s);
    } else {
        uint16_t half = uint16_t(int16_t(rd16(s + 0x12)) >> 1);
        int32_t twice = int32_t(int16_t(rd16(u + 0x12))) * 2;
        DamageStructure(s, twice < int32_t(half) ? uint16_t(twice) : half, 1);
        uint32_t off = rd32(0x6B938 + uint32_t(rd16(si + 0x3C)) * 4);
        uint32_t sp = rd32(s + 0xA);
        SpawnEffect(3, ((sp & 0xFFFF0000) + (off & 0xFFFF0000)) + (((sp & 0xFFFF) + (off & 0xFFFF)) & 0xFFFF), 0, 0);
    }
    ReleaseTarget(s);
    FreeUnit(u);
}

// 0x103AA: a structure's build or upgrade cancelled, its price refunded.
void CancelBuild(uint32_t s) {
    if (!s) return;
    uint32_t h = HouseGet(uint16_t(int16_t(int8_t(rd8(s + 8)))));
    if (rd8(s + 3) != 0xFF) {
        uint16_t held = uint16_t(int16_t(int8_t(rd8(s + 3))));
        if (rd8(s + 2) == 8) FreeStructure(StructureGet(held));
        else FreeUnit(UnitGet(held));
        U8{s + 4} &= uint8_t(~0x40);
        wr16(s + 0x56, 0);
        wr8(s + 3, 0xFF);
        U32{h + 0x12} += rd16(s + 0x5A);
        wr16(s + 0x5A, 0);
    }
    if (rd16(s + 6) & 2) {
        wr8(s + 0x55, 0);
        wr8(s + 0x50, 0);
        if (rd8(s + 2) == 8) wr16(s + 0x52, 1);
        U8{s + 7} &= uint8_t(~2);
        U32{h + 0x12} += rd16(s + 0x5A);
        wr16(s + 0x5A, 0);
    }
}

// 0xF71E: a structure gone from the map: its cells freed and redrawn as
// rubble (0x6BBFA by layout), the selections cleared, a slot of the
// house's rebuild list (+0x32 type, +0x34 cell) taken, freed; the house's
// types, power, storage or radar updated.
void StructureLost(uint32_t s) {
    if (!s) return;
    ClearMarks(s);
    uint16_t cell = PosCell(rd32(s + 0xA));
    uint16_t k = rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x3C);
    uint32_t offs = 0x6B740 + k * 18u;
    for (int16_t i = 0; i < int16_t(rd16(0x6B82E + k * 2u)); i++) {
        uint32_t m = rd32(0x4AA24) + uint32_t(int32_t(int16_t(uint16_t(cell + rd16(offs + i * 2u)))) * 4);
        U8{m + 2} &= uint8_t(~0x20);
        wr8(m + 3, 0);
    }
    DrawStructureTiles(rd16(0x6BBFA + k * 2u), 0, cell, k, 0);
    if (s == U32{0xFFC578}) U32{0xFFC578} = 0;
    if (s == U32{0xFFC57C}) U32{0xFFC57C} = 0;
    uint16_t house = uint16_t(int16_t(int8_t(rd8(s + 8))));
    uint32_t h = HouseGet(house);
    for (int i = 0; i < 5; i++) {
        if (rd16(h + 0x32 + i * 4u)) continue;
        wr16(h + 0x32 + i * 4u, uint16_t(int16_t(int8_t(rd8(s + 2)))));
        wr16(h + 0x34 + i * 4u, PosCell(rd32(s + 0xA)));
        break;
    }
    FreeStructure(s);
    ForgetStructure(s);
    wr32(HouseGet(house) + 0xE, OwnedTypes(house));
    switch (rd8(s + 2)) {
    case 9: UpdatePower(int16_t(house)); break;
    case 0xC:
    case 0x11: UpdateStorage(house); break;
    case 0x12: UpdateRadarStatus(house); break;
    default: break;
    }
}

// 0x108D2: a free cell around a structure (ring at 0x6B83C by layout,
// from a random start, every other entry) for the unit it holds; for a
// harvester with spice found within 32 cells, the one nearest the spice.
uint16_t FreeCellBeside(uint32_t s, uint16_t harvester) {
    if (!s || rd8(s + 3) == 0xFF) return 0;
    uint32_t u = rd32(0x4A85A + uint32_t(int32_t(int8_t(rd8(s + 3))) * 4));
    int16_t utype = int8_t(rd8(u + 2));
    uint32_t si = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
    uint16_t cell = PosCell((rd32(s + 0xA) & 0xFF00FF00) + 0x800080);
    uint32_t ring = 0x6B83C + uint32_t(rd16(si + 0x3C) << 5);
    uint16_t spice = 0;
    if (harvester) {
        spice = FindSpice(cell, 0x20, rd16(u));
        if (spice) U8{u + 7} &= uint8_t(~0x20);
        else harvester = 0;
    }
    uint16_t best = 0;
    int16_t best_d = 0;
    int16_t k = int16_t(Random() & 0xF);
    int16_t left = 0x10;
    uint32_t ui = rd32(0x6C5BC + uint32_t(utype << 2));
    do {
        if (rd16(ring + k * 2u)) {
            uint16_t c = uint16_t(cell + rd16(ring + k * 2u));
            uint16_t t = TerrainAt(c);
            bool ok = !(rd8(rd32(0x4AA24) + uint32_t(int32_t(int16_t(c)) * 4) + 2) & 0x30) && CellInMap(c) && t != 0xB &&
                      !((t == 6 || t == 7) && rd16(ui + 0x3E));
            if (ok) {
                if (!harvester) return c;
                int16_t d = int16_t(CellDistance(c, spice));
                if (!best_d || d < best_d) {
                    best = c;
                    best_d = d;
                }
            }
        }
        k++;
        left--;
        if (k > 0xF || !rd16(ring + k * 2u)) {
            left = int16_t(left - (0x10 - k));
            k = 0;
        } else {
            k++;
        }
    } while (left > 0);
    return best;
}

// 0x10DA2: what a factory's next upgrade (+0x54 its level) unlocks.
uint16_t UpgradeItem(uint32_t s) {
    uint32_t owned = rd32(HouseGet(rd16(s + 0x4C)) + 0xE);
    uint8_t level = rd8(s + 0x54);
    switch (rd8(s + 2)) {
    case 3: return level == 1 ? 0x10 : 0xF;
    case 4:
        if (!(owned & 0x40000)) return 0x10;
        if (level == 1) return rd16(s + 0x4C) == 2 ? 0xA : 7;
        return level == 2 ? 0xA : 0x11;
    case 5: return rd8(s + 8) ? 1 : 0;
    case 7: return 3;
    case 0xA: return rd8(s + 8) == 2 && level == 1 ? 5 : 2;
    default: return 0;
    }
}

} // namespace game
