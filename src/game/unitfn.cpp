// Unit and team script functions (tables at 0xFED7C and 0xFED40). Each
// takes the script; arguments are the stack words from its top. The
// running unit is at 0xFFDEA0 (also 0xFFC208, the running object, with its
// type info at 0xFFDE9C); the running team at 0xFFDCA8.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U32 cur_unit{0xFFDEA0};
constexpr U32 cur_object{0xFFC208};
constexpr U32 cur_team{0xFFDCA8};
constexpr U32 selected_unit{0xFFC25C};

int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t UnitInfo(uint32_t u) { return rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4); }
uint16_t Arg(uint32_t s, int i) { return rd16(s + 0x16 + uint32_t((int8_t(rd8(s + 0xB)) + i) * 2)); }
// The script's object field before it (+0x14 of the object: the delay).
void SetDelay(uint32_t s, uint16_t d) { wr16(s - 2, d); }

// 0x2E40E: a unit leaves its team; the room left.
uint16_t LeaveTeam(uint32_t u) {
    if (!u || !rd8(u + 0x75)) return 0;
    uint32_t t = TeamGet(uint16_t(int8_t(rd8(u + 0x75)) - 1));
    U16{t + 4} -= 1;
    wr8(u + 0x75, 0);
    return uint16_t(rd16(t + 8) - rd16(t + 4));
}

// 0x2E3E4: a unit joins a team; the room left.
uint16_t JoinTeam(uint32_t t, uint32_t u) {
    if (!t || !u) return 0;
    wr8(u + 0x75, uint8_t(rd16(t) + 1));
    U16{t + 4} += 1;
    return uint16_t(rd16(t + 8) - rd16(t + 4));
}

// 0x2E0B0: the distance from an object to a target; to a structure, to its
// edge cell facing the object (offsets at 0x6B7BE).
uint16_t DistanceTo(uint32_t o, uint16_t t) {
    uint32_t pos;
    if (uint32_t s = TargetStructure(t)) {
        uint16_t cell = PosCell(rd32(s + 0xA));
        uint16_t oct = uint16_t(((((Direction(rd32(o + 0xA), TargetPos(t)) + 0x10) & 0xFF) >> 5) + 4) & 7);
        uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
        cell = uint16_t(cell + rd16(0x6B7BE + uint32_t(int32_t(int16_t(rd16(info + 0x3C) << 3)) + oct) * 2));
        pos = ((uint32_t(cell & 0xFC0) << 10) + (cell & 0x3F)) << 8;
        pos += 0x800080;
    } else {
        pos = TargetPos(t);
    }
    return DistanceFine(rd32(o + 0xA), pos);
}

} // namespace

// 0x486A4: a unit taken off the map (picked up): its script reset, hidden.
void LiftUnit(uint32_t u) {
    if (!u) return;
    U8{u + 5} |= 0x40;
    CancelOrder(0, u);
    U8{u + 5} &= uint8_t(~0x40);
    ScriptReset(u + 0x16, 0xFFC1B0);
    ForgetObject(u);
    U8{u + 5} |= 4;
    UnitUnseen(u, int16_t(player_house));
    HideSprite(rd16(u + 0x10));
    wr32(u + 0xA, 0xFFFFFFFF);
}

// 0x206A2, 0x206B2: the units on the allied (0) or enemy (1) radar list.
uint32_t NextListedUnit() {
    uint32_t b = U32{0xFFDBE0};
    if (!b) return 0;
    U32{0xFFDBE0} = Node(rd16(b + 4));
    return rd32(0x4A85A + uint16_t(rd16(b) << 2));
}
uint32_t FirstListedUnit(uint16_t enemy) {
    U32{0xFFDBE0} = U32{rd32(0x2069A + enemy * 4u)};
    return NextListedUnit();
}

// 0x20658, 0x20668: the structures on the allied (0) or enemy (1) list.
uint32_t NextListedStructure() {
    uint32_t b = U32{0xFFDBD4};
    if (!b) return 0;
    U32{0xFFDBD4} = Node(rd16(b + 4));
    return rd32(0x4A71E + uint16_t(rd16(b) << 2));
}
uint32_t FirstListedStructure(uint16_t enemy) {
    U32{0xFFDBD4} = U32{rd32(0x20650 + enemy * 4u)};
    return NextListedStructure();
}

// Script functions shared by units, teams and structures.

// 0x11018: a debug message: a text of the file printed with three
// arguments into 0xFFC5B0.
uint16_t ScriptPrint(uint32_t s) {
    uint32_t info = rd32(s + 4);
    uint32_t text = rd32(info);
    uint32_t fmt = text + int16_t(rd16(text + int32_t(int16_t(Arg(s, 0))) * 2));
    Sprintf(0xFFC5B0, fmt, {W(Arg(s, 1)), W(Arg(s, 2)), W(Arg(s, 3))});
    return 0;
}

// 0x1108E: delay by a number of ticks (in fifths).
uint16_t ScriptDelay(uint32_t s) {
    uint16_t d = uint16_t(int16_t(Arg(s, 0)) / 5);
    SetDelay(s, d);
    return d;
}

// 0x110AA: a random delay up to the argument.
uint16_t ScriptDelayRandom(uint32_t s) {
    uint16_t r = Random();
    uint16_t d = uint16_t(uint32_t(ScaleHealth(Arg(s, 0), r)) / 5);
    SetDelay(s, d);
    return d;
}

// 0x110DC: a random number in a range.
uint16_t ScriptRandomRange(uint32_t s) { return RandomRange(uint8_t(Arg(s, 0)), uint8_t(Arg(s, 1))); }

// 0x1110A, 0x112F2, 0x43CF4, 0x446CC, 0x2E508, 0x45F8A, 0xC652: nothing.
uint16_t ScriptNothing(uint32_t) { return 0; }

// 0x11110: the distance to a target (-1 gone).
uint16_t ScriptDistanceToTarget(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (!TargetValid(t)) return 0xFFFF;
    return DistanceFine(rd32(cur_object + 0xA), TargetPos(t));
}

// 0x11156: the distance to a target's nearest edge (-1 gone).
uint16_t ScriptDistanceToObject(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (!TargetValid(t)) return 0xFFFF;
    return DistanceTo(cur_object, t);
}

// 0x1118E: a target is not an object (as the original compares a signed
// target with an unsigned one, structures never match).
uint16_t ScriptIsNotObject(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (uint32_t st = TargetStructure(t))
        if (uint32_t(MakeTarget(rd16(st), 2)) == uint32_t(int32_t(int16_t(t)))) return 1;
    return TargetObject(t) ? 0 : 1;
}

// 0x111EA: a target unit's body facing (0x80 if not a unit).
uint16_t ScriptTargetFacing(uint32_t s) {
    uint32_t u = TargetUnit(Arg(s, 0));
    return u ? rd8(u + 0x6A) : 0x80;
}

// 0x11216: how many units of a type the object's house has.
uint16_t ScriptCountUnits(uint32_t s) {
    uint16_t n = 0;
    for (uint32_t u = FirstUnit(0, uint16_t(HouseOf(cur_object)), Arg(s, 0)); u; u = NextUnit(0)) n++;
    return n;
}

