// The path search (0x12AB2): a straight line towards the goal; at an
// obstacle, its far side along the line is found and the wall followed
// both ways round (0x12D5E), the cheaper kept; followed paths are then
// smoothed (0x12E66). Paths are direction bytes (0-7), 0xFE removed, 0xFF
// the end. The result record at 0xFFD6E8: +0 start cell, +2 cost, +4
// length, +6 the buffer.
#include "game.hpp"

namespace game {

namespace {

// 0x12A90: the next cell in a direction (offsets at 0x12AA0).
uint16_t Step(uint16_t cell, uint16_t dir) { return uint16_t(cell + rd16(0x12AA0 + uint16_t(dir * 2))); }

uint16_t Dir8(int v) { return uint16_t(v & 7); }

// 0x12E66: removes detours from a followed path (opposite steps cancel,
// turns are cut where the cell is passable), then recounts its cost.
uint16_t Smooth(uint32_t rec, PathCostFn cost, int16_t limit) {
    uint32_t buf = rd32(rec + 6);
    if (!rec || !buf || !cost) return 0;
    wr8(buf + uint32_t(int32_t(int16_t(rd16(rec + 4)))), 0xFF);
    uint16_t cell = rd16(rec);
    if (int16_t(rd16(rec + 4)) > 1) {
        uint32_t a = buf + 1;
        while (rd8(a) != 0xFF) {
            uint32_t b = a;
            do b--;
            while (rd8(b) == 0xFE && b != buf);
            if (rd8(b) == 0xFE) {
                a++;
                continue;
            }
            int8_t d = int8_t(rd8(a) - rd8(b));
            if (d < 0) d = int8_t(d + 8);
            d = int8_t(rd8(0x6C750 + d));
            if (d == 3) {
                wr8(b, 0xFE);
                wr8(a, 0xFE);
                a++;
                continue;
            }
            if (d == 0) {
                cell = Step(cell, rd8(b));
                a++;
                continue;
            }
            uint16_t nd;
            if (rd8(b) & 1) {
                nd = Dir8(rd8(b) + (d < 0 ? -1 : 1));
                if ((d < 0 ? -d : d) == 1) {
                    if (int16_t(cost(Step(cell, nd), nd)) <= limit) {
                        wr8(a, uint8_t(nd));
                        wr8(b, uint8_t(nd));
                    }
                    cell = Step(cell, rd8(b));
                    a++;
                    continue;
                }
            } else {
                nd = Dir8(rd8(b) + d);
            }
            wr8(a, uint8_t(nd));
            wr8(b, 0xFE);
            while (rd8(b) == 0xFE && b != buf) b--;
            if (rd8(b) == 0xFE) cell = rd16(rec);
            else cell = Step(cell, Dir8(rd8(b) + 4));
        }
    }
    uint32_t out = buf, in = buf;
    cell = rd16(rec);
    wr16(rec + 2, 0);
    wr16(rec + 4, 0);
    for (; rd8(in) != 0xFF; in++) {
        if (rd8(in) == 0xFE) continue;
        cell = Step(cell, rd8(in));
        U16{rec + 2} += cost(cell, rd8(in));
        U16{rec + 4} += 1;
        wr8(out++, rd8(in));
    }
    U16{rec + 4} += 1;
    wr8(out, 0xFF);
    return rd16(rec + 4);
}

// 0x12D5E: follows the wall turning one way (turn +-1) from the record's
// start until the target cell; 1 when reached (the record then holds the
// smoothed path).
uint16_t Follow(uint16_t target, uint32_t rec, int16_t turn, uint16_t dir, PathCostFn cost, int16_t limit) {
    if (!rec || !cost) return 0;
    uint16_t cell = rd16(rec);
    uint32_t p = rd32(rec + 6);
    int16_t n = 0;
    for (;;) {
        uint16_t d = dir, next;
        for (;;) {
            d = Dir8(d + turn);
            if ((d & 1) && Step(cell, Dir8(d + turn)) == target) {
                d = Dir8(d + turn);
                next = Step(cell, d);
                break;
            }
            if (d == dir) return 0;
            next = Step(cell, d);
            if (int16_t(cost(next, d)) <= limit) break;
        }
        wr8(p++, uint8_t(d));
        n++;
        if (next == target) {
            wr8(p, 0xFF);
            wr16(rec + 4, uint16_t(n));
            Smooth(rec, cost, limit);
            U16{rec + 4} -= 1;
            return 1;
        }
        if (next == rd16(rec)) return 0;
        dir = Dir8(d - 3 * turn);
        cell = next;
        if (n >= 0x80) return 0;
    }
}

} // namespace

// 0x12AB2
uint32_t FindPath(uint16_t from, uint16_t to, uint32_t buf, uint16_t max, PathCostFn cost, uint16_t limit_) {
    if (!buf || !cost) return 0;
    int16_t limit = int16_t(limit_);
    constexpr uint32_t res = 0xFFD6E8;
    U16 len{res + 4};
    wr16(res, from);
    wr16(res + 2, 0);
    wr32(res + 6, buf);
    wr8(buf, 0xFF);
    len = 0;
    int16_t room = int16_t(int16_t(max) - 1);
    md::Frame frame(0x140);
    uint32_t rec_a = frame.base, rec_b = frame.base + 0xA, buf_b = frame.base + 0x14, buf_a = frame.base + 0x96;
    auto put = [&](uint8_t d) {
        wr8(buf + uint32_t(int32_t(int16_t(len))), d);
        len += 1;
    };
    // One step along the line from a cell towards the goal.
    auto along = [&](uint16_t& c, uint16_t& d) {
        d = uint16_t(int16_t(CellDirection(c, to)) >> 5);
        c = Step(c, d);
        return uint16_t(cost(c, d));
    };
    uint16_t cur = from;
    while (room > int16_t(len)) {
        if (cur == to) break;
        uint16_t dir = (CellDirection(cur, to) >> 5) & 7;
        uint16_t next = Step(cur, dir);
        uint16_t c = cost(next, dir);
        if (int16_t(c) <= limit) {
            put(uint8_t(dir));
            U16{res + 2} += c;
            cur = next;
            continue;
        }
        if (next == to) break;
        uint16_t ra, rb;
    far_side:
        // The obstacle's far side along the line; a blocked diagonal step
        // with both side cells open moves to the side.
        for (;;) {
            if (next == to) goto done;
            uint16_t d;
            if (int16_t(along(next, d)) <= limit) break;
            if (!(d & 1)) continue;
            uint16_t da = Dir8(d + 3), db = Dir8(d + 5);
            if (int16_t(cost(Step(next, da), da)) > limit) continue;
            if (int16_t(cost(Step(next, db), db)) > limit) continue;
            next = Step(next, da);
            break;
        }
        wr32(rec_a + 6, buf_a);
        wr16(rec_a + 2, 0);
        wr16(rec_a + 4, 0);
        wr16(rec_a, cur);
        ra = Follow(next, rec_a, -1, dir, cost, limit);
        wr32(rec_b + 6, buf_b);
        wr16(rec_b + 2, 0);
        wr16(rec_b + 4, 0);
        wr16(rec_b, cur);
        rb = Follow(next, rec_b, 1, dir, cost, limit);
        if (!ra && !rb) {
            // Neither way round: on past the next obstacle along the line.
            for (;;) {
                if (next == to) goto done;
                uint16_t d;
                if (int16_t(along(next, d)) > limit) goto far_side;
            }
        }
        {
            uint32_t pick = rec_a;
            if (rb) pick = (ra && int16_t(rd16(rec_a + 2)) < int16_t(rd16(rec_b + 2))) ? rec_a : rec_b;
            int16_t n = int16_t(rd16(pick + 4));
            int32_t left = int32_t(room) - int32_t(int16_t(len));
            if (int32_t(n) >= left) n = int16_t(room - int16_t(len));
            if (n <= 0) break;
            MemCpy(buf + uint32_t(int32_t(int16_t(len))), rd32(pick + 6), n);
            len += uint16_t(n);
            U16{res + 2} += rd16(pick + 2);
        }
        cur = next;
    }
done:
    if (room > int16_t(len)) put(0xFF);
    return res;
}

} // namespace game
