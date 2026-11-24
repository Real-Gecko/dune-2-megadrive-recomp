// The object lists (units 0xFFDCC0, count 0xFFDCBC; structures 0xFFD31C,
// count 0xFFD318; teams 0xFFDC60, count 0xFFDC5C) with their iterators,
// and the set-up after a scenario is loaded (0x2657C).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};

// The shared body of the unit and structure iterators: it +0 house, +2
// type (-1 any), +4 position. Objects flagged +4 bit 2 are skipped unless
// a scenario is loading.
uint32_t IterNext(uint32_t it, uint32_t list, uint16_t count) {
    int16_t house = int16_t(rd16(it)), type = int16_t(rd16(it + 2)), i = int16_t(rd16(it + 4));
    if (i < int16_t(count)) i++;
    for (; i < int16_t(count); i++) {
        uint32_t o = rd32(list + i * 4u);
        if ((rd16(o + 4) & 4) && !U16{0xFFC148}) continue;
        if (house != -1 && int8_t(rd8(o + 8)) != house) continue;
        if (type != -1 && int8_t(rd8(o + 2)) != type) continue;
        wr16(it + 4, uint16_t(i));
        return o;
    }
    wr16(it + 4, uint16_t(i));
    return 0;
}

// 0xB794, 0x43196, 0x2DDEC: the lists rebuilt from the used slots; house
// unit counts recounted.
void RebuildStructureList() {
    for (uint32_t h = FirstHouse(0); h; h = NextHouse(0)) wr16(h + 6, 0);
    U16{0xFFD318} = 0;
    for (int i = 0; i < 0x49; i++) {
        uint32_t s = rd32(0x4A71E + i * 4u);
        if (rd16(s + 4) & 1) {
            wr32(0xFFD31C + U16{0xFFD318} * 4u, s);
            U16{0xFFD318} += 1;
        }
    }
}
void RebuildUnitList() {
    for (uint32_t h = FirstHouse(0); h; h = NextHouse(0)) wr16(h + 6, 0);
    U16{0xFFDCBC} = 0;
    for (int i = 0; i < 0x66; i++) {
        uint32_t u = rd32(0x4A85A + i * 4u);
        if (!(rd16(u + 4) & 1)) continue;
        U16{HouseGet(uint16_t(int8_t(rd8(u + 8)))) + 6} += 1;
        wr32(0xFFDCC0 + U16{0xFFDCBC} * 4u, u);
        U16{0xFFDCBC} += 1;
    }
}
void RebuildTeamList() {
    U16{0xFFDC5C} = 0;
    for (int i = 0; i < 0x10; i++) {
        uint32_t t = U32{0xFFDCA0} + i * 0x54u;
        if (!rd16(t + 2)) continue;
        wr32(0xFFDC60 + U16{0xFFDC5C} * 4u, t);
        U16{0xFFDC5C} += 1;
    }
}

} // namespace

// 0x48712: the player's unit uncovers its sight radius.
void RevealUnit(uint32_t u) {
    if (!u || (rd16(u + 4) & 4) || int16_t(int8_t(rd8(u + 8))) != int16_t(player_house)) return;
    uint32_t info = rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4);
    if (uint16_t r = rd16(info + 0x12)) RevealAround(rd32(u + 0xA), r);
}

namespace {

// 32-bit value times a signed byte, as the compiler's mulu/muls sequence.
uint32_t MulByte(uint32_t v, int8_t b) { return uint32_t(int32_t(v) * b); }


} // namespace

// 0x235E2: a house's spice storage from its silos (type 0xC) and
// refineries (type 0x11).
void UpdateStorage(uint16_t house) {
    uint32_t counts = rd32(0x4A9FA + int16_t(house) * 4);
    uint32_t v = MulByte(rd16(rd32(0x6B984) + 0x38), int8_t(rd8(counts + 0xC))) +
                 MulByte(rd16(rd32(0x6B998) + 0x38), int8_t(rd8(counts + 0x11)));
    wr32(HouseGet(house) + 0x16, v);
}

