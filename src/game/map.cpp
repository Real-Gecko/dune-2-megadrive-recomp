// The map: 64x64 cells of 4 bytes at 0xFF7D9C (word: tile number in the low
// 9 bits, the high bits from 0xFFC224; byte +2 flags, byte +3 object index
// + 1) and the terrain types, a byte per cell, at 0xFF6C98.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t kMap = 0xFF7D9C;
constexpr uint32_t kTerrain = 0xFF6C98;

} // namespace

// 0x1B1D8: the map of a scenario from the ROM table at 0x1B27A (one byte
// per cell; a negative pointer holds only the middle 32x32).
void GenerateMap(uint32_t seed) {
    uint16_t base = uint16_t(U16{0xFFC224} >> 7 | U16{0xFFC224} << 9);
    uint32_t src = rd32(0x1B27A + uint16_t((seed - 1) << 2));
    auto cell = [&](uint32_t c, uint32_t t) {
        uint8_t b = rd8(src++);
        wr8(t, b);
        wr16(c, uint16_t((base & 0xFF00) | b));
        wr16(c + 2, 0);
    };
    if (int32_t(src) >= 0) {
        for (int i = 0; i < 0x1000; i++) cell(kMap + i * 4u, kTerrain + i);
    } else {
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) cell(kMap + 0x1040 + y * 0x100u + x * 4u, kTerrain + 0x410 + y * 64u + x);
    }
    for (int i = 0; i < 0x1000; i++) {
        uint8_t t = rd8(kTerrain + i);
        if (t == 0xB0) t = 0x7F;
        else if (t == 0xC0) t = 0xBF;
        wr8(kTerrain + i, t);
        wr8(kMap + i * 4u + 1, t);
    }
}

// 0x57EA: the terrain kind of a cell (table at 0x581C by tile, overridden
// by kind 0xD from the high byte).
uint16_t TerrainAt(uint16_t cell) {
    uint32_t c = kMap + uint16_t(cell << 2);
    uint16_t t = uint16_t(int8_t(rd8(0x581C + (rd16(c) & 0x1FF))));
    if (rd8(0x581C + ((rd8(c) >> 1) & 0x7F)) == 0xD) t = 0xD;
    return t;
}

// 0x1AFEA: a cell's overlay (word bits 9-15) is outside the current range
// (0xFFC224 - 15 .. 0xFFC224) of a flagged cell (byte +2 bit 3).
uint16_t IsSpiceCell(uint16_t cell) {
    uint32_t c = kMap + int16_t(cell) * 4;
    uint16_t t = (rd16(c) >> 9) & 0x7F;
    if (!(rd8(c + 2) & 8)) return 0;
    return t > U16{0xFFC224} || int32_t(t) < int32_t(U16{0xFFC224}) - 0xF;
}

// 0x115D2, 0x115C6: the distance between positions (longest axis plus
// half the other), in 1/256 cells and rounded to cells.
uint16_t DistanceFine(uint32_t a, uint32_t b) {
    uint16_t dy = uint16_t(int16_t(a >> 16) - int16_t(b >> 16)), dx = uint16_t(int16_t(a) - int16_t(b));
    if (int16_t(dy) < 0) dy = uint16_t(-dy);
    if (int16_t(dx) < 0) dx = uint16_t(-dx);
    return int16_t(dy) < int16_t(dx) ? uint16_t((dy >> 1) + dx) : uint16_t(dy + (dx >> 1));
}
uint16_t Distance(uint32_t a, uint32_t b) { return uint16_t(uint16_t(DistanceFine(a, b) + 0x80) >> 8); }

