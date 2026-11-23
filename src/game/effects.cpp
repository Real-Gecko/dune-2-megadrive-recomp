// Map effects: 16-byte nodes from the pool at 0xFFC8C8 (free list
// 0xFFD2F8), on two lists: by cell (0xFFD2FA, cell word at +4) and by
// position (0xFFD2FC, position long at +0, sprite node at +6).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 free_list{0xFFD2F8};
constexpr U16 cell_list{0xFFD2FA};
constexpr U16 pos_list{0xFFD2FC};

// Unlinks node n from a list and pushes it on the free list.
void Release(uint32_t n, U16 head) {
    uint16_t p = rd16(n + 0xC), q = rd16(n + 0xE);
    if (p) wr16(Node(p) + 0xE, q);
    else head = q;
    if (q) wr16(Node(q) + 0xC, p);
    wr16(n + 0xC, 0);
    wr16(n + 0xE, free_list);
    if (free_list) wr16(Node(free_list) + 0xC, uint16_t(n));
    free_list = uint16_t(n);
}

} // namespace

// 0x9664, 0x966A: frees a sprite node and clears its object's link.
void FreeObjectSprite(uint16_t n) {
    if (!n) return;
    uint32_t a = Node(n);
    wr16(rd32(a) + 6, 0);
    FreeSprite(a, kSpriteList);
}

// 0xB2C2: removes the effect on a cell.
void RemoveCellEffect(uint16_t c) {
    for (uint16_t w = cell_list; w; w = rd16(Node(w) + 0xE))
        if (rd16(Node(w) + 4) == c) {
            Release(Node(w), cell_list);
            return;
        }
}

// 0xB2BE: the same for a cell passed as a long; it reads the long's high
// word, so it looks for cell 0.
void MarkCell(uint32_t cell) { RemoveCellEffect(uint16_t(cell >> 16)); }

// 0xB0A8: an effect's tiles onto the map: tile n (single-tile kinds at
// 0xB146 set the overlay instead) and following over the layout's cells.
void DrawEffect(uint32_t n) {
    uint16_t tile = rd16(n + 6);
    if (!tile) return;
    uint16_t cell = rd16(n + 4);
    if (tile < 0x80 && !rd8(0xB146 + tile)) {
        uint32_t c = 0xFF7D9C + uint16_t(cell << 2);
        wr8(c, uint8_t((rd8(c) & 1) + tile * 2));
        RedrawCell(cell);
        return;
    }
    uint8_t layout = rd8(n + 0xA);
    uint32_t offs = 0x6B740 + layout * 18u;
    for (int k = int8_t(rd8(0xB0A0 + layout)); k >= 0; k--, offs += 2, tile++) {
        uint16_t c = uint16_t(cell + rd16(offs));
        uint32_t a = 0xFF7D9C + uint16_t(c << 2);
        uint16_t w = rd16(a) & 0xFE00;
        if (w == 0x4200 || w <= 0x2600) w = 0;
        wr16(a, uint16_t(w | tile));
        RedrawCell(c);
    }
}

// 0xAE5C: a structure's tiles (set at 0x6A9BC, frame) drawn on its cells
// through a new cell effect.
void DrawStructureTiles(uint16_t tiles, uint16_t frame, uint16_t cell, uint16_t layout, uint16_t house) {
    RemoveCellEffect(cell);
    uint16_t f = free_list;
    if (!f) return;
    uint32_t n = Node(f);
    uint16_t next = rd16(n + 0xE);
    wr16(Node(next) + 0xC, 0);
    free_list = next;
    wr16(n + 0xC, 0);
    uint16_t head = cell_list;
    cell_list = uint16_t(n);
    wr16(n + 0xE, head);
    if (head) wr16(Node(head) + 0xC, uint16_t(n));
    uint32_t t = rd32(0x6A9BC + (tiles & 0x3F) * 4u) + frame * 8u;
    wr16(n + 6, rd16(t));
    wr16(n + 8, rd16(t + 2));
    wr32(n, t + 4);
    wr16(n + 4, cell);
    wr8(n + 0xA, uint8_t(layout));
    wr8(n + 0xB, uint8_t(house));
    DrawEffect(n);
}