// 0x43224, 0x4324E; 0xB854, 0xB87E: unit and structure iterators (it 0:
// the shared one).
uint32_t FirstUnit(uint32_t it, uint16_t house, uint16_t type) {
    if (!it) it = 0xFFDCB4;
    wr16(it, house);
    wr16(it + 2, type);
    wr16(it + 4, 0xFFFF);
    return NextUnit(it);
}
uint32_t NextUnit(uint32_t it) { return IterNext(it ? it : 0xFFDCB4, 0xFFDCC0, U16{0xFFDCBC}); }
uint32_t FirstStructure(uint32_t it, uint16_t house, uint16_t type) {
    if (!it) it = 0xFFD310;
    wr16(it, house);
    wr16(it + 2, type);
    wr16(it + 4, 0xFFFF);
    return NextStructure(it);
}
uint32_t NextStructure(uint32_t it) { return IterNext(it ? it : 0xFFD310, 0xFFD31C, U16{0xFFD318}); }

// 0x2DE82, 0x2DEA6: the team iterator (it 0: 0xFFDC54; +0 house, -1 any;
// +4 position) over the list at 0xFFDC60.
uint32_t FirstTeam(uint32_t it, uint16_t house) {
    if (!it) it = 0xFFDC54;
    wr16(it, house);
    wr16(it + 4, 0xFFFF);
    return NextTeam(it);
}
uint32_t NextTeam(uint32_t it) {
    if (!it) it = 0xFFDC54;
    int16_t house = int16_t(rd16(it)), i = int16_t(rd16(it + 4));
    if (i < S16{0xFFDC5C}) i++;
    for (; i < S16{0xFFDC5C}; i++) {
        uint32_t t = rd32(0xFFDC60 + uint32_t(i * 4));
        if (house == -1 || house == int16_t(rd16(t + 0x10))) {
            wr16(it + 4, uint16_t(i));
            return t;
        }
    }
    wr16(it + 4, uint16_t(i));
    return 0;
}

namespace {

// 0x229B8: the mission is over: the player has nothing left, or (after
// the first) the enemy has nothing left, or the spice quota (0xFFC058) is
// met when the scenario's lose flags (0xFFC060 bit 2) say so. Input stops.
uint16_t MissionOver() {
    uint16_t over = 0;
    if (!U16{0xFFDBEE} || (!U16{0xFFDBEC} && U16{0xFFC050})) over = 1;
    if ((U16{0xFFC060} & 4) && U32{0xFFC058} != 0xFFFF && uint32_t(rd16(U32{0xFFC278} + 0x20)) <= U32{0xFFC058}) over = 1;
    if (over) {
        InstallSpriteTableHook();
        wr8(0xFFBE9C, 1);
    }
    return over;
}

// 0x22A2C: the player won, by the win flags (0xFFC062): bits 0-1 the enemy
// destroyed, bit 2 the quota, bit 3 before the time limit (0xFFC144).
uint16_t MissionWon() {
    uint16_t f = U16{0xFFC062};
    uint16_t won = 0;
    if ((f & 3) && U16{0xFFDBEE} && (!U16{0xFFC050} || !U16{0xFFDBEC})) won = 1;
    if ((f & 4) && !won && U32{0xFFC058} != 0xFFFF) won = uint32_t(rd16(U32{0xFFC278} + 0x20)) <= U32{0xFFC058};
    if (f & 8) won &= S32{0xFFC598} < S32{0xFFC144} ? 1 : 0;
    return won;
}

void ShowScore(uint16_t won) {
    ScoreScreen(uint16_t(int16_t(U16{0xFFC050}) + 1), U16{0xFFC05E}, U32{0xFFC598} - U32{0xFFC5A4}, U16{0xFFC09E},
                U16{0xFFC0A0}, U16{0xFFC098}, U16{0xFFC096}, U16{0xFFC09C}, U16{0xFFC09A}, won);
}

// 0x1F344: empty in this version.
void UpdateHighScores(uint16_t) {}

} // namespace