// 0x11264: a target's cell (-1 gone).
uint16_t ScriptTargetCell(uint32_t s) {
    uint16_t t = Arg(s, 0);
    return TargetValid(t) ? TargetCell(t) : 0xFFFF;
}

// 0x11296: a target's kind (-1 gone).
uint16_t ScriptTargetKind(uint32_t s) {
    uint16_t t = Arg(s, 0);
    return TargetValid(t) ? uint16_t(t >> 14) : 0xFFFF;
}

// 0x112C8: the type of the unit linked to the object (-1 none).
uint16_t ScriptLinkedType(uint32_t) {
    uint32_t o = cur_object;
    if (rd8(o + 3) == 0xFF) return 0xFFFF;
    return uint16_t(int8_t(rd8(UnitGet(uint16_t(int8_t(rd8(o + 3)))) + 2)));
}

// 0x112F6: a cell target for spice near the object (unless searching
// already failed: +6 bit 5).
uint16_t ScriptFindSpice(uint32_t) {
    uint32_t o = cur_object;
    if (rd16(o + 6) & 0x20) return 0;
    if (uint16_t c = FindSpice(PosCell(rd32(o + 0xA)), 0x20, rd16(o))) return MakeTarget(c, 3);
    U8{o + 7} |= 0x20;
    return 0;
}

// Team functions.

// 0x2E50C
uint16_t TeamNothing(uint32_t) { return 0; }
// 0x2E528, 0x2E534, 0x2E540: the team's members, minimum, and +0x1A.
uint16_t TeamMembers(uint32_t) { return rd16(cur_team + 4); }
uint16_t TeamMinimum(uint32_t) { return rd16(cur_team + 6); }
uint16_t TeamField1A(uint32_t) { return rd16(cur_team + 0x1A); }

// 0x2E54C: the team recruits the nearest free unit of its house and
// movement type (one in a smaller team if no free one).
uint16_t TeamRecruit(uint32_t) {
    uint32_t t = cur_team;
    if (int16_t(rd16(t + 4)) >= int16_t(rd16(t + 8))) return 0;
    uint16_t move = rd16(t + 0xA);
    uint32_t at = rd32(t + 0x14);
    uint32_t free_u = 0, other = 0;
    int16_t free_d = 0, other_d = 0;
    for (uint32_t u = FirstListedUnit(Allied(rd16(t + 0x10), player_house)); u; u = NextListedUnit()) {
        if (HouseOf(u) != int16_t(rd16(t + 0x10))) continue;
        if (rd16(UnitInfo(u) + 0x3E) != move) continue;
        if ((rd16(u + 4) & 0x200) || rd8(u + 2) == 6) continue;
        if (!rd8(u + 0x75)) {
            int16_t d = int16_t(DistanceFine(at, rd32(u + 0xA)));
            if (d < free_d || !free_d) {
                free_d = d;
                free_u = u;
            }
        } else if (!free_u) {
            uint32_t o = TeamGet(uint16_t(int8_t(rd8(u + 0x75)) - 1));
            if (int16_t(rd16(o + 4)) > int16_t(rd16(o + 6))) continue;
            int16_t d = int16_t(DistanceFine(at, rd32(u + 0xA)));
            if (d < other_d || !other_d) {
                other_d = d;
                other = u;
            }
        }
    }
    if (!free_u) free_u = other;
    if (!free_u) return 0;
    LeaveTeam(free_u);
    return JoinTeam(t, free_u);
}

// Unit functions.

// 0x43CF8: how much the running unit wants a target.
uint16_t UnitTargetScore(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (TargetUnit(t)) return UnitScore(cur_unit, TargetUnit(t));
    if (TargetStructure(t)) return StructureScore(cur_unit, TargetStructure(t));
    return 0;
}

// 0x43D68: a target for the running unit.
uint16_t UnitFindTarget(uint32_t s) { return ChooseTarget(cur_unit, Arg(s, 0)); }

// 0x43D88: a carryall drops the unit it carries: into a structure it
// targets (a repair facility or one that takes it), else onto its cell.
uint16_t UnitDrop(uint32_t) {
    uint32_t u = cur_unit;
    if (rd8(u + 3) == 0xFF) return 0;
    uint32_t v = UnitGet(uint16_t(int8_t(rd8(u + 3))));
    switch (rd16(u + 0x5C) >> 14) {
    case 1:
        wr16(u + 0x5C, 0);
        wr16(u + 0x5A, 0);
        return 0;
    case 2: {
        uint32_t st = TargetStructure(rd16(u + 0x5C));
        uint32_t si = rd32(0x6B954 + int16_t(int8_t(rd8(st + 2))) * 4);
        if (rd8(st + 2) == 0xB) {
            wr8(st + 3, rd8(u + 3));
            wr8(u + 3, 0xFF);
            U8{u + 4} &= uint8_t(~1);
            wr8(u + 0x5E, 0);
            CancelOrder(2, u);
            U8{u + 6} |= 2;
            UpdateUnitSprite(U32{0xFFC260});
            SetStructureState(st, 2);
            ReleaseTarget(cur_object);
            wr16(u + 0x5C, 0);
            return 1;
        }
        if ((!rd16(st + 0x5C) || ((rd16(si + 0xC) & 0x10) && rd16(st + 0x5C) == 1)) && rd8(st + 3) == 0xFF) {
            EnterStructure(v, st);
            ReleaseTarget(cur_object);
            wr16(u + 0x5C, 0);
            wr8(u + 3, 0xFF);
            U8{u + 4} &= uint8_t(~1);
            wr8(u + 0x5E, 0);
            CancelOrder(2, u);
            return 1;
        }
        ReleaseTarget(cur_object);
        wr16(u + 0x5C, 0);
        return 0;
    }
    default: {
        uint32_t p = (rd32(u + 0xA) & 0xFF00FF00) + 0x800080;
        if (!CellInMap(PosCell(p))) {
            wr16(u + 0x5C, 0);
            return 0;
        }
        if (!PlaceUnit(v, p)) return 0;
        if (HouseOf(v) == int16_t(player_house)) PlaySoundEffect(0x18);
        UnitSetFacing(v, rd8(u + 0x6A), 1, 0);
        UnitSetFacing(v, rd8(u + 0x6A), 1, 1);
        SetUnitSpeed(v, 0);
        wr8(u + 3, rd8(v + 3));
        wr8(v + 3, 0xFF);
        if (rd8(u + 3) == 0xFF) {
            U8{u + 4} &= uint8_t(~1);
            ReleaseTarget(cur_object);
            wr16(u + 0x5C, 0);
        }
        return 1;
    }
    }
}

