// Structure script functions (table at 0x6B9A0). The running structure is
// at 0xFFD5AC (also 0xFFC208, the running object), its type info at
// 0xFFD5A8. Script variable 2 (+0x26) holds a turret's target.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U32 cur_struct{0xFFD5AC};
constexpr U32 cur_object{0xFFC208};

int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t UnitInfo(uint32_t u) { return rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4); }
uint16_t Arg(uint32_t s, int i) { return rd16(s + 0x16 + uint32_t((int8_t(rd8(s + 0xB)) + i) * 2)); }
uint32_t CellCentre(uint16_t cell) {
    return ((uint32_t(int32_t(int16_t(cell)) & 0xFC0) << 10) + (cell & 0x3F)) * 0x100 + 0x800080;
}
uint32_t StructInfoOf(uint32_t s) { return rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4); }
uint16_t Layout() { return rd16(rd32(0xFFD5A8) + 0x3C); }

// The base ground tile of a turret type's eight directions (0x4AA2C).
uint16_t TurretBase(uint32_t s) {
    uint32_t t = rd32(0x4AA2C);
    return rd16(t + 4 + rd16(t + (rd8(s + 2) == 0x10 ? 0x30 : 0x2E)) * 2u);
}

uint32_t MapEntry(uint16_t cell) { return rd32(0x4AA24) + uint32_t(int32_t(int16_t(cell)) * 4); }

// A position offset by a random 48-160 (in 16ths) each way, as the
// explosion scatter of 0xD1EE does it.
uint32_t Jitter(uint32_t pos) {
    int16_t dx = int16_t(RandomRange(3, 10) << 4);
    if (Random() & 1) dx = int16_t(-dx);
    int16_t dy = int16_t(RandomRange(3, 10) << 4);
    if (!(Random() & 1)) dy = int16_t(-dy);
    uint32_t off = (uint32_t(int32_t(dy)) << 16) + uint32_t(int32_t(dx));
    return (pos & 0xFFFF0000) + (off & 0xFFFF0000) + (((pos & 0xFFFF) + (off & 0xFFFF)) & 0xFFFF);
}

} // namespace

// 0x9A0C: all of a structure's marks cleared and its marker freed.
void ClearMarks(uint32_t s) {
    wr8(0xFFC868 + rd16(s), 0);
    uint16_t n = rd16(s + 0x10);
    if (!n) return;
    wr16(s + 0x10, 0);
    FreeSprite(Node(n), kSpriteList);
}

// 0xC656: the structure's state.
uint16_t StructState(uint32_t) { return rd16(cur_struct + 0x5C); }

// 0xC662: the unit the object targets still targets the structure back;
// else both let go.
uint16_t StructCheckLink(uint32_t) {
    uint16_t me = MakeTarget(rd16(cur_struct), 2);
    uint16_t t = ObjectTarget(cur_object);
    uint32_t v = TargetUnit(t);
    if (ObjectTarget(v) == me) return t;
    ReleaseTarget(v);
    ReleaseTarget(cur_object);
    return 0;
}

// 0xC6C4: a finished unit waits for a carryall (one sent for it unless it
// can drive out, a player's harvester being sent anyway).
uint16_t StructCallCarryall(uint32_t s) {
    uint32_t st = cur_struct;
    if (rd16(st + 0x5C) != 2 || rd8(cur_object + 3) == 0xFF) return 0;
    uint16_t type = Arg(s, 0);
    uint16_t cell = FreeCellBeside(st, 0);
    uint32_t v = LinkedUnit(cur_object);
    if (HouseOf(st) == int16_t(player_house) && v && rd8(v + 2) == 0x10 && !rd32(v + 0x60) && cell) return 0;
    uint32_t w = NearestUnit(type, uint16_t(HouseOf(cur_object)), MakeTarget(rd16(cur_object), 2), cell ? 0 : 1);
    if (!w) return 0;
    uint16_t t = MakeTarget(rd16(w), 1);
    SetTarget(cur_object, t);
    U16{st + 6} &= 0x1FFF;
    if (st == U32{0xFFC578}) U16{st + 6} |= 0x8000;
    return t;
}

// 0xC7D0: the structure's state (-2: by what it holds).
uint16_t StructSetState(uint32_t s) {
    uint16_t a = Arg(s, 0);
    uint32_t st = cur_struct;
    if (a == 0xFFFE) a = rd8(st + 3) == 0xFF ? 0 : rd16(st + 0x56) ? 1 : 2;
    SetStructureState(st, a);
    return 0;
}

