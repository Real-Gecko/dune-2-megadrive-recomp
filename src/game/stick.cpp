// The structure tick (0xBCDC): timers for upgrades and building (30
// ticks), decay (0x2A30), scripts (5) and the palace weapon (60), then
// every structure stepped. Structure fields: +0x12 hit points, +0x14
// script delay, +0x16 script, +0x4E palace delay, +0x50 next item, +0x52
// item, +0x54 upgrade level, +0x55 upgrade progress, +0x56 build time
// left (palace: weapon charge), +0x58 build fraction, +0x5A money spent.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U16 scenario{0xFFC050};

uint32_t StructInfo(int16_t type) { return rd32(0x6B954 + type * 4); }
uint32_t UnitTypeInfo(int16_t type) { return rd32(0x6C5BC + type * 4); }
int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t HouseRec(uint32_t s) { return rd32(0x4A9FA + HouseOf(s) * 4); }

// An upgrade step (0x14 of the cost per 30 ticks); done, the level rises
// (a light factory becomes a heavy one, Ordos high-tech a starport...).
void TickUpgrade(uint32_t s, uint32_t info, uint32_t h) {
    uint32_t cost = uint32_t(int32_t(int16_t(rd16(info + 0x16)) / 0x14));
    if (cost > U32{h + 0x12}) return;
    U32{h + 0x12} -= cost;
    U16{s + 0x5A} += uint16_t(cost);
    U8{s + 0x55} -= 10;
    if (int8_t(rd8(s + 0x55)) > 0) return;
    U8{s + 0x54} += 1;
    U8{s + 7} &= uint8_t(~2);
    wr16(s + 0x52, uint16_t(int8_t(rd8(s + 0x50))));
    wr8(s + 0x50, 0);
    if (rd8(s + 2) == 3 && rd8(s + 0x54) == 2) {
        wr8(s + 2, 4);
        wr8(s + 0x54, 0);
        wr16(s + 0x5E, rd16(rd32(0x6B964) + 0x10));
        wr16(s + 0x12, rd16(s + 0x5E));
        U8{h + 0x11} |= 0x10;
    }
    if (rd8(s + 2) == 0xA && rd8(s + 8) == 2 && rd8(s + 0x54) == 2) {
        wr8(s + 2, 7);
        wr8(s + 0x54, 0);
        wr16(s + 0x5E, rd16(rd32(0x6B970) + 0x10));
        wr16(s + 0x12, rd16(s + 0x5E));
        U8{h + 0x11} |= 0x80;
    }
    if (rd8(s + 8) == 2 && rd8(s + 2) == 4 && rd8(s + 0x54) == 2) wr8(s + 0x54, 3);
    if (rd8(s + 2) == 8) {
        uint32_t r = HouseRec(s);
        if ((!rd8(r + 0x12) || !rd8(r + 9)) && rd16(s + 0x52) == 0x10) wr16(s + 0x52, 1);
    }
    if (rd8(s + 2) == 4 && !rd8(HouseRec(s) + 0x12)) {
        switch (rd8(s + 8)) {
        case 1: wr16(s + 0x52, 0xD); break;
        case 2: wr16(s + 0x52, 0xE); break;
        default: wr16(s + 0x52, 0xF); break;
        }
    }
    wr8(s + 0x55, HasUpgrade(s) ? 100 : 0);
}

// A repair step: hit points for an eighth of the cost (palaces free).
void TickRepair(uint32_t s, uint32_t info, uint32_t h) {
    uint16_t cost = rd8(s + 2) == 2 ? 0 : ScaleHealth(rd16(info + 0x16), PartOf(rd16(info + 0x10), 8));
    if (cost > U32{h + 0x12}) {
        U8{s + 4} &= uint8_t(~0x20);
        ClearMark(s, 2);
        return;
    }
    U32{h + 0x12} -= cost;
    if (HouseOf(s) != int16_t(player_house)) U16{s + 0x12} += S16{0xFFC050} >= 3 ? 8 : 6;
    else U16{s + 0x12} += 0xE;
    if (int16_t(rd16(s + 0x12)) > int16_t(rd16(info + 0x10))) {
        wr16(s + 0x12, rd16(info + 0x10));
        U16{s + 4} &= 0x9FFF;
        ClearMark(s, 2);
    }
}