// 0x43FE8: a carryall picks up a unit from a structure it targets, or one
// it targets (to a refinery for harvesters, else a repair facility).
uint16_t UnitPickUp(uint32_t) {
    uint32_t u = cur_unit;
    if (rd8(u + 3) != 0xFF) return 0;
    switch (rd16(u + 0x5C) >> 14) {
    case 2: {
        uint32_t st = TargetStructure(rd16(u + 0x5C));
        if (rd16(st + 0x5C) != 2) {
            ReleaseTarget(cur_object);
            wr16(u + 0x5C, 0);
            return 0;
        }
        U8{u + 4} |= 1;
        ReleaseTarget(cur_object);
        wr16(u + 0x5C, 0);
        uint32_t v = UnitGet(uint16_t(int8_t(rd8(st + 3))));
        wr8(u + 3, uint8_t(rd16(v)));
        wr8(st + 3, rd8(v + 3));
        wr8(v + 3, 0xFF);
        HideSprite(rd16(v + 0x10));
        if (rd8(st + 3) == 0xFF) SetStructureState(st, 0);
        if (rd32(v + 0x60)) {
            wr16(u + 0x5C, MakeTarget(PosCell(rd32(v + 0x60)), 3));
        } else if (rd8(v + 2) == 0x10 && HouseOf(v) != int16_t(player_house)) {
            wr16(u + 0x5C, 0);
            if (!(rd16(u + 6) & 0x20)) {
                if (uint16_t c = FindSpice(PosCell(rd32(u + 0xA)), 0x20, rd16(u))) wr16(u + 0x5C, MakeTarget(c, 3));
                else U8{u + 7} |= 0x20;
            }
        }
        CancelOrder(2, u);
        return 1;
    }
    case 1: {
        uint32_t v = TargetUnit(rd16(u + 0x5C));
        if (v == selected_unit) {
            selected_unit = 0;
            DropSelection();
            SetCursorShape(0, -1);
        }
        if (!(rd16(v + 4) & 2)) return 0;
        uint32_t best = 0;
        int16_t best_d = 0;
        uint32_t st = FirstListedStructure(Allied(uint16_t(HouseOf(u)), player_house) ? 0 : 1);
        if (rd8(v + 2) == 0x10) {
            if (rd16(v + 0x5A)) HideSprite(rd16(v + 0x5A));
            for (; st; st = NextListedStructure()) {
                if (rd8(st + 2) == 0xC && !rd16(st + 0x5C) && !rd16(st + 0x2A)) {
                    best = st;
                    break;
                }
            }
        } else {
            for (; st; st = NextListedStructure()) {
                if (rd8(st + 2) != 0xD || rd16(st + 0x5C) || rd16(st + 0x2A)) continue;
                int16_t d = int16_t(Distance(rd32(st + 0xA), rd32(u + 0xA)));
                if (!best_d || d < best_d) {
                    best_d = d;
                    best = st;
                }
            }
        }
        if (!best) return 0;
        if (v == selected_unit) {
            DropSelection();
            selected_unit = 0;
        }
        wr8(u + 3, uint8_t(rd16(v)));
        U8{u + 4} |= 1;
        CancelOrder(0, v);
        LiftUnit(v);
        LinkTargets(MakeTarget(rd16(u), 1), MakeTarget(rd16(best), 2));
        wr16(u + 0x5C, ObjectTarget(cur_object));
        CancelOrder(2, u);
        if (rd8(v + 2) == 0x10) {
            if (!FindSpice(PosCell(rd32(v + 0xA)), 4, rd16(v))) {
                wr32(v + 0x64, 0);
                wr32(v + 0x60, 0);
            }
            HideSprite(rd16(u + 0x5A));
        }
        HideSprite(rd16(v + 0x10));
        return 0;
    }
    default: return 0;
    }
}

// 0x44360: the running unit stops.
uint16_t UnitStop(uint32_t) {
    SetUnitSpeed(cur_unit, 0);
    CancelOrder(2, cur_unit);
    return 0;
}

// 0x44388: the running unit's speed (0-255, slowed on rough ground: +4
// bit 9); its whole cells per step.
uint16_t UnitSetSpeed(uint32_t s) {
    uint32_t u = cur_unit;
    int16_t v = int16_t(Arg(s, 0));
    if (v <= 0) v = 0;
    if (v >= 0xFF) v = 0xFF;
    uint16_t speed = uint16_t(v);
    if (rd16(u + 4) & 0x200) speed = ScaleHealth(speed, 0xC0);
    SetUnitSpeed(u, speed);
    return rd8(u + 0x70);
}

// 0x44400: the running unit's sprite frame.
uint16_t UnitSetFrame(uint32_t s) {
    wr8(cur_unit + 0x73, uint8_t(-Arg(s, 0)));
    CancelOrder(2, cur_unit);
    return 0;
}

// 0x44432: an aircraft flies to its target (a refinery's or repair
// facility's pad): slowing in, then set down; repeats with a delay.
uint16_t UnitFlyTo(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t t = rd16(u + 0x5C);
    uint16_t delay = 0;
    if (t) {
        uint32_t from = rd32(u + 0xA);
        uint32_t to = TargetPos(t);
        if (uint32_t st = TargetStructure(t)) {
            if (rd8(st + 2) == 0xC) to += 0x800100;
            else if (rd8(st + 2) == 0xB) to += 0xE000E0;
        }
        int16_t dist = int16_t(DistanceFine(from, to));
        if (dist < 0x80) {
            SetUnitSpeed(u, 0);
            int16_t x0 = int16_t(from), y0 = int16_t(from >> 16);
            int16_t dx = int16_t(int16_t(to) - x0), dy = int16_t(int16_t(to >> 16) - y0);
            dx = dx > 0x10 ? 0x10 : dx < -0x10 ? -0x10 : dx;
            dy = dy > 0x10 ? 0x10 : dy < -0x10 ? -0x10 : dy;
            uint32_t np = (uint32_t(uint16_t(y0 + dy)) << 16) + uint32_t(int32_t(x0) + dx);
            wr32(u + 0xA, np);
            CancelOrder(2, u);
            if (dist <= 0x40) {
                wr32(u + 0xA, to);
                CancelOrder(2, u);
                return 1;
            }
            delay = 2;
        } else {
            uint16_t dir = Direction(from, to);
            UnitSetFacing(u, dir, 0, 0);
            int16_t turn = int16_t(dir - rd8(u + 0x6A));
            if (turn < 0) turn = int16_t(-turn);
            int16_t sp = int16_t(dist >> 3);
            if (sp >= 0xFF) sp = 0xFF;
            SetUnitSpeed(u, ScaleHealth(uint16_t(sp), uint16_t(0xFF - turn)));
            int16_t d = int16_t(dist >> 10);
            delay = uint16_t(d > 1 ? d : 1);
            CancelOrder(2, u);
        }
    }
    if (delay) {
        SetDelay(s, delay);
        wr32(s, rd32(s) - 2);
    }
    return 0;
}

// 0x445F6: the running unit dies: the kill counted for the score, a
// saboteur explodes, then it is removed.
uint16_t UnitDie(uint32_t) {
    uint32_t u = cur_unit;
    HideSprite(rd16(u + 0x10));
    if (rd16(U32{0xFFDE9C} + 0x3E) != 4) {
        int16_t pts = int16_t(int16_t(rd16(UnitInfo(u) + 0x16)) / 100);
        if (pts <= 1) pts = 1;
        if (HouseOf(u) == int16_t(player_house)) {
            U16{0xFFC096} += 1;
            if (pts > S16{0xFFC05E}) pts = S16{0xFFC05E};
            U16{0xFFC05E} -= uint16_t(pts);
        } else {
            U16{0xFFC098} += 1;
            U16{0xFFC05E} += uint16_t(pts);
        }
    }
    UnitUnseen(u, int16_t(player_house));
    if (rd8(u + 2) == 6) {
        SpawnEffect(4, rd32(u + 0xA), 0x1F4, 0);
        Scorch(rd32(u + 0xA));
    }
    CrushUnit(u);
    return 0;
}