// 0x1AAE0: a cell's fog edge tile from which of its four neighbours
// (0x71572) are still hidden; hidden cells get the full fog tile.
void UpdateFog(uint16_t cell) {
    cell &= 0xFFF;
    uint32_t c = kMap + cell * 4u;
    uint16_t edge = 0;
    if (!(rd8(c + 2) & 8)) {
        edge = 0xF;
    } else {
        uint16_t t = (rd16(c) >> 9) & 0x7F;
        if (!U16{0xFFC148} && (t > U16{0xFFC224} || int32_t(t) < int32_t(U16{0xFFC224}) - 0xF)) return;
        for (int k = 0; k < 4; k++) {
            int16_t o = int16_t(rd16(0x71572 + k * 2u));
            uint16_t n = uint16_t(cell + o);
            int16_t x = int16_t(int8_t(n & 0x3F)), y = int16_t(int16_t(n & 0xFC0) >> 6);
            if (x < 0 || x >= 0x40 || y < 0 || y >= 0x40 || !(rd8(kMap + (int32_t(cell) + o) * 4 + 2) & 8))
                edge |= uint16_t(1 << k);
        }
    }
    if (edge) {
        if (edge != 0xF) UnitSeenBy(UnitAt(cell), int16_t(U16{0xFFC274}));
        uint32_t t = rd32(0x4AA2C);
        edge = rd16(t + rd16(t + 0xE) * 2u + edge * 2u);
    }
    wr16(c, uint16_t((rd16(c) & 0x1FF) + ((edge & 0x7F) << 9)));
    UpdateCellSprite(cell, 0, 0);
}

// 0x1A9AC: the player uncovers a cell: units on it are seen, the
// structure marked, the fog around redrawn. 0 when nothing changed.
uint16_t RevealCell(uint16_t cell, uint16_t house) {
    if (house != U16{0xFFC274}) return 0;
    uint32_t c = kMap + int16_t(cell) * 4;
    uint16_t t = (rd16(c) >> 9) & 0x7F;
    if ((rd8(c + 2) & 8) && (t > U16{0xFFC224} || int32_t(t) < int32_t(U16{0xFFC224}) - 0x10)) return 0;
    int16_t x = int16_t(int8_t(cell & 0x3F)), y = int16_t(int16_t(cell & 0xFC0) >> 6);
    if (y < 0 || y >= 0x40 || x < 0 || x >= 0x40) return 0;
    U8{c + 2} |= 8;
    UnitSeenBy(UnitAt(cell), int16_t(house));
    if (uint32_t s = StructureAt(cell)) {
        U8{s + 9} |= uint8_t(1u << house);
        if (house == 1) U8{s + 9} |= 8;
    }
    UpdateFog(cell);
    UpdateFog(uint16_t(cell + 1));
    UpdateFog(uint16_t(cell - 1));
    UpdateFog(uint16_t(cell - 0x40));
    UpdateFog(uint16_t(cell + 0x40));
    return 1;
}

// 0x1A002: the player uncovers the cells within radius of pos.
void RevealAround(uint32_t pos, uint16_t radius) {
    uint16_t cell = PosCell(pos);
    if (!CellInMap(cell)) return;
    int16_t x = int16_t(int8_t(cell & 0x3F)), y = int16_t(int16_t(cell & 0xFC0) >> 6);
    int16_t r = int16_t(radius);
    uint32_t centre = (uint32_t(uint16_t(y)) << 16 | uint16_t(x)) << 8;
    for (int16_t dy = int16_t(-r); dy <= r; dy++) {
        int16_t yy = int16_t(y + dy);
        uint16_t row = uint16_t((int32_t(y) + dy) << 6);
        for (int16_t dx = int16_t(-r); dx <= r; dx++) {
            int16_t xx = int16_t(x + dx);
            if (xx < 0 || xx >= 0x40 || yy < 0 || yy >= 0x40) continue;
            uint16_t c = uint16_t(xx + row);
            uint32_t p = (uint32_t(uint16_t(yy)) << 16 | uint16_t(xx)) << 8;
            if (rd8(kMap + int16_t(c) * 4 + 2) & 8) continue;
            if (int16_t(Distance(centre, p)) > r) continue;
            RevealCell(c, U16{0xFFC274});
            if (!(rd16(kMap + int16_t(c) * 4) & 0xFE00)) U8{kMap + int16_t(c) * 4 + 2} |= 8;
        }
    }
}

// 0x19A90: a cell redrawn (mode 1: unless a seen structure covers it).
void UpdateCellSprite(uint16_t cell, uint16_t mode, uint16_t) {
    switch (mode) {
    case 1: {
        uint8_t f = rd8(kMap + int16_t(cell) * 4 + 2);
        if ((f & 0x20) && (f & 8)) return;
        [[fallthrough]];
    }
    case 0: case 2: case 3: RedrawCell(cell); break;
    }
}