// 0x22AB2: every 300 ticks, a finished mission plays its ending: on a win
// the next scenario (after the ninth the finale and a reset), on a loss
// the same again; then the restart flag.
uint16_t CheckGameEnd() {
    if (U32{0xFFC598} <= U32{0xFFDC28}) return 1;
    if (MissionOver()) {
        PlayMusicTrack(0);
        PlayVoice(0xFFFE);
        SetGameMode(0);
        if (MissionWon() == 1) {
            WinScreen();
            WinBriefing();
            if (U16{0xFFC050} == 8) PlayMusicTrack(0x26);
            UpdateHighScores(player_house);
            WinMap(player_house, U16{0xFFC050});
            ShowScore(1);
            U16{0xFFC050} += 1;
            if (U16{0xFFC050} == 9) {
                FadeAll(U32{0xFFC28C});
                Finale(player_house);
                Reset();
            }
            U16{0xFFC04C} = uint16_t(U16{0xFFC050} + 1);
        } else {
            PlayVoice(0x29);
            LoseScreen();
            LoseBriefing();
            UpdateHighScores(player_house);
            LoseMap(player_house, U16{0xFFC050});
            U16{0xFFC05E} = uint16_t(int16_t(U16{0xFFC05E}) / 10);
            if (S16{0xFFC05E} < 0) U16{0xFFC05E} = 0;
            ShowScore(0);
        }
        U8{U32{0xFFC278} + 5} &= uint8_t(~4);
        U16{0xFFC14C} = 1;
        U16{0xFFC01C} = 0;
        U16{0xFFC020} = 0;
    }
    U32{0xFFDC28} = U32{0xFFC598} + 0x12C;
    return 1;
}

// 0x432DA: a house (-1 any) has a unit of a type (-1 any) in the unit list.
uint16_t HasUnit(uint16_t house, uint16_t type) {
    for (uint16_t i = 0; int16_t(i) < S16{0xFFDCBC}; i++) {
        uint32_t u = rd32(0xFFDCC0 + i * 4u);
        if (house != 0xFFFF && int16_t(int8_t(rd8(u + 8))) != int16_t(house)) continue;
        if (type != 0xFFFF && int16_t(int8_t(rd8(u + 2))) != int16_t(type)) continue;
        return 1;
    }
    return 0;
}

// 0x48068: a house without a harvester (none carried, none on the map)
// gets one delivered to its first refinery; 1 when it has or gets one.
uint16_t EnsureHarvester(uint16_t house) {
    for (uint32_t u = FirstUnit(0, house, 0); u; u = NextUnit(0)) {
        uint32_t v = LinkedUnit(u);
        if (v && rd8(v + 2) == 0x10) return 1;
    }
    if (HasUnit(house, 0x10)) return 1;
    uint32_t st = FirstStructure(0, house, 0xC);
    if (!st) return 0;
    DeliverUnit(house, 0x10, MakeTarget(rd16(st), 2));
    return 1;
}

// 0x10598: low power wears a house's structures down towards their share
// of the power (but never below half health); the player's build menu is
// refreshed first.
void ApplyPower(uint16_t house) {
    uint32_t h = HouseGet(house);
    uint16_t made = rd16(h + 0x1A), used = rd16(h + 0x1C);
    if (house == player_house) UpdateRadarStatus(house);
    uint16_t ratio = PartOf(used, made);
    if (ratio >= 0x100) ratio = 0x100;
    for (uint32_t s = FirstStructure(0, house, 0xFFFF); s; s = NextStructure(0)) {
        uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
        if (int16_t(int8_t(rd8(s + 8))) != int16_t(house)) continue;
        int16_t v = int16_t(ScaleHealth(rd16(info + 0x10), ratio));
        int16_t half = int16_t(int16_t(rd16(info + 0x10)) >> 1);
        if (half >= v) v = half;
        wr16(s + 0x5E, uint16_t(v));
        if (v >= int16_t(rd16(s + 0x12))) continue;
        int16_t d = int16_t(((int32_t(int16_t(rd16(s + 0x12))) - v) >> 3) + 1);
        if (d > 0) DamageStructure(s, uint16_t(d), 0);
    }
}