// The finished item of a computer house's construction yard is placed at
// a cell of its build list (+0x32: five type, cell pairs) or refunded.
void PlaceForAi(uint32_t s, uint32_t h) {
    uint32_t o = BuiltStructure(s);
    wr8(s + 3, 0xFF);
    SetStructureState(s, 0);
    int k = 0;
    for (; k < 5; k++) {
        uint32_t e = h + 0x32 + k * 4u;
        if (int16_t(int8_t(rd8(o + 2))) == int16_t(rd16(e)) && PlaceStructure(o, rd16(e + 2))) {
            wr16(e, 0);
            wr16(e + 2, 0);
            wr8(o + 9, 0xFF);
            break;
        }
    }
    if (k == 5) {
        U32{h + 0x12} += uint32_t(int32_t(int16_t(rd16(StructInfo(int8_t(rd8(o + 2))) + 0x16))));
        FreeStructure(o);
    }
}

// A build step: the item's time scaled by the structure's health (the
// computer's capped by scenario), paid as it goes.
void TickBuild(uint32_t s, uint32_t info, uint32_t h) {
    uint8_t type = rd8(s + 2);
    uint32_t o = type == 8 ? StructureGet(uint16_t(int8_t(rd8(s + 3)))) : UnitGet(uint16_t(int8_t(rd8(s + 3))));
    uint32_t oi = type == 8 ? StructInfo(int8_t(rd8(o + 2))) : UnitTypeInfo(int8_t(rd8(o + 2)));
    uint16_t rate = rd16(s + 0x12) == rd16(info + 0x10) ? 0x100 : PartOf(rd16(info + 0x10), rd16(s + 0x12));
    if (HouseOf(s) != int16_t(player_house)) {
        int32_t cap = int32_t(int16_t(scenario)) * 20 + 0x5F;
        if (int32_t(rate) >= cap) rate = uint16_t(scenario * 20 + 0x5F);
    }
    uint16_t step = PartOf(rd16(oi + 0x18), rd16(oi + 0x16));
    if (rate != 0x100) step = ScaleHealth(step, rate);
    if (type == 0xD) {
        step >>= 2;
        if (step <= 1) step = 1;
    }
    step = uint16_t(step + rd16(s + 0x58));
    uint16_t pay = step >> 8;
    uint32_t credits = U32{h + 0x12};
    if (credits && uint32_t(int32_t(int16_t(pay))) >= credits) pay = uint16_t(credits);
    if (uint32_t(int32_t(int16_t(pay))) > U32{h + 0x12}) {
        if (HouseOf(s) == int16_t(player_house)) U8{s + 4} |= 0x40;
        return;
    }
    wr16(s + 0x58, step & 0xFF);
    U32{HouseGet(uint16_t(HouseOf(s))) + 0x12} -= pay;
    U16{s + 0x5A} += pay;
    if (rate <= rd16(s + 0x56)) U16{s + 0x56} -= rate;
    else wr16(s + 0x56, 0);
    if (rd16(s + 0x56)) return;
    wr16(s + 0x58, 0);
    SetStructureState(s, 2);
    if (HouseOf(s) == int16_t(player_house)) {
        if (type != 0xA && type != 7) {
            U8{s + 6} |= 0x20;
            PlayVoice(0);
        }
    } else if (type == 8) {
        PlaceForAi(s, h);
    }
}