// 0x446D0: a big explosion with seven around it, the screen shaking. The
// effect delays accumulate from d6, which the unit tick leaves at 1.
uint16_t UnitExplode(uint32_t) {
    SpawnEffect(0xB, rd32(cur_unit + 0xA), RandomRange(0x19, 0x32), 0);
    uint16_t delay = 1;
    for (int i = 0; i < 7; i++) {
        delay = uint16_t(delay + RandomRange(0xA, 0x19));
        int16_t dx = int16_t(RandomRange(3, 10) << 4);
        if (Random() & 1) dx = int16_t(-dx);
        int16_t dy = int16_t(RandomRange(3, 10) << 4);
        if (!(Random() & 1)) dy = int16_t(-dy);
        uint32_t pos = rd32(cur_unit + 0xA);
        uint32_t off = (uint32_t(int32_t(dy)) << 16) + uint32_t(int32_t(dx));
        uint32_t p = (pos & 0xFFFF0000) + (off & 0xFFFF0000) + (((pos & 0xFFFF) + (off & 0xFFFF)) & 0xFFFF);
        SpawnEffect(6, p, delay, 0);
    }
    Scorch(rd32(cur_unit + 0xA));
    ShakeScreen(0x10);
    return 0;
}

// 0x44C4C: the running unit turns its body to a direction.
uint16_t UnitFace(uint32_t s) {
    UnitSetFacing(cur_unit, Arg(s, 0), 0, 0);
    return rd8(cur_unit + 0x6A);
}

// 0x44C80: the running unit turns (its turret if it has one) towards its
// target; 1 while turning or moving.
uint16_t UnitAim(uint32_t) {
    uint32_t u = cur_unit;
    uint32_t info = U32{0xFFDE9C};
    if (rd16(info + 0x3E) != 4 && rd32(u + 0x4E)) return 1;
    uint16_t turret = (rd16(info + 0xC) & 0x40) ? 1 : 0;
    uint16_t facing;
    if (turret) {
        if (rd8(u + 0x6B)) return 1;
        facing = rd8(u + 0x6D);
    } else {
        if (rd8(u + 0x68)) return 1;
        facing = rd8(u + 0x6A);
    }
    uint16_t t = rd16(u + 0x5A);
    if (!TargetValid(t)) return 0;
    uint16_t d = Direction(rd32(u + 0xA), TargetPos(t));
    if (d == facing) return 0;
    UnitSetFacing(u, d, 0, turret);
    return 1;
}

// 0x44D36: the direction to a target (else the body's facing).
uint16_t UnitDirectionTo(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (TargetValid(t)) return Direction(rd32(cur_unit + 0xA), TargetPos(t));
    return rd8(cur_unit + 0x6A);
}

// 0x44D86: the running unit heads for a target (a harvester only to a free
// structure, or a cell).
uint16_t UnitHeadFor(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t t = Arg(s, 0);
    if (!t || !TargetValid(t)) {
        wr16(u + 0x5C, 0);
        return 0;
    }
    if (rd8(u + 2) == 0x10) {
        if (uint32_t st = TargetStructure(t)) {
            if (!ObjectTarget(st)) UnitMoveTo(u, t);
        } else {
            wr16(u + 0x5C, t);
            wr8(u + 0x7C, 0xFF);
        }
    } else {
        UnitMoveTo(u, t);
    }
    return 0;
}

// 0x44E0C: the running unit's attack target, its turret (or body) turned
// to it; a sandworm keeps a cell target it has not reached.
uint16_t UnitSetTarget(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t t = Arg(s, 0);
    if (rd8(u + 2) == 0x10) return 0;
    if (rd8(u + 2) == 0x19 && (rd16(u + 0x5A) >> 14) == 3 && rd16(u + 0x5A) == rd16(u + 0x5C)) {
        if (PosCell(rd32(u + 0xA)) != TargetCellOf(rd16(u + 0x5A))) return rd16(u + 0x5A);
        if (rd32(u + 0xA) & 0x7F007F) return rd16(u + 0x5A);
    }
    if (!t || !TargetValid(t)) {
        wr16(u + 0x5A, 0);
        return 0;
    }
    uint16_t d = Direction(rd32(u + 0xA), TargetPos(t));
    if (rd16(U32{0xFFDE9C} + 0xC) & 0x40) {
        wr16(u + 0x5A, t);
    } else {
        wr16(u + 0x5C, t);
        wr16(u + 0x5A, t);
        UnitSetFacing(u, d, 0, 0);
    }
    UnitSetFacing(u, d, 0, 1);
    return rd16(u + 0x5A);
}

// 0x44F36: the running unit's action (the player's harvest order is kept
// while another is queued).
uint16_t UnitSetActionFn(uint32_t s) {
    uint16_t a = Arg(s, 0);
    uint32_t u = cur_unit;
    if (!(HouseOf(u) == int16_t(player_house) && a == 5 && rd8(u + 0x55) != 0xFF)) SetUnitAction(u, int16_t(a));
    return 0;
}

// 0x44F80: the running unit back to its type's default action.
uint16_t UnitDefaultAction(uint32_t) {
    SetUnitAction(cur_unit, int16_t(rd16(U32{0xFFDE9C} + 0x28)));
    return 0;
}

// 0x44F9A: the running unit faces a target (its course set there unless
// moving, or always for flying types).
uint16_t UnitFaceTarget(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t t = Arg(s, 0);
    if (!TargetValid(t)) return 0;
    if (!rd32(u + 0x4E) || (rd16(U32{0xFFDE9C} + 0x38) & 0x8000)) wr32(u + 0x4E, TargetPos(t));
    UnitSetFacing(u, Direction(rd32(u + 0xA), rd32(u + 0x4E)), 0, 0);
    return 0;
}

// 0x45014: a property of the running unit by number.
uint16_t UnitGetInfo(uint32_t s) {
    uint32_t u = cur_unit;
    uint32_t info = U32{0xFFDE9C};
    bool turret = rd16(info + 0xC) & 0x40;
    auto diff = [](uint8_t a, uint8_t b) {
        int16_t d = int16_t(a - b);
        return uint16_t(d < 0 ? -d : d);
    };
    switch (int16_t(Arg(s, 0))) {
    case 0: return PartOf(rd16(info + 0x10), rd16(u + 0x12));
    case 1: return TargetValid(rd16(u + 0x5C)) ? rd16(u + 0x5C) : 0;
    case 2: return uint16_t(rd16(info + 0x52) << 8);
    case 3: return rd16(u);
    case 4: return rd8(u + 0x6A);
    case 5: return rd16(u + 0x5A);
    case 6:
        if (!rd16(u + 0x52) || rd8(u + 2) == 0x10) FindReturnTarget(u);
        return rd16(u + 0x52);
    case 7: return uint16_t(int8_t(rd8(u + 2)));
    case 8: return MakeTarget(rd16(u), 1);
    case 9: return rd8(u + 0x71);
    case 10: return diff(rd8(u + 0x69), rd8(u + 0x6A));
    case 11: return rd32(u + 0x4E) ? 1 : 0;
    case 12: return rd8(u + 0x56) ? 0 : 1;
    case 13: return rd16(info + 0x38) & 4;
    case 14: return uint16_t(HouseOf(u));
    case 15: return (rd16(u + 4) & 0x200) ? 1 : 0;
    case 16: return turret ? rd8(u + 0x6D) : rd8(u + 0x6A);
    case 17: return turret ? diff(rd8(u + 0x6C), rd8(u + 0x6D)) : diff(rd8(u + 0x69), rd8(u + 0x6A));
    case 18: return turret ? 1 : 0;
    case 19: return ((1u << player_house) & rd8(u + 9)) ? 1 : 0;
    default: return 0;
    }
}