// 0x23760: the houses' tick: reinforcements arrive by carryall (every
// 600 ticks, 0xFFC0A2: sixteen of unit, ?, delay, repeat delay, repeat),
// a starport's order delivered (0xFFC17C the starport, house +0x2E/+0x30
// the delay and the frigate's cargo), power and storage (every 900),
// spice rot (every 0x2A30 after 0xFFC150), and the CHOAM stock (0xFFC2A0)
// restocked every 0x708.
void HouseTick() {
    uint32_t now = U32{0xFFC598};
    bool slow = false, rot = false, starport = false, reinf = false, special = false;
    if (now >= U32{0xFFDC44}) {
        slow = true;
        U32{0xFFDC44} = now + 0x384;
    }
    if (int32_t(now) >= S32{0xFFC150} && now > U32{0xFFDC40}) {
        rot = true;
        U32{0xFFDC40} = now + 0x2A30;
    }
    if (now >= U32{0xFFDC3C}) {
        starport = true;
        U32{0xFFDC3C} = now + 0x3C;
    }
    if (int32_t(now) >= S32{0xFFDC38}) {
        reinf = true;
        U32{0xFFDC38} = now + 0x258;
    }
    if (int32_t(now) >= S32{0xFFDC34}) U32{0xFFDC34} = now + 5;
    if (int32_t(now) >= S32{0xFFDC30}) {
        special = true;
        U32{0xFFDC30} = now + 0x3C;
    }
    if (int32_t(now) >= S32{0xFFDC2C}) {
        uint16_t r = RandomRange(0, 0x1A);
        U16 stock{0xFFC2A0 + r * 2u};
        if (int16_t(stock) >= 0 && int16_t(stock) < 10) stock += 1;
        U32{0xFFDC2C} = U32{0xFFC598} + 0x708;
    }
    if (special && U16{0xFFC1A4}) {
        U16{0xFFC1A4} -= 1;
        if (!U16{0xFFC1A4}) OrderSpecial(PickCell(4, player_house));
    }
    if (reinf) {
        uint32_t carrier = 0;
        for (int k = 0; k < 0x10; k++) {
            uint32_t e = 0xFFC0A2 + k * 10u;
            if (rd16(e) == 0xFFFF || !rd16(e + 4)) continue;
            U16{e + 4} -= 1;
            if (rd16(e + 4)) continue;
            uint32_t u = UnitGet(rd16(e));
            int16_t n = 0;
            if (S16{0xFFC050} < 8 && rd8(u + 8) == 4) {
                uint32_t c = rd32(0x4AA1E);
                n = int16_t(int8_t(rd8(c + 3)) + int8_t(rd8(c + 5)));
            }
            if (n >= 10) {
                wr16(e + 4, 10);
                continue;
            }
            uint16_t cell = PickCell(7, uint16_t(int8_t(rd8(u + 8))));
            if (!CellInMap(cell)) {
                wr16(e + 4, 10);
                continue;
            }
            // (The original's other path, placing the unit on the cell, needs
            // a mode below 4 and is never taken.)
            if (!carrier) carrier = DeliverUnit(uint16_t(int8_t(rd8(u + 8))), 0, MakeTarget(cell, 3));
            if (!carrier || rd8(carrier + 8) != rd8(u + 8)) {
                wr16(e + 4, 1);
                continue;
            }
            wr8(u + 3, rd8(carrier + 3));
            wr8(carrier + 3, uint8_t(rd16(u)));
            U8{carrier + 4} |= 1;
            wr16(e, 0xFFFF);
            if (!rd16(e + 8)) continue;
            U16{0xFFC148} += 1;
            uint32_t v = CreateUnitAt(0xFFFF, uint16_t(int8_t(rd8(u + 2))), uint16_t(int8_t(rd8(u + 8))), 0xFFFFFFFF, 0);
            U16{0xFFC148} -= 1;
            if (v) {
                wr16(e, rd16(v));
                wr16(e + 4, rd16(e + 6));
            }
        }
    }
    uint32_t h = U32{0xFFC204};
    if (rd16(h) != player_house) {
        if (rd32(h + 0x16) < rd32(h + 0x12)) wr32(h + 0x12, rd32(h + 0x16));
    } else {
        uint32_t cap = rd32(h + 0x16) > U32{0xFFC054} ? rd32(h + 0x16) : U32{0xFFC054};
        if (cap < rd32(h + 0x12)) wr32(h + 0x12, cap);
    }
    md::Frame frame(6);
    uint32_t it = frame.base;
    for (h = FirstHouse(it); h;) {
        U32{0xFFC204} = h;
        uint16_t house = rd16(h);
        if (slow) {
            if (house == player_house && U32{0xFFC054} < rd32(h + 0x16)) U32{0xFFC054} = 0;
            EnsureHarvester(house);
        }
        if (rd16(h + 0x30) != 0xFFFF && starport) {
            int16_t d = int16_t(int16_t(rd16(h + 0x2E)) - 1);
            wr16(h + 0x2E, uint16_t(d > 0 ? d : 0));
            if (!rd16(h + 0x2E)) {
                uint32_t frigate = 0;
                auto deliver = [&](uint32_t st) {
                    frigate = DeliverUnit(house, 0x1A, MakeTarget(rd16(st), 2));
                    if (!frigate) return;
                    wr8(frigate + 3, uint8_t(rd16(h + 0x30)));
                    wr16(h + 0x30, 0xFFFF);
                    U8{frigate + 4} |= 1;
                };
                uint32_t st = StructureGet(U16{0xFFC17C});
                if (st && rd8(st + 2) == 0xB && int16_t(int8_t(rd8(st + 8))) == int16_t(house)) {
                    deliver(st);
                } else {
                    for (st = FirstStructure(0, house, 0xB); st; st = NextStructure(0)) {
                        if (rd8(st + 3) != 0xFF) continue;
                        deliver(st);
                        break;
                    }
                }
                wr16(h + 0x2E, frigate ? rd16(0x6C768 + house * 30u) : 1);
            }
        }
        if (slow) {
            UpdatePower(int16_t(house));
            ApplyPower(house);
            if (rd16(h + 0x28)) U16{h + 0x28} -= 1;
            if (rd16(h + 0x2A)) U16{h + 0x2A} -= 1;
            if (rd16(h + 0x2C)) U16{h + 0x2C} -= 1;
            if (rd16(h + 2) && DeliverUnit(house, 0x10, 0)) U16{h + 2} -= 1;
        }
        if (rot) {
            uint32_t d = uint32_t(int32_t(int16_t(rd16(h + 0x1C)) >> 5) + 1);
            uint32_t c = rd32(h + 0x12);
            wr32(h + 0x12, c - (d < c ? d : c));
        }
        h = NextHouse(it);
        if (U16{0xFFC02C}) h = 0;
    }
    if (rd32(U32{0xFFC278} + 0x12) < 1000) U32{0xFFC054} = 1000;
}