// 0x114F0: the distance between cells (longest axis plus half the other).
uint16_t CellDistance(uint16_t a, uint16_t b) {
    int16_t dx = int16_t((a & 0x3F) - (b & 0x3F));
    if (dx < 0) dx = int16_t(-dx);
    int16_t dy = int16_t(((b >> 6) & 0x3F) - ((a >> 6) & 0x3F));
    if (dy < 0) dy = int16_t(-dy);
    return dx >= dy ? uint16_t(dx + (dy >> 1)) : uint16_t((dx >> 1) + dy);
}

// 0x1152E: the direction (by octant, table 0x115A6) from cell a to b.
uint16_t CellDirection(uint16_t a, uint16_t b) {
    uint16_t x0 = a & 0x3F, y0 = (a >> 6) & 0x3F, x1 = b & 0x3F, y1 = (b >> 6) & 0x3F;
    int16_t dx = int16_t(x1 - x0), dy = int16_t(y1 - y0);
    uint16_t k = 0;
    if (!(y1 > y0)) k += 8;
    if (!(x1 > x0)) k += 4;
    int16_t ax2 = int16_t(dx * 2), ady = dy, ady2, adx = dx;
    if (ax2 < 0) ax2 = int16_t(-ax2);
    if (ady < 0) ady = int16_t(-ady);
    if (ady > ax2) k += 2;
    ady2 = int16_t(ady * 2);
    if (ady2 < 0) ady2 = int16_t(-ady2);
    if (adx < 0) adx = int16_t(-adx);
    if (adx > ady2) k += 1;
    return rd16(0x115A6 + k * 2u);
}

// 0x11632: a position moved a distance in a direction (sines at 0x6CFAC
// and 0x6D0AC; distances over 255 lose their low byte to 0xFF).
uint32_t MovePos(uint32_t pos, uint16_t dir, uint16_t dist) {
    if (!dist) return pos;
    uint16_t d = dist > 0xFF ? uint16_t(dist | 0xFF) : dist;
    uint16_t a = dir & 0xFF;
    int16_t dy = int16_t(int16_t(uint16_t(-int8_t(rd8(0x6D0AC + a)) * int16_t(d) + 0x40)) >> 7);
    int16_t dx = int16_t(int16_t(uint16_t(int8_t(rd8(0x6CFAC + a)) * int16_t(d) + 0x40)) >> 7);
    uint16_t y = uint16_t((pos >> 16) + dy), x = uint16_t(pos + dx);
    return uint32_t(y) << 16 | x;
}

// 0x1168C: a random position within a radius (in 16ths of a cell), kept on
// the map (else the position itself); snapped to a cell centre if asked.
uint32_t ScatterPos(uint32_t pos, uint16_t radius, uint16_t snap) {
    if (!radius) return pos;
    uint16_t r = Random();
    while (radius <= r) r >>= 1;
    uint16_t a = Random();
    int16_t dx = int16_t(int16_t(int16_t(int8_t(rd8(0x6CFAC + a)) * int16_t(r)) >> 3) & 0xFFF0);
    int16_t dy = int16_t(int16_t(int16_t(-int8_t(rd8(0x6D0AC + a)) * int16_t(r)) >> 3) & 0xFFF0);
    int16_t x = int16_t(uint16_t(pos) + dx), y = int16_t(uint16_t(pos >> 16) + dy);
    if (y >= 0x3F00 || y < 0x100 || x >= 0x3F00 || x < 0x100) return pos;
    uint32_t p = uint32_t(uint16_t(y)) << 16 | uint16_t(x);
    if (snap) p = (p & 0xFF00FF00) + 0x800080;
    return p;
}

// 0x1B03E: a waypoint for a team: a cell up to 20 cells out from the
// target, towards the unit, turned a random 31-94 either way; 0 if near.
uint16_t TeamWaypoint(uint16_t from, uint16_t target) {
    if (!from || !target) return 0;
    uint16_t dist = CellDistance(from, target);
    uint16_t dir = CellDirection(target, from);
    if (int16_t(dist) <= 10) return 0;
    for (int k = 0; k < 4; k++) {
        int16_t off = int16_t((Random() & 0x3F) + 0x1F);
        if (Random() & 1) off = int16_t(-off);
        uint16_t r = uint16_t((int16_t(dist) < 0x14 ? dist : 0x14) << 8);
        uint32_t c = ((uint32_t(int32_t(int16_t(target)) & 0xFC0) << 10) + (target & 0x3F)) * 0x100 + 0x800080;
        uint32_t p1 = MovePos(c, uint16_t(dir + off), r);
        uint32_t p2 = MovePos(c, uint16_t(off + dir), r);
        uint16_t cell = uint16_t(((p2 >> 24) << 6) + ((p1 >> 8) & 0xFF));
        if (CellInMap(cell)) return cell;
    }
    return 0;
}