// 0xB32A: removes a positioned effect and its sprite.
void FreeEffect(uint32_t n) {
    for (uint16_t w = pos_list; w; w = rd16(Node(w) + 0xE)) {
        if (Node(w) != Node(uint16_t(n))) continue;
        Release(Node(w), pos_list);
        if (uint16_t s = rd16(Node(w) + 6)) FreeObjectSprite(s);
        return;
    }
}

// 0xB754: removes the positioned effects on the cell of pos.
void RedrawUnitsAt(uint32_t pos) {
    for (;;) {
        uint16_t w = pos_list;
        for (; w; w = rd16(Node(w) + 0xE))
            if ((rd32(Node(w)) & 0xFF00FF00) + 0x800080 == pos) break;
        if (!w) return;
        FreeEffect(Node(w));
    }
}

// 0xB228: the positioned effects step their animations (+4/+8 the script
// position: class, time pairs; a negative class or no time ends it).
void UpdateEffects() {
    uint16_t w = pos_list;
    if (!w) return;
    int16_t dt = int16_t(U16{0xFFFFFE});
    if (dt <= 0) dt = int16_t(rd16(Node(w) + 0xA));
    while (w) {
        uint32_t n = Node(w);
        U16{n + 0xA} -= uint16_t(dt);
        if (S16{n + 0xA} > 0) {
            w = rd16(n + 0xE);
            continue;
        }
        uint32_t p = (rd32(n + 4) & 0xFFFF0000) | rd16(n + 8);
        int16_t cls = int16_t(rd16(p));
        wr16(n + 0xA, rd16(p + 2));
        p += 4;
        if (S16{n + 0xA} <= 0) {
            w = rd16(n + 0xE);
            FreeEffect(n);
            continue;
        }
        wr16(n + 8, uint16_t(p));
        wr16(n + 4, uint16_t(p >> 16) & 0x7FFF);
        if (cls < 0) {
            w = rd16(n + 0xE);
            FreeEffect(n);
            continue;
        }
        if (uint16_t sp = rd16(n + 6)) {
            FreeObjectSprite(sp);
            CreateSprite(n, uint16_t(cls), 0);
        }
        w = rd16(n + 0xE);
    }
}

// 0xB02C, 0xAFCE: two tile animations cycling three frames every 10 ticks.
void AnimateTilesA() {
    U16{0xFFD2FE} -= U16{0xFFFFFE};
    if (S16{0xFFD2FE} > 0) return;
    U16{0xFFD2FE} = 10;
    int16_t f = S16{0xFFD300};
    if (f <= 0) f = 3;
    f--;
    U16{0xFFD300} = uint16_t(f);
    QueueDma(2, rd32(0xB020 + f * 4), 0xDF200060);
}
void AnimateTilesB() {
    U16{0xFFD302} -= U16{0xFFFFFE};
    if (S16{0xFFD302} > 0) return;
    U16{0xFFD302} = 10;
    int16_t f = S16{0xFFD304};
    if (f <= 0) f = 3;
    f--;
    U16{0xFFD304} = uint16_t(f);
    uint32_t t = rd32(0xAFC2 + f * 4);
    for (int i = 0; i < 3; i++, t += 8) QueueDma(2, rd32(t), rd32(t + 4));
}

// 0xB1C6: the tile animations, then the cell effects step (+0 script of
// tile, time pairs; no time removes it).
void UpdateCellEffects() {
    AnimateTilesA();
    AnimateTilesB();
    uint16_t w = cell_list;
    if (!w) return;
    int16_t dt = int16_t(U16{0xFFFFFE});
    if (dt <= 0) dt = int16_t(rd16(Node(w) + 8));
    while (w) {
        uint32_t n = Node(w);
        U16{n + 8} -= uint16_t(dt);
        if (S16{n + 8} > 0) {
            w = rd16(n + 0xE);
            continue;
        }
        uint16_t cell = rd16(n + 4);
        uint32_t p = rd32(n);
        wr16(n + 6, rd16(p));
        wr16(n + 8, rd16(p + 2));
        if (S16{n + 8} <= 0) {
            w = rd16(n + 0xE);
            RemoveCellEffect(cell);
            continue;
        }
        wr32(n, p + 4);
        DrawEffect(n);
        w = rd16(n + 0xE);
    }
}