// 0x2E44C: every 5-12 ticks, the teams of houses with team AI (+4 bit 3)
// run their scripts (+0x1C delay, +0x1E script); a stopped script ends
// the round.
void TeamTick() {
    if (S32{0xFFC598} < S32{0xFFDCA4}) return;
    U32{0xFFDCA4} = U32{0xFFC598} + (Random() & 7) + 5;
    for (uint32_t t = FirstTeam(0, 0xFFFF); t;) {
        U32{0xFFDCA8} = t;
        uint32_t h = HouseGet(rd16(t + 0x10));
        U32{0xFFC204} = h;
        if (rd16(h + 4) & 8) {
            if (rd16(t + 0x1C)) {
                U16{t + 0x1C} -= 1;
            } else if (rd32(t + 0x1E) && rd32(t + 0x22)) {
                if (!ScriptStep(t + 0x1E)) return;
            }
        }
        t = NextTeam(0);
        if (U16{0xFFC02C}) t = 0;
    }
}

// 0x10490: a house's storage (+0x16), power produced (+0x1A, damaged
// plants give less) and used (+0x1C).
void UpdatePower(int16_t house) {
    uint32_t h = HouseGet(uint16_t(house));
    if (!h) return;
    uint16_t made = 0, used = 0;
    wr32(h + 0x16, 0);
    for (uint32_t s = FirstStructure(0, uint16_t(house), 0xFFFF); s; s = NextStructure(0)) {
        uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
        U32{h + 0x16} += rd16(info + 0x38);
        int16_t p = int16_t(rd16(info + 0x3A));
        if (p >= 0) {
            used = uint16_t(used + p);
        } else if (int16_t(rd16(s + 0x12)) < int16_t(rd16(info + 0x10))) {
            uint16_t f = PartOf(rd16(info + 0x10), rd16(s + 0x12));
            if (f <= 0x80) f = 0x80;
            if (f >= 0xFF) f = 0xFF;
            made = uint16_t(made + ScaleHealth(uint16_t(-p), f));
        } else {
            made = uint16_t(made - p);
        }
    }
    wr16(h + 0x1A, made);
    wr16(h + 0x1C, used);
    if (rd16(h) == player_house && !rd32(h + 0xE) && !U16{0xFFC148}) U32{0xFFC054} = 0;
}