// 0xC81C: the unit inside comes out: flying units take off, others onto
// a free cell beside it, facing away.
uint16_t StructReleaseUnit(uint32_t) {
    uint32_t st = cur_struct;
    if (rd8(st + 3) == 0xFF) return 0;
    uint32_t v = LinkedUnit(st);
    if (!v) {
        wr8(st + 3, 0xFF);
        ReleaseTarget(cur_object);
        U16{st + 6} &= 0x9FFF;
        return 0;
    }
    if (rd8(v + 2) == 0x10 && rd8(st + 2) == 0xD) wr16(v + 0x5A, rd16(st + 0x4E));
    if (rd16(UnitInfo(v) + 0x3E) == 4 && PlaceUnit(v, rd32(st + 0xA))) {
        wr8(st + 3, rd8(v + 3));
        wr8(v + 3, 0xFF);
        if (rd8(st + 3) == 0xFF) SetStructureState(st, 0);
        ReleaseTarget(cur_object);
        U16{st + 6} &= 0x9FFF;
        return 1;
    }
    uint16_t c = FreeCellBeside(st, rd8(v + 2) == 0x10 ? 1 : 0);
    if (!c) return 0;
    U8{v + 9} |= rd8(st + 9);
    if (!PlaceUnit(v, (CellCentre(c) & 0xFF00FF00) + 0x800080)) return 0;
    wr8(st + 3, rd8(v + 3));
    wr8(v + 3, 0xFF);
    UnitSetFacing(v, Direction(rd32(st + 0xA), rd32(v + 0xA)) & 0xE0, 1, 0);
    UnitSetFacing(v, rd8(v + 0x6A), 1, 1);
    if (rd8(st + 3) == 0xFF) SetStructureState(st, 0);
    ReleaseTarget(cur_object);
    U16{st + 6} &= 0x9FFF;
    if (HouseOf(st) == int16_t(player_house) && rd8(st + 2) != 0xD)
        PlayVoice(uint16_t(player_house + (rd8(v + 2) == 0x10 ? 0x44 : 0x1E)));
    ShowSprite(rd16(v + 0x10));
    return 1;
}

// 0xCA10: a turret's target: the current one while in range (three times
// for aircraft), else the nearest seen enemy within range, or the first
// unseen aircraft (type 1) within three times. +7 bit 7 shows it.
uint16_t StructFindTarget(uint32_t s) {
    uint32_t st = cur_struct;
    int32_t range = int16_t(Arg(s, 0));
    int32_t air = range * 3;
    uint32_t v = TargetUnit(rd16(st + 0x26));
    if (v && (rd16(v + 4) & 2)) {
        int32_t d = int16_t(DistanceFine(rd32(st + 0xA), rd32(v + 0xA)));
        if (rd16(UnitInfo(v) + 0x3E) == 4) {
            if (d < air) return rd16(st + 0x26);
        } else if (d < range) {
            return rd16(st + 0x26);
        }
    }
    uint32_t best = 0;
    int32_t best_d = 0x7D00;
    uint8_t seen = uint8_t(1 << HouseOf(st));
    for (v = FirstListedUnit(Allied(uint16_t(HouseOf(st)), player_house)); v; v = NextListedUnit()) {
        if (!(rd16(v + 4) & 2) || !CellInMap(PosCell(rd32(v + 0xA)))) continue;
        int32_t d;
        if (rd8(v + 9) & seen) {
            d = int16_t(DistanceFine(rd32(st + 0xA), rd32(v + 0xA)));
            if (d >= best_d || d >= range) continue;
            best = v;
            best_d = d;
        } else if (rd8(v + 2) == 1) {
            d = int16_t(DistanceFine(rd32(st + 0xA), rd32(v + 0xA)));
            if (d >= best_d || d >= air) continue;
            best = v;
            break;
        }
    }
    if (best) {
        U8{st + 7} |= 0x80;
        return MakeTarget(rd16(best), 1);
    }
    U8{st + 7} &= 0x7F;
    return 0;
}

// 0xCB96: a turret turns its ground tile one of eight steps towards a
// target; 1 while turning.
uint16_t StructTurn(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (!t) return 0;
    uint32_t st = cur_struct;
    wr8(st + 0xF, 0x1E);
    uint16_t tc = TargetCellOf(t);
    uint16_t c = PosCell(rd32(st + 0xA));
    uint16_t base = TurretBase(st);
    uint32_t m = MapEntry(c);
    int16_t cur = int16_t((rd16(m) & 0x1FF) - base);
    if (cur < 0 || cur >= 8) return 1;
    int16_t want = int16_t((((Direction(CellCentre(c), CellCentre(tc)) & 0xFF) + 0x10) & 0xFF) >> 5);
    if (want == cur) return 0;
    int16_t diff = int16_t(want - cur);
    while (diff > 4) diff = int16_t(diff - 8);
    while (diff < -4) diff = int16_t(diff + 8);
    cur = int16_t((diff < 0 ? cur - 1 : cur + 1) & 7);
    wr16(m, uint16_t((rd16(m) & 0xFE00) + ((base + cur) & 0x1FF)));
    wr16(st + 0x4E, uint16_t(cur));
    UpdateCellSprite(c, 0, 0);
    return 1;
}

