// Units (0x8C bytes each, slots listed at 0x4A85A): creation, actions,
// facing, speed, visibility. A unit: +0 index, +2 type (info at 0x6C5BC),
// +3 linked unit, +4 flags (bit 0 used, 1 placed, 2 not yet on the map),
// +6, +8 house, +9 seen by houses (bits), +0xA position (y word, x word in
// 1/256 cells), +0x12 hit points, +0x16 script, +0x2A target, +0x4C radar
// blip, +0x4E, +0x54 action, +0x55 next action, +0x68/+0x6B facing
// (step, target, current) of body and turret, +0x6E-+0x71 speed.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};

uint32_t UnitInfo(uint8_t type) { return rd32(0x6C5BC + int16_t(int8_t(type)) * 4); }
int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }

} // namespace

// 0x2058E: a radar blip for types flagged at 0x20572, on the allied
// (0xFFDBE8) or enemy (0xFFDBE4) list.
uint16_t AddBlip(uint32_t u) {
    if (!rd8(0x20572 + rd8(u + 2))) return 0;
    uint32_t b = U32{0xFFDBD0};
    uint32_t n = Node(rd16(b + 4));
    wr16(n + 2, 0);
    U32{0xFFDBD0} = n;
    wr32(b + 2, 0);
    wr16(b, rd16(u));
    U32 list{Allied(uint16_t(HouseOf(u)), player_house) ? 0xFFDBE8u : 0xFFDBE4u};
    uint32_t head = list;
    wr16(b + 4, uint16_t(head));
    if (uint16_t(head)) wr16(head + 2, uint16_t(b));
    list = b;
    return uint16_t(b);
}

// 0x16E7C, 0x16EA0: an object's script: reset onto a script file; started
// at the entry of a type.
void ScriptReset(uint32_t s, uint32_t info) {
    if (!s) return;
    wr32(s, 0);
    wr8(s + 0x34, 0);
    wr8(s + 0xA, 0x11);
    wr8(s + 0xB, 0xF);
    wr32(s + 4, info);
}

// 0x48EBE: the unit linked to a unit (+3).
uint32_t LinkedUnit(uint32_t u) {
    if (!u || rd8(u + 3) == 0xFF) return 0;
    return UnitGet(uint16_t(int8_t(rd8(u + 3))));
}

namespace {

// 0x23EA2: an object's target cleared; a structure (info flag 0x10) left
// empty starts its idle state.
void ClearTarget(uint32_t o) {
    if (!o) return;
    wr16(o + 0x2A, 0);
    if (rd16(o + 6) & 1) return;
    uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(o + 2))) * 4);
    if (!(rd16(info + 0xC) & 0x10)) return;
    if (!LinkedUnit(o)) SetStructureState(o, 0);
}

} // namespace

// 0x23E08: a unit and what it targets let go of each other.
void ReleaseTarget(uint32_t u) {
    if (!u) return;
    uint16_t t = rd16(u + 0x2A);
    uint32_t o = TargetObject(t);
    ClearTarget(u);
    ClearTarget(o);
    if (!t) return;
    o = 0;
    if (uint32_t v = TargetUnit(t)) {
        o = v;
        if (uint32_t(rd16(v + 0x5C)) == uint32_t(int32_t(int16_t(rd16(v + 0x2A))))) wr16(v + 0x5C, 0);
    } else if (TargetStructure(t)) {
        o = TargetStructure(t);
    }
    if (o) wr16(o + 0x2A, 0);
    wr16(u + 0x2A, 0);
}

// 0x23D74: an object targets t; a structure (info flag 0x10) with nothing
// linked starts its busy state.
uint16_t SetTarget(uint32_t o, uint16_t t) {
    if (!o) return 0;
    wr16(o + 0x2A, t);
    if (rd16(o + 6) & 1) return 1;
    uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(o + 2))) * 4);
    if ((rd16(info + 0xC) & 0x10) && !LinkedUnit(o)) SetStructureState(o, 1);
    return 1;
}

// 0x23CD0: two objects target each other (dropping other targets first).
uint16_t LinkTargets(uint16_t a, uint16_t b) {
    if (!TargetValid(a) || !TargetValid(b)) return 0;
    uint32_t oa = TargetObject(a), ob = TargetObject(b);
    if (!oa || !ob) return 0;
    if (ObjectTarget(ob) != ObjectTarget(oa)) {
        ReleaseTarget(oa);
        ReleaseTarget(ob);
    }
    if (!ObjectTarget(oa)) {
        SetTarget(oa, b);
        SetTarget(ob, a);
    }
    return 1;
}

// 0x2E302: a target still exists (a unit on the map, a used structure, any
// cell).
uint16_t TargetValid(uint16_t t) {
    uint16_t i = t & 0x3FFF;
    switch (t >> 14) {
    case 1: {
        uint32_t u = UnitGet(i);
        return u && (rd16(u + 4) & 3) == 3;
    }
    case 2: return rd16(StructureGet(i) + 4) & 1;
    case 3: return 1;
    default: return 0;
    }
}

// 0x2E1FE: the cell a target is on.
uint16_t TargetCellOf(uint16_t t) {
    uint16_t i = t & 0x3FFF;
    switch (t >> 14) {
    case 1: return PosCell(rd32(UnitGet(i) + 0xA));
    case 2: return PosCell(rd32(StructureGet(i) + 0xA));
    case 3: return uint16_t(((i & 0x3F00) >> 2) | ((i & 0x7E) >> 1));
    default: return 0;
    }
}

// 0x2E3BA: the cell of a cell target, else the index.
uint16_t TargetCell(uint16_t t) {
    if ((t & 0xC000) == 0xC000) return uint16_t(((t & 0x3F00) >> 2) | ((t & 0x7E) >> 1));
    return t & 0x3FFF;
}

// 0x23DF6: an object's target word.
uint16_t ObjectTarget(uint32_t o) { return o ? rd16(o + 0x2A) : 0; }

// 0x23724: houses a and b are on one side (the table at 0x23754; a
// negative sum is allied unless one is the player).
bool Allied(uint16_t a, uint16_t b) {
    int16_t x = int16_t(a), y = int16_t(b);
    if (x < 0 || y < 0) return false;
    if (x == y) return true;
    int8_t t = int8_t(int8_t(rd8(0x23754 + x)) + int8_t(rd8(0x23754 + y)));
    if (t > 0) return true;
    if (t == 0) return false;
    return x != int16_t(player_house) && y != int16_t(player_house);
}

// 0x1A8DC: the unit, else the structure, on a cell.
uint32_t UnitOnCell(uint16_t cell) {
    if (int16_t(cell) >= 0x1000) return 0;
    uint32_t c = 0xFF7D9C + int16_t(cell) * 4;
    int16_t i = int16_t(rd8(c + 3) - 1);
    if (i < 0) return 0;
    if (rd8(c + 2) & 0x10) return UnitGet(uint16_t(i));
    if (rd8(c + 2) & 0x20) return StructureGet(uint16_t(i));
    return 0;
}

// 0x4365E, 0xBB02: unit and structure slots.
uint32_t UnitGet(uint16_t i) {
    if (int16_t(i) < 0 || int16_t(i) >= 0x66) return 0;
    return rd32(0x4A85A + i * 4u);
}
uint32_t StructureGet(uint16_t i) {
    if (int16_t(i) < 0 || int16_t(i) >= 0x49) return 0;
    return rd32(0x4A71E + i * 4u);
}

// 0x2E088: team slot (0x54 bytes).
uint32_t TeamGet(uint16_t i) {
    if (int16_t(i) < 0 || int16_t(i) >= 0x10) return 0;
    return U32{0xFFDCA0} + int16_t(i) * 0x54;
}

// 0x2E1D8, 0x2E394, 0x2E35A: target words, the top two bits the kind (1
// unit, 2 structure), the rest the index.
uint32_t TargetStructure(uint16_t t) { return (t >> 14) == 2 ? StructureGet(t & 0x3FFF) : 0; }
uint32_t TargetUnit(uint16_t t) { return (t >> 14) == 1 ? UnitGet(t & 0x3FFF) : 0; }
uint32_t TargetObject(uint16_t t) {
    switch (t >> 14) {
    case 1: return UnitGet(t & 0x3FFF);
    case 2: return StructureGet(t & 0x3FFF);
    default: return 0;
    }
}

void ScriptStart(uint32_t s, int16_t type) {
    if (!s) return;
    uint32_t info = rd32(s + 4);
    if (!info) return;
    wr32(s, 0);
    wr8(s + 0x34, 0);
    wr8(s + 0xA, 0x11);
    wr8(s + 0xB, 0xF);
    uint32_t text = rd32(info + 4), offsets = rd32(info + 8);
    wr32(s, text + int32_t(int16_t(rd16(offsets + int32_t(type) * 2))) * 2);
}