// 0x2657C: after the scenario: lists, map flags and fog, sight, starport
// deliveries, scripts, house state.
void RunMission() {
    U16{0xFFC148} += 1;
    uint16_t mode = U16{0xFFC264};
    U16{0xFFC264} = 0;
    RebuildStructureList();
    RebuildUnitList();
    RebuildTeamList();
    for (uint16_t cell = 0; cell < 0x1000; cell++) {
        uint32_t c = 0xFF7D9C + cell * 4u;
        uint32_t u = UnitAt(cell);
        if (!u || !(rd16(u + 4) & 1)) U8{c + 2} &= uint8_t(~0x10);
        uint32_t s = StructureAt(cell);
        if (!s || !(rd16(s + 4) & 1)) U8{c + 2} &= uint8_t(~0x20);
        if (rd8(c + 2) & 8) RevealCell(cell, player_house);
    }
    for (uint32_t u = FirstUnit(0, 0xFFFF, 0xFFFF); u; u = NextUnit(0)) {
        if ((rd16(u + 4) & 4) || int16_t(int8_t(rd8(u + 8))) != int16_t(player_house)) continue;
        RevealUnit(u);
        CancelOrder(1, u);
    }
    for (uint32_t s = FirstStructure(0, 0xFFFF, 0xFFFF); s; s = NextStructure(0)) {
        if (rd16(s + 4) & 4) continue;
        RevealStructure(s);
        uint8_t type = rd8(s + 2);
        if (type == 0xB && rd8(s + 3) != 0xFF) {
            uint32_t o = UnitGet(uint16_t(int8_t(rd8(s + 3))));
            if ((rd16(o + 4) & 5) != 5) {
                wr8(s + 3, 0xFF);
                wr16(s + 0x56, 0);
            } else {
                SetStructureState(s, 2);
                if (rd16(s + 0x56)) wr16(s + 0x56, int16_t(rd16(s + 0x56)) > 1 ? rd16(s + 0x56) : 1);
            }
        }
        ScriptStart(s + 0x16, int16_t(int8_t(type)));
        if (type == 2) wr32(HouseGet(uint16_t(int8_t(rd8(s + 8)))) + 0x22, rd32(s + 0xA));
        uint32_t h = HouseGet(uint16_t(int8_t(rd8(s + 8))));
        if (!rd32(h + 0x22)) wr32(h + 0x22, rd32(s + 0xA));
    }
    for (uint32_t h = FirstHouse(0); h; h = NextHouse(0)) {
        wr32(h + 0xE, OwnedTypes(rd16(h)));
        UpdateStorage(rd16(h));
        UpdatePower(int16_t(rd16(h)));
    }
    wr8(0xFFBE9C, 0);
    U16{0xFFC05C} = 0x2AB;
    SetCursorAt(U16{0xFFC248});
    U32{0xFFC174} = U16{0xFFC178} != 0xFFFF ? StructureGet(U16{0xFFC178}) : 0;
    if (U16{0xFFC170} == 0xFFFF) StructureAt(U16{0xFFC248});
    U16{0xFFC348} = 0;
    U32{0xFFC25C} = 0;
    ResetSelection();
    U32{0xFFC578} = 0;
    U32{0xFFC260} = 0;
    U32{0xFFC150} = U32{0xFFC598} + 0x46;
    U16{0xFFC26C} = 1;
    U32{0xFFC058} = 0xFFFF;
    UpdateHouse(player_house, 2);
    U16{0xFFC264} = mode;
    U16{0xFFC148} -= 1;
}

// 0x2DF06: a free team slot (or the given one if free), cleared, marked
// used and listed (0xFFDC60, count 0xFFDC5C).
uint32_t AllocTeam(uint16_t index) {
    uint32_t base = U32{0xFFDCA0};
    if (!base) return 0;
    uint32_t t = 0;
    uint16_t i = index;
    if (index == 0xFFFF) {
        for (i = 0; i < 0x10; i++) {
            if (!rd16(base + i * 0x54u + 2)) {
                t = base + i * 0x54u;
                break;
            }
        }
    } else if (!rd16(base + uint32_t(int32_t(int16_t(i)) * 0x54) + 2)) {
        t = base + uint32_t(int32_t(int16_t(i)) * 0x54);
    }
    if (!t) return 0;
    MemSet(t, 0, 0x54);
    wr16(t, i);
    wr16(t + 2, 1);
    uint16_t n = U16{0xFFDC5C};
    U16{0xFFDC5C} = uint16_t(n + 1);
    wr32(0xFFDC60 + uint16_t(n << 2), t);
    return t;
}