// 0xCD0A: the direction (in eighths) to a target, else the turret's.
uint16_t StructTargetDirection(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (TargetValid(t))
        return uint16_t((((Direction(rd32(cur_struct + 0xA) + 0x800080, TargetPos(t)) + 0x10) & 0xFF) >> 5) << 5);
    return uint16_t(rd16(cur_struct + 0x4E) << 5);
}

// 0xCD72: a unit target lets go (and stops heading in).
uint16_t StructReleaseUnitTarget(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (TargetValid(t) && (t >> 14) == 1) {
        uint32_t v = TargetUnit(t);
        ReleaseTarget(v);
        if (v) wr16(v + 0x5C, 0);
    }
    return 0;
}

// 0xCDD4: a turret fires at its target within a range: a rocket turret
// rockets beyond 0x300, both shells nearer; the reload time.
uint16_t StructFire(uint32_t s) {
    uint32_t st = cur_struct;
    uint32_t pos = rd32(st + 0xA);
    uint16_t t = rd16(st + 0x26);
    if (!t || !CellInMap(TargetCellOf(t))) return 0;
    uint32_t v = TargetUnit(t);
    if (v && rd8(v + 2) != 1) {
        if (int16_t(DistanceFine(pos, rd32(v + 0xA))) >= int16_t(Arg(s, 0)) || !(rd16(v + 4) & 2) || (rd16(v + 4) & 4) ||
            Allied(uint16_t(HouseOf(st)), uint16_t(HouseOf(v)))) {
            wr16(st + 0x26, 0);
            U8{st + 7} &= 0x7F;
            return 0;
        }
    }
    uint16_t c = PosCell(pos);
    uint16_t weapon;
    uint32_t t4 = rd32(0x4AA2C);
    if (rd8(st + 2) == 0x10) {
        weapon = 0x14;
        if (int16_t(DistanceFine(pos, TargetPos(t))) < 0x300) {
            weapon = 0x17;
            uint16_t tile = uint16_t((rd16(MapEntry(c)) & 0x1FF) - rd16(t4 + 4 + rd16(t4 + 0x30) * 2u) + 0x23);
            DrawStructureTiles(tile, 0, c, Layout(), uint16_t(HouseOf(st)));
        }
    } else {
        weapon = 0x17;
        uint16_t tile = uint16_t((rd16(MapEntry(c)) & 0x1FF) - rd16(t4 + 4 + rd16(t4 + 0x2E) * 2u) + 0x1B);
        DrawStructureTiles(tile, 0, c, Layout(), uint16_t(HouseOf(st)));
    }
    uint16_t damage, reload;
    if (weapon == 0x14) {
        damage = 0x1E;
        reload = uint16_t(rd16(rd32(0x6C5D8) + 0x50) + damage);
    } else {
        damage = 0x14;
        reload = rd16(rd32(0x6C5E0) + 0x50);
    }
    uint32_t p = FireProjectile(pos + 0x800080, weapon, uint16_t(HouseOf(st)), damage, t);
    if (!p) return 0;
    wr16(p + 0x52, MakeTarget(rd16(st), 2));
    wr8(p + 3, 9);
    return reload;
}

// 0xCFE6: a sound for the player's structures.
uint16_t StructSound(uint32_t s) {
    if (HouseOf(cur_struct) == int16_t(player_house)) PlaySoundEffect(Arg(s, 0));
    return 0;
}

// 0xD016: the player's structure uncovers its sight radius.
uint16_t StructReveal(uint32_t) {
    uint32_t st = cur_struct;
    if (HouseOf(st) == int16_t(player_house))
        RevealAround(rd32(st + 0xA) + rd32(0x6B938 + rd16(StructInfoOf(st) + 0x3C) * 4u), rd16(rd32(0xFFD5A8) + 0x12));
    return 0;
}