// A repair facility mends the unit inside.
void TickRepairUnit(uint32_t s, uint32_t info, uint32_t h) {
    if (rd16(s + 4) & 0x4000) {
        if (U32{h + 0x12}) U8{s + 4} &= uint8_t(~0x40);
        return;
    }
    if (!rd16(s + 0x56) || rd8(s + 3) == 0xFF) return;
    uint16_t rate = rd16(s + 0x12) == rd16(info + 0x10) ? 0x100 : PartOf(rd16(info + 0x10), rd16(s + 0x12));
    uint32_t v = LinkedUnit(s);
    int16_t cost = int16_t(ScaleHealth(rd16(UnitTypeInfo(int8_t(rd8(v + 2))) + 0x16), 2));
    if (uint32_t(int32_t(cost)) > U32{h + 0x12}) return;
    U32{h + 0x12} -= uint32_t(int32_t(cost));
    if (rate <= rd16(s + 0x56)) U16{s + 0x56} -= rate;
    else wr16(s + 0x56, 0);
    if (rd16(s + 0x56)) return;
    uint32_t u = rd32(0x4A85A + int8_t(rd8(s + 3)) * 4);
    if (rd8(u + 2) == 0x10) wr16(u + 0x5A, rd16(s + 0x4E));
    wr16(s + 0x56, 0);
    SetStructureState(s, 2);
    if (HouseOf(s) == int16_t(player_house)) PlayVoice(uint16_t(player_house + 0x37));
}

} // namespace

// 0x9A66, 0x9ADA, 0x9B2A: a structure's marker sprite (at +0xA, node +0x10)
// shows a frame, set as mark bit 1, 0 or 2 (0xFFC868 by index).
void SetMark(uint32_t s, uint8_t bit, uint32_t frame, uint32_t attr) {
    if (uint16_t n = rd16(s + 0x10)) {
        wr32(Node(n) + 8, frame);
    } else {
        wr8(s + 0xE, 3);
        uint32_t n2 = AllocSprite(s + 0xA, attr, frame, kSpriteList);
        wr16(s + 0x10, uint16_t(n2));
        if (!uint16_t(n2)) return;
    }
    U8{0xFFC868u + rd16(s)} |= uint8_t(1 << bit);
}
void SetReadyMark(uint32_t s, uint16_t k) { SetMark(s, 1, rd32(0x9ABA + k * 4u), 0x6060000); }
void SetPowerMark(uint32_t s) { SetMark(s, 0, 0x658E8, 0x3030000); }
void SetRepairMark(uint32_t s, uint16_t k) { SetMark(s, 2, rd32(0x659FE + k * 4u), 0x7070000); }