// 0x4332E: a unit of a type for a house, in slot index or the type's first
// free slot (0 when the house is full or none is free).
uint32_t CreateUnit(uint16_t index, uint16_t type, uint16_t house) {
    if (type == 0xFFFF || house == 0xFFFF) return 0;
    if (!U32{0xFFDE58}) return 0;
    uint32_t h = HouseGet(house);
    uint32_t info = rd32(0x6C5BC + type * 4u);
    bool counts = rd8(0x20572 + type) != 0;
    if (rd16(h + 6) >= rd16(h + 8) && counts && rd16(info + 0x3E) != 4 && rd16(info + 0x3E) != 5 &&
        !U16{0xFFC148})
        return 0;
    int16_t i = int16_t(index);
    uint32_t u = 0;
    if (i <= 0) {
        i = int16_t(rd16(info + 0x34));
        int16_t last = int16_t(rd16(info + 0x36));
        for (; i <= last; i++) {
            uint32_t c = rd32(0x4A85A + int32_t(i) * 4);
            if (!(rd16(c + 4) & 1)) {
                u = c;
                break;
            }
        }
        if (last == 10) U16{0xFFC728} = i < 10;
        else if (last == 0x65) U16{0xFFC72C} = i >= 0x65;
    } else {
        u = rd32(0x4A85A + int32_t(i) * 4);
        if (rd16(u + 4) & 1) u = 0;
    }
    if (!u) return 0;
    if (counts && rd16(info + 0x3E) != 4) U16{h + 6} += 1;
    MemSet(u, 0, 0x8C);
    wr16(u, uint16_t(i));
    wr8(u + 2, uint8_t(type));
    wr8(u + 3, 0xFF);
    wr16(u + 4, 3);
    wr16(u + 6, 1);
    wr8(u + 8, uint8_t(house));
    wr16(u + 0x14, 0);
    wr8(u + 0x74, uint8_t(house));
    wr8(u + 0x7C, 0xFF);
    wr8(0xFFC650 + int32_t(i) * 2, 0);
    wr8(0xFFC651 + int32_t(i) * 2, 0);
    if (type == 0x19) wr8(u + 0x5E, 3);
    wr16(u + 0x4C, AddBlip(u));
    wr32(0xFFDCC0 + U16{0xFFDCBC} * 4u, u);
    U16{0xFFDCBC} += 1;
    U8{rd32(0x4AA0E + int16_t(house) * 4) + type} += 1;
    return u;
}

// 0x436A8: a unit's action (0x6CF00: 12-byte records; word 0: 0 waits for
// the current one, 1 interrupts); starts its script.
void SetUnitAction(uint32_t u, int16_t action) {
    if (!u) return;
    if (rd8(u + 0x54) == 0xD || rd8(u + 0x54) == 0xA) return;
    if (action == -1) return;
    if (rd8(u + 8) == 3 && action == 3) action = 0xB;
    if (rd8(u + 2) == 0x10) {
        uint32_t s = TargetStructure(ObjectTarget(u));
        if (s && rd8(s + 2) == 0xC) ReleaseTarget(u);
    }
    uint16_t kind = rd16(0x6CF00 + uint16_t(action * 12));
    if (kind == 0) {
        if (rd32(u + 0x4E)) {
            wr8(u + 0x55, uint8_t(action));
            return;
        }
    } else if (kind != 1) {
        return;
    }
    wr8(u + 0x54, uint8_t(action));
    wr8(u + 0x55, 0xFF);
    wr32(u + 0x4E, 0);
    wr16(u + 0x14, 0);
    ScriptReset(u + 0x16, 0xFFC1B0);
    wr16(u + 0x22, uint16_t(action));
    ScriptStart(u + 0x16, int16_t(int8_t(rd8(u + 2))));
}

// 0xBB1E: a house's structures (but slabs) numbered 0x10, 0x20, ...
void NumberStructures(int16_t house) {
    uint16_t v = 0x10;
    for (int i = 0; i < 0x49; i++) {
        uint32_t s = rd32(0x4A71E + i * 4u);
        if (HouseOf(s) == house && rd8(s + 2) != 0xC) {
            wr16(s + 0x14, v);
            v = uint16_t(v + 0x10);
        }
    }
}

// 0x23264: house sees unit u: counts it once, marks enemies as met, warns
// the player.
void UnitSeenBy(uint32_t u, int16_t house) {
    if (!u) return;
    uint32_t h = HouseGet(uint16_t(house));
    uint8_t bit = uint8_t(1u << house);
    if (house == 1 && rd8(u + 2) != 0x19) bit |= 8;
    if ((rd8(u + 9) & bit) && (rd16(h + 4) & 8)) {
        U8{u + 9} |= bit;
        return;
    }
    uint32_t info = UnitInfo(rd8(u + 2));
    if (!(rd16(info + 0x38) & 0x8000) && rd8(u + 2) != 0x19) return;
    int16_t owner = HouseOf(u);
    if (!(rd8(u + 9) & bit)) {
        if (Allied(uint16_t(owner), uint16_t(house))) U16{h + 0xC} += 1;
        else U16{h + 0xA} += 1;
    }
    if (rd16(info + 0x3E) != 4 && rd8(u + 2) != 0x19 && !Allied(uint16_t(owner), uint16_t(house))) {
        U8{h + 5} |= 8;
        if (!(rd16(HouseGet(uint16_t(owner)) + 4) & 8)) {
            NumberStructures(owner);
            U8{HouseGet(uint16_t(owner)) + 5} |= 8;
        }
    }
    if (house == int16_t(player_house) && U16{0xFFC264} && !Allied(uint16_t(owner), player_house) &&
        rd8(u + 2) != 0x19) {
        uint32_t p = U32{0xFFC278};
        if (!rd16(p + 0x28)) PlayVoice(1);
        wr16(p + 0x28, 8);
        if (uint32_t t = TeamGet(uint16_t(int8_t(rd8(u + 0x75))))) wr16(t + 0x32, 1);
    }
    if (!Allied(uint16_t(owner), uint16_t(house)) && rd8(u + 0x54) == 8) SetUnitAction(u, 0xB);
    if (owner == int16_t(player_house) || (owner == 3 && player_house == 1)) wr8(u + 9, 0xFF);
    else U8{u + 9} |= bit;
}

// 0x47E24: body (turret 0) or turret facing towards a direction (0-255);
// immediate sets it, otherwise it turns by the type's rate the short way.
void UnitSetFacing(uint32_t u, uint16_t facing, uint16_t immediate, uint16_t turret) {
    if (!u) return;
    uint32_t f = u + (turret ? 0x6B : 0x68);
    wr8(f, 0);
    wr8(f + 1, uint8_t(facing));
    if (immediate) {
        wr8(f + 2, uint8_t(facing));
        return;
    }
    if (rd8(f + 2) == uint8_t(facing)) return;
    wr8(f, uint8_t(rd16(UnitInfo(rd8(u + 2)) + 0x44) << 2));
    int16_t d = int16_t(facing - rd8(f + 2));
    if ((d > -0x80 && d < 0) || d > 0x80) wr8(f, uint8_t(-int8_t(rd8(f))));
}

// 0x47F4C: a unit's speed as a fraction (1-255) of its type's.
void SetUnitSpeed(uint32_t u, uint16_t speed) {
    wr8(u + 0x70, 0);
    wr8(u + 0x6F, 0);
    wr8(u + 0x6E, 0);
    if (speed == 0 || speed >= 0x100) {
        wr8(u + 0x71, 0);
        return;
    }
    wr8(u + 0x71, uint8_t(speed));
    uint16_t v = ScaleHealth(rd16(UnitInfo(rd8(u + 2)) + 0x42), speed);
    uint16_t frac = uint16_t(v << 4), whole = uint16_t(v >> 4);
    if (whole) frac = 0xFF;
    else whole = 1;
    wr8(u + 0x70, uint8_t(whole));
    wr8(u + 0x6E, uint8_t(frac));
}