namespace {

// 0x45274: the path search's cost of a cell for the running unit (0 at
// its target, 0x100 impassable).
uint16_t PathCost(uint16_t cell, uint16_t dir) {
    if (TargetCellOf(rd16(cur_unit + 0x5C)) == cell) return 0;
    uint16_t c = MoveCost(cur_unit, cell, uint16_t(int16_t(dir) << 5));
    return c == 0xFFFF ? 0x100 : c;
}

// A unit is attacked by a deviator-free friend: the nearest of its house
// is sent against it (0x48762) and they target each other.
void CallForHelp(uint32_t u) {
    ReleaseTarget(cur_object);
    uint16_t t = MakeTarget(rd16(u), 1);
    if (uint32_t w = NearestUnit(0, uint16_t(HouseOf(u)), t, 1)) {
        LinkTargets(t, MakeTarget(rd16(w), 1));
        wr16(w + 0x5C, t);
    }
}

} // namespace

// 0x452CA: the running unit plans (0xFFDE5C, up to 16 steps at +0x7C) and
// walks a path to a target; 0 when it cannot.
uint16_t UnitFollowPath(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t t = Arg(s, 0);
    if (rd32(u + 0x4E) || !TargetValid(t)) return 1;
    uint16_t from = PosCell(rd32(u + 0xA));
    uint16_t to = TargetCellOf(t);
    if (rd8(u + 2) == 0x10) {
        if (uint32_t st = TargetStructure(t)) {
            if (rd8(st + 2) == 0xC) to = PosCell(rd32(st + 0xA) + 0x1000200);
        } else if (uint8_t n = rd8(rd32(0x4AA24) + int16_t(to) * 4 + 3)) {
            uint8_t a = rd8(rd32(0x4A856 + n * 4u) + 0x54);
            if (a == 3 || a == 5 || a == 7 || a == 8) {
                wr8(u + 0x7C, 0xFF);
                wr16(u + 0x5C, 0);
                wr16(s + 0x16 + uint32_t(int8_t(rd8(s + 0xB)) * 2), 0);
            }
        }
    }
    if (to == from || !CellInMap(to)) {
        wr8(u + 0x7C, 0xFF);
        wr16(u + 0x5C, 0);
        return 0;
    }
    if (int8_t(rd8(u + 0x7C)) == -1) {
        uint32_t path = FindPath(from, to, 0xFFDE5C, 0x28, PathCost, 0xFF);
        int16_t n = int16_t(rd16(path + 4));
        MemCpy(u + 0x7C, rd32(path + 6), n < 0x10 ? n : 0x10);
        if (rd8(u + 0x7C) == 0xFF) {
            wr16(u + 0x5C, 0);
            if (rd8(u + 2) == 0x19) SetDelay(s, 0x2D0);
            if (!CellInMap(PosCell(rd32(u + 0xA)))) {
                if (rd16(u + 0x5A)) CrushUnit(u);
            } else if (rd16(u + 0x5A) && rd8(u + 2) != 0x10) {
                uint32_t st = TargetStructure(ObjectTarget(cur_object));
                if (st && rd8(st + 2) == 0xD) {
                    uint32_t v = TargetUnit(ObjectTarget(u));
                    if (!v || rd8(v + 2)) CallForHelp(u);
                } else if (HouseOf(u) == int16_t(player_house)) {
                    wr16(u + 0x5A, 0);
                    SetUnitAction(u, 3);
                } else if (rd8(u + 0x54) == 0xB) {
                    wr16(u + 0x5A, 0);
                } else {
                    SetUnitAction(u, 4);
                }
            } else {
                uint32_t v = TargetUnit(ObjectTarget(u));
                if (!v || rd8(v + 2)) CallForHelp(u);
            }
        }
    } else if ((rd16(u + 0x5C) >> 14) == 1) {
        int16_t k = int16_t(CellDistance(to, from));
        if (k < 0x10) wr8(u + 0x7C + k, 0xFF);
    }
    int16_t d = int8_t(rd8(u + 0x7C));
    if (d == -1) return 1;
    d = int16_t(d << 5);
    if (uint16_t(d) != rd8(u + 0x6A)) {
        UnitSetFacing(u, uint16_t(d), 0, 0);
        return 1;
    }
    if (UnitStartStep(u)) {
        MemMove(u + 0x7C, u + 0x7D, 0xF);
        wr8(u + 0x8B, 0xFF);
        return 1;
    }
    wr8(u + 0x7C, 0xFF);
    return 0;
}

// 0x456C0: a free structure of a type for the running unit to go to (its
// carried unit's own first); they target each other.
uint16_t UnitFindStructure(uint32_t s) {
    uint32_t u = cur_unit;
    uint16_t type = Arg(s, 0);
    uint16_t ut = MakeTarget(rd16(u), 1);
    if (uint32_t v = LinkedUnit(u)) {
        uint32_t st = TargetStructure(rd16(v + 0x52));
        if (st && !rd16(st + 0x5C) && !ObjectTarget(st)) {
            uint16_t t = MakeTarget(rd16(st), 2);
            LinkTargets(ut, t);
            wr16(cur_unit + 0x5C, ObjectTarget(cur_object));
            return t;
        }
    }
    for (uint32_t st = FirstStructure(0, uint16_t(HouseOf(u)), type); st; st = NextStructure(0)) {
        if (rd16(st + 0x5C) || ObjectTarget(st)) continue;
        uint16_t t = MakeTarget(rd16(st), 2);
        LinkTargets(ut, t);
        wr16(cur_unit + 0x5C, t);
        return t;
    }
    return 0;
}

// 0x457CA: the spice carried (by the carried unit if any).
uint16_t UnitSpice(uint32_t) {
    uint32_t u = cur_unit;
    if (rd8(u + 3) != 0xFF) return uint16_t(int8_t(rd8(UnitGet(uint16_t(int8_t(rd8(u + 3)))) + 0x5E)));
    return uint16_t(int8_t(rd8(u + 0x5E)));
}

// 0x457F6: the running unit carries one (+4 bit 8).
uint16_t UnitIsCarrying(uint32_t) { return (rd16(cur_unit + 4) & 0x100) ? 1 : 0; }

// 0x45810: the running unit's death explosion (variable 1 picks the kind).
uint16_t UnitBlowUp(uint32_t) {
    uint32_t u = cur_unit;
    bool tracked = rd16(U32{0xFFDE9C} + 0x4C) == 3;
    if (rd16(u + 0x24) == 1) {
        ExplosionAt(tracked ? 0x16 : 0x17, rd32(u + 0xA), 0);
        PlaySoundEffect(0x23);
    } else {
        ExplosionAt(tracked ? 0x14 : 0x15, rd32(u + 0xA), uint16_t(HouseOf(u)));
        PlaySoundEffect(0x1E);
    }
    return 1;
}