namespace {

// 0xB3E4: a structure's flag: the house's flag overlay on the cell below,
// and its two tiles put straight into plane A when on screen (the frame
// table the original reads is unused). Once a frame (flag +1 bit 4); else
// the original returns d0 as left: the house.
uint16_t DrawFlag(uint32_t pos, int16_t layout, int16_t house, uint32_t flags) {
    U8 f{flags + 1};
    if (f & 0x10) return uint16_t(house);
    f |= 0x10;
    int16_t cy = int16_t(int16_t(uint16_t(pos >> 16) >> 3) >> 5) + 1;
    if (layout >= 6) cy++;
    int16_t cx = int16_t(int16_t(uint16_t(pos) >> 3) >> 3) & 0xFFFC;
    uint32_t c = 0xFF7D9C + uint32_t(int32_t(int16_t(cx + uint16_t(cy * 0x100))));
    wr8(c, uint8_t((rd8(c) & 1) + (house + 0x16) * 2));
    int16_t x = int16_t((uint16_t(pos) >> 3) - (U16{0xFFE3EC} & 0xFFE0));
    if (x < 0 || x > int16_t(screen_width)) return 0;
    uint16_t col = uint16_t(x) >> 2;
    int16_t py = int16_t(uint16_t(pos >> 16) >> 3);
    int16_t rel = int16_t(py - U16{0xFFE3EE});
    int16_t row = int16_t(py - (U16{0xFFE3EE} & 0xFFE0) + int16_t(rd16(0xB3D4 + layout * 2)));
    if (row < 0) return 0;
    rel = int16_t(rel + int16_t(rd16(0xB3D4 + layout * 2)));
    if (row > 0xF0) return 0;
    uint16_t at = uint16_t(row << 4);
    col = uint16_t(col + ((uint16_t(-U16{0xFFE3B6}) & 0x1E0) >> 2));
    if (col >= 0x80) col -= 0x80;
    at = uint16_t((at + ((U16{0xFFE3BA} & 0x1E0) << 4)) & 0xFFC);
    at = uint16_t(at + col + plane_a_base);
    uint32_t pal = rd32(0xB530 + house * 4);
    uint32_t top = 0x2BE02BF | pal, bottom = 0x2C002C1 | pal;
    display::vram_w16(at, uint16_t(top >> 16));
    display::vram_w16(uint16_t(at + 2), uint16_t(top));
    if (rel < 0xE8) {
        display::vram_w16(uint16_t(at + 0x80), uint16_t(bottom >> 16));
        display::vram_w16(uint16_t(at + 0x82), uint16_t(bottom));
    }
    return 1;
}

// A cell's ground tile set; if on screen and in its reveal fade, the reveal
// advanced.
void SetGround(uint16_t cell, uint16_t tile) {
    uint32_t m = 0xFF7D9C + uint32_t(int32_t(int16_t(cell)) * 4);
    wr16(m, uint16_t((rd16(m) & 0xFE00) + (tile & 0x1FF)));
    if (!RedrawCell(cell)) return;
    int16_t fog = int16_t((rd16(m) >> 9) & 0x7F);
    if (fog > 0 && int32_t(fog) < int32_t(U16{0xFFC224})) {
        RevealAround(CellPos(cell), 1);
        RedrawCell(cell);
    }
}

// A cell's position at its centre.
uint32_t CellCentre(uint16_t cell) {
    return ((uint32_t(int32_t(int16_t(cell)) & 0xFC0) << 10) + (cell & 0x3F)) * 0x100 + 0x800080;
}

void CancelIf(uint32_t s, bool hit) {
    if (hit && (rd16(s + 6) & 0x4000)) {
        CancelBuild(s);
        U8{s + 6} &= uint8_t(~0x40);
    }
}

uint16_t HouseItem(uint32_t s) {
    switch (rd8(s + 8)) {
    case 1: return 0xD;
    case 2: return 0xE;
    default: return 0xF;
    }
}

// 0xDEA4: a yard's or factory's item checked against the house's
// structures (0x...+0xE bits): a lost prerequisite cancels its build and
// picks another item.
void CheckPrerequisites(uint32_t s) {
    if (int8_t(rd8(s + 3)) < 0) {
        wr8(s + 3, 0xFF);
        U16{s + 6} &= 0x9FFF;
    }
    uint32_t have = rd32(HouseGet(uint16_t(HouseOf(s))) + 0xE);
    int16_t item = int16_t(rd16(s + 0x52));
    if (rd8(s + 2) == 8) {
        uint32_t o = rd32(0x4A71E + int8_t(rd8(s + 3)) * 4);
        int16_t ot = o ? int8_t(rd8(o + 2)) : -1;
        if (!(have & (1u << 9))) {
            CancelIf(s, o && ot >= 2 && ot <= 18 && ot != 6 && ot != 8 && ot != 9);
            if (item < 2 || item > 18 || item == 6 || item == 8) wr16(s + 0x52, 1);
            else if (item != 9) wr16(s + 0x52, 9);
        } else if (!(have & (1u << 18))) {
            CancelIf(s, o && (ot == 4 || ot == 5 || ot == 7 || ot == 10 || (ot >= 13 && ot <= 16)));
            if (item == 5 || item == 7 || item == 10 || (item >= 13 && item <= 16)) wr16(s + 0x52, 0x12);
        } else {
            if (!(have & (1u << 4))) {
                CancelIf(s, o && (ot == 5 || ot == 0xD));
                if (item == 5 || item == 0xD) wr16(s + 0x52, 4);
            }
            if (!(have & (1u << 11))) {
                CancelIf(s, o && ot == 2);
                if (rd16(s + 0x52) == 2) wr16(s + 0x52, 0xB);
            }
        }
    } else if (rd8(s + 2) == 4) {
        uint32_t o = rd32(0x4A85A + int8_t(rd8(s + 3)) * 4);
        if ((have & 0x40000) != 0x40000) {
            uint8_t ot = o ? rd8(o + 2) : 0xFF;
            CancelIf(s, o && (ot == 7 || ot == 9 || ot == 10 || ot == 0x11));
            item = int16_t(rd16(s + 0x52));
            if (item == 7 || item == 9 || item == 10 || item == 0x11) wr16(s + 0x52, HouseItem(s));
        }
        if (!(have & (1u << 5))) {
            o = rd32(0x4A85A + int8_t(rd8(s + 3)) * 4);
            uint8_t ot = rd8(o + 2);
            CancelIf(s, o && (ot == 8 || ot == 0xB || ot == 0xC));
            ot = rd8(o + 2);
            if (ot == 8 || ot == 0xB || ot == 0xC) wr16(s + 0x52, HouseItem(s));
        }
    }
}

// The refinery and silo-like (0xB, 0xC) light animation: four frames
// every 20 ticks while busy (timers at 0xFFD4B8 by index).
void AnimateLights(uint32_t s) {
    uint32_t t = 0xFFD4B8 + rd16(s) * 2u;
    if (int8_t(rd8(s + 0xF)) < 0) wr8(t, RandomRange(0x48, 0x54));
    if (rd16(s + 0x5C) != 1 && !rd8(t + 1)) return;
    U8{t} -= uint8_t(FrameDelta());
    if (int8_t(rd8(t)) > 0) return;
    wr8(t, 0x14);
    uint16_t f = uint16_t((int8_t(rd8(t + 1)) + 1) & 3);
    wr8(t + 1, uint8_t(f));
    uint16_t cell = PosCell(rd32(s + 0xA));
    if (rd8(s + 2) == 0xC) {
        cell += 2;
        SetGround(cell, rd16(0x6BA14 + f * 2u));
        cell += 0x40;
        SetGround(cell, rd16(0x6BA24 + f * 2u));
    } else {
        cell += 0x41;
        SetGround(cell, rd16(0x6BA34 + f * 2u));
        cell += 1;
        SetGround(cell, rd16(0x6BA44 + f * 2u));
        cell += 0x40;
        SetGround(cell, rd16(0x6BA64 + f * 2u));
        cell -= 1;
        SetGround(cell, rd16(0x6BA54 + f * 2u));
    }
}

} // namespace