// 0x12210: a unit's sprite frame from its type's sprite kind (info +0x4C)
// and its facing.
void UpdateUnitSprite(uint32_t u) {
    if (!u || !(rd16(u + 4) & 2) || int8_t(rd8(u + 0x73)) < 0 || !rd16(u + 0x10)) return;
    uint8_t type = rd8(u + 2);
    uint32_t info = UnitInfo(type);
    uint16_t node = rd16(u + 0x10), facing = rd8(u + 0x6A);
    if (type == 9 || type == 0xA) {
        if (rd16(u + 6) & 0x40) return;
        SetTankFrame(type == 9 ? 0x2EC7A : 0x2F2FA, node, facing, rd8(u + 0x6D), uint16_t(HouseOf(u)));
        return;
    }
    uint16_t cls = rd16(info + 0x46), flip = 0;
    if (type == 0 && rd8(u + 3) != 0xFF) cls = uint16_t(cls + 3);
    uint16_t dir8 = uint16_t(((facing + 0x10) & 0xFF) >> 5 & 7);
    uint16_t dir16 = uint16_t(((facing + 8) & 0xFF) >> 4 & 0xF);
    switch (rd16(info + 0x4C)) {
    case 0:
        if (type == 0x18) cls = rd16(0x6C6CC + dir16 * 2u);
        break;
    case 1:
        if (type == 0x10 && !(rd16(u + 4) & 4)) {
            uint16_t t = rd8(u + 0x54) == 5 ? TerrainAt(PosCell(rd32(u + 0xA))) : 0;
            if (t == 8 || t == 9) SetDirFrame(rd16(u + 0x5A), facing, int16_t(int8_t(rd8(u + 0x73))));
            else HideSprite(rd16(u + 0x5A));
        }
        if (type == 0x19) return;
        [[fallthrough]];
    case 5: {
        uint16_t f = rd16(0x6C668 + dir8 * 4u);
        cls = uint16_t(cls + f);
        flip = rd16(0x6C66A + dir8 * 4u);
        if (type == 1) {
            SetFrame4(node, f, flip);
            return;
        }
        if (type == 0x1A) {
            if (rd16(u + 6) & 0x200) f = 3;
            UploadFrame4(node, f, flip);
            return;
        }
        break;
    }
    case 2:
        cls = uint16_t(cls + rd16(0x6C688 + dir16 * 4u));
        flip = rd16(0x6C68A + dir16 * 4u);
        break;
    case 3: case 4:
        cls = uint16_t(cls + rd16(0x6C648 + dir8 * 4u) * 3 + int8_t(rd8(0x6C6C8 + (rd8(u + 0x73) & 3))));
        flip = rd16(0x6C64A + dir8 * 4u);
        break;
    }
    SetSpriteClass(node, int16_t(cls), flip, uint16_t(HouseOf(u)));
}