// 0x2DFBC: a team for a house: its script mode (+0xC, kept as the
// original +0xE), movement type (+0xA), minimum and maximum size (+6, +8).
uint32_t CreateTeam(uint16_t house, uint16_t ai, uint16_t move, uint16_t min, uint16_t max) {
    uint32_t t = AllocTeam(0xFFFF);
    if (!t) return 0;
    wr16(t + 2, 1);
    wr16(t + 0x10, house);
    wr16(t + 0xC, ai);
    wr16(t + 0xE, ai);
    wr16(t + 0xA, move);
    wr16(t + 6, min);
    wr16(t + 8, max);
    ScriptReset(t + 0x1E, 0xFFC1E0);
    ScriptStart(t + 0x1E, int16_t(ai));
    wr16(t + 0x1C, 0);
    return t;
}

// 0x2313E: the special weapon (0xFFC1A0, a placeholder unit) launched at
// a cell: a rocket from the house's palace (+0x22) to a random spot
// within ten cells of it; the player's order mode ends.
void OrderSpecial(uint16_t cell) {
    uint32_t w = U32{0xFFC1A0};
    if (!w) return;
    uint32_t centre = ((uint32_t(int32_t(int16_t(cell)) & 0xFC0) << 10) + uint32_t(int32_t(int8_t(cell & 0x3F)))) * 0x100 + 0x800080;
    uint16_t c = PosCell(ScatterPos(centre, 0xA0, 0));
    bool own = int8_t(rd8(w + 8)) == int16_t(U16{0xFFC274});
    FreeUnit(w);
    PlayVoice(0xFFFE);
    uint16_t house = uint16_t(int16_t(int8_t(rd8(w + 8))));
    FireProjectile(rd32(HouseGet(house) + 0x22), uint16_t(int16_t(int8_t(rd8(w + 2)))), house, 0x1F4, MakeTarget(c, 3));
    if (int8_t(rd8(U32{0xFFC1A0} + 8)) != int16_t(U16{0xFFC274})) PlaySoundEffect(0x1B);
    U16{0xFFC1A4} = 0;
    U32{0xFFC1A0} = 0;
    if (own) SetGameMode(4);
}

// 0x23500: what a computer's structure builds next: a yard the first
// lost structure it can rebuild (else -1); a factory, of what it can make
// (no carryall at a hi-tech factory while one exists, bits 16-17 never at
// a heavy factory), now and then a random one, else the most valuable.
uint16_t AiChooseBuild(uint32_t s) {
    if (!s) return 0xFFFF;
    uint16_t house = uint16_t(int16_t(int8_t(rd8(s + 8))));
    uint32_t h = HouseGet(house);
    uint32_t mask = BuildableMask(s);
    if (rd8(s + 2) == 5 && FirstUnit(0, house, 0)) mask &= ~1u;
    if (rd8(s + 2) == 4) mask &= 0xFFFCFFFF;
    if (rd8(s + 2) == 8) {
        for (int i = 0; i < 5; i++) {
            uint16_t t = rd16(h + 0x32 + i * 4u);
            if (t && t < 64 && ((uint64_t(1) << t) & mask)) return t;
        }
        return 0xFFFF;
    }
    uint16_t best = 0xFFFF;
    for (uint16_t k = 0; k < 0x20; k++) {
        if (!((1u << k) & mask)) continue;
        if (!(Random() & 3)) return k;
        if (best == 0xFFFF || int16_t(rd16(rd32(0x6C5BC + k * 4u) + 0x2E)) > int16_t(rd16(rd32(0x6C5BC + best * 4u) + 0x2E))) best = k;
    }
    return best;
}

