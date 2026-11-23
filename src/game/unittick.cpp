// The unit tick (0x4378C): timers for turning, moving, scripts and
// animation, then every unit on the map stepped. Unit fields used here:
// +0x14 script delay, +0x16 script, +0x4E move destination, +0x56 turn
// timer, +0x5A target, +0x5F deviated time, +0x6E-0x70 speed, +0x73
// animation frame, +0x76 animation delay.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr uint32_t kMap = 0xFF7D9C;

int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t UnitInfo(uint32_t u) { return rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4); }

// 0x16EE0: a script with a file and a position.
bool ScriptValid(uint32_t s) { return s && rd32(s) && rd32(s + 4); }


// 0x47E9C: the body (0) or turret (1) turns one step; crossing a facing
// boundary redraws the unit.
void TurnStep(uint32_t u, uint16_t turret) {
    uint32_t f = u + (turret ? 0x6B : 0x68);
    int16_t step = int8_t(rd8(f));
    if (!step) return;
    uint16_t target = rd8(f + 1), cur = rd8(f + 2);
    int16_t d = int16_t(target - cur);
    if (d > 0x80) d -= 0x100;
    if (d < -0x80) d += 0x100;
    if (d < 0) d = int16_t(-d);
    uint16_t next = uint16_t(cur + step);
    if (d <= (step < 0 ? -step : step)) {
        wr8(f, 0);
        next = target;
    }
    wr8(f + 2, uint8_t(next));
    if (((next + 8) & 0xFF) >> 4 == ((cur + 8) & 0xFF) >> 4 && ((next + 0x10) & 0xFF) >> 5 == ((cur + 0x10) & 0xFF) >> 5) return;
    CancelOrder(2, u);
}

// 0x47FC2: a unit turns towards its course, else moves by its speed.
void MoveTick(uint32_t u) {
    uint32_t info = UnitInfo(u);
    if ((rd16(info + 0x3E) == 1 || !rd16(info + 0x3E)) && rd8(u + 0x69) != rd8(u + 0x6A)) {
        TurnStep(u, 0);
        UpdateUnitSprite(u);
        return;
    }
    uint16_t speed = rd8(u + 0x70);
    if (!speed) return;
    uint16_t acc = uint16_t(rd8(u + 0x6F) + rd8(u + 0x6E));
    if (acc & 0xFF00) {
        acc &= 0xFF;
        speed = uint16_t(speed << 4);
        uint16_t d = uint16_t(DistanceFine(rd32(u + 0xA), rd32(u + 0x4E)) + 0x10);
        if (d < speed) speed = d;
        MoveUnit(u, speed);
    }
    wr8(u + 0x6F, uint8_t(acc));
}


// 0x45F8E: the current unit (an MCV) becomes a construction yard.
uint16_t DeployMcv() {
    uint32_t u = U32{0xFFDEA0};
    uint16_t cell = PosCell(rd32(u + 0xA));
    uint32_t c = kMap + int16_t(cell) * 4;
    wr8(c + 3, 0);
    if (CanPlace(cell, 8)) {
        if (uint32_t s = CreateStructure(0xFFFF, 8, uint16_t(HouseOf(u)), cell)) {
            if (rd8(s + 2) == 8 && S16{0xFFC050} >= 6) wr8(s + 0x54, 1);
            U8{c + 2} &= uint8_t(~0x10);
            PlaySoundEffect(0x14);
            U8{s + 4} &= uint8_t(~4);
            wr16(s + 0x5C, 0);
            wr16(s + 0x12, rd16(rd32(0x6B974) + 0x10));
            wr8(s + 9, 0xFF);
            CrushUnit(u);
            return 1;
        }
    }
    wr8(c + 3, uint8_t(rd16(u) + 1));
    PlaySoundEffect(0x2F);
    return 0;
}

} // namespace

// 0x5DEA: a position within the visible map (plus a cell around).
bool OnScreen(uint32_t pos) {
    int16_t y = int16_t(pos >> 24), x = int16_t((pos >> 8) & 0xFF);
    int16_t sy = int16_t(U16{0xFFE3C0} >> 5), sx = int16_t(U16{0xFFE3BE} >> 5);
    return y > sy - 1 && y < sy + 8 && x > sx - 1 && x < sx + 11;
}