// 0x207E0: a unit's radar blip back to the free list (lists at 0x207D8).
void FreeUnitBlip(uint16_t b) {
    if (int16_t(b) >= 0) return;
    uint32_t n = Node(b);
    uint32_t u = rd32(0x4A85A + rd16(n) * 4u);
    wr16(n, 0);
    bool allied = Allied(uint16_t(HouseOf(u)), player_house);
    uint16_t prev = rd16(n + 2), next = rd16(n + 4);
    if (!prev) {
        wr32(rd32(0x207D8 + (allied ? 4 : 0)), Node(next));
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

// 0x43504: a unit record freed.
void FreeUnit(uint32_t u) {
    if (!u) return;
    uint32_t info = UnitInfo(rd8(u + 2));
    ScriptReset(u + 0x16, 0xFFC1B0);
    ForgetObject(u);
    U8{rd32(0x4AA0E + HouseOf(u) * 4) + int16_t(int8_t(rd8(u + 2)))} -= 1;
    FreeObjectSprite(rd16(u + 0x10));
    FreeUnitBlip(rd16(u + 0x4C));
    wr16(u + 0x4C, 0);
    if (rd8(u + 2) == 0x10) FreeObjectSprite(rd16(u + 0x5A));
    if (u == U32{0xFFC260}) U32{0xFFC260} = 0;
    wr16(u + 4, 0);
    U16 count{0xFFDCBC};
    for (int16_t i = 0; i < int16_t(count); i++) {
        uint32_t at = 0xFFDCC0 + i * 4u;
        if (rd32(at) != u) continue;
        count = uint16_t(count - 1);
        if (rd8(0x20572 + int16_t(int8_t(rd8(u + 2)))) && rd16(info + 0x3E) != 4)
            U16{HouseGet(uint16_t(int8_t(rd8(u + 0x74)))) + 6} -= 1;
        int16_t idx = int16_t(rd16(u));
        if (idx >= 0x19 && idx <= 0x65) U16{0xFFC72C} = 0;
        else if (idx >= 0 && idx <= 10) U16{0xFFC728} = 1;
        if (i < int16_t(count)) MemCpy(at, at + 4, (int16_t(count) - i) * 4);
        return;
    }
}

// 0x23450: no house sees the unit any more; an enemy selection is dropped.
void UnitUnseen(uint32_t u, int16_t) {
    if (!u) return;
    if (u == U32{0xFFC25C} && HouseOf(u) != int16_t(player_house)) {
        U32{0xFFC25C} = 0;
        DropSelection();
        SetCursorShape(0, -1);
    }
    if (!rd8(u + 9)) return;
    for (uint32_t h = FirstHouse(0); h; h = NextHouse(0)) {
        uint8_t bit = uint8_t(1u << rd16(h));
        if (!(rd8(u + 9) & bit)) continue;
        if (Allied(uint16_t(HouseOf(u)), rd16(h))) U16{h + 0xC} -= 1;
        else U16{h + 0xA} -= 1;
        U8{u + 9} &= uint8_t(~bit);
    }
}

// 0x47072: a unit taken off the map and freed (under a new structure).
void CrushUnit(uint32_t u) {
    if (!u) return;
    U8{u + 5} |= 2;
    if (u == U32{0xFFC25C}) {
        DropSelection();
        U32{0xFFC25C} = 0;
    }
    U8{u + 5} |= 0x40;
    CancelOrder(0, u);
    UnitUnseen(u, -1);
    FreeUnit(u);
}

// 0x4C5C: the selection box hidden.
void DropSelection() {
    if (uint16_t n = U16{0xFFBF60}) {
        U8{0xFFBF0D} &= uint8_t(~2);
        U8{Node(n) + 7} |= 0x80;
    }
}

namespace {

// 0x19D38: a unit leaves a cell it covered.
void UnmarkCell(uint32_t u, uint16_t cell) {
    uint32_t c = 0xFF7D9C + int16_t(cell) * 4;
    if (!(rd8(c + 2) & 0x10) || UnitAt(cell) != u) return;
    if (cell == PosCell(rd32(u + 0x4E)) && !(rd16(u + 4) & 0x40)) return;
    wr8(c + 3, 0);
    U8{c + 2} &= uint8_t(~0x10);
}

// 0x19DBC: the player's unit uncovers a cell it covers.
void SightCell(uint32_t u, uint16_t cell) {
    if (int16_t(int8_t(rd8(u + 8))) != int16_t(player_house)) return;
    if (rd8(0xFF7D9C + int16_t(cell) * 4 + 2) & 8) return;
    RevealCell(cell, uint16_t(int8_t(rd8(u + 8))));
    UpdateCellSprite(cell, 1, 0);
}

// 0x19E10: calls fn (0x714EE: 0 unmark, 1 sight, 2 nothing) for the cells
// a unit of a size (16-32) at pos covers (offsets at 0x6C80E, origin
// shift at 0x6CC2E). Its only caller passes sizes up to 0x20, so the
// original's branch for larger ones is not reached.
void ForCoveredCells(uint16_t size, uint32_t pos, uint32_t u, uint16_t fn) {
    if (!size || !pos) return;
    int16_t r = int16_t(size - 1);
    if (r > 0x20) r = 0x20;
    if (r < 0xF) r = 0xF;
    pos -= rd32(0x6CC2E + r * 4u);
    uint32_t offs = 0x6C80E + uint32_t(r << 5);
    uint32_t hi = pos & 0xFFFF0000, lo = pos & 0xFFFF, d = 0;
    uint16_t last = 0;
    for (int k = 0; k < 9; k++) {
        uint32_t p = hi + (d & 0xFFFF0000) + ((lo + (d & 0xFFFF)) & 0xFFFF);
        if (!(p & 0xC000C000)) {
            uint16_t c = PosCell(p);
            if (c != last) {
                U16{0xFFC214} += 1;
                if (fn == 0) UnmarkCell(u, c);
                else if (fn == 1) SightCell(u, c);
                last = c;
            }
        }
        d = rd32(offs);
        offs += 4;
        if (!d) break;
    }
}

} // namespace

// 0x19B4C: a unit's cells updated (mode 0 off the map, 1 onto it, 2
// refresh); visibility to the player first.
void CancelOrder(uint16_t mode, uint32_t u) {
    if (!u || (rd16(u + 4) & 4) || !(rd16(u + 4) & 1)) return;
    uint32_t info = UnitInfo(rd8(u + 2));
    if (rd16(info + 0x3E) == 4) return;
    uint32_t pos = U32{0xFFC610} ? uint32_t(U32{0xFFC610}) : rd32(u + 0xA);
    uint16_t cell = PosCell(pos);
    if ((rd8(0xFF7D9C + int16_t(PosCell(rd32(u + 0xA))) * 4 + 2) & 8) || HouseOf(u) == int16_t(player_house))
        UnitSeenBy(u, int16_t(player_house));
    else
        UnitUnseen(u, int16_t(player_house));
    if (mode == 1) {
        if (Allied(uint16_t(HouseOf(u)), player_house) && !IsSpiceCell(cell) && rd8(u + 2) != 0x19)
            RevealAround(rd32(u + 0xA), 1);
        if (!UnitOnCell(cell)) {
            uint32_t c = 0xFF7D9C + int16_t(cell) * 4;
            wr8(c + 3, uint8_t(rd16(u) + 1));
            U8{c + 2} |= 0x10;
        }
    }
    if (mode) {
        U8{u + 4} |= 0x10;
        U16{0xFFC21C} += 1;
    }
    int16_t r = int16_t(rd16(info + 0x3A) + 3);
    if (r > 0x20) r = 0x20;
    ForCoveredCells(uint16_t(r), pos, u, mode);
    if (rd8(u + 2) == 0x19) {
        ForCoveredCells(uint16_t(r), rd32(u + 0x60), u, mode);
        ForCoveredCells(uint16_t(r), rd32(u + 0x64), u, mode);
    }
}

// A cell target on a unit or structure becomes that object.
uint16_t ObjectOnCellTarget(uint16_t t, uint32_t self) {
    uint16_t c = TargetCell(t);
    if (uint32_t o = UnitAt(c)) return o == self ? t : MakeTarget(rd16(o), 1);
    if (uint32_t o = StructureAt(c)) return MakeTarget(rd16(o), 2);
    return t;
}

// 0x47D20: a unit's destination (+0x5C; its path +0x7C reset); a
// structure it may enter (or any, for aircraft) links with it.
void UnitMoveTo(uint32_t u, uint16_t t) {
    if (!u || !TargetValid(t) || t == rd16(u + 0x5C)) return;
    if ((t >> 14) == 3) t = ObjectOnCellTarget(t, u);
    if (uint32_t st = TargetStructure(t)) {
        if ((rd8(st + 8) == rd8(u + 8) && CanEnter(u, st) == 1) || rd16(UnitInfo(rd8(u + 2)) + 0x3E) == 4)
            LinkTargets(MakeTarget(rd16(u), 1), MakeTarget(rd16(st), 2));
    }
    wr16(u + 0x5C, t);
    wr8(u + 0x7C, 0xFF);
}

// 0x47BD4: a unit's attack target (+0x5A); units without a turret also
// head there. Harvesters never attack; aircraft only by anti-air units.
void UnitAttack(uint32_t u, uint16_t t) {
    if (rd8(u + 2) == 0x10) return;
    if (!TargetValid(t) || !u || t == rd16(u + 0x5A)) return;
    if (uint32_t v = TargetUnit(t))
        if (rd16(UnitInfo(rd8(v + 2)) + 0x3E) == 4 && !(rd16(UnitInfo(rd8(u + 2)) + 0xC) & 0x1000)) return;
    if ((t >> 14) == 3) {
        uint16_t c = TargetCell(t);
        if (uint32_t o = UnitAt(c)) t = MakeTarget(rd16(o), 1);
        else if (uint32_t o2 = StructureAt(c)) t = MakeTarget(rd16(o2), 2);
    }
    if (t == MakeTarget(rd16(u), 1)) t = MakeTarget(PosCell(rd32(u + 0xA)), 3);
    wr16(u + 0x5A, t);
    if (!(rd16(UnitInfo(rd8(u + 2)) + 0xC) & 0x40)) {
        wr16(u + 0x5C, t);
        wr8(u + 0x7C, 0xFF);
    }
}

// 0x5628: what a cell costs a unit to enter (facing the 0x20-step
// direction passed; the original's diagonal penalty tests bit 0 of it, so
// never applies): 0x100 blocked, 0 its saboteur target, -1/-2 a structure it
// may enter, else 255 minus the terrain's speed for its movement type
// (0x6CCB6). Infantry of other sides can be crushed by tracked and wheeled
// units; walls of other sides stop saboteurs.
uint16_t MoveCost(uint32_t u, uint16_t cell, uint16_t facing) {
    if (!u) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    uint16_t move = rd16(ui + 0x3E);
    if (!CellInMap(cell) && move != 4) return 0x100;
    uint32_t v = UnitAt(cell);
    if (v && v != u && rd8(u + 2) != 0x19) {
        if (rd8(u + 2) == 6 && MakeTarget(rd16(v), 1) == rd16(u + 0x5C)) return 0;
        uint16_t hu = rd8(u + 0x5F) ? 2 : rd8(u + 8);
        uint16_t hv = rd8(v + 0x5F) ? 2 : rd8(v + 8);
        if (Allied(hu, hv)) return 0x100;
        if (rd16(UnitInfo(rd8(v + 2)) + 0x3E)) return 0x100;
        if (move != 1 && move != 2) return 0x100;
    }
    if (uint32_t st = StructureAt(cell)) {
        uint16_t r = CanEnter(u, st);
        if (!r) return 0x100;
        return uint16_t(-r);
    }
    uint16_t terr = TerrainAt(cell);
    uint16_t c = rd8(0x6CCB6 + uint16_t(terr * 28 + move));
    if (rd8(u + 2) == 6 && terr == 0xB) {
        uint16_t owner = rd8(rd32(0x4AA24) + uint16_t(cell << 2) + 2) & 7;
        if (!Allied(uint16_t(int16_t(int8_t(rd8(u + 8)))), owner)) c = 0xFF;
    }
    if (!c) return 0x100;
    c ^= 0xFF;
    if (facing & 1) c = uint16_t(c - ((int16_t(c) >> 2) + (int16_t(c) >> 3)));
    return c;
}

namespace {

// 0x19ADC: the position one cell on in a direction (offsets at 0x714CE,
// 0x714DE), unchanged off the map.
uint32_t NextCellPos(uint32_t pos, uint16_t dir) {
    uint16_t d = uint16_t(((dir + 0x10) & 0xFF) >> 5);
    uint16_t x = uint16_t(uint16_t(pos) + rd16(0x714CE + d * 2u));
    uint16_t y = uint16_t(uint16_t(pos >> 16) + rd16(0x714DE + d * 2u));
    if (x > 0x4000 || y > 0x4000) return pos;
    return uint32_t(y) << 16 | x;
}

// 0x11724, 0x11762: the y and x parts of a step of a length in a direction.
int16_t StepPart(uint32_t sines, uint16_t dir, uint16_t dist, bool negate) {
    if (!dist) return 0;
    uint16_t d = dist > 0xFF ? uint16_t(dist | 0xFF) : dist;
    int32_t v = int8_t(rd8(sines + (dir & 0xFF)));
    if (negate) v = int16_t(-v);
    return int16_t((int32_t(int16_t(v)) * int16_t(d) + 0x40) >> 7);
}

} // namespace

// 0x476B6: a unit starts a step to the next cell ahead (+0x4E), facing it
// squarely, at the terrain's speed (slower when badly hurt); 0 if blocked.
// +0x78/+0x7A the step's x and y per move.
uint16_t UnitStartStep(uint32_t u) {
    if (!u) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    uint16_t dir = (rd8(u + 0x6A) + 0x10) & 0xE0;
    UnitSetFacing(u, dir, 1, 0);
    UnitSetFacing(u, dir, 0, 1);
    uint32_t to = NextCellPos(rd32(u + 0xA), dir);
    uint16_t cell = PosCell(to);
    wr16(u + 0x58, 0x7FFF);
    uint16_t c = MoveCost(u, cell, uint16_t(int16_t(dir) >> 5));
    if (int16_t(c) > 0xFF || c == 0xFFFF) return 0;
    uint16_t terr = TerrainAt(cell);
    if (terr == 0xC) terr = 0xA;
    uint16_t speed = rd8(0x6CCB6 + uint32_t(int32_t(int16_t(terr * 28))) + int32_t(int16_t(rd16(ui + 0x3E))));
    if (rd8(u + 2) == 6 && terr == 0xB) speed = 0xFF;
    U8{u + 5} &= 0x7F;
    if (rd16(0x6CCBC + uint16_t(terr * 28))) U8{u + 5} |= 0x80;
    if ((int16_t(rd16(ui + 0x10)) >> 1) > int16_t(rd16(u + 0x12)) && rd16(ui + 0x3E) != 4)
        speed = uint16_t(speed - (int16_t(speed) >> 2));
    SetUnitSpeed(u, speed);
    if (rd16(ui + 0x3E) != 5) {
        U32{0xFFC610} = to;
        CancelOrder(1, u);
        U32{0xFFC610} = 0;
    }
    wr32(u + 0x4E, to);
    Undeviate(u, 10);
    uint16_t quad = dir & 0xF0;
    uint16_t len = (quad == 0 || quad == 0x40 || quad == 0x80 || quad == 0xC0) ? 0x20 : 0x28;
    wr16(u + 0x7A, uint16_t(StepPart(0x6D0AC, rd8(u + 0x6A), len, true)));
    wr16(u + 0x78, uint16_t(StepPart(0x6CFAC, rd8(u + 0x6A), len, false)));
    return 1;
}

// 0x46084: a unit moves by a speed: aircraft along their facing, others
// by their step (+0x78/+0x7A) towards the next cell (+0x4E). Units leaving
// the map are removed (aircraft turn back); infantry are crushed under
// tracked units; projectiles explode on arrival, ground units arrive on
// their cell (entering a structure there, flattening a spice bloom).
// Returns 1 when the unit arrived or was removed.
uint16_t MoveUnit(uint32_t u, uint16_t speed) {
    if (!u || !(rd16(u + 4) & 1)) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    uint8_t type = rd8(u + 2);
    uint16_t trampled = 0;
    uint32_t np;
    if (rd16(ui + 0x3E) == 4) {
        if (type == 0) {
            uint32_t v = TargetUnit(rd16(u + 0x2A));
            if (v && (rd8(v + 0x54) == 0 || rd8(v + 0x54) == 0xB)) {
                ReleaseTarget(u);
                wr16(u + 0x2A, 0);
                return 0;
            }
        }
        np = MovePos(rd32(u + 0xA), rd8(u + 0x6A), speed);
    } else {
        uint32_t p = rd32(u + 0xA);
        np = (uint32_t(uint16_t((p >> 16) + rd16(u + 0x7A))) << 16 | uint16_t(p + rd16(u + 0x78))) & 0x3FFF3FFF;
    }
    if (np == rd32(u + 0xA)) return 0;
    if (np & 0xC000C000) {
        if (!(rd16(ui + 0x38) & 0x80)) {
            CrushUnit(u);
            return 1;
        }
        np = rd32(u + 0xA);
        UnitSetFacing(u, ((Random() & 0xF) + rd8(u + 0x6A)) & 0xFF, 0, 0);
        if ((rd16(u + 4) & 0x200) && rd8(u + 3) == 0xFF && !ObjectTarget(u)) {
            CrushUnit(u);
            return 1;
        }
    }
    wr8(u + 0x72, 0);
    if ((rd16(ui + 0x38) & 0x10) && (rd16(u + 4) & 0x80)) wr8(u + 0x72, Random() & 7);
    int16_t dist = int16_t(DistanceFine(np, rd32(u + 0x4E)));
    uint16_t cell = PosCell(np);
    if ((rd16(ui + 0x38) & 0x20) && dist < 0x30) {
        uint32_t v = UnitAt(cell);
        if (v && !rd16(UnitInfo(rd8(v + 2)) + 0x3E) && (rd16(v + 4) & 2) &&
            !Allied(uint16_t(int16_t(int8_t(rd8(u + 8)))), uint16_t(int16_t(int8_t(rd8(v + 8))))) &&
            NearPoint(int16_t(rd32(u + 0xA) >> 16), int16_t(rd32(u + 0xA)), int16_t(rd32(v + 0xA) >> 16), int16_t(rd32(v + 0xA)))) {
            if (v == U32{0xFFC25C}) {
                U32{0xFFC25C} = 0;
                DropSelection();
            }
            ForgetObject(v);
            wr16(v + 0x24, 1);
            SetUnitAction(v, 0xA);
        }
    }
    CancelOrder(0, u);
    if (rd16(ui + 0x3E) == 4) U8{u + 5} ^= 0x20;
    uint32_t dest = rd32(u + 0x4E);
    dist = int16_t(DistanceFine(np, dest));
    uint16_t arrived = 0;
    int16_t house = int8_t(rd8(u + 8));
    if (type == 0x18) {
        uint16_t dmg = uint16_t((int16_t(rd16(u + 0x12)) >> 2) + 1);
        if (uint32_t v = UnitAt(cell)) {
            if (!(rd16(UnitInfo(rd8(v + 2)) + 0x38) & 8)) DamageUnit(v, dmg, 0);
        } else if (uint32_t st = StructureAt(cell)) {
            DamageStructure(st, dmg, 0);
        }
        if ((int16_t(rd16(ui + 0x54)) >> 1) > int16_t(rd16(u + 0x12))) U8{u + 5} |= 0x40;
        U16{u + 0x12} -= 1;
        if (!rd16(u + 0x12) || !rd8(u + 0x56)) CrushUnit(u);
        goto end;
    }
    {
        uint16_t terr = TerrainAt(PosCell(np));
        if (type == 0x17) {
            if ((terr == 0xB || terr == 0xC) && (rd16(u + 0x52) >> 14) == 2 &&
                (rd8(rd32(0x4AA24) + uint16_t(PosCell(np) << 2) + 2) & 7) == rd8(u + 8))
                terr = 0;
            if (terr == 0xB || terr == 0xC || terr == 6) {
                wr32(u + 0xA, np);
                SpawnEffect(rd8(u + 3) == 9 ? 1 : 0, rd32(u + 0xA), rd16(u + 0x12), rd16(u + 0x52));
                CrushUnit(u);
                return 1;
            }
            if (terr == 0xE) {
                SpiceBloom(PosCell(np), 0xFFFF);
                return 1;
            }
        }
        arrived = (dist > int16_t(rd16(u + 0x58)) || dist <= 0xC) ? 1 : 0;
        if (!arrived) goto end;
        if (rd16(ui + 0x38) & 2) {
            if (rd8(u + 0x56) && type != 0x14) goto end;
            if (type == 0x12) {
                uint16_t x = uint16_t(np), y = uint16_t(np >> 16);
                for (int k = 0; k < 0x11; k++) {
                    uint32_t p = (uint32_t(uint16_t(y + rd16(0xFEE86 + k * 4u))) << 16) + uint32_t(int32_t(int16_t(x)) + int16_t(rd16(0xFEE84 + k * 4u)));
                    SpawnEffect(rd16(ui + 0x56), p, 0xC8, 0);
                    Scorch(p);
                }
                PlaySoundEffect(0xE);
            } else if (rd16(ui + 0x56) != 0xFFFF) {
                if (terr == 0xE) SpiceBloom(PosCell(np), 0xFFFF);
                uint16_t here = PosCell(rd32(u + 0xA));
                if ((rd16(ui + 0x38) & 0x800) && !(rd8(rd32(0x4AA24) + uint16_t(here << 2) + 2) & 0x10) && !TerrainAt(here)) {
                    SpawnEffect(8, np, rd16(u + 0x12), rd16(u + 0x52));
                    if (type != 0x17 || int8_t(rd8(u + 3)) >= 0) Scorch(np);
                } else {
                    switch (type) {
                    case 0x16:
                        SpawnEffect(2, np, rd16(u + 0x12), rd16(u + 0x52));
                        Scorch(np);
                        break;
                    case 0x13:
                        SpawnEffect(3, np, rd16(u + 0x12), rd16(u + 0x52));
                        Scorch(np);
                        break;
                    case 0x15: Explosion(7, np, uint16_t(house)); break;
                    default: {
                        uint16_t big = rd8(u + 3) == 9 ? 1 : 0;
                        uint32_t p = RandomCorner(np);
                        SpawnEffect(big, p, rd16(u + 0x12), rd16(u + 0x52));
                        if (big == 1) Scorch(p);
                        break;
                    }
                    }
                }
            }
            CrushUnit(u);
            return arrived;
        }
        if (!(rd16(ui + 0x38) & 0x40)) goto end;
        if (dest) np = dest;
        wr32(u + 0x64, rd32(u + 0x60));
        wr32(u + 0x60, rd32(u + 0xA));
        wr32(u + 0x4E, 0);
        if ((rd16(u + 4) & 0x400) && !(Random() & 3)) DamageUnit(u, 1, 0);
        if (type == 6) {
            uint32_t v = UnitAt(cell);
            if ((v && v != u) || (rd16(u + 0x5C) && int16_t(DistanceFine(rd32(u + 0xA), TargetPos(rd16(u + 0x5C)))) < 0x20)) {
                SpawnEffect(4, np, 0x1F4, 0);
                CrushUnit(u);
                return 1;
            }
        }
        SetUnitSpeed(u, 0);
        if (MakeTarget(cell, 3) == rd16(u + 0x5C)) wr16(u + 0x5C, 0);
        if (uint32_t st = StructureAt(cell)) {
            wr32(u + 0x64, 0);
            wr32(u + 0x60, 0);
            EnterStructure(u, st);
            return 1;
        }
        if (type != 0x19) {
            uint32_t m = rd32(0x4AA24) + uint32_t(int32_t(int16_t(cell)) * 4);
            if ((rd16(m) & 0x1FF) == U16{0xFFC228}) {
                wr16(m, uint16_t((rd16(m) & 0xFE00) + (rd8(0xFF6C98 + cell) & 0x1FF)));
                trampled = 1;
            }
        }
    }
end:
    wr16(u + 0x58, uint16_t(dist));
    wr32(u + 0xA, np);
    if (u == U32{0xFFC25C} && !rd8(0xFFBF7E)) wr32(0xFFBF5A, np);
    CancelOrder(1, u);
    if (trampled) SpiceBloom(cell, uint16_t(house));
    return arrived;
}

// 0x48352: a projectile unit (types 0x12-0x18) launched at a target:
// bullets and rockets from the shooter (rockets scattered by distance),
// shells (0x17, 0x18) from half a cell up and ahead; revealed to the
// player when unseen.
uint32_t FireProjectile(uint32_t pos, uint16_t type, uint16_t house, uint16_t damage, uint16_t target) {
    uint32_t ti = UnitInfo(uint8_t(type));
    uint32_t to = TargetPos(target);
    if (!TargetValid(target)) return 0;
    uint32_t p = 0;
    switch (type) {
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16: {
        uint16_t dir = Direction(pos, to);
        p = CreateUnitAt(0xFFFF, type, house, pos, dir);
        if (!p) return 0;
        PlaySoundEffect(rd16(ti + 0x5A));
        wr16(p + 0x5A, target);
        wr16(p + 0x12, damage);
        wr32(p + 0x4E, to);
        if (rd16(ti + 0x38) & 0x4000) {
            int16_t r = (Random() & 0xF) ? int16_t(int16_t(DistanceFine(pos, to)) >> 8) : int16_t(Random() & 0xFF);
            wr32(p + 0x4E, ScatterPos(to, uint16_t(r + 8), 0));
        }
        wr8(p + 0x56, uint8_t(rd16(ti + 0x52)));
        if (uint32_t v = TargetUnit(target))
            if (rd16(UnitInfo(rd8(v + 2)) + 0x3E) == 4) U8{p + 0x56} = uint8_t(rd8(p + 0x56) * 2);
        if (type == 0x12) return p;
        break;
    }
    case 0x17:
    case 0x18: {
        uint16_t dir = Direction(pos, to);
        uint32_t start = MovePos(MovePos(pos, 0, 0x20), dir, 0x80);
        p = CreateUnitAt(0xFFFF, type, house, start, dir);
        if (!p) return 0;
        if (type == 0x18) {
            wr8(p + 0x56, uint8_t(rd16(ti + 0x52)));
            UpdateUnitSprite(p);
        }
        wr32(p + 0x4E, to);
        wr16(p + 0x12, damage);
        if (int16_t(damage) > 0xF) U8{p + 5} |= 0x40;
        break;
    }
    default: return 0;
    }
    if (!((1u << player_house) & rd8(p + 9))) RevealAround(rd32(p + 0xA), 2);
    return p;
}

// 0x46F70: a unit cannot stand where it is: impassable ground, a friend
// or a non-infantry unit there (tracked units may run over enemy
// infantry), or a structure. Sandworms and aircraft are never blocked.
uint16_t UnitBlocked(uint32_t u) {
    if (!u) return 1;
    uint32_t ui = UnitInfo(rd8(u + 2));
    uint16_t cell = PosCell(rd32(u + 0xA));
    uint16_t terr = TerrainAt(cell);
    if (!rd8(0x6CCB6 + uint32_t(int32_t(int16_t(terr * 28))) + int32_t(int16_t(rd16(ui + 0x3E))))) return 1;
    if (rd8(u + 2) == 0x19 || rd16(ui + 0x3E) == 4) return 0;
    uint32_t v = UnitAt(cell);
    if (v && v != u) {
        if (Allied(uint16_t(int16_t(int8_t(rd8(v + 8)))), uint16_t(int16_t(int8_t(rd8(u + 8)))))) return 1;
        if (rd16(ui + 0x3E) != 1 || rd16(UnitInfo(rd8(v + 2)) + 0x3E)) return 1;
    }
    return StructureAt(cell) ? 1 : 0;
}

// 0x48DB8: a unit's home (+0x52): a harvester's nearest refinery (an idle
// one first), else its own cell. 1 if it had one before.
uint16_t FindReturnTarget(uint32_t u) {
    uint16_t had = rd16(u + 0x52) ? 1 : 0;
    if (rd8(u + 2) != 0x10) {
        wr16(u + 0x52, MakeTarget(PosCell(rd32(u + 0xA)), 3));
        return had;
    }
    uint32_t best = 0;
    int16_t best_d = 0;
    uint16_t house = uint16_t(int16_t(int8_t(rd8(u + 8))));
    for (uint32_t s = FirstStructure(0, house, 0xC); s; s = NextStructure(0)) {
        if (rd16(s + 0x5C) != 1) continue;
        int16_t d = int16_t(DistanceFine(rd32(u + 0xA), rd32(s + 0xA)));
        if (!best_d || d < best_d) {
            best_d = d;
            best = s;
        }
    }
    if (!best) {
        for (uint32_t s = FirstStructure(0, house, 0xC); s; s = NextStructure(0)) {
            int16_t d = int16_t(DistanceFine(rd32(u + 0xA), rd32(s + 0xA)));
            if (!best_d || d < best_d) {
                best_d = d;
                best = s;
            }
        }
    }
    if (best) wr16(u + 0x52, MakeTarget(rd16(best), 2));
    return had;
}

// 0x468BA: a unit created at a position (-1: off the map, hidden) facing
// a direction, with full health, its action and sprite; dropped again when
// it cannot stand there. Some light vehicles (0x6C760 by house) start
// rough (+4 bit 10). The harvester's spice sprite is made first.
uint32_t CreateUnitAt(uint16_t index, uint16_t type, uint16_t house, uint32_t pos, uint16_t facing) {
    uint32_t ti = UnitInfo(uint8_t(type));
    uint32_t u = CreateUnit(index, type, house);
    if (!u) return 0;
    UnitSetFacing(u, facing, 1, 0);
    UnitSetFacing(u, facing, 1, 1);
    SetUnitSpeed(u, 0);
    wr16(u + 0x12, rd16(ti + 0x10));
    wr32(u + 0xA, pos);
    wr16(u + 0x52, 0);
    wr8(u + 0x7C, 0xFF);
    wr32(u + 0x4E, 0);
    if (pos != 0xFFFFFFFF) {
        wr16(u + 0x52, FindReturnTarget(u));
        wr32(u + 0x60, pos);
        wr32(u + 0x64, pos);
    }
    wr8(u + 0x56, 0);
    wr8(u + 0x72, 0);
    wr8(u + 0x73, 0);
    wr16(u + 0x76, 0);
    wr16(u + 0x5C, 0);
    wr8(u + 0x5E, 0);
    wr8(u + 3, 0xFF);
    wr8(u + 0x55, 0xFF);
    wr8(u + 0x54, 3);
    wr16(u + 0x58, 0x7FFF);
    wr16(u + 0x14, 0);
    ScriptReset(u + 0x16, 0xFFC1B0);
    U8{u + 5} |= 2;
    if (rd16(ti + 0x3E) == 1) {
        int32_t r = Random();
        if (r < int32_t(rd16(0x6C760 + uint16_t(int16_t(house) * 30)))) U8{u + 4} |= 4;
    }
    bool ok = true;
    if (rd16(ti + 0x3E) == 4) SetUnitSpeed(u, 0xFF);
    else if (pos != 0xFFFFFFFF) ok = !UnitBlocked(u);
    uint16_t cls = rd16(UnitInfo(rd8(u + 2)) + 0x46);
    uint16_t pal = int16_t(cls) >= 0xEE ? uint16_t(int16_t(int8_t(rd8(u + 8)))) : 0;
    auto sprites = [&]() {
        if (rd8(u + 2) == 0x10) {
            CreateSprite(u + 0xA, 0xDF, 0);
            wr16(u + 0x5A, rd16(u + 0x10));
            HideSprite(rd16(u + 0x5A));
            wr16(u + 0x10, 0);
        }
        CreateSprite(u + 0xA, cls, pal);
        UpdateUnitSprite(u);
        UnitShadowFlag(u);
    };
    if (pos == 0xFFFFFFFF) {
        sprites();
        HideSprite(rd16(u + 0x10));
        U8{u + 5} |= 4;
        return u;
    }
    if (ok) {
        CancelOrder(1, u);
        SetUnitAction(u, int16_t(house == player_house ? rd16(ti + 0x28) : rd16(ti + 0x4A)));
        sprites();
    } else {
        FreeUnit(u);
        u = 0;
    }
    if (u && !(rd16(u + 4) & 4) && rd16(UnitInfo(rd8(u + 2)) + 0x3E) != 4 && !CellInMap(PosCell(rd32(u + 0xA)))) Assert(u);
    return u;
}

// 0x49140: a player's unit leaves the map: off its team, and the order
// mode ends if it was the selected unit. 1 if it was the player's.
uint16_t UnitEaten(uint32_t u) {
    if (!u || int8_t(rd8(u + 8)) != int16_t(player_house) || !(rd16(u + 4) & 2)) return 0;
    U8{u + 5} &= uint8_t(~2);
    LeaveTeamOf(u);
    if (u == U32{0xFFC25C} && U16{0xFFC264} == 1) {
        U32{0xFFC18C} = 0;
        U16{0xFFC190} = 0xFFFF;
        SetGameMode(4);
    }
    return 1;
}

// 0x46BAE: a unit takes damage (only types flagged so, and sandworms; not
// the player's while 0xFFC018 is set). Destroyed: a harvester spills its
// spice, an explosion by movement type, it dies (1). Hurt (with effect):
// a small blast. Below half: sandworms leave, light infantry (2, 3) become
// the next type up at full health and may flee; ground units start to
// smoke. Healthy sandworms without a target strike at a random sand cell.
uint16_t DamageUnit(uint32_t u, uint16_t damage, uint16_t effect) {
    if (!u) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    if (!(rd16(u + 4) & 2)) return 0;
    if (U16{0xFFC018} && int8_t(rd8(u + 8)) == int16_t(player_house)) return 0;
    if (!(rd16(ui + 0x38) & 0x8000) && rd8(u + 2) != 0x19) return 0;
    if (int16_t(damage) > int16_t(rd16(u + 0x12))) wr16(u + 0x12, 0);
    else U16{u + 0x12} -= damage;
    Undeviate(u, 0);
    uint32_t pos = rd32(u + 0xA);
    if (!rd16(u + 0x12)) {
        UnitEaten(u);
        if (rd8(u + 2) == 0x10) SpreadSpice(PosCell(pos), uint16_t(int16_t(int8_t(rd8(u + 0x5E)) >> 5)));
        switch (rd16(ui + 0x3E)) {
        case 1:
            SpawnEffect(3, pos, 0, 0);
            Scorch(uint32_t(int32_t(int16_t(PosCell(rd32(u + 0xA))))));
            break;
        case 2: SpawnEffect(0x13, pos, 0, 0); break;
        case 3: SpawnEffect(1, pos, 0, 0); break;
        case 4: SpawnEffect(3, pos, 0, 0); break;
        default: break;
        }
        SetUnitAction(u, 0xA);
        HideSprite(rd16(u + 0x10));
        return 1;
    }
    if (effect) {
        SpawnEffect(int16_t(damage) >= 0x19 ? 1 : 0, pos, 0, 0);
        if (rd8(u + 2) != 0x17 || int8_t(rd8(u + 3)) >= 0) Scorch(rd32(u + 0xA));
    }
    int16_t house = int8_t(rd8(u + 8));
    if (house != int16_t(player_house) && rd8(u + 0x54) == 8 && rd8(u + 2) != 0x10) SetUnitAction(u, 0);
    if ((int16_t(rd16(ui + 0x10)) >> 1) > int16_t(rd16(u + 0x12))) {
        if (rd8(u + 2) == 0x19) SetUnitAction(u, 0xA);
        if (rd8(u + 2) == 3 || rd8(u + 2) == 2) {
            ExplosionAt(0x15, rd32(u + 0xA), uint16_t(house));
            uint32_t counts = rd32(0x4AA0E + uint32_t(house) * 4);
            U8{counts + uint32_t(int32_t(int8_t(rd8(u + 2))))} -= 1;
            U8{u + 2} += 2;
            U8{counts + uint32_t(int32_t(int8_t(rd8(u + 2))))} += 1;
            ui = UnitInfo(rd8(u + 2));
            wr16(u + 0x12, rd16(ui + 0x10));
            CancelOrder(2, u);
            if (int32_t(Random() & 0xFF) < int32_t(rd16(0x6C75E + uint16_t(house * 30))) && rd8(u + 8) != 4)
                SetUnitAction(u, 2);
        }
        uint16_t m = rd16(ui + 0x3E);
        if (m == 1 || m == 2 || m == 3) {
            U8{u + 5} |= 8;
            wr8(u + 0x73, 0);
            wr16(u + 0x76, 0);
        }
        return 0;
    }
    if (rd8(u + 2) != 0x19 || rd16(u + 0x5A)) return 0;
    uint16_t cell = 0;
    int16_t tries = 10;
    do {
        cell = PosCell(rd32(u + 0xA));
        int16_t ry = int16_t(RandomRange(0, 0xF));
        int16_t rx = int16_t(RandomRange(0, 0xF));
        cell = uint16_t(cell + ((ry - 8) << 6) + rx - 8);
        if (!CellInMap(cell) || TerrainAt(cell)) cell = 0;
    } while (--tries && !cell);
    if (!tries) return 0;
    SetUnitAction(u, 0xB);
    UnitAttack(u, MakeTarget(cell, 3));
    return 0;
}

// 0x49018: everything targeting a unit lets go of it: units (harvesters
// keep attacking), turrets (types 0xF, 0x10), teams; it leaves its team.
void ForgetObject(uint32_t u) {
    if (!u) return;
    uint16_t t = MakeTarget(rd16(u), 1);
    ReleaseTarget(u);
    md::Frame frame(0x12);
    for (uint32_t v = FirstUnit(frame.base, 0xFFFF, 0xFFFF); v; v = NextUnit(frame.base)) {
        if (rd16(v + 0x5C) == t) wr16(v + 0x5C, 0);
        if (rd16(v + 0x5A) == t && rd8(v + 2) != 0x10) wr16(v + 0x5A, 0);
        if (ObjectTarget(v) == t) ReleaseTarget(v);
    }
    for (uint32_t s = FirstStructure(frame.base, 0xFFFF, 0xFFFF); s; s = NextStructure(frame.base))
        if ((rd8(s + 2) == 0xF || rd8(s + 2) == 0x10) && rd16(s + 0x26) == t) wr16(s + 0x26, 0);
    LeaveTeamOf(u);
    for (uint32_t m = FirstTeam(frame.base, 0xFFFF); m; m = NextTeam(frame.base))
        if (rd16(m + 0x1A) == t) wr16(m + 0x1A, 0);
}

// 0x48762: a free unit of a type (holding nothing, going nowhere) of a
// house, sent to a target. With create set and none free, a carryall
// (type 0) is made off the map for it (marked as such, +4 bit 9).
uint32_t NearestUnit(uint16_t type, uint16_t house, uint16_t target, uint16_t create) {
    uint32_t found = 0;
    for (uint32_t u = FirstUnit(0, house, type); u; u = NextUnit(0)) {
        if (rd8(u + 3) == 0xFF && !rd16(u + 0x5C)) {
            found = u;
            break;
        }
    }
    if (create && !found && !type) {
        U16{0xFFC148} += 1;
        found = CreateUnitAt(0xFFFF, type, house, 0, 0x60);
        U16{0xFFC148} -= 1;
        if (found) U8{found + 4} |= 2;
    }
    if (found) {
        wr16(found + 0x5C, target);
        SetTarget(found, target);
    }
    return found;
}

// 0x48EE4: the structure a structure holds.
uint32_t BuiltStructure(uint32_t s) {
    if (!s || rd8(s + 3) == 0xFF) return 0;
    return StructureGet(uint16_t(int16_t(int8_t(rd8(s + 3)))));
}

namespace {

// 0x47556: how much a sandworm wants a unit: by its movement (infantry
// 100, vehicles 1000, type 3 5000; four times when moving or firing) over
// the distance, doubled right next to it; 0 unseen or off sand.
uint16_t PreyScore(uint32_t u, uint32_t v) {
    if (!u || !v) return 0;
    uint16_t cell = PosCell(rd32(v + 0xA));
    if (!IsSpiceCell(cell)) return 0;
    if (!rd16(0x6CCC0 + uint16_t(TerrainAt(cell) * 28))) return 0;
    int16_t score;
    switch (rd16(UnitInfo(rd8(v + 2)) + 0x3E)) {
    case 0: score = 100; break;
    case 1:
    case 2: score = 1000; break;
    case 3: score = 5000; break;
    default: score = 0; break;
    }
    if (rd8(v + 0x70) || rd8(v + 0x56)) score = int16_t(score << 2);
    int16_t d = int16_t(Distance(rd32(u + 0xA), rd32(v + 0xA)));
    if (d && score) score = int16_t(int32_t(score) / d);
    if (d < 2) score = int16_t(score * 2);
    return uint16_t(score);
}

} // namespace

// 0x47658: a sandworm's best prey (the last of the best scores).
uint32_t NearbyUnit(uint32_t u) {
    uint32_t best = 0;
    int16_t best_score = 0;
    md::Frame frame(0x12);
    for (uint32_t v = FirstUnit(frame.base, 0xFFFF, 0xFFFF); v; v = NextUnit(frame.base)) {
        int16_t sc = int16_t(PreyScore(u, v));
        if (sc >= best_score) {
            best = v;
            best_score = sc;
        }
    }
    return best_score ? best : 0;
}

// 0x4853C: a unit put down at a cell centre (hidden, +4 bit 2, if it
// cannot stand there): orders cleared, seen if the cell is, its default
// action, shown. A carryall delivery done (+6 bit 9) is counted off.
uint16_t PlaceUnit(uint32_t u, uint32_t pos) {
    if (!u) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    U8{u + 5} &= uint8_t(~4);
    wr32(u + 0xA, (pos & 0xFF00FF00) + 0x800080);
    wr16(u + 0x78, 0);
    wr16(u + 0x7A, 0);
    if (!rd16(u + 0x52)) FindReturnTarget(u);
    wr16(u + 0x2A, 0);
    if (UnitBlocked(u)) {
        U8{u + 5} |= 4;
        return 0;
    }
    wr32(u + 0x4E, 0);
    wr16(u + 0x5C, 0);
    if (rd8(u + 2) != 0x10) wr16(u + 0x5A, 0);
    uint16_t cell = PosCell(pos);
    if (rd8(rd32(0x4AA24) + uint32_t(int32_t(int16_t(cell)) * 4) + 2) & 8) {
        int16_t house = int8_t(rd8(u + 8));
        wr8(u + 9, house == int16_t(player_house) ? 0xFF : uint8_t(1 << house));
        UnitSeenBy(u, int16_t(player_house));
    }
    bool own = int8_t(rd8(u + 8)) == int16_t(player_house) && rd8(u + 2) != 0x10 && rd8(u + 2) != 6;
    SetUnitAction(u, int16_t(rd16(ui + (own ? 0x28 : 0x4A))));
    wr8(u + 0x73, 0);
    CancelOrder(1, u);
    if (rd16(u + 6) & 0x200) {
        U8{u + 6} &= uint8_t(~2);
        if (S16{0xFFC348} - 1 < 0) U16{0xFFC348} = 0;
        else U16{0xFFC348} -= 1;
    }
    ShowSprite(rd16(u + 0x10));
    return 1;
}

// 0x1AD70: a free cell for an arrival (the house is not used): modes 0-3
// on the top, right, bottom or left map edge (limits at 0x6CE86/0x6CE88
// by map scale 0xFFC068), 4 anywhere, 5 near 0xFFC238, 6 and 7 near one of
// the player side's structures (five rounds, else 0).
uint16_t PickCell(uint16_t mode, uint16_t) {
    uint32_t lim = 0x6CE86 + uint32_t(U16{0xFFC068} << 3);
    uint8_t w = uint8_t(rd16(lim)), h = uint8_t(rd16(lim + 2));
    for (;;) {
        uint16_t c;
        switch (mode) {
        case 0: c = uint16_t(RandomRange(1, w) + 0x40); break;
        case 1: c = uint16_t((RandomRange(1, h) << 6) + rd16(lim)); break;
        case 2: c = uint16_t(RandomRange(1, w) + (rd16(lim + 2) << 6)); break;
        case 3: c = uint16_t((RandomRange(1, h) << 6) + 1); break;
        case 4:
            do {
                uint16_t x = RandomRange(1, w);
                c = uint16_t(x + (RandomRange(1, h) << 6));
            } while (!CellInMap(c));
            break;
        case 5:
            do {
                uint16_t dx = RandomRange(0, 6);
                uint16_t dy = RandomRange(0, 4);
                uint16_t at = U16{0xFFC238};
                c = uint16_t(((((at & 0xFC0) >> 6) + dy) << 6) + int8_t(at & 0x3F) + dx);
            } while (!CellInMap(c));
            break;
        case 6:
        case 7:
            for (int round = 0; round < 5; round++) {
                for (uint32_t st = FirstListedStructure(0); st; st = NextListedStructure()) {
                    uint32_t p1 = ScatterPos(rd32(st + 0xA), 0x38, 1);
                    uint32_t p2 = ScatterPos(rd32(st + 0xA), 0x38, 1);
                    uint16_t cell = uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF));
                    if (CellInMap(cell) && !UnitOnCell(cell) && int16_t(CellDistance(PosCell(rd32(st + 0xA)), cell)) <= 7)
                        return cell;
                }
            }
            return 0;
        default: return 0;
        }
        c &= 0xFFF;
        if (c && UnitOnCell(c)) c = 0;
        if (c) return c;
    }
}