// 0xAEDE: a positioned effect of a kind (animation scripts at 0xC80FE:
// class, time pairs) at a position in a palette; the node (or, when the
// first time is none, the free node as the original leaves it).
uint32_t ExplosionAt(uint16_t kind, uint32_t pos, uint16_t pal) {
    uint16_t f = free_list;
    if (!f) return 0;
    uint32_t n = Node(f);
    uint32_t a = rd32(0xC80FE + uint16_t(kind << 2));
    uint16_t cls = rd16(a);
    wr16(n + 0xA, rd16(a + 2));
    if (S16{n + 0xA} <= 0) return f;
    wr32(n + 6, a + 4);
    wr16(n + 4, rd16(n + 6) & 0x7FFF);
    wr32(n, pos);
    if (!CreateSprite(n, cls, pal)) return 0;
    uint16_t next = rd16(n + 0xE);
    wr16(Node(next) + 0xC, 0);
    free_list = next;
    wr16(n + 0xC, 0);
    uint16_t head = pos_list;
    pos_list = uint16_t(n);
    wr16(n + 0xE, head);
    if (head) wr16(Node(head) + 0xC, uint16_t(n));
    return n;
}

namespace {

// 0x4367A: the team a unit belongs to.
uint32_t TeamOf(uint32_t u) { return u && rd8(u + 0x75) ? TeamGet(uint16_t(int8_t(rd8(u + 0x75)) - 1)) : 0; }

// 0x1B164: a wall cell becomes rubble ground (0xFFC234).
uint16_t DestroyWall(uint16_t cell) {
    if (TerrainAt(cell) != 0xB) return 0;
    uint32_t m = rd32(0x4AA24) + uint32_t(int32_t(int16_t(cell)) * 4);
    wr16(m, uint16_t((rd16(m) & 0xFE00) + (U16{0xFFC234} & 0x1FF)));
    U8{m + 2} &= 0xC8;
    wr8(m + 3, 0);
    UpdateCellSprite(cell, 0, 0);
    return 1;
}

// 0x23660: a house learns it is attacked: the player hears it (every 8
// slow ticks); a computer house's idle carryalls go hunting.
uint16_t HouseAttacked(uint16_t house) {
    uint32_t h = HouseGet(house);
    if (house != U16{0xFFC274} && (rd16(h + 4) & 4)) return 0;
    U8{h + 5} |= 4;
    if (rd16(h + 4) & 2) {
        if (!rd16(h + 0x2C)) {
            PlayVoice(0x30);
            wr16(h + 0x2C, 8);
        }
        return 1;
    }
    for (uint32_t u = FirstUnit(0, house, 0); u; u = NextUnit(0)) {
        uint32_t ui = rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4);
        if (rd16(ui + 0x58) != 0xFFFF && rd8(u + 0x54) == 4) SetUnitAction(u, 0xB);
    }
    return 1;
}

} // namespace