// 0x45884: by chance (type +0xE) a soldier escapes the dying unit.
uint16_t UnitSpawnSoldier(uint32_t s) {
    if (Random() >= rd16(U32{0xFFDE9C} + 0xE)) return 0;
    uint16_t r = Random();
    uint32_t pos = ScatterPos(rd32(cur_unit + 0xA), 0x14, 1);
    uint32_t v = CreateUnitAt(0xFFFF, 4, uint16_t(HouseOf(cur_unit)), pos, r);
    if (!v) return 0;
    wr8(v + 0x5F, rd8(cur_unit + 0x5F));
    SetUnitAction(v, int16_t(Arg(s, 0)));
    return 1;
}

// 0x4591A: the running unit's target, else (for types that call for
// help) the nearest unit of its house sent with it.
uint16_t UnitCallHelp(uint32_t s) {
    uint32_t u = cur_unit;
    if (ObjectTarget(cur_object)) return ObjectTarget(cur_object);
    if (!(rd16(U32{0xFFDE9C} + 0xC) & 0x100)) return 0;
    if (rd8(u + 0x5F)) return 0;
    uint16_t ut = MakeTarget(rd16(u), 1);
    uint32_t w = NearestUnit(Arg(s, 0), uint16_t(HouseOf(u)), MakeTarget(rd16(u), 1), 0);
    if (!w) return 0;
    uint16_t t = MakeTarget(rd16(w), 1);
    LinkTargets(ut, t);
    wr16(w + 0x5C, ut);
    return t;
}

// 0x459E0: a type-0 unit (carryall) targeting the object lets go.
uint16_t UnitReleaseCarrier(uint32_t) {
    uint16_t t = rd16(cur_object + 0x2A);
    if (!t) return 0;
    uint32_t v = TargetUnit(t);
    if (v && !rd8(v + 2)) {
        wr16(v + 0x5C, 0);
        ReleaseTarget(cur_object);
    }
    return 0;
}

// 0x45A1E: a free structure of a type of the running unit's house.
uint16_t UnitFreeStructure(uint32_t s) {
    for (uint32_t st = FirstStructure(0, uint16_t(HouseOf(cur_unit)), Arg(s, 0)); st; st = NextStructure(0))
        if (!rd16(st + 0x5C) && rd8(st + 3) == 0xFF && !rd16(st + 0x2A)) return MakeTarget(rd16(st), 2);
    return 0;
}

// 0x45A90: returns d0 as the machine left it (the opcode's jump offset).
uint16_t UnitNoResult(uint32_t) { return 0x288; }

// 0x45A92: the running unit shows itself to the houses near it.
uint16_t UnitReveal(uint32_t) {
    RevealUnit(cur_unit);
    return 0;
}

// 0x45AA4: a harvester takes spice from its cell; 1 while it goes on.
uint16_t UnitHarvest(uint32_t) {
    uint32_t u = cur_unit;
    if (rd8(u + 2) != 0x10 || int8_t(rd8(u + 0x5E)) >= 100) return 0;
    uint16_t cell = PosCell(rd32(u + 0xA));
    uint16_t terr = TerrainAt(cell);
    if (terr != 8 && terr != 9) return 0;
    if (rd16(u + 6) & 0x400) {
        if (Random() & 3) return 1;
        U8{u + 6} &= uint8_t(~4);
        return 0;
    }
    U8{u + 0x5E} += Random() & 1;
    U8{u + 4} |= 1;
    CancelOrder(2, u);
    if (int8_t(rd8(u + 0x5E)) > 100) wr8(u + 0x5E, 100);
    if (Random() & 0x1F) return 1;
    ChangeSpice(cell, -1);
    return 0;
}

// 0x113B0: a target belongs to another house than the object.
uint16_t ScriptIsEnemy(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (!TargetValid(t)) return 0;
    switch (t >> 14) {
    case 1: return rd8(TargetUnit(t) + 8) != rd8(cur_object + 8);
    case 2: return rd8(TargetStructure(t) + 8) != rd8(cur_object + 8);
    default: return 0;
    }
}

// 0x1135C: a target is a friendly object on the map.
uint16_t ScriptIsFriend(uint32_t s) {
    uint32_t o = TargetObject(Arg(s, 0));
    if (!o || (rd16(o + 4) & 5) != 1) return 0;
    return ScriptIsEnemy(s) ? 0 : 1;
}

// 0x11442: a structure target of the object's house that is free; for a
// type, the first free one of the object's house.
uint16_t ScriptFreeStructure(uint32_t s) {
    uint16_t t = Arg(s, 0);
    if (t & 0xC000) {
        if ((t >> 14) != 2) return 0;
        uint32_t st = TargetStructure(t);
        return rd8(st + 8) == rd8(cur_object + 8) && !rd16(st + 0x5C) ? 1 : 0;
    }
    for (uint32_t st = FirstStructure(0, uint16_t(HouseOf(cur_object)), t); st; st = NextStructure(0))
        if (!rd16(st + 0x5C)) return MakeTarget(rd16(st), 2);
    return 0;
}

// A unit of the running team.
bool InTeam(uint32_t u) { return int16_t(int8_t(rd8(u + 0x75)) - 1) == int16_t(rd16(cur_team)); }

// 0x2E684: the team's centre (+0x14) from its members; their mean
// distance from it. Near its waypoint target, the route is done (+0x18 2).
uint16_t TeamCentre(uint32_t) {
    uint32_t t = cur_team;
    uint16_t n = 0, sx = 0, sy = 0, d = 0;
    for (uint32_t u = FirstUnit(0, rd16(t + 0x10), 0xFFFF); u; u = NextUnit(0)) {
        if (!InTeam(u)) continue;
        n++;
        sx = uint16_t(sx + ((rd32(u + 0xA) & 0xFFFF) >> 8));
        sy = uint16_t(sy + (rd32(u + 0xA) >> 24));
    }
    if (!n) return 0;
    sx = sx / n;
    sy = sy / n;
    wr32(t + 0x14, ((uint32_t(sy) << 16) + sx) << 8);
    for (uint32_t u = FirstUnit(0, rd16(t + 0x10), 0xFFFF); u; u = NextUnit(0))
        if (InTeam(u)) d = uint16_t(d + Distance(rd32(u + 0xA), rd32(t + 0x14)));
    d = d / n;
    if (rd16(t + 0x1A) && rd16(t + 0x18) && int16_t(CellDistance(uint16_t((sy << 6) + sx), TargetCellOf(rd16(t + 0x1A)))) <= 10)
        wr16(t + 0x18, 2);
    return d;
}

