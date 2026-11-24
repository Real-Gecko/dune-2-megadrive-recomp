// The in-game panel: the credits counter (0xFFC058 shown, 0xFFD6C0
// counting towards the house's credits, 0xFFD6BC its speed) and its
// low-credits blink.
#include "game.hpp"

namespace game {

namespace {

constexpr U32 shown{0xFFC058};
constexpr U32 counter{0xFFD6C0};
constexpr U32 speed{0xFFD6BC};

// 0x99AC: the credits sprite blinks while low (on), else stays.
void CreditsBlink(bool on) {
    uint32_t n = Node(U16{0xFFC862});
    if (!on) {
        U16{0xFFC864} = 0x5A;
        U16{0xFFC866} = 0;
        U8{n + 7} |= 1;
        U8{n + 7} |= 0x80;
        return;
    }
    if (!(rd8(n + 7) & 1)) return;
    uint16_t d = U16{0xFFFFFE};
    if (S16{0xFFC866} >= 0) {
        U16{0xFFC866} -= d;
        return;
    }
    U16{0xFFC864} -= d;
    if (S16{0xFFC864} >= 0) {
        U8{n + 7} &= 0x7F;
        return;
    }
    U16{0xFFC866} = 0x384;
    U16{0xFFC864} = 0x3C;
    U8{n + 7} |= 0x80;
}

// 0xA5DA: the credits digits (cached at 0xFFC85A) into the tiles at
// 0xF6C0, uploading only those that changed.
void DrawCredits(uint32_t str) {
    uint32_t cache = 0xFFC85A;
    uint16_t vram = 0xF6C0;
    while (uint8_t c = rd8(str)) {
        if (c != rd8(cache)) break;
        str++;
        cache++;
        vram = uint16_t(vram + 0x20);
    }
    while (uint8_t c = rd8(str++)) {
        wr8(cache++, c);
        uint16_t t = uint16_t(c - 0x20);
        if (t) t = uint16_t((t - 0xF) << 5);
        QueueDma(2, 0x50AD4 + t, uint32_t(vram) << 16 | 0x10);
        vram = uint16_t(vram + 0x20);
    }
}

} // namespace

// 0x11F38: the credits counter rolls towards a house's credits once per
// tick (mode 2 snaps, 3 shows the target), clicking as it goes.
void UpdateHouse(uint16_t house, uint16_t mode) {
    if (U32{0xFFC594} < U32{0xFFD6B8} && !mode) return;
    U32{0xFFD6B8} = U32{0xFFC594} + 1;
    uint32_t h = HouseGet(house);
    uint32_t credits = rd32(h + 0x12);
    if (shown >= 0x32) CreditsBlink(false);
    else if (shown == credits) CreditsBlink(true);
    if (shown == credits) return;
    md::Frame f(0x28);
    if (mode == 2) {
        shown = credits;
        counter = 0;
    }
    if (mode == 3) {
        counter = uint32_t(shown);
        Sprintf(f.base, 0x11F30, {L(counter)});
        DrawCredits(f.base);
        return;
    }
    if (!mode && counter == credits) return;
    int16_t d = int16_t(credits - counter);
    if (d) {
        d = int16_t(d >> 2);
        if (!d) d = 1;
        if (d > 0x80) d = 0x80;
        if (d < -0x80) d = -0x80; // moveq #$80 sign-extends
        speed = uint32_t(int32_t(speed) + d);
    } else {
        speed = 0;
    }
    if (int32_t(speed) < 0 && !counter) speed = 0;
    counter = uint32_t(int32_t(counter) + (int32_t(speed) >> 3));
    if (int32_t(speed) < 0) counter = counter + 1;
    if (int32_t(speed) > 0) speed = speed & 7;
    if (int32_t(speed) < 0) speed = speed | 0xFFF8;
    if (int32_t(counter) < 0) {
        counter = 0;
        if (rd32(h + 0x12) > 100000000) wr32(h + 0x12, 0);
    }
    int32_t lo = int32_t(counter), hi = lo;
    if (int32_t(speed) < 0) lo = lo - 1 < 0 ? 0 : lo - 1;
    if (int32_t(speed) > 0) hi++;
    shown = uint32_t(lo);
    Sprintf(f.base, 0x11F34, {L(uint32_t(hi))});
    DrawCredits(f.base);
    if (!(U16{0xFFD6B4} & 3)) PlaySoundEffect(d > 0 ? 0x34 : 0x35);
    U16{0xFFD6B4} += 1;
}

// 0x11E7C: the tactical cell (0xFFC248) set from a cell: in placement mode
// (2) also whether the building fits; a structure snaps to its origin.
void SetCursorAt(uint16_t cell) {
    if (U16{0xFFC264} == 1) return;
    if (U16{0xFFC264} == 2) {
        U16{0xFFC180} = uint16_t(CanPlace(cell, U16{0xFFC170}));
    } else if (((rd16(rd32(0x4AA24) + int16_t(cell) * 4) >> 9) & 0x7F) != U16{0xFFC224}) {
        if (uint32_t s = StructureAt(cell)) cell = PosCell(rd32(s + 0xA));
        UnitAt(cell);
    }
    U16{0xFFC248} = cell;
}

} // namespace game
