// The radar: one row of the map drawn per frame into the radar tiles, after
// the static animation (0xFFBF6E frame list, 0xFFBF72 its timer) when it is
// switched on. 0xFFBF6A the row counter, 0xFFBF6C radar on (else only the
// player's structures show), 0xFFBF51 zoomed in (the middle 32x32 cells).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr uint32_t kMap = 0xFF7D9C;
constexpr uint32_t kRow = 0xFFF3B0;

// 0x547C: the row of colour bytes packed to pixels, edged in the house's
// colour (0x54C4); zoomed rows double each cell.
void PackRow(uint32_t buf) {
    uint8_t edge = rd8(0x54C4 + player_house);
    if (rd8(0xFFBF51)) {
        for (int i = 0; i < 32; i++) wr8(buf + i, uint8_t(rd8(buf + i) | rd8(buf + i) << 4));
        wr8(buf + 31, uint8_t((rd8(buf + 31) & 0xF0) | edge));
        wr8(buf, uint8_t((rd8(buf) & 0x0F) | edge << 4));
        return;
    }
    wr8(buf, edge);
    wr8(buf + 0x3F, edge);
    for (int i = 0; i < 32; i++) wr8(buf + i, uint8_t(rd8(buf + 2 * i) << 4 | rd8(buf + 2 * i + 1)));
}

// The first cell of a radar row and the cell count minus one.
uint32_t RowStart(uint16_t& row, uint16_t& n) {
    uint32_t c = kMap;
    n = 0x3F;
    if (rd8(0xFFBF51)) {
        c += 0x1040;
        row >>= 1;
        n = 0x1F;
    }
    return c + uint32_t(row) * 0x100;
}

} // namespace

void DrawRadarRow();

// 0x5162: the static animation over; the radar frame sprite back.
void EndRadarStatic() {
    U32{0xFFBF6E} = 0;
    uint32_t n = Node(U16{0xFFBF58});
    wr32(n + 8, 0x6A842);
    U16{n + 6} &= 0x9FFF;
    U16{n + 6} |= rd16(0x4E6A + player_house * 2u);
    U16{0xFFBF72} = 0;
}

// 0x5128
void UpdateRadar() {
    if (uint32_t p = U32{0xFFBF6E}) {
        U16{0xFFBF72} -= U16{0xFFFFFE};
        if (S16{0xFFBF72} > 0) return;
        U16{0xFFBF72} = uint16_t(int8_t(rd8(p)));
        uint32_t frame = rd32(p);
        if (frame) {
            U32{0xFFBF6E} = p + 4;
            QueueDma(2, frame, 0xD0000100);
            return;
        }
        EndRadarStatic();
    }
    DrawRadarRow();
}

// 0x51A0: the next radar row.
void DrawRadarRow() {
    uint16_t row = U16{0xFFBF6A} & 0x3F;
    if (row == 0 || row >= 0x3F) {
        uint32_t colour = rd32(0x5440 + player_house * 4u);
        for (int k = 0; k < 8; k++) wr32(kRow + k * 4u, colour);
        RadarColumn(kRow);
        U16{0xFFBF6A} += 1;
        return;
    }
    uint16_t n;
    uint32_t c = RowStart(row, n);
    for (uint16_t i = 0; i <= n; i++, c += 4) {
        int8_t colour;
        if (rd8(0xFFBF6C)) {
            uint16_t w = rd16(c);
            colour = int8_t(rd8(0x525A + ((w >> 9) & 0x7F)));
            if (colour <= 0) {
                uint32_t o = 0;
                if (uint8_t idx = rd8(c + 3)) o = (rd8(c + 2) & 0x10) ? UnitGet(idx - 1) : StructureGet(idx - 1);
                colour = o ? int8_t(rd8(0x5252 + rd8(o + 8))) : int8_t(rd8(0x525A + (w & 0x1FF)));
            }
        } else {
            colour = 0xC;
            if ((rd8(c + 2) & 0x20) && rd8(c + 3)) {
                uint32_t s = StructureGet(rd8(c + 3) - 1);
                if (s && rd8(s + 8) == player_house) colour = int8_t(rd8(0x5252 + rd8(s + 8)));
            }
        }
        wr8(kRow + i, uint8_t(colour));
    }
    PackRow(kRow);
    RadarColumn(kRow);
    U16{0xFFBF6A} += 1;
}

// 0x54CC: the whole radar redrawn at once.
void RefreshRadar() {
    if (!U16{0xFFBF58}) return;
    EndRadarStatic();
    DrawRadarRow();
    for (int i = 0; i <= 0x40; i++) UpdateRadar();
}

namespace {

// 0x4E2E, 0x50C0: the radar switched on, with the static animation or
// without; 0x4F44 off again, with the closing animation.
void RadarOn() {
    U8{0xFFBF6C} = 0xFF;
    U32{0xFFBF6E} = 0x68C1E;
    U16{0xFFBF72} = 0;
    uint32_t n = Node(U16{0xFFBF58});
    wr32(n + 8, 0x68C6E);
    U16{n + 6} |= rd16(0x4E6A + player_house * 2u);
    U16{0xFFBF6A} = 0;
}
void RadarOnQuiet() {
    U8{0xFFBF6C} = 0xFF;
    U16{0xFFBF6A} = 0;
}
void RadarOff() {
    U32{0xFFBF6E} = 0x68BD2;
    U16{0xFFBF72} = 0;
    uint32_t n = Node(U16{0xFFBF58});
    wr32(n + 8, 0x68C6E);
    U16{n + 6} |= rd16(0x4F3C + player_house * 2u);
    ClearRadar();
    U8{0xFFBF6C} = 0;
    UpdateRadarFrame();
}

} // namespace

// 0x1A7FE: the player's radar runs while an outpost (structure bit 18)
// has power; switched on (1) or off (2) as that changes (0xFFBF7C: 1 with
// the animation, 2 quietly). 1 when it was switched on.
uint16_t UpdateRadarStatus(uint16_t house) {
    if (house != player_house) return 0;
    uint32_t h = HouseGet(house);
    bool outpost = rd32(h + 0xE) & (1u << 18);
    bool low = int16_t(rd16(h + 0x1C)) > int16_t(rd16(h + 0x1A));
    int state = 0;
    if (rd16(h + 4) & 0x10) {
        if (!outpost || low) state = 2;
        else if (U8{0xFFBF7C} == 2) state = 1;
    } else if (outpost && !low) {
        state = 1;
    }
    if (state == 1) {
        if (U8{0xFFBF7C} == 1) {
            RadarOn();
            PlaySoundEffect(0x3E);
        }
        if (U8{0xFFBF7C} == 2) RadarOnQuiet();
        U8{h + 5} |= 0x10;
    } else if (state == 2) {
        if (U8{0xFFBF7C}) {
            PlaySoundEffect(0x3E);
            RadarOff();
        }
        U8{h + 5} &= uint8_t(~0x10);
    }
    return state == 1;
}

} // namespace game