namespace {

uint32_t Cell(uint16_t cell) { return rd32(0x4AA24) + uint32_t(int32_t(int16_t(cell)) * 4); }

// A cell's ground tile from the tile set's spice group (0x4AA2C, group at
// word +0x12) by index.
void SetSpiceTile(uint16_t cell, uint16_t index) {
    uint32_t t = rd32(0x4AA2C);
    uint16_t tile = rd16(t + uint32_t(rd16(t + 0x12)) * 2 + uint32_t(index) * 2);
    uint32_t m = Cell(cell);
    wr16(m, uint16_t((rd16(m) & 0xFE00) + (tile & 0x1FF)));
    wr8(kTerrain + int16_t(cell), uint8_t(rd16(m)));
}

// 0x1A6F2: a spice cell's tile joined to its neighbours (0x7156A), redrawn.
void JoinSpice(uint16_t cell) {
    cell &= 0xFFF;
    uint16_t t = TerrainAt(cell);
    if (t == 8 || t == 9) {
        uint16_t mask = 0;
        for (int k = 0; k < 4; k++) {
            uint16_t n = TerrainAt(uint16_t(cell + rd16(0x7156A + k * 2u)));
            if (t == 8 ? (n == 8 || n == 9) : n == 9) mask |= uint16_t(1 << k);
        }
        SetSpiceTile(cell, uint16_t(mask + (t == 8 ? 0x31 : 0x41)));
    }
    UpdateCellSprite(cell, 0, 0);
}

} // namespace

// 0x1A51C: spice on a cell up (sand -> spice -> thick) or down a step,
// its neighbours rejoined.
void ChangeSpice(uint16_t cell, int16_t delta) {
    if (!delta) return;
    uint16_t t = TerrainAt(cell);
    if (t == 0) {
        if (delta < 0) return;
    } else if (t == 9) {
        if (delta > 0) return;
    } else if (t != 8) {
        return;
    }
    if (delta > 0) t = t == 8 ? 9 : 8;
    else t = t == 9 ? 8 : 0;
    SetSpiceTile(cell, t == 8 ? 0x31 : t == 9 ? 0x41 : 0);
    if (delta > 0) {
        JoinSpice(cell);
    } else {
        if (t == 8) wr16(Cell(cell), uint16_t((rd16(Cell(cell)) & 0xFE00) | 0xBF));
        UpdateCellSprite(cell, 0, 0);
    }
    JoinSpice(uint16_t(cell + 1));
    JoinSpice(uint16_t(cell - 1));
    JoinSpice(uint16_t(cell - 0x40));
    JoinSpice(uint16_t(cell + 0x40));
}

// 0x1ACC8: spice spread around a cell within a radius (cells at the
// radius by chance; never onto terrain 8), and on the cell itself.
void SpreadSpice(uint16_t cell, uint16_t radius) {
    if (!radius) return;
    int16_t r = int16_t(radius);
    int16_t x = int16_t(int8_t(cell & 0x3F)), y = int16_t((cell & 0xFC0) >> 6);
    for (int16_t dy = int16_t(-r); dy <= r; dy++) {
        int16_t row = int16_t((y + dy) << 6);
        for (int16_t dx = int16_t(-r); dx <= r; dx++) {
            uint16_t c = uint16_t(row + x + dx);
            int16_t d = int16_t(CellDistance(cell, c));
            if (d > r || (d == r && !(Random() & 1))) continue;
            if (TerrainAt(c) == 8) continue;
            ChangeSpice(c, 1);
        }
    }
    ChangeSpice(cell, 1);
}