// 0x22CC8: a palace's special weapon by house (0x6C76C): 1 a death hand
// rocket (the player aims it, a computer fires at the first enemy
// structure), 2 a Fremen band (0x95CE0 by scenario, types 0x95CD8) at a
// random spot off screen sent at 0xFFC1A8, 3 a saboteur out beside the
// palace sent at 0xFFC1A8. The recharge time (0x6C766) is set; 1 if
// launched.
uint16_t LaunchSpecial(uint32_t s) {
    uint16_t house = uint16_t(int16_t(int8_t(rd8(s + 8))));
    uint32_t h = HouseGet(house);
    bool human = rd16(h + 4) & 2;
    if (!human && !(rd16(h + 4) & 1)) return 0;
    uint32_t hd = uint32_t(int32_t(int16_t(house)) * 30);
    switch (rd16(0x6C76C + hd)) {
    case 1: {
        U16{0xFFC148} += 1;
        uint16_t facing = Random();
        U32{0xFFC1A0} = CreateUnitAt(0xFFFF, 0x12, house, 0xFFFFFFFF, facing);
        U16{0xFFC148} -= 1;
        if (!U32{0xFFC1A0}) return 0;
        wr16(s + 0x56, rd16(0x6C766 + hd));
        wr16(s + 0x4E, 0x12C);
        if (human) {
            U16{0xFFC1A4} = 7;
            SetGameMode(1);
            return 1;
        }
        for (uint32_t st = FirstStructure(0, 0xFFFF, 0xFFFF); st; st = NextStructure(0)) {
            if (Allied(uint16_t(int16_t(int8_t(rd8(st + 8)))), house)) continue;
            OrderSpecial(PosCell(rd32(st + 0xA)));
            return 1;
        }
        FreeUnit(U32{0xFFC1A0});
        U32{0xFFC1A0} = 0;
        return 1;
    }
    case 2: {
        uint32_t pos;
        do {
            uint16_t c1 = PickCell(4, 0xFFFF);
            uint16_t c2 = PickCell(4, 0xFFFF);
            pos = ((uint32_t(int32_t(int16_t(c2)) & 0xFC0) << 10) + uint32_t(int32_t(int8_t(c1 & 0x3F)))) * 0x100 + 0x800080;
        } while (OnScreen(pos));
        uint32_t sc = U32{0x4AA1A};
        int16_t n = int8_t(rd8(0x95CE0 + int16_t(int16_t(int8_t(rd8(sc + 3))) + int8_t(rd8(sc + 5)))));
        for (int16_t i = 0; i < n; i++) {
            U16{0xFFC148} += 1;
            uint16_t facing = Random();
            uint32_t at = ScatterPos(pos, 0x1E, 1);
            uint16_t type = rd16(0x95CD8 + RandomRange(0, 3) * 2u);
            uint32_t u = CreateUnitAt(0xFFFF, type, 3, at, facing);
            U16{0xFFC148} -= 1;
            if (!u) continue;
            U16{u + 0x12} = uint16_t(rd16(u + 0x12) * 2);
            uint16_t c = U16{0xFFC1A8};
            if (!c) continue;
            // The original tests (flags & 0x10) | 0x20, always set.
            SetUnitAction(u, 0);
            uint32_t m = rd32(0x4AA24) + uint32_t(c) * 4;
            uint16_t t = MakeTarget(uint16_t(rd8(m + 3) - 1), (rd8(m + 2) & 0x10) ? 1 : 2);
            if ((t >> 14) == 3) UnitMoveTo(u, t);
            else UnitAttack(u, t);
        }
        wr16(s + 0x56, rd16(0x6C766 + hd));
        wr16(s + 0x4E, 0xB4);
        return 1;
    }
    case 3: {
        uint32_t u;
        if (rd8(s + 3) == 0xFF) {
            U16{0xFFC148} += 1;
            uint16_t facing = Random();
            u = CreateUnitAt(0xFFFF, 6, house, 0xFFFFFFFF, facing);
            U16{0xFFC148} -= 1;
        } else {
            u = UnitGet(uint16_t(int16_t(int8_t(rd8(s + 3)))));
        }
        if (!u) return 0;
        wr8(s + 3, uint8_t(rd16(u)));
        uint16_t c = FreeCellBeside(s, 0);
        if (!c) {
            wr16(s + 0x56, 1);
            return 0;
        }
        wr8(s + 3, 0xFF);
        wr16(s + 0x56, rd16(0x6C766 + hd));
        wr16(s + 0x4E, 0xF0);
        PlaceUnit(u, ((uint32_t(int32_t(int16_t(c)) & 0xFC0) << 10) + uint32_t(int32_t(int8_t(c & 0x3F)))) * 0x100 + 0x800080);
        SetUnitAction(u, 9);
        UnitAttack(u, MakeTarget(U16{0xFFC1A8}, 3));
        return 1;
    }
    default: return 0;
    }
}

} // namespace game