// 0x4810A: a unit of a type delivered for a house from a random map edge
// (the opposite one if on screen): aircraft fly in themselves; others come
// in a free carryall off screen (or a new one), sent to the destination.
// A harvester that cannot come is owed (+2).
uint32_t DeliverUnit(uint16_t house, uint16_t type, uint16_t dest) {
    uint16_t edge = Random() & 3;
    auto edge_pos = [&](uint16_t e) {
        uint16_t c1 = PickCell(e, house);
        uint16_t c2 = PickCell(e, house);
        return ((uint32_t(int32_t(int16_t(c2)) & 0xFC0) << 10) + uint32_t(int32_t(int8_t(c1 & 0x3F)))) * 0x100 + 0x800080;
    };
    uint32_t pos = edge_pos(edge);
    if (OnScreen(pos)) pos = edge_pos(edge ^ 3);
    uint32_t h = HouseGet(house);
    uint16_t facing = Direction(pos, 0x20002000);
    if (rd16(UnitInfo(uint8_t(type)) + 0x3E) == 4) {
        U16{0xFFC148} += 1;
        uint32_t u = CreateUnitAt(0xFFFF, type, house, pos, facing);
        U16{0xFFC148} -= 1;
        if (!u) return 0;
        U8{u + 4} |= 2;
        if (dest) UnitMoveTo(u, dest);
        if (rd8(u + 2) == 0x1A) U32{0xFFC260} = u;
        return u;
    }
    uint32_t c = 0;
    for (uint32_t u = FirstUnit(0, house, 0); u; u = NextUnit(0)) {
        if (rd8(u + 3) == 0xFF && !rd16(u + 0x5C) && !OnScreen(rd32(u + 0xA))) {
            c = u;
            break;
        }
    }
    if (!c) {
        U16{0xFFC148} += 1;
        c = CreateUnitAt(0xFFFF, 0, house, pos, facing);
        U16{0xFFC148} -= 1;
        if (c && (!Allied(house, player_house) || HasUnit(house, 0))) U8{c + 4} |= 2;
    }
    if (c) {
        U16{0xFFC148} += 1;
        uint32_t v = CreateUnitAt(0xFFFF, type, house, 0xFFFFFFFF, 0);
        U16{0xFFC148} -= 1;
        if (v) {
            U8{c + 4} |= 1;
            wr8(c + 3, uint8_t(rd16(v)));
            if (type == 0x10) wr8(v + 0x5E, 1);
            if (dest) UnitMoveTo(c, dest);
            return v;
        }
        CrushUnit(c);
    }
    if (type == 0x10 && !rd16(h + 2)) U16{h + 2} += 1;
    return 0;
}