// 0xD06C: a refinery takes spice from the harvester inside (3 by health,
// 7 credits each, the computer's 6-9), counted for the score; 1 while it
// goes on.
uint16_t StructRefine(uint32_t) {
    uint32_t st = cur_struct;
    if (rd8(st + 3) == 0xFF) {
        SetStructureState(st, 0);
        return 0;
    }
    uint32_t v = UnitGet(uint16_t(int8_t(rd8(st + 3))));
    uint16_t n = ScaleHealth(3, PartOf(rd16(rd32(0xFFD5A8) + 0x10), rd16(st + 0x12)));
    int16_t spice = int8_t(rd8(v + 0x5E));
    if (int32_t(n) >= spice) n = uint16_t(spice);
    if (rd8(v + 0x5E) && n <= 1) n = 1;
    if (!n) {
        SetStructureState(st, 2);
        return 0;
    }
    uint16_t each = 7;
    if (HouseOf(v) != int16_t(player_house)) each = uint16_t(each + (Random() & 3) - 1);
    uint16_t credits = uint16_t(each * n);
    U16 score{Allied(uint16_t(HouseOf(st)), player_house) ? 0xFFC09Eu : 0xFFC0A0u};
    uint16_t sc = uint16_t(score + credits);
    score = sc >= 0xFDE8 ? 0xFDE8 : sc;
    uint32_t h = HouseGet(uint16_t(HouseOf(st)));
    uint32_t cap = rd32(h + 0x16) > U32{0xFFC054} ? rd32(h + 0x16) : U32{0xFFC054};
    U32{h + 0x12} += credits;
    if (cap < rd32(h + 0x12)) wr32(h + 0x12, cap);
    U8{v + 0x5E} -= uint8_t(n);
    if (!rd8(v + 0x5E)) U8{v + 4} &= uint8_t(~1);
    wr16(st + 0x14, 6);
    return 1;
}

// 0xD1EE: a structure blows up: explosions over its cells (and on past
// its layout, as the original reads on), a shake; a starport's cargo and
// the frigate go with it.
uint16_t StructExplode(uint32_t) {
    uint32_t st = cur_struct;
    uint16_t delay = 0;
    uint16_t cell = PosCell(rd32(st + 0xA));
    uint32_t offs = 0x6B740 + Layout() * 18u;
    ClearMarks(st);
    auto burst = [&]() {
        for (int16_t k = 0; k < int16_t(rd16(0x6B82E + Layout() * 2u)); k++) {
            uint16_t c = uint16_t(cell + rd16(offs));
            offs += 2;
            SpawnEffect(6, Jitter(CellCentre(c)), delay, 0);
            delay = uint16_t(delay + RandomRange(10, 0x19));
        }
    };
    burst();
    ShakeScreen(0x3C);
    st = cur_struct;
    if (rd8(st + 2) == 0xB) {
        if (rd8(st + 3) != 0xFF) {
            int16_t k = int8_t(rd8(st + 3));
            wr8(st + 3, 0xFF);
            while (k != -1) {
                uint32_t v = UnitGet(uint16_t(k));
                if (!v) break;
                k = int8_t(rd8(v + 3));
                wr8(v + 3, 0xFF);
                CrushUnit(v);
            }
            U16{0xFFC348} = 0;
        }
        if (uint32_t f = U32{0xFFC260}; f && (rd16(f + 6) & 0x200)) CrushUnit(f);
    }
    burst();
    PlaySoundEffect(0x33);
    return 0;
}

// 0xD4CC: survivors: soldiers by chance (type +0xE) on the structure's
// cells, the player's scattering, the computer's standing.
uint16_t StructSurvivors(uint32_t) {
    uint32_t st = cur_struct;
    uint16_t cell = PosCell(rd32(st + 0xA));
    StructureLost(st);
    uint32_t offs = 0x6B740 + Layout() * 18u;
    for (int16_t k = 0; k < int16_t(rd16(0x6B82E + Layout() * 2u)); k++) {
        uint16_t c = uint16_t(cell + rd16(offs));
        offs += 2;
        if (int32_t(Random()) >= int32_t(rd16(rd32(0xFFD5A8) + 0xE))) continue;
        uint16_t r = Random();
        uint32_t v = CreateUnitAt(0xFFFF, 4, uint16_t(HouseOf(st)), CellCentre(c), r);
        if (!v) continue;
        wr16(v + 0x12, ScaleHealth(rd16(rd32(0x6C5CC) + 0x10), uint16_t(Random() | 3)));
        if (HouseOf(st) != int16_t(player_house)) {
            SetUnitAction(v, 0);
            continue;
        }
        SetUnitAction(v, 1);
        uint32_t p1 = ScatterPos(rd32(v + 0xA), 0x20, 1);
        uint32_t p2 = ScatterPos(rd32(v + 0xA), 0x20, 1);
        wr16(v + 0x5C, MakeTarget(uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF)), 3));
    }
    return 0;
}

} // namespace game