// 0xD65A: the running structure's animation, flag, prerequisites and side
// counts (0xFFDBEE the player's, 0xFFDBEC others), and the player's
// structures' markers.
void StructureUpdate() {
    uint32_t s = U32{0xFFD5AC};
    int16_t type = int8_t(rd8(s + 2));
    if (type == 0 || type == 1 || type == 14) return;
    if (type == 15 || type == 16) {
        if (rd16(s + 6) & 0x80) return;
        U8{s + 0xF} -= uint8_t(FrameDelta());
        if (int8_t(rd8(s + 0xF)) > 0) return;
        uint16_t cell = PosCell(rd32(s + 0xA));
        uint32_t tbl = rd32(0x4AA2C);
        uint16_t base = rd16(tbl + 4 + rd16(tbl + (type == 0x10 ? 0x30 : 0x2E)) * 2u);
        uint32_t m = 0xFF7D9C + uint32_t(int32_t(int16_t(cell)) * 4);
        int16_t f = int16_t((rd16(m) & 0x1FF) - base);
        f = int16_t(int8_t(uint8_t(f + RandomRange(0, 2) - 1) & 7));
        uint16_t tile = uint16_t(base + f);
        wr16(m, uint16_t((rd16(m) & 0xFE00) + (tile & 0x1FF)));
        RedrawCell(cell);
        wr8(s + 0xF, RandomRange(0x64, 0x7D));
        return;
    }
    if ((type == 0xB || type == 0xC) && int16_t(rd16(s)) < 0x49) AnimateLights(s);
    if (int16_t(rd16(s)) < 0x49) {
        uint32_t t = 0xFFD54C + rd16(s);
        if (int8_t(rd8(s + 0xF)) < 0) {
            wr8(s + 0xF, 1);
            wr8(t, RandomRange(0x43, 0x51));
        }
        U8{t} -= uint8_t(FrameDelta());
        int16_t layout = int16_t(rd16(rd32(0xFFD5A8) + 0x3C));
        if (int8_t(rd8(t)) <= 0) {
            wr8(t, 7);
            bool draw = true;
            if (HouseOf(s) != int16_t(player_house)) {
                uint16_t c = uint16_t(PosCell(rd32(s + 0xA)) + rd16(0x6BA04 + layout * 2));
                int16_t fog = int16_t((rd16(0xFF7D9C + uint32_t(int32_t(int16_t(c)) * 4)) >> 9) & 0x7F);
                if (fog > 0 && int32_t(fog) < int32_t(U16{0xFFC224})) RevealAround(CellCentre(c), 1);
                draw = int32_t(fog) < int32_t(U16{0xFFC224});
            }
            if (draw && DrawFlag(rd32(s + 0xA), layout, HouseOf(s), s + 6)) wr8(s + 0xF, uint8_t((rd8(s + 0xF) + 1) & 7));
        }
    }
    CheckPrerequisites(s);
    if (HouseOf(s) != int16_t(player_house) || !rd16(s + 0x12)) {
        U16{0xFFDBEC} += 1;
        return;
    }
    U16{0xFFDBEE} += 1;
    U8{s + 6} &= uint8_t(~1);
    switch (type) {
    case 2:
        if (rd8(s + 8) == 1 && U16{0xFFC72C}) U8{s + 6} |= 1;
        if (!rd16(s + 0x56) && !U16{0xFFDC4C} && !(rd16(s + 6) & 0x100)) SetReadyMark(s, 6);
        else if (rd16(s + 4) & 0x2000) SetRepairMark(s, 6);
        else ClearMark(s, 1);
        break;
    case 8:
        if (!U16{0xFFBEA2}) U8{s + 6} |= 1;
        if (!(rd16(s + 6) & 0x8000) && (rd16(s + 6) & 0x2000)) SetReadyMark(s, 3);
        else if (rd16(s + 4) & 0x2000) SetRepairMark(s, 3);
        else ClearMark(s, 1);
        break;
    case 9:
        if (rd16(s + 4) & 0x2000) break;
        if ((rd16(s + 4) & 2) && int16_t(rd16(U32{0xFFC278} + 0x1C)) > int16_t(rd16(U32{0xFFC278} + 0x1A))) SetPowerMark(s);
        else ClearMark(s, 0);
        break;
    case 3:
    case 4:
    case 7:
    case 10:
    case 11: {
        uint32_t h = HouseGet(uint16_t(HouseOf(s)));
        if ((U16{0xFFC72C} || rd16(h + 6) >= rd16(h + 8)) && !(rd16(s + 6) & 2)) U8{s + 6} |= 1;
        break;
    }
    case 5:
        if (!U16{0xFFC728} && !(rd16(s + 6) & 2)) U8{s + 6} |= 1;
        break;
    default: break;
    }
}