// 0x47A62: a deviated unit's time runs out by n (0: the house's rate);
// then it returns to its own house.
uint16_t Undeviate(uint32_t u, uint16_t n) {
    if (!u || !rd8(u + 0x5F)) return 0;
    if (!n) n = rd16(0x6C75E + HouseOf(u) * 30);
    uint32_t info = UnitInfo(u);
    if (!(rd16(info + 0x38) & 0x8000)) return 0;
    if (int8_t(rd8(u + 0x5F)) > int16_t(n)) {
        U8{u + 0x5F} -= uint8_t(n);
        return 0;
    }
    wr8(u + 0x5F, 1);
    int16_t type = int8_t(rd8(u + 2));
    U8{rd32(0x4AA0E + HouseOf(u) * 4) + type} -= 1;
    FreeUnitBlip(rd16(u + 0x4C));
    wr8(u + 8, rd8(u + 0x74));
    wr16(u + 0x4C, AddBlip(u));
    SetSpritePalette(rd16(u + 0x10), uint16_t(HouseOf(u)));
    U8{rd32(0x4AA0E + HouseOf(u) * 4) + type} += 1;
    wr8(u + 0x5F, 0);
    U8{u + 5} |= 0x40;
    CancelOrder(2, u);
    U8{u + 5} &= uint8_t(~0x40);
    if (HouseOf(u) == int16_t(player_house)) {
        wr8(u + 9, 0xFF);
        SetUnitAction(u, int16_t(rd16(info + 0x28)));
    } else {
        wr8(u + 9, uint8_t(1 << HouseOf(u)));
        SetUnitAction(u, int16_t(rd16(info + 0x4A)));
    }
    ForgetObject(u);
    if (rd8(u + 2) != 0x10) wr16(u + 0x5A, 0);
    wr16(u + 0x5C, 0);
    return 1;
}

// 0x2E25E: the position a target word points at (structures at their
// centre by size, offsets at 0x2E2D6).
uint32_t TargetPos(uint16_t t) {
    uint16_t i = t & 0x3FFF;
    switch (t >> 14) {
    case 1: return rd32(UnitGet(i) + 0xA);
    case 2: {
        uint32_t s = StructureGet(i);
        uint32_t info = rd32(0x6B954 + rd8(s + 2) * 4u);
        return rd32(s + 0xA) + rd32(0x2E2D6 + (rd16(info + 0x3C) & 7) * 4u);
    }
    case 3: return uint32_t(i & 0x3F80) << 16 | uint16_t((i & 0x7F) << 7);
    default: return 0;
    }
}

// 0x115F8, 0x19944: the direction (0-255, 0 north) from one position to
// another, from the slope against the table at 0x198BC.
uint16_t Direction(uint32_t from, uint32_t to) {
    int16_t dx = int16_t(int16_t(to) - int16_t(from));
    int16_t dy = int16_t(int16_t(to >> 16) - int16_t(from >> 16));
    int q = 0;
    if (dy <= 0) {
        q += 2;
        dy = int16_t(-dy);
    }
    if (dx < 0) {
        q += 1;
        dx = int16_t(-dx);
    }
    uint16_t base = rd16(0x1993C + q * 2);
    bool steep = dx < dy;
    uint32_t r = 0x7FFF;
    if (!steep && dy) r = (uint32_t(uint16_t(dx)) << 8) / uint16_t(dy);
    if (steep && dx) r = (uint32_t(uint16_t(dy)) << 8) / uint16_t(dx);
    uint16_t a = 32;
    for (uint16_t i = 0; i < 32; i++) {
        if (r >= rd32(0x198BC + i * 4u)) {
            a = i;
            break;
        }
    }
    if (!steep) a = uint16_t(0x40 - a);
    uint16_t d = rd8(0x199D8 + q) ? uint16_t(base + a) : uint16_t(base - a + 0x40);
    return d & 0xFF;
}

namespace {

// 0x2068C: the next blip after a blip word.
uint16_t BlipNext(uint16_t b) { return b ? rd16(Node(b) + 4) : 0; }
// 0x2070C, 0x2071A: the units on the blip lists (0xFFDBE8 allied, 0xFFDBE4
// others) from a list index or a blip word.
uint32_t BlipUnit(uint32_t b) { return b ? rd32(0x4A85A + uint16_t(rd16(b) << 2)) : 0; }
uint32_t FirstListUnit(uint16_t k) { return BlipUnit(rd32(rd32(0x20704 + k * 4u))); }
// 0x206DE, 0x206EC: the same for structures (0xFFDBDC, 0xFFDBD8).
uint32_t BlipStructure(uint32_t b) { return b ? rd32(0x4A71E + uint16_t(rd16(b) << 2)) : 0; }
uint32_t FirstListStructure(uint16_t k) { return BlipStructure(rd32(rd32(0x206D6 + k * 4u))); }

uint32_t StructInfoOf(uint32_t s) { return rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4); }

} // namespace