// 0x2E7C8: members far from the centre (beyond the radius, or behind it on
// the way) rally near it; the others guard. The number sent.
uint16_t TeamRally(uint32_t s) {
    uint32_t t = cur_team;
    uint16_t radius = Arg(s, 0);
    uint16_t n = 0;
    uint16_t spread = uint16_t(uint32_t(radius) << 4);
    for (uint32_t u = FirstUnit(0, rd16(t + 0x10), 0xFFFF); u; u = NextUnit(0)) {
        if (!InTeam(u)) continue;
        uint32_t goal = TargetPos(rd16(u + 0x5C));
        uint16_t d_centre = Distance(rd32(u + 0xA), rd32(t + 0x14));
        uint16_t d_goal, centre_goal;
        if (rd16(u + 0x5C)) {
            d_goal = Distance(rd32(u + 0xA), goal);
            centre_goal = Distance(rd32(t + 0x14), goal);
        } else {
            d_goal = centre_goal = 0x40;
        }
        if ((d_goal < centre_goal && int32_t(d_centre) > int32_t(radius) + 2) || (d_goal >= centre_goal && d_centre > radius)) {
            SetUnitAction(u, 1);
            uint32_t p1 = ScatterPos(rd32(t + 0x14), spread, 1);
            uint32_t p2 = ScatterPos(rd32(t + 0x14), spread, 1);
            uint16_t cell = uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF));
            UnitMoveTo(u, MakeTarget(cell, 3));
            n++;
        } else {
            SetUnitAction(u, 3);
        }
    }
    return n;
}

// 0x2E90E: the team's target from the first member that finds one (mode
// 4 for kamikaze teams, +0xC 3), with a waypoint route to it.
uint16_t TeamFindTarget(uint32_t) {
    uint32_t t = cur_team;
    md::Frame frame(6);
    for (uint32_t u = FirstUnit(frame.base, rd16(t + 0x10), 0xFFFF); u; u = NextUnit(frame.base)) {
        if (!InTeam(u)) continue;
        uint16_t target = ChooseTarget(u, rd16(t + 0xC) == 3 ? 4 : 0);
        if (!target) continue;
        if (uint32_t(target) != uint32_t(int32_t(int16_t(rd16(t + 0x1A))))) {
            wr16(t + 0x1A, target);
            wr16(t + 0x18, TeamWaypoint(PosCell(rd32(u + 0xA)), TargetCellOf(target)));
        }
        return target;
    }
    return 0;
}

// 0x2E9CC: members not yet on it move in on the team's target from around
// it (within their range) and attack.
uint16_t TeamAttack(uint32_t) {
    uint32_t t = cur_team;
    if (!rd16(t + 0x1A)) return 0;
    uint32_t at = TargetPos(rd16(t + 0x1A));
    uint16_t cell = PosCell(at);
    for (uint32_t u = FirstUnit(0, rd16(t + 0x10), 0xFFFF); u; u = NextUnit(0)) {
        if (!InTeam(u)) continue;
        if (!rd16(t + 0x1A)) {
            SetUnitAction(u, 3);
            continue;
        }
        uint16_t range = uint16_t(rd16(UnitInfo(u) + 0x52) << 8);
        if (!rd8(u + 0x54) && uint32_t(rd16(u + 0x5A)) == uint32_t(int32_t(int16_t(rd16(t + 0x1A))))) {
            if (rd16(u + 0x5C)) continue;
            if (int16_t(range) <= int16_t(DistanceFine(rd32(u + 0xA), at))) continue;
        }
        if (rd8(u + 0x54)) SetUnitAction(u, 0);
        int32_t r = int32_t(RandomRange(0, 0x7F));
        int16_t dir = int16_t(uint16_t(Direction(at, rd32(u + 0xA)) + r) - 0x40);
        if (dir < 0) dir = int16_t(dir + 0x100);
        uint32_t p1 = MovePos(at, uint16_t(dir), range);
        uint32_t p2 = MovePos(at, uint16_t(dir), range);
        uint16_t c = uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF));
        UnitMoveTo(u, MakeTarget(UnitOnCell(c) ? cell : c, 3));
        UnitAttack(u, rd16(t + 0x1A));
    }
    return 0;
}

// 0x2EB6E: the team's script mode (+0xC) changed, its script restarted.
uint16_t TeamSetMode(uint32_t s) {
    uint32_t t = cur_team;
    uint16_t m = Arg(s, 0);
    if (m == rd16(t + 0xC)) return 0;
    wr16(t + 0xC, m);
    ScriptReset(t + 0x1E, 0xFFC1E0);
    ScriptStart(t + 0x1E, int16_t(m));
    return 0;
}

// 0x2EBBA: the team back to its original mode (+0xE).
uint16_t TeamRestoreMode(uint32_t) {
    uint32_t t = cur_team;
    if (rd16(t + 0xE) == rd16(t + 0xC)) return 0;
    wr16(t + 0xC, rd16(t + 0xE));
    ScriptReset(t + 0x1E, 0xFFC1E0);
    ScriptStart(t + 0x1E, int16_t(rd16(t + 0xE)));
    return 0;
}

// 0x447EA: the running unit fires at its target when aimed, in range and
// reloaded: a sandworm eats a unit, others launch a projectile (0x48352);
// then the reload time (doubled shots for types flagged so).
uint16_t UnitFire(uint32_t s) {
    uint32_t u = cur_unit;
    uint32_t info = U32{0xFFDE9C};
    uint32_t p = 0;
    uint16_t t = rd16(u + 0x5A);
    if (!t || !TargetValid(t)) return 0;
    bool worm = rd8(u + 2) == 0x19;
    if (!worm && t == MakeTarget(PosCell(rd32(u + 0xA)), 3)) wr16(u + 0x5A, 0);
    if (t != rd16(u + 0x5A)) {
        UnitAttack(u, t);
        return 0;
    }
    uint16_t facing;
    if (worm) {
        facing = rd8(u + 0x6A);
    } else if (rd16(info + 0xC) & 0x40) {
        facing = rd8(u + 0x6D);
        if (rd8(u + 0x6C) != rd8(u + 0x6D)) return 0;
    } else {
        facing = rd8(u + 0x6A);
        if (rd16(info + 0x3E) != 4 && rd8(u + 0x69) != rd8(u + 0x6A)) return 0;
    }
    if ((t >> 14) == 3 && UnitOnCell(TargetCellOf(t))) UnitAttack(u, t);
    if (rd8(u + 0x56)) return 0;
    uint32_t at = TargetPos(t);
    int16_t dist = int16_t(DistanceTo(cur_object, t));
    if (int16_t(rd16(info + 0x52) << 8) < dist) return 0;
    int16_t diff = int16_t(facing - Direction(rd32(u + 0xA), at));
    if (diff < 0) diff = int16_t(-diff);
    if (rd16(info + 0x3E) == 4) diff = int16_t(diff >> 3);
    if (worm) diff = 0;
    if ((t >> 14) == 1 && rd16(UnitInfo(TargetUnit(t)) + 0x3E) == 4) diff = 0;
    if (diff >= 8) return 0;
    int16_t damage = int16_t(rd16(info + 0x54));
    uint16_t sound = rd16(info + 0x5A);
    uint16_t weapon = rd16(info + 0x58);
    bool twice = (rd16(info + 0x38) & 0x400) && (int16_t(rd16(info + 0x10)) >> 1) < int16_t(rd16(u + 0x12));
    if (rd8(u + 2) == 3 || rd8(u + 2) == 5) {
        if (dist > 0x200) {
            weapon = 0x16;
            sound = 0x2A;
        }
        if (rd8(u + 8) == 3) damage = int16_t(damage << 2);
    }
    switch (weapon) {
    case 0x19: {
        CancelOrder(0, u);
        if (uint32_t v = TargetUnit(t)) {
            if (v == selected_unit) {
                selected_unit = 0;
                DropSelection();
            }
            wr16(v + 0x24, 0xFFFF);
            UnitEaten(v);
            UnitUnseen(v, int16_t(player_house));
            CrushUnit(v);
        }
        SpawnEffect(rd16(info + 0x56), rd32(u + 0xA), 0, 0);
        PlaySoundEffect(0x3F);
        CancelOrder(1, u);
        U8{u + 0x5E} -= 1;
        SetDelay(s, 0xC);
        if (int8_t(rd8(u + 0x5E)) < 1) SetUnitAction(u, 0xA);
        p = u;
        break;
    }
    case 0x16:
        damage = int16_t(damage - (damage >> 2));
        [[fallthrough]];
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x17:
    case 0x18:
        p = FireProjectile(rd32(u + 0xA), weapon, uint16_t(HouseOf(u)), uint16_t(damage), t);
        if (!p) break;
        wr16(p + 0x52, MakeTarget(rd16(u), 1));
        PlaySoundEffect(sound);
        Undeviate(u, 0x14);
        if (weapon == 0x17) {
            switch (rd8(u + 2)) {
            case 9:
                SetTankFrame(0x2F97A, rd16(u + 0x10), rd8(u + 0x6A), rd8(u + 0x6D), uint16_t(HouseOf(u)));
                U8{u + 7} |= 0x40;
                wr8(p + 3, 9);
                break;
            case 0xA:
                SetTankFrame(0x2FFFA, rd16(u + 0x10), rd8(u + 0x6A), rd8(u + 0x6D), uint16_t(HouseOf(u)));
                U8{u + 7} |= 0x40;
                wr8(p + 3, 9);
                break;
            case 0xB: wr8(p + 3, 9); break;
            default: wr8(p + 3, 0xFF); break;
            }
        }
        if (rd16(u + 6) & 0x20) {
            U8{u + 7} &= uint8_t(~0x20);
            SetUnitAction(u, 3);
        }
        break;
    default: break;
    }
    if (!p) return 0;
    wr8(u + 0x56, uint8_t(rd16(info + 0x50) * 2));
    if (twice) {
        U8{u + 5} ^= 0x10;
        if (rd16(u + 4) & 0x10) wr8(u + 0x56, 5);
    } else {
        U8{u + 5} &= uint8_t(~0x10);
    }
    U8{u + 0x56} += Random() & 1;
    CancelOrder(2, u);
    return 1;
}