// 0xBCDC
void StructureTick() {
    uint32_t now = U32{0xFFC598};
    bool build = false, decay = false, script = false, palace = false;
    if (now >= U32{0xFFD5A0}) {
        build = true;
        U32{0xFFD5A0} = now + 0x1E;
    }
    if (now >= U32{0xFFD5A4} && S16{0xFFC050} > 1) {
        decay = true;
        U32{0xFFD5A4} = now + 0x2A30;
    }
    if (int32_t(now) >= S32{0xFFD59C}) {
        script = true;
        U32{0xFFD59C} = now + 5;
    }
    if (int32_t(now) >= S32{0xFFD598}) {
        palace = true;
        U32{0xFFD598} = now + 0x3C;
    }
    U16{0xFFDBEC} = 0;
    U16{0xFFDBEE} = 0;
    md::Frame frame(0x10);
    uint32_t it = frame.base;
    for (uint32_t s = FirstStructure(it, 0xFFFF, 0xFFFF); s;) {
        U32{0xFFD5AC} = s;
        uint32_t info = StructInfo(int8_t(rd8(s + 2)));
        U32{0xFFD5A8} = info;
        U32{0xFFC208} = s;
        uint32_t h = HouseGet(uint16_t(HouseOf(s)));
        U32{0xFFC204} = h;
        StructureUpdate();
        if (palace && rd8(s + 2) == 2) {
            if (rd16(s + 0x4E)) {
                U16{s + 0x4E} -= 1;
            } else {
                if (rd16(s + 0x56) && !(rd16(s + 6) & 0x100)) U16{s + 0x56} -= 1;
                if (!rd16(s + 0x56)) {
                    if (rd8(s + 8) == 2) {
                        if (uint32_t u = FirstUnit(0, 2, 6)) {
                            SpawnEffect(4, rd32(u + 0xA), 0x1F4, 0);
                            CrushUnit(u);
                        }
                    }
                    if ((rd8(h + 5) & 0xA) == 8) LaunchSpecial(s);
                    U8{s + 7} |= 0x80;
                }
            }
        }
        if (decay && (rd16(s + 4) & 0x400) && (int16_t(rd16(info + 0x10)) >> 1) < int16_t(rd16(s + 0x12))) {
            uint16_t d = rd16(0x6C762 + HouseOf(s) * 30);
            if (rd16(s + 4) & 0x400) d++;
            DamageStructure(s, d, 0);
        }
        if (build) {
            if (rd16(s + 6) & 2) {
                TickUpgrade(s, info, h);
            } else if (rd16(s + 4) & 0x2000) {
                TickRepair(s, info, h);
            } else {
                if (!(rd16(s + 4) & 0x4000) && !(rd16(s + 6) & 0x1000) && rd16(s + 0x56) && rd8(s + 3) != 0xFF &&
                    rd16(s + 0x5C) == 1 && (rd16(info + 0xC) & 2)) {
                    TickBuild(s, info, h);
                } else if (U32{h + 0x12}) {
                    U8{s + 4} &= uint8_t(~0x40);
                }
                if (rd8(s + 2) == 0xD) TickRepairUnit(s, info, h);
                if ((rd16(h + 4) & 8) && (rd16(s + 4) & 2) && HouseOf(s) != int16_t(player_house) && U32{h + 0x12}) {
                    if ((int16_t(rd16(info + 0x10)) >> 1) > int16_t(rd16(s + 0x12))) {
                        U8{s + 4} |= 0x20;
                    } else if ((rd16(info + 0xC) & 2) && !rd16(s + 0x56) && rd8(s + 3) == 0xFF) {
                        uint16_t item = AiChooseBuild(s);
                        if (item != 0xFFFF) StructureBuild(s, int16_t(item));
                    }
                }
            }
        }
        if (script) {
            if (rd16(s + 0x14)) {
                U16{s + 0x14} -= 1;
            } else {
                if (rd32(s + 0x16) && rd32(s + 0x1A)) {
                    for (int i = 0; i < 3; i++)
                        if (!ScriptStep(s + 0x16)) return;
                } else {
                    ScriptReset(s + 0x16, rd32(s + 0x1A));
                    ScriptStart(s + 0x16, int8_t(rd8(s + 2)));
                }
                if (rd8(s + 2) == 0x10 || rd8(s + 2) == 0xF) wr16(s + 0x14, uint16_t(int16_t(rd16(s + 0x14)) >> 1));
            }
        }
        s = NextStructure(it);
        if (U16{0xFFC02C}) {
            if (!U16{0xFFDBEE} && FirstListedStructure(0)) U16{0xFFDBEE} += 1;
            if (!U16{0xFFDBEC} && FirstListedStructure(1)) U16{0xFFDBEC} += 1;
        }
    }
}

} // namespace game