// 0x1068C: how much a unit wants an enemy structure it has seen: its value
// (+0x2E + +0x30) over the distance, at most 0x7D00.
uint16_t StructureScore(uint32_t u, uint32_t s) {
    if (!u || !s) return 0;
    if (Allied(uint16_t(HouseOf(u)), uint16_t(HouseOf(s)))) return 0;
    if (!((1u << HouseOf(u)) & rd8(s + 9))) return 0;
    uint32_t info = StructInfoOf(s);
    uint16_t score = uint16_t(rd16(info + 0x2E) + rd16(info + 0x30));
    if (uint16_t d = Distance(rd32(u + 0xA), rd32(s + 0xA))) score = score / d;
    return score > 0x7D00 ? 0x7D00 : score;
}

// 0x4716E: the same for an enemy unit on the map (aircraft only for
// anti-air units, the player's only over spice); 1 more than the ratio.
uint16_t UnitScore(uint32_t u, uint32_t v) {
    if (!u || !v) return 0;
    if (!(rd16(v + 4) & 2)) return 0;
    if (!((1u << HouseOf(u)) & rd8(v + 9))) return 0;
    if (u == v) return 0;
    if (Allied(uint16_t(HouseOf(v)), uint16_t(HouseOf(u)))) return 0;
    uint32_t vi = UnitInfo(v), ui = UnitInfo(u);
    if (!(rd16(vi + 0xC) & 0x2000)) return 0;
    if (rd16(vi + 0x3E) == 4) {
        if (!(rd16(ui + 0xC) & 0x1000)) return 0;
        if (!IsSpiceCell(PosCell(rd32(v + 0xA))) && HouseOf(v) == int16_t(player_house)) return 0;
    }
    if (!CellInMap(PosCell(rd32(v + 0xA)))) return 0;
    uint16_t d = Distance(rd32(u + 0xA), rd32(v + 0xA));
    if (!CellInMap(PosCell(rd32(u + 0xA)))) return 0;
    uint16_t score = uint16_t(rd16(vi + 0x30) + rd16(vi + 0x2E));
    if (d) score = uint16_t(score / d + 1);
    return score > 0x7D00 ? 0x7D00 : score;
}

// 0x47314: the best enemy unit for a unit: mode 1 within its range, 2
// within twice its range of its guard point (+0x52, set here if none), 0
// and 4 anywhere. (The original's check of the unit's range against the
// target type's passes 0 or 1 to the map test as a cell.)
uint32_t FindUnitTarget(uint32_t u, uint16_t mode) {
    if (!u) return 0;
    uint32_t best = 0;
    int16_t best_score = 0;
    uint32_t guard;
    if (!rd16(u + 0x52)) {
        guard = rd32(u + 0xA);
        wr16(u + 0x52, MakeTarget(PosCell(guard), 3));
    } else {
        guard = TargetPos(rd16(u + 0x52));
    }
    uint32_t ui = UnitInfo(u);
    uint16_t range = uint16_t(rd16(ui + 0x52) << 8);
    bool aa = rd16(ui + 0xC) & 0x1000;
    if (mode == 2) range = uint16_t(range * 2);
    uint8_t seen = uint8_t(1 << HouseOf(u));
    for (uint32_t v = FirstListUnit(Allied(uint16_t(HouseOf(u)), player_house)); v;) {
        uint16_t next = BlipNext(rd16(v + 0x4C));
        uint32_t vi = UnitInfo(v);
        bool ok = (rd8(v + 9) & seen) && (rd16(v + 4) & 2) && CellInMap(PosCell(rd32(v + 0xA)));
        if (ok && rd16(vi + 0x3E) == 4 && !aa) ok = false;
        if (ok && mode != 0 && mode != 4) {
            if (mode == 1) ok = int16_t(DistanceFine(rd32(u + 0xA), rd32(v + 0xA))) <= int16_t(range);
            else if (mode == 2) ok = int16_t(DistanceFine(guard, rd32(v + 0xA))) <= int16_t(range);
            else ok = false;
        }
        uint16_t d = ok ? Distance(rd32(u + 0xA), rd32(v + 0xA)) : 0;
        if (d) {
            uint16_t near = (PosCell(rd32(u + 0xA)) && int16_t(d) <= int16_t(rd16(vi + 0x52))) ? 1 : 0;
            int16_t score;
            if (!CellInMap(near)) {
                score = 1;
            } else {
                score = int16_t(int16_t(rd16(vi + 0x30) + rd16(vi + 0x2E)) / int32_t(d) + 1);
                if (score > 0x7D00) score = 0x7D00;
            }
            if (score > best_score) {
                best = v;
                best_score = score;
            }
        }
        v = BlipUnit(Node(next));
    }
    return best_score ? best : 0;
}