// 0x4788A: a deviator hit: a unit (flagged types, not already deviated,
// not immune, not house 3) changes to a house by its chance (0x6C75E;
// less for the computer) for 120 ticks (+0x5F), its orders dropped.
uint16_t Deviate(uint32_t u, uint16_t house) {
    if (!u) return 0;
    uint32_t ui = UnitInfo(rd8(u + 2));
    if (!(rd16(ui + 0x38) & 0x8000) || rd8(u + 0x5F) || (rd16(ui + 0x38) & 0x1000) || rd8(u + 8) == 3) return 0;
    int16_t own = int8_t(rd8(u + 8));
    uint16_t chance = rd16(0x6C75E + uint16_t(own * 30));
    if (own != int16_t(player_house)) chance = uint16_t(chance - (chance >> 3));
    if (int32_t(Random() & 0xFF) >= int32_t(chance)) return 0;
    U8{rd32(0x4AA0E + uint32_t(int32_t(own) * 4)) + int8_t(rd8(u + 2))} -= 1;
    FreeUnitBlip(rd16(u + 0x4C));
    wr8(u + 8, uint8_t(house));
    wr16(u + 0x4C, AddBlip(u));
    SetSpritePalette(rd16(u + 0x10), house);
    U8{rd32(0x4AA0E + uint32_t(house) * 4) + int8_t(rd8(u + 2))} += 1;
    wr8(u + 0x5F, 0x78);
    CancelOrder(2, u);
    int16_t now = int8_t(rd8(u + 8));
    wr8(u + 9, now == int16_t(player_house) ? 0xFF : uint8_t(1 << now));
    if (rd8(rd32(0x4AA24) + uint32_t(int32_t(int16_t(PosCell(rd32(u + 0xA))))) * 4 + 2) & 8) UnitSeenBy(u, int16_t(player_house));
    SetUnitAction(u, int16_t(rd16(ui + (player_house == 2 ? 0x28 : 0x4A))));
    ForgetObject(u);
    if (rd8(u + 2) != 0x10) wr16(u + 0x5A, 0);
    wr16(u + 0x5C, 0);
    return 1;
}


} // namespace game