// 0x45B72: a carryall may drop its cargo at a target: a cell where the
// unit fits, or a structure of another house it may enter.
uint16_t UnitCanDropAt(uint32_t s) {
    uint16_t t = Arg(s, 0);
    uint16_t c = TargetCell(t);
    switch (t >> 14) {
    case 3: {
        if (!CellInMap(c)) return 1;
        uint32_t v = LinkedUnit(cur_unit);
        if (!v) return 1;
        wr32(v + 0xA, TargetPos(t));
        if (!UnitBlocked(v)) return 0;
        wr32(v + 0xA, 0xFFFFFFFF);
        return 1;
    }
    case 2: {
        if (rd8(StructureGet(c) + 8) == rd8(cur_unit + 8)) return 0;
        uint32_t v = LinkedUnit(cur_unit);
        if (!v) return 1;
        return CanEnter(v, StructureGet(c)) ? 1 : 0;
    }
    default: return 1;
    }
}

// 0x45C50: for a cell target, a random cell within five cells of the unit.
uint16_t UnitRandomCell(uint32_t s) {
    if ((Arg(s, 0) >> 14) != 3) return 0;
    uint32_t p1 = ScatterPos(rd32(cur_unit + 0xA), 0x50, 1);
    uint32_t p2 = ScatterPos(rd32(cur_unit + 0xA), 0x50, 1);
    return MakeTarget(uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF)), 3);
}

// 0x45CE0: idle fidgets: infantry change frame, now and then any ground
// unit turns somewhere.
uint16_t UnitFidget(uint32_t) {
    uint16_t r = RandomRange(0, 10);
    uint16_t m = rd16(U32{0xFFDE9C} + 0x3E);
    if (m == 0) {
        if (int16_t(r) > 8) {
            wr8(cur_unit + 0x73, Random() & 0x3F);
            CancelOrder(2, cur_unit);
        }
    } else if (m != 1 && m != 3) {
        return 0;
    }
    if (int16_t(r) <= 2) {
        uint16_t turret = (Random() & 1) ? 0 : 1;
        UnitSetFacing(cur_unit, Random(), 0, turret);
    }
    return 0;
}

// 0x45D76: the running unit heads for the nearest free structure of a type.
uint16_t UnitGoToStructure(uint32_t s) {
    uint32_t best = 0;
    int16_t best_d = 0;
    for (uint32_t st = FirstStructure(0, uint16_t(HouseOf(cur_unit)), Arg(s, 0)); st; st = NextStructure(0)) {
        if (rd16(st + 0x5C) || rd8(st + 3) != 0xFF || rd16(st + 0x2A)) continue;
        int16_t d = int16_t(Distance(rd32(st + 0xA), rd32(cur_unit + 0xA)));
        if (d < best_d || !best_d) {
            best_d = d;
            best = st;
        }
    }
    if (!best) return 0;
    SetUnitAction(cur_unit, 1);
    UnitMoveTo(cur_unit, MakeTarget(rd16(best), 2));
    return 1;
}

// 0x45E34: an MCV turns into a construction yard on its cell or one of
// three beside it (0xFEE7C).
uint16_t UnitDeploy(uint32_t) {
    uint32_t u = cur_unit;
    CancelOrder(0, u);
    uint16_t cell = PosCell(rd32(u + 0xA));
    uint32_t st = 0;
    for (int k = 0; k < 4 && !st; k++)
        st = CreateStructure(0xFFFF, 8, uint16_t(HouseOf(u)), uint16_t(cell + rd16(0xFEE7C + k * 2u)));
    if (!st) {
        CancelOrder(1, u);
        return 0;
    }
    CrushUnit(u);
    return 1;
}

// 0x45ECE: a target unit nearby (0x47658).
uint16_t UnitNearbyTarget(uint32_t) {
    uint32_t v = NearbyUnit(cur_unit);
    return v ? MakeTarget(rd16(v), 1) : 0;
}

// 0x45EFA: 1 while the object's target targets the running unit back (of
// its house); else the link is dropped.
uint16_t UnitStillLinked(uint32_t) {
    uint16_t t = ObjectTarget(cur_object);
    if (uint32_t o = TargetObject(t)) {
        if (uint32_t(MakeTarget(rd16(cur_unit), 1)) == uint32_t(int32_t(int16_t(rd16(o + 0x2A)))) && rd8(o + 8) == rd8(cur_unit + 8))
            return 1;
        if ((t >> 14) == 1) wr16(o + 0x5C, 0);
    }
    ReleaseTarget(cur_object);
    return 0;
}

// 0x5DD0: the screen shakes (unless the C button is held).
void ShakeScreen(uint16_t n) {
    if (rd8(0xFFE41B) & 0x20) return;
    wr32(0xFFBEA4, rd32(0xFFE3BE));
    wr16(0xFFBEA8, n);
}

uint16_t LeaveTeamOf(uint32_t u) { return LeaveTeam(u); }

} // namespace game