// 0x10730: the best enemy structure for a unit, by the same modes (from
// the structure's centre, 0x6B938).
uint32_t FindStructureTarget(uint32_t u, uint16_t mode) {
    if (!u) return 0;
    uint32_t best = 0;
    uint16_t best_score = 0;
    uint32_t guard = TargetPos(rd16(u + 0x52));
    int16_t range = int16_t(rd16(UnitInfo(u) + 0x52) << 8);
    for (uint32_t s = FirstListStructure(Allied(uint16_t(HouseOf(u)), player_house)); s;) {
        uint16_t next = BlipNext(rd16(s + 0x60));
        uint32_t centre = rd32(s + 0xA) + rd32(0x6B938 + rd16(StructInfoOf(s) + 0x3C) * 4u);
        bool ok;
        switch (mode) {
        case 0:
        case 4: ok = true; break;
        case 1: ok = int16_t(DistanceFine(rd32(u + 0xA), centre)) <= range; break;
        case 2: ok = int32_t(int16_t(DistanceFine(guard, centre))) <= int32_t(range) * 2; break;
        default: ok = false; break;
        }
        if (ok) {
            uint16_t score = StructureScore(u, s);
            if (int16_t(score) >= int16_t(best_score)) {
                best = s;
                best_score = score;
            }
        }
        s = BlipStructure(Node(next));
    }
    return best_score ? best : 0;
}

// 0x48F0A: a target for a unit: a structure or unit nearby, whichever
// scores higher (mode 4: structures first).
uint16_t ChooseTarget(uint32_t u, uint16_t mode) {
    if (!u) return 0;
    if (mode == 4) {
        if (uint32_t s = FindStructureTarget(u, mode)) return MakeTarget(rd16(s), 2);
        if (uint32_t v = FindUnitTarget(u, mode)) return MakeTarget(rd16(v), 1);
        return 0;
    }
    uint32_t v = FindUnitTarget(u, mode), s = 0;
    if (rd8(u + 2) != 8) s = FindStructureTarget(u, mode);
    if (v && s) {
        uint16_t ps = StructureScore(u, s);
        if (UnitScore(u, v) > ps) return MakeTarget(rd16(v), 1);
        return MakeTarget(rd16(s), 2);
    }
    if (v) return MakeTarget(rd16(v), 1);
    if (s) return MakeTarget(rd16(s), 2);
    return 0;
}

