// The player's input in the game loop (0xE264): the side menu, orders for
// the selected unit (mode 1) and structure placement (mode 2).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 game_mode{0xFFC264};
constexpr U16 player_house{0xFFC274};

} // namespace

// 0x2E184: a target word for an index of a kind (1 unit if on the map, 2
// structure, 3 cell as its centre in half cells).
uint16_t MakeTarget(uint16_t index, uint16_t kind) {
    switch (kind) {
    case 1: return (rd16(UnitGet(index) + 4) & 2) ? uint16_t(index | 0x4000) : 0;
    case 2: return uint16_t(index | 0x8000);
    case 3: return uint16_t(((index & 0x3F) * 2 + 1) | (((index & 0xFC0) << 2) + 0x80) | 0xC000);
    default: return 0;
    }
}

// 0x48D2E: outside placement, a cell near a unit (offsets at 0xFEEC8)
// snaps to that unit's cell.
uint16_t SnapToUnit(uint16_t cell) {
    if (game_mode == 2 || StructureAt(cell) || TerrainAt(cell) == 0xE) return cell;
    for (int k = 0; k < 9; k++)
        if (uint32_t u = UnitAt(uint16_t(cell + int16_t(rd16(0xFEEC8 + k * 2u))))) return PosCell(rd32(u + 0xA));
    return cell;
}

// 0x4C8C: input (unless frozen: 0xFFBE9C), the button edges latched
// into 0xFFBF2B, then the player's unit tick.
void UpdateWorld(uint32_t) {
    if (!rd8(0xFFBE9C)) {
        HandleInput();
        wr8(0xFFBF2B, rd8(0xFFBF2A));
        wr8(0xFFBF2A, 0);
    }
    UpdatePanel();
}

// 0xE264
uint16_t HandleInput() {
    uint16_t act = SideMenu();
    uint16_t cell = U16{0xFFC244};
    if (act && game_mode == 1) {
        if (U16{0xFFBF74}) cell = U16{0xFFBF74};
        U16{0xFFBF74} = 0;
        if (U32{0xFFC1A0}) {
            OrderSpecial(cell);
            return 1;
        }
        uint16_t order = U16{0xFFC190};
        uint32_t u = U32{0xFFC18C};
        uint32_t t = TargetUnit(ObjectTarget(u));
        if (t) {
            uint32_t s = TargetStructure(rd16(t + 0x5A));
            int16_t k = int16_t(int8_t(rd8(u + 2)) - 7);
            if (k < 0 || k > 10) t = 0;
            else if (!((k == 9 && rd8(s + 2) == 0xC) || rd8(s + 2) == 0xD)) t = 0;
        }
        if (!t) {
            if (rd8(u + 2) != 0x10) wr16(u + 0x5A, 0);
            wr16(u + 0x5C, 0);
            wr8(u + 0x7C, 0xFF);
            uint16_t target;
            if (order == 1 || order == 5 || TerrainAt(cell) >= 0xB) target = MakeTarget(cell, 3);
            else target = MakeTarget(SnapToUnit(cell), 3);
            if (order == 6) target = 0;
            SetUnitAction(U32{0xFFC18C}, int16_t(order));
            if (order == 1) UnitMoveTo(u, target);
            else if (order == 5) wr16(U32{0xFFC18C} + 0x5C, target);
            else UnitAttack(u, target);
        }
        U32{0xFFC18C} = 0;
        U16{0xFFC190} = 0xFFFF;
        SetGameMode(3);
        U32{0xFFC25C} = 0;
        return 1;
    }
    if (act && game_mode == 2) {
        if (!PlaceStructure(U32{0xFFC174}, U16{0xFFC248})) return 1;
        StructurePlaced();
        uint32_t s = U32{0xFFC174};
        if (rd8(s + 2) == 2) wr32(HouseGet(uint16_t(int8_t(rd8(s + 8)))) + 0x22, rd32(s + 0xA));
        if (U16{0xFFC170} == 0xC && !U16{0xFFC148}) {
            U16{0xFFC148} += 1;
            uint16_t dest = uint16_t(MakeTarget(PosCell(rd32(s + 0xA)), 3) + 0x103);
            uint32_t h = DeliverUnit(player_house, 0x10, dest);
            U16{0xFFC148} -= 1;
            if (h) wr16(h + 0x52, MakeTarget(rd16(U32{0xFFC174}), 2));
        }
        SetGameMode(4);
        if (uint32_t f = StructureAt(U16{0xFFC188}))
            if (!(BuildableMask(f) & (1u << rd16(f + 0x52)))) StructureBuild(f, -2);
        U16{0xFFC170} = 0xFFFF;
        U32{0xFFC174} = 0;
        U16{0xFFC180} = 0;
        UpdateRadarStatus(player_house);
        return 1;
    }
    if (game_mode == 1) SetCursorAt(SnapToUnit(cell));
    else if (game_mode == 2) SetCursorAt(cell);
    return 1;
}

} // namespace game