// 0xA938: an explosion of a kind with damage: units within its radius (2
// cells, 1 for most) take damage falling off with distance and answer the
// attacker (src target); a structure on the cell is damaged, a wall may
// fall; then its effect and sound. Kind 6 damages only from type 0xB and
// takes the damage as the effect's time.
void SpawnEffect(uint16_t kind, uint32_t pos, uint16_t damage, uint16_t src) {
    uint16_t time = 0;
    if (kind == 6) {
        time = damage;
        uint32_t v = TargetUnit(src);
        if (v && rd8(v + 2) != 0xB) damage = 0;
    }
    int16_t radius = kind == 0xB ? 0x20 : 0x10;
    uint16_t cell = PosCell(pos);
    if (damage) {
        md::Frame frame(6);
        for (uint32_t u = FirstUnit(frame.base, 0xFFFF, 0xFFFF); u; u = NextUnit(frame.base)) {
            uint32_t ui = rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4);
            int16_t dist = int16_t(int16_t(DistanceFine(pos, rd32(u + 0xA))) >> 4);
            if (dist >= radius) continue;
            if (!(rd8(u + 2) == 0x19 && kind == 0xD) && rd8(u + 2) != 0x1A)
                DamageUnit(u, uint16_t(uint32_t(damage) >> (dist >> 2)), 0);
            if (int8_t(rd8(u + 8)) == int16_t(U16{0xFFC274})) continue;
            uint32_t att = TargetUnit(src);
            if (!att || att == u) continue;
            if (Allied(uint16_t(int16_t(int8_t(rd8(att + 8)))), uint16_t(int16_t(int8_t(rd8(u + 8)))))) continue;
            if (uint32_t team = TeamOf(u)) {
                if (rd16(team + 0xC) == 1) {
                    LeaveTeamOf(u);
                    SetUnitAction(u, 0xB);
                    continue;
                }
                if (uint32_t tt = TargetUnit(rd16(team + 0x1A)))
                    if (rd16(rd32(0x6C5BC + int16_t(int8_t(rd8(tt + 2))) * 4) + 0x58) == 0xFFFF) wr16(team + 0x1A, src);
                continue;
            }
            if (rd8(u + 2) == 0x10 && !rd16(rd32(0x6C5BC + int16_t(int8_t(rd8(att + 2))) * 4) + 0x3E) && !rd16(u + 0x5C)) {
                if (rd8(u + 0x54) != 1) SetUnitAction(u, 1);
                wr16(u + 0x5C, src);
                continue;
            }
            if (rd16(ui + 0x58) == 0xFFFF) continue;
            if (int8_t(rd8(u + 8)) != int16_t(U16{0xFFC274}) && rd8(u + 0x54) == 3 && (rd16(u + 4) & 0x200)) SetUnitAction(u, 0xB);
            if (rd8(u + 2) == 0x19) continue;
            if (rd16(u + 0x5A) && rd8(u + 0x54) != 0xB) continue;
            if (TargetUnit(rd16(u + 0x5A)) &&
                int16_t(CellDistance(TargetCellOf(rd16(u + 0x5A)), PosCell(rd32(u + 0xA)))) < int16_t(rd16(ui + 0x52)))
                continue;
            UnitAttack(u, src);
        }
    }
    if (damage) {
        if (uint32_t st = StructureAt(cell)) {
            uint32_t si = rd32(0x6B954 + int16_t(int8_t(rd8(st + 2))) * 4);
            if (kind == 2 && (int16_t(rd16(si + 0x10)) >> 1) > int16_t(rd16(st + 0x12))) kind = 0xF;
            HouseAttacked(uint16_t(int16_t(int8_t(rd8(st + 8)))));
            DamageStructure(st, damage, 0);
        }
    }
    if (TerrainAt(cell) == 0xB && damage) {
        uint32_t wi = rd32(0x6B98C);
        bool falls = int32_t(damage) >= int32_t(int16_t(rd16(wi + 0x10)));
        if (!falls) falls = int16_t(Random()) <= int16_t(PartOf(rd16(wi + 0x10), damage));
        if (falls) {
            DestroyWall(cell);
            uint16_t t = MakeTarget(cell, 3);
            for (uint32_t u = FirstUnit(0, 0xFFFF, 0xFFFF); u; u = NextUnit(0)) {
                if (rd16(u + 0x5A) == t) wr16(u + 0x5A, 0);
                if (rd16(u + 0x5C) == t) wr16(u + 0x5C, 0);
            }
        }
    }
    if (kind == 6) {
        uint32_t e = ExplosionAt(kind, pos, 3);
        if (uint16_t(e)) wr16(e + 0xA, time);
    } else {
        ExplosionAt(kind, pos, kind == 0x13 ? 0 : 3);
    }
    static const uint8_t sounds[20] = {0, 0x29, 0x32, 0x31, 0x38, 0, 0, 0, 0x39, 0, 0, 0, 0, 0x3F, 0x33, 0, 0, 0, 0x33, 0x39};
    if (kind >= 1 && kind <= 19 && sounds[kind]) PlaySoundEffect(sounds[kind]);
}

// 0xADC2: a deviator cloud: its effect, and the units of the other side
// within two cells change sides (0x4788A).
void Explosion(uint16_t kind, uint32_t pos, uint16_t house) {
    PlaySoundEffect(0x39);
    ExplosionAt(kind, pos, 3);
    uint32_t b = rd32(rd32(0x20704 + Allied(house, U16{0xFFC274}) * 4u));
    while (b) {
        uint32_t v = rd32(0x4A85A + uint16_t(rd16(b) << 2));
        uint16_t next = rd16(v + 0x4C) ? rd16(Node(rd16(v + 0x4C)) + 4) : 0;
        if ((int16_t(DistanceFine(pos, rd32(v + 0xA))) >> 4) < 0x20) Deviate(v, house);
        b = Node(next);
    }
}

} // namespace game