// 0x1AC1C: a spice bloom bursts (unless 0xFFC148): the unit on it is
// lost, the ground restored, an explosion; the player hears it if theirs;
// spice spreads five cells round.
void SpiceBloom(uint16_t cell, uint16_t house) {
    if (!U16{0xFFC148}) {
        CrushUnit(UnitAt(cell));
        uint32_t m = Cell(cell);
        wr16(m, uint16_t((rd16(m) & 0xFE00) + rd8(kTerrain + int16_t(cell))));
        uint32_t pos = ((uint32_t(int32_t(int16_t(cell)) & 0xFC0) << 10) + (cell & 0x3F)) * 0x100 + 0x800080;
        SpawnEffect(0x13, pos, 0, 0);
    }
    if (house == U16{0xFFC274}) PlayVoice(0x24);
    SpreadSpice(cell, 5);
}

namespace {

// 0x2760C: a crater on a cell (map word bits 9-15): fresh ones start at 7
// (1 on rock types 4, 5) plus chance, others deepen by 2 up to the limits.
void Crater(uint16_t cell) {
    uint32_t m = Cell(cell);
    uint16_t d = (rd16(m) >> 9) & 0x7F;
    switch (d) {
    case 5:
    case 6:
    case 11:
    case 12: return;
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 8:
    case 9:
    case 10: d = uint16_t(d + 2); break;
    default: {
        d = 7;
        uint16_t t = TerrainAt(cell);
        if (t == 4 || t == 5) d = 1;
        d = uint16_t(d + int8_t(Random() & 1));
        break;
    }
    }
    wr16(m, uint16_t((rd16(m) & 0x1FF) + ((d & 0x7F) << 9)));
    RedrawCell(cell);
}

} // namespace

// 0x273EE: an explosion marks the ground of a seen cell: spice thins
// (thick spice nearby too), a bloom bursts, type 10 is restored, others
// get a crater (unless flagged).
void Scorch(uint32_t pos) {
    uint16_t cell = PosCell(pos);
    if ((cell & 0xFFF) != cell || !IsSpiceCell(cell)) return;
    uint16_t t = TerrainAt(cell);
    if (rd16(0xC7F38 + uint16_t(rd16(0x6CCC6 + uint16_t(t * 28)) * 2)) == 0xFFFF) return;
    uint16_t over = (rd16(Cell(cell)) >> 9) & 0x7F;
    if (int32_t(over) > int32_t(U16{0xFFC224}) - 0x10 && over <= U16{0xFFC224}) return;
    switch (t) {
    case 8: {
        static const int16_t near[4] = {1, -1, 0x40, -0x40};
        for (int16_t d : near)
            if (TerrainAt(uint16_t(cell + d)) == 9) ChangeSpice(uint16_t(cell + d), -1);
        if (over != 9) ChangeSpice(cell, -1);
        break;
    }
    case 9: ChangeSpice(cell, -1); break;
    case 10: {
        uint32_t m = Cell(cell);
        wr16(m, uint16_t((rd16(m) & 0xFE00) + rd8(kTerrain + int16_t(cell))));
        RedrawCell(cell);
        break;
    }
    case 12:
    case 13: break;
    case 14: SpiceBloom(cell, U16{0xFFC274}); break;
    default:
        if (!(rd8(Cell(cell) + 2) & 0x20)) Crater(cell);
        break;
    }
}

namespace {

// A free spice cell (no object on it, not a bloom/edge tile) for a
// harvester: plain spice prefers thick spice next to it (0x1A4B6, offsets
// at 0x714FA).
uint16_t SpiceTarget(uint16_t c) {
    if (!CellInMap(c)) return 0;
    uint32_t m = Cell(c);
    if (rd8(m + 3)) return 0;
    uint16_t t = TerrainAt(c);
    if (t == 8 && (rd16(m) & 0x1FF) != 0xBF) {
        uint32_t o = 0x714FA;
        for (int16_t n = 0x18; n > 0; n--, o += 4) {
            uint16_t d = uint16_t(c + rd16(o) + rd16(o + 2));
            if (!CellInMap(d)) continue;
            uint32_t dm = Cell(d);
            if (rd8(dm + 3) || (rd16(dm) & 0x1FF) == 0xCF || TerrainAt(d) != 9) continue;
            return d;
        }
        return c;
    }
    if (t == 9 && (rd16(m) & 0x1FF) != 0xCF) return c;
    return 0;
}

} // namespace