// 0x4378C
void GameTick() {
    uint32_t now = U32{0xFFC598};
    bool turn = false, move = false, aim = false, script = false, anim = false, slow = false;
    if (now >= U32{0xFFDE98}) {
        U32{0xFFDE98} = now + 3;
        turn = true;
    }
    if (now >= U32{0xFFDE94}) {
        U32{0xFFDE94} = now + 2;
        move = true;
    }
    if (now >= U32{0xFFDE90}) {
        aim = true;
        U32{0xFFDE90} = now + 0x14;
    }
    if (int32_t(now) >= S32{0xFFDE8C}) {
        script = true;
        U32{0xFFDE8C} = now + 5;
    }
    if (int32_t(now) >= S32{0xFFDE88}) {
        anim = true;
        U32{0xFFDE88} = now + 5;
    }
    if (int32_t(now) >= S32{0xFFDE84}) {
        slow = true;
        U32{0xFFDE84} = now + 0x3C;
    }
    md::Frame frame(0x30);
    uint32_t it = frame.base;
    for (uint32_t u = FirstUnit(it, 0xFFFF, 0xFFFF); u;) {
        U32{0xFFDEA0} = u;
        if (rd8(u + 2) == 0x1A && (rd16(u + 6) & 0x200)) {
            if (U16{0xFFC348}) {
                u = NextUnit(it);
                if (!u) return;
                U32{0xFFDEA0} = u;
            } else {
                U8{u + 6} &= uint8_t(~2);
            }
        }
        if (rd8(u + 8) == 3) {
            int32_t hp = int16_t(rd16(u + 0x12));
            bool low = (rd8(u + 2) == 3 && hp < 2 * int16_t(rd16(rd32(0x6C5C8) + 0x10))) ||
                       (rd8(u + 2) == 5 && hp < 2 * int16_t(rd16(rd32(0x6C5D0) + 0x10)));
            if (low && rd8(u + 0x54) != 0xB) {
                SetUnitAction(u, 0xB);
                UnitAttack(u, ChooseTarget(u, 4));
            }
        }
        if ((rd16(u + 6) & 0x800) && !TickBlink(u + 0xA)) U8{u + 6} &= uint8_t(~8);
        if (!(rd16(u + 4) & 4)) {
            U32{0xFFC208} = u;
            uint32_t info = UnitInfo(u);
            U32{0xFFDE9C} = info;
            U32{0xFFC204} = HouseGet(uint16_t(HouseOf(u)));
            if (aim && rd16(u + 0x5A) && (rd16(info + 0xC) & 0x40)) {
                UnitSetFacing(u, Direction(rd32(u + 0xA), TargetPos(rd16(u + 0x5A))), 0, 1);
                UpdateUnitSprite(u);
                U8{u + 7} &= uint8_t(~0x40);
            }
            if (turn) {
                MoveTick(u);
                if (rd8(u + 0x56)) {
                    if (rd16(info + 0x3E) == 4 && !(rd16(info + 0x38) & 0x8000)) {
                        uint16_t t = rd16(u + 0x5A);
                        uint32_t to;
                        if ((t >> 14) == 1 && rd16(UnitInfo(TargetUnit(t)) + 0x3E) == 4) to = TargetPos(t);
                        else to = rd32(u + 0x4E);
                        UnitSetFacing(u, Direction(rd32(u + 0xA), to), 0, 0);
                    }
                    U8{u + 0x56} -= 1;
                }
            }
            if (move) {
                TurnStep(u, 0);
                if (rd16(info + 0xC) & 0x40) TurnStep(u, 1);
                UpdateUnitSprite(u);
                U8{u + 7} &= uint8_t(~0x40);
            }
            if (slow) Undeviate(u, 1);
            if (rd16(info + 0x3E) != 4 && !UnitOnCell(PosCell(rd32(u + 0xA)))) CancelOrder(1, u);
            if (anim) {
                if (rd16(u + 0x76)) {
                    U16{u + 0x76} -= 1;
                } else {
                    if (!rd16(info + 0x3E) && rd8(u + 0x70) && int8_t(rd8(u + 0x73)) >= 0) {
                        U8{u + 0x73} &= 0x3F;
                        U8{u + 0x73} += 1;
                        wr16(u + 0x76, uint16_t(int16_t(rd16(info + 0x40)) / 5));
                    }
                    if (rd8(u + 2) == 0x10 && !(rd16(u + 4) & 4)) {
                        uint16_t t = rd8(u + 0x54) == 5 ? TerrainAt(PosCell(rd32(u + 0xA))) : 0;
                        if (t == 8 || t == 9) {
                            U8{u + 0x73} += 1;
                            if (int8_t(rd8(u + 0x73)) > 2) wr8(u + 0x73, 0);
                            SetDirFrame(rd16(u + 0x5A), rd8(u + 0x6A), int8_t(rd8(u + 0x73)));
                            wr16(u + 0x76, 1);
                        } else {
                            HideSprite(rd16(u + 0x5A));
                        }
                    }
                }
            }
            if (script) {
                if (rd16(u + 0x14)) {
                    U16{u + 0x14} -= 1;
                } else if (ScriptValid(u + 0x16)) {
                    U16{0xFFC200} = ((rd16(info + 0xC) & 0x800) || OnScreen(rd32(u + 0xA))) ? 0x30 : 0x10;
                    wr16(u + 0x28, player_house);
                    for (;;) {
                        int16_t n = S16{0xFFC200};
                        U16{0xFFC200} -= 1;
                        if (n < 0 || rd16(u + 0x14) || !ScriptStep(u + 0x16)) break;
                    }
                }
            }
            if (rd8(u + 0x55) != 0xFF && !rd32(u + 0x4E)) {
                SetUnitAction(u, int8_t(rd8(u + 0x55)));
                wr8(u + 0x55, 0xFF);
            }
            if ((rd16(u + 6) & 0x80) && !TickBlink(u + 0xA)) {
                U8{u + 7} &= 0x7F;
                if (rd8(u + 2) == 0x11 && !DeployMcv()) PlaySoundEffect(0x2F);
                if (rd8(u + 2) == 0xB) SetUnitAction(u, 0xA);
            }
        }
        u = NextUnit(it);
        if (U16{0xFFC02C}) u = 0;
    }
}

} // namespace game