// 0x1A14A: spice for a unit within a radius, searched ring by ring out
// from a cell inside the map's bounds (0x6CE82 by scale); each unit's
// search (0xFFC650: radius, ring) resumes where it stopped.
uint16_t FindSpice(uint16_t cell, uint16_t radius, uint16_t unit) {
    uint32_t st = 0xFFC650 + uint32_t(int32_t(int16_t(unit)) * 2);
    if (int8_t(rd8(st)) != int16_t(radius)) {
        wr8(st, uint8_t(radius));
        wr8(st + 1, 0);
    }
    uint32_t lim = 0x6CE82 + uint32_t(U16{0xFFC068} << 3);
    int16_t left = int16_t(rd16(lim)), top = int16_t(rd16(lim + 2));
    int16_t right = int16_t(left + int16_t(rd16(lim + 4)) - 1), bottom = int16_t(top + int16_t(rd16(lim + 6)) - 1);
    for (; int8_t(rd8(st + 1)) < int8_t(rd8(st)); U8{st + 1} += 1) {
        int16_t r = int8_t(rd8(st + 1));
        int16_t cx = int8_t(cell & 0x3F), cy = int16_t((cell & 0xFC0) >> 6);
        int16_t x0 = int16_t(cx - r), x1 = int16_t(cx + r), y0 = int16_t(cy - r), y1 = int16_t(cy + r);
        if (x0 <= left) x0 = left;
        if (x1 >= right) x1 = right;
        if (y0 <= top) y0 = top;
        if (y1 >= bottom) y1 = bottom;
        for (int16_t y = y0; y <= y1; y++) {
            auto check = [&](int16_t x) {
                uint16_t c = uint16_t((y << 6) + x);
                uint16_t found = SpiceTarget(c);
                if (found) wr8(st, 0);
                return found;
            };
            if (y == y0 || y == y1) {
                for (int16_t x = x0; x <= x1; x++)
                    if (uint16_t f = check(x)) return f;
            } else {
                if (uint16_t f = check(x0)) return f;
                if (uint16_t f = check(x1)) return f;
            }
        }
    }
    wr8(st, 0);
    return 0;
}

// 0xEDB2: a wall cell's tile joined to the walls (type 0xB) and wall
// foundations (0xD) beside it (0x6BA74), the neighbouring walls first when
// asked. The joining table (0xFFD5B4) is built on first use from 0x6BA7C
// and 0x6BB7C. 1 when the tile changed.
uint16_t ConnectWalls(uint16_t cell, uint16_t neighbours) {
    constexpr uint32_t table = 0xFFD5B4;
    if (!U16{0xFFD5B0}) {
        U16{0xFFD5B0} = 1;
        for (int i = 0; i < 0x100; i++) wr8(table + i, rd8(0x6BA7C + i));
        for (int i = 0; i < 0x4A; i++) wr8(table + rd8(0x6BB7C + i), uint8_t(i));
        wr8(table + 0xA, 1);
        wr8(table + 8, 1);
        wr8(table + 1, 3);
        wr8(table + 5, 3);
        wr8(table + 0x22, 0xD);
        wr8(table + 0x88, 0xE);
        wr8(table + 0x44, 0x11);
    }
    uint16_t mask = 0;
    bool foundation = TerrainAt(cell) == 0xD;
    for (int k = 0; k < 4; k++) {
        uint16_t n = uint16_t(cell + rd16(0x6BA74 + k * 2u));
        if (neighbours && TerrainAt(n) == 0xB) ConnectWalls(n, 0);
        if (foundation) continue;
        if (TerrainAt(n) == 0xB) mask |= uint16_t(1 << k);
        if (TerrainAt(n) == 0xD) mask |= uint16_t(1 << k);
    }
    if (foundation) return 0;
    uint16_t tile = uint16_t(U16{0xFFC234} + rd8(table + mask) + 1);
    uint32_t m = Cell(cell);
    if ((rd16(m) & 0x1FF) == tile) return 0;
    wr16(m, uint16_t((rd16(m) & 0xFE00) + (tile & 0x1FF)));
    UpdateCellSprite(cell, 0, 0);
    return 1;
}

} // namespace game
