// Starting a mission (0x26540): the world reset, the scenario file
// SCEN<house><nnn>.INI (a word stream: high byte a section, low byte a key)
// read into houses, teams, units and structures, then the screen set up.
//
// RAM: 0xFFC068 the map scale (0..2; positions in the file add the word of
// 0x714A8 for it); 0xFFC238 the view cell, 0xFFC244 the tactical cell;
// 0xFFC0A2 16 reinforcements of 10 bytes; 0xFFC2A0 the "choam" words;
// 0xFFC71C/0xFFC720 the bloom and field cell lists; 0xFF7D9C the map,
// 64x64 cells of 4 bytes (word: tile & 0x1FF, flags; byte +2 flags,
// byte +3 object index + 1).
#include "game.hpp"

namespace game {

namespace {

constexpr U16 campaign{0xFFC04C};
constexpr U16 scenario{0xFFC050};
constexpr U16 player_house{0xFFC274};
constexpr U32 player_house_ptr{0xFFC278};
constexpr U16 map_scale{0xFFC068};
constexpr U16 view_cell{0xFFC238};
constexpr U16 tactical_cell{0xFFC244};
constexpr U32 bloom_list{0xFFC71C};
constexpr U32 field_list{0xFFC720};
constexpr U32 houses{0xFFDC24};
constexpr U16 house_count{0xFFDC08};
constexpr uint32_t kHouseList = 0xFFDC0C;
constexpr uint32_t kMap = 0xFF7D9C;

uint16_t ScaleOffset() { return rd16(0x714A8 + map_scale * 2u); }

uint32_t CopyString(uint32_t dst, uint32_t src) {
    while (uint8_t c = rd8(src++)) wr8(dst++, c);
    wr8(dst, 0);
    return dst;
}

// 0x1641C: [BASIC]/[MAP] keys.
uint32_t ScenarioBasic(uint32_t p, uint16_t key) {
    switch (key) {
    case 0: case 1: case 2: {
        static const uint32_t dst[3] = {0xFFC088, 0xFFC07A, 0xFFC06C};
        uint16_t len = rd16(p);
        p += 2;
        CopyString(dst[key], p);
        p += len;
        break;
    }
    case 3: U16{0xFFC06A} = rd16(p); p += 2; break;
    case 4: map_scale = rd16(p); p += 2; break;
    case 5: {
        uint16_t c = uint16_t(rd16(p) + ScaleOffset());
        p += 2;
        tactical_cell = c;
        U16{0xFFC248} = c;
        break;
    }
    case 6: {
        uint16_t c = uint16_t(rd16(p) + ScaleOffset());
        p += 2;
        view_cell = c;
        U16{0xFFC23C} = c;
        break;
    }
    case 7: U16{0xFFC062} = rd16(p); p += 2; break;
    case 8: U16{0xFFC060} = rd16(p); p += 2; break;
    }
    return p;
}

// 0x16504: the map: bloom cells, field cells, and the seed it grows from.
uint32_t ScenarioMap(uint32_t p, uint16_t key) {
    switch (key) {
    case 'B':
        bloom_list = p;
        p += 2 + int32_t(int16_t(rd16(p))) * 2;
        break;
    case 'F':
        field_list = p;
        p += 2 + int32_t(int16_t(rd16(p))) * 2;
        break;
    case 'S': {
        int32_t seed = int16_t(rd16(p));
        p += 2;
        U32{0xFFC064} = uint32_t(seed);
        GenerateMap(uint32_t(seed));
        uint16_t x = uint16_t(int8_t(tactical_cell & 0x3F) - 4);
        uint16_t y = uint16_t((tactical_cell & 0xFC0) - 0xC0);
        view_cell = uint16_t(x | y);
        U16{0xFFC23C} = view_cell;
        SetMapBounds(0x6CE82 + map_scale * 8u);
        SetRadarMode(map_scale);
        break;
    }
    }
    return p;
}

// 0x165B0: a house section: Brain (Human marks the player), Credits,
// Max units, Quota; four keys.
uint32_t ScenarioHouse(uint32_t p, uint16_t section, uint16_t key) {
    uint16_t id = rd16(0x714A8 + section * 2u);
    uint32_t h = HouseGet(id);
    uint16_t brain = 0;
    for (int i = 0; i < 4; i++) {
        switch (key) {
        case 'B': brain = rd16(p); p += 2; break;
        case 'C': wr32(h + 0x12, uint32_t(int32_t(int16_t(rd16(p))))); p += 2; break;
        case 'M': wr16(h + 8, rd16(p)); p += 2; break;
        case 'Q': wr16(h + 0x20, rd16(p)); p += 2; break;
        }
        key = rd16(p) & 0xFF;
        p += 2;
    }
    p -= 2;
    if (uint8_t(brain) == 'H') {
        U8{h + 5} |= 2;
        player_house = id;
        player_house_ptr = h;
        U32{0xFFC054} = rd32(h + 0x12);
    } else {
        U16{0xFFC27C} = id;
    }
    return p;
}

// 0x16654: a team: house, AI (names at 0x6CE6E), movement (names at
// 0x6CE56), min and max units.
uint32_t ScenarioTeam(uint32_t p) {
    uint16_t house = rd16(p), ai_key = rd16(p + 2);
    p += 4;
    int16_t ai = -1;
    for (int i = 0; i < 5; i++)
        if (rd8(rd32(0x6CE6E + i * 4u)) == uint8_t(ai_key)) {
            ai = int16_t(i);
            break;
        }
    uint16_t move_key = rd16(p);
    p += 2;
    int16_t move = -1;
    for (int i = 0; i < 6; i++)
        if (rd8(rd32(0x6CE56 + i * 4u)) == uint8_t(move_key)) {
            move = int16_t(i);
            break;
        }
    uint16_t min = rd16(p), max = rd16(p + 2);
    p += 4;
    if (ai != -1 && move != -1) CreateTeam(house, uint16_t(ai), uint16_t(move), min, max);
    return p;
}

// 0x166CE: a unit: house, type, health (of 256), cell, facing, action.
uint32_t ScenarioUnit(uint32_t p) {
    uint16_t house = rd16(p), type = rd16(p + 2), health = rd16(p + 4);
    uint16_t cell = uint16_t(rd16(p + 6) + ScaleOffset());
    uint16_t facing = rd16(p + 8), action = rd16(p + 10);
    p += 12;
    uint32_t u = CreateUnit(0xFFFF, type, house);
    if (!u) return p;
    U8{u + 4} |= 2;
    uint32_t info = rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4);
    wr16(u + 0x12, ScaleHealth(rd16(info + 0x10), health));
    uint32_t pos = (uint32_t(cell & 0xFC0) << 10) + (cell & 0x3F);
    wr32(u + 0xA, (pos << 8) + 0x800080);
    wr8(u + 0x6A, uint8_t(facing));
    wr8(u + 0x54, uint8_t(action));
    wr8(u + 0x55, 0xFF);
    if (!(rd16(u + 4) & 4)) SetUnitAction(u, int16_t(int8_t(rd8(u + 0x54))));
    wr8(u + 9, 0);
    UnitSeenBy(u, int16_t(int8_t(rd8(u + 8))));
    UnitSetFacing(u, rd8(u + 0x6A), 1, 0);
    UnitSetFacing(u, rd8(u + 0x6A), 1, 1);
    SetUnitSpeed(u, 0);
    uint16_t orient = uint16_t(int8_t(rd8(u + 8)));
    if (rd8(u + 2) == 0x19) orient = 0;
    CreateSprite(u + 0xA, rd16(info + 0x46), orient);
    UpdateUnitSprite(u);
    UnitShadowFlag(u);
    if (!CellInMap(cell)) Assert(u);
    return p;
}

// 0x1685A: a structure: house, type, health, cell; 'G' lines are walls and
// slabs (house, type, cell).
uint32_t ScenarioStructure(uint32_t p, uint16_t key) {
    if (key == 'G') {
        uint16_t cell = uint16_t(rd16(p) + ScaleOffset());
        uint16_t house = rd16(p + 2), type = rd16(p + 4);
        p += 6;
        CreateStructure(0xFFFF, type, house, cell);
        return p;
    }
    uint16_t house = rd16(p + 2), type = rd16(p + 4);
    uint16_t cell = uint16_t(rd16(p + 8) + ScaleOffset());
    p += 10;
    if (StructureAt(cell)) return p;
    uint32_t s = CreateStructure(0xFFFF, type, house, cell);
    if (!s) return p;
    U8{s + 4} &= uint8_t(~4);
    wr16(s + 0x5C, 0);
    wr16(s + 0x12, rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x10));
    if (rd8(s + 2) == 0xC) U16{HouseGet(uint16_t(int8_t(rd8(s + 8)))) + 2} += 1;
    if (int16_t(int8_t(rd8(s + 8))) == int16_t(player_house)) {
        if (rd8(s + 2) == 8) {
            U32{0xFFC578} = s;
            U32{0xFFC57C} = s;
            if (int16_t(scenario) >= 6) wr8(s + 0x54, 1);
            for (int y = 0; y < 7; y++)
                for (int x = 0; x < 10; x++) RedrawCell(uint16_t(view_cell + y * 64 + x));
        }
        U32{0xFFC578} = s;
    }
    return p;
}

// 0x1698C: a reinforcement: house, type, target cell, delay (high byte:
// minutes / 10, low byte '+' repeats).
uint32_t ScenarioReinforcement(uint32_t p, uint16_t index) {
    uint16_t a = rd16(p), b = rd16(p + 2), c = rd16(p + 4), d = rd16(p + 6);
    p += 8;
    uint32_t u = CreateUnitAt(0xFFFF, b, a, 0xFFFFFFFF, 0);
    if (!u) return p;
    UpdateUnitSprite(u);
    uint32_t r = 0xFFC0A2 + index * 10u;
    wr16(r, rd16(u));
    wr16(r + 2, c);
    uint16_t repeat = d & 0xFF;
    int16_t delay = int16_t(d) >> 8;
    wr16(r + 4, uint16_t(delay * 6 + 1));
    wr16(r + 6, rd16(r + 4));
    wr16(r + 8, repeat == '+' ? 1 : 0);
    return p;
}

// 0x2282E: allocates house n (0..5); 0 when out of range or in use.
uint32_t AllocHouse(int16_t n) {
    if (!houses || n < 0 || n >= 6) return 0;
    uint32_t h = houses + n * 0x46u;
    if (rd16(h + 4) & 1) return 0;
    MemSet(h, 0, 0x46);
    wr16(h, uint16_t(n));
    wr16(h + 0x30, 0xFFFF);
    wr16(h + 4, 1);
    wr32(kHouseList + house_count * 4u, h);
    house_count = uint16_t(house_count + 1);
    return h;
}

// 0x228C0: drops a house from the list.
void FreeHouse(uint32_t h) {
    for (int16_t i = 0; i < int16_t(house_count); i++) {
        uint32_t at = kHouseList + i * 4u;
        if (rd32(at) != h) continue;
        house_count = uint16_t(house_count - 1);
        wr16(h + 4, 0);
        if (i < int16_t(house_count)) MemCpy(at, at + 4, (int16_t(house_count) - i) * 4);
    }
}

// 0xB068: the free list of 0xA2 16-byte nodes at 0xFFC8C8.
void InitEffectPool() {
    uint32_t a = 0xFFC8C8;
    U16{0xFFD2F8} = uint16_t(a);
    U32{0xFFD2FA} = 0;
    wr16(a + 0xC, 0);
    for (int i = 0; i <= 0xA1; i++) {
        uint32_t b = a + 0x10;
        wr16(b + 0xC, uint16_t(a));
        wr16(a + 0xE, uint16_t(b));
        a = b;
    }
    wr16(a + 0xE, 0);
}

// 0x2060C: the free list of 0xC7 6-byte nodes at 0xFFD720.
void InitMessagePool() {
    uint32_t a = 0xFFD720;
    U32{0xFFDBD0} = a;
    for (int i = 0; i < 6; i++) wr32(0xFFDBD4 + i * 4u, 0);
    wr16(a + 2, 0);
    for (int i = 0; i <= 0xC6; i++) {
        uint32_t b = a + 6;
        wr16(a, 0);
        wr16(a + 4, uint16_t(b));
        wr16(b + 2, uint16_t(a));
        a = b;
    }
    wr16(a, 0);
    wr16(a + 4, 0);
}

// 0x9BDE, 0x9C2E: the selection and order cursors.
void CreateCursors() {
    U16{0xFFC858} = uint16_t(AllocSprite(0x6A808, 0x40, 0x6A7EE, kSpriteList));
    uint32_t n = AllocSprite(0x65CD4, 0xC0, 0x65CAE, kSpriteList);
    U16{0xFFC862} = uint16_t(n);
    U16{0xFFC864} = 0xFFFF;
    U16{0xFFC866} = 0xFFFF;
    StartBlink(n, 8);
}
void CreateMarkers() {
    U16{0xFFC732} = uint16_t(AllocSprite(0x6A7A6, 0xC0, 0x6A798, kSpriteList));
    U16{0xFFC734} = 0xFFFF;
    U16{0xFFC736} = uint16_t(AllocSprite(0x6A7BA, 0xC0, 0x6A7AC, kSpriteList));
    U16{0xFFC750} = uint16_t(AllocSprite(0x65B48, 0xC0, 0x65B3A, kSpriteList));
    U16{0xFFC73C} = uint16_t(AllocSprite(0x6A7CE, 0xC0, 0x6A7C0, kSpriteList));
    U16{0xFFC73E} = 0xFFFF;
    U16{0xFFC740} = uint16_t(AllocSprite(0x6A7E2, 0xC0, 0x6A7D4, kSpriteList));
}

// 0x4E72: the radar view frame sprites.
void CreateRadarSprites() {
    uint32_t a = 0xFFBF4C;
    wr8(a + 4, 0xFC);
    uint32_t n = AllocSprite(a, 0, 0x6A834, kSpriteList);
    wr16(a + 6, uint16_t(n));
    if (!n) return;
    U8{n + 7} |= 0x40;
    QueueDma(2, 0x6A814, uint32_t(uint16_t(rd16(rd32(n + 8) + 0xA) << 5)) << 16 | 0x10);
    U16{0xFFBF76} = 0xF0;
    U16{0xFFBF78} = 0x90;
    U16{0xFFBF7A} = 0xFB00;
    n = AllocSprite(0xFFBF76, 0, 0x6A842, kSpriteList);
    U16{0xFFBF58} = uint16_t(n);
    if (n) U8{n + 7} |= 0x40;
}

// 0x4AD6: the map cursor (class 1) and its shadow.
void CreateMapCursor() {
    uint32_t n = CreateSprite(0xFFBF12, 1, 0);
    if (n) U8{n + 7} |= 0x40;
    U16{0xFFBF62} = 0;
    n = AllocSprite(0xFFBF5A, 0xA0008, 0x64DEE, kSpriteList);
    U16{0xFFBF60} = uint16_t(n);
    U8{n + 7} |= 0x80;
    wr8(0xFFBF5E, 0xFA);
}

} // namespace

// 0xA422: tile sets from the table at 0xA48C (length, source pairs;
// negative sources are raw, others LCW packed).
void LoadGameTiles() {
    uint32_t t = 0xA48C;
    uint32_t len = rd32(t);
    t += 4;
    for (;;) {
        uint32_t src = rd32(t);
        t += 4;
        if (int32_t(src) >= 0) {
            FlushDma();
            Decompress(src, 0xFF0000);
            src = 0xFF0000;
        }
        QueueDma(2, src, len);
        len = rd32(t);
        t += 4;
        if (!len) break;
        FlushDma();
        src = rd32(t);
        t += 4;
        if (int32_t(src) >= 0) {
            Decompress(src, 0xFF0800);
            src = 0xFF0800;
        }
        QueueDma(2, src, len);
        FlushDma();
        len = rd32(t);
        t += 4;
        if (!len) break;
    }
    FlushDma();
}

// 0x4DAC: the map cursor's limits.
void SetCursorLimits(uint32_t lo, uint32_t hi) {
    wr32(0xFFBF1A, lo);
    wr32(0xFFBF1E, hi);
}

namespace {


// 0x9630: six blank tiles at 0xF6C0.
void ClearOrderTiles() {
    for (int i = 0; i < 6; i++) QueueDma(2, 0x50AD4, uint32_t(0xF6C0 + i * 0x20) << 16 | 0x10);
    wr32(0xFFC85A, 0);
    wr32(0xFFC85E, 0);
}

// 0x2687E: the world before a scenario.
void ResetWorld() {
    InitUnitArray(0);
    InitStructureArray(0);
    InitTeamArray(0);
    InitHouseArray(0);
    ClearMapRam();
    ClearUnitLists();
    U32{0xFFC054} = 0;
    U16{0xFFBF88} = 0;
    MemSet(0xFFC2A0, 0xFF, 0x38);
    MemSet(0xFFC2D8, 0xFF, 0x6C);
    MemSet(0xFFC868, 0, 0x49);
    PlayVoice(0xFFFE);
    U16{0xFFC1A4} = 0;
    U32{0xFFC1A0} = 0;
    U32{0xFFC18C} = 0;
    U16{0xFFC190} = 0xFFFF;
    U16{0xFFC170} = 0xFFFF;
    U32{0xFFC174} = 0;
    U16{0xFFC180} = 0;
    U16{0xFFC184} = 0xFFFF;
    U16{0xFFC188} = 0;
}

} // namespace

// 0x2295E, 0x2297E: walks the house list (it 0: the shared iterator).
uint32_t FirstHouse(uint32_t it) {
    if (!it) it = 0xFFDC00;
    wr16(it + 4, 0xFFFF);
    return NextHouse(it);
}
uint32_t NextHouse(uint32_t it) {
    if (!it) it = 0xFFDC00;
    int16_t i = int16_t(rd16(it + 4));
    if (i < int16_t(house_count)) i++;
    wr16(it + 4, uint16_t(i));
    return i < int16_t(house_count) ? rd32(kHouseList + i * 4u) : 0;
}

// 0xA8E2: a voice (table at 0x6A95C) when effects are on.
void PlayVoice(uint16_t id) {
    if (!U8{0xFFDC51} || int16_t(id) >= 0x5E) return;
    int8_t v = int8_t(rd8(0x6A95C + int16_t(id)));
    if (v < 0) return;
    U16 cur{0xFFC8B4};
    if (int16_t(cur) != v) {
        GemsStopSeq(uint32_t(int32_t(int16_t(cur))));
        cur = uint16_t(v);
    }
    PlayMusic(uint32_t(int32_t(v)));
}

// 0x4DC6: the map cursor onto a cell.
void SetCursorCell(uint16_t cell) {
    uint32_t v = uint32_t((cell & 0x3F) << 5) << 16 | uint16_t((cell & 0xFC0) >> 1);
    wr32(0xFFBF12, v - rd32(0xFFE3EC));
}

// 0x4B38: the map cursor's frame (class a; negative: the shapes at
// 0x64FEA) and its shadow's (class b).
void SetCursorShape(int16_t a, int16_t b) {
    uint32_t n = Node(U16{0xFFBF18});
    if (a < 0) {
        if (a == int16_t(U16{0xFFBF62})) goto move;
        U16{0xFFBF62} = uint16_t(a);
        U16{0xFFBF64} = uint16_t(a);
        wr32(n + 8, rd32(0x64FEA + int16_t(-a * 4) - 4));
        U16{n + 6} &= 0x9FFF;
        U8{n + 7} &= uint8_t(~0x20);
        U32{0xFFBF04} = 0x64746;
        U32{0xFFBF08} = 0x64846;
        return;
    }
    if (a != int16_t(U16{0xFFBF62})) {
        U16{0xFFBF62} = uint16_t(a);
        U16{n + 6} &= 0x9FFF;
        if (a == 0x1C || a == 0x1E) U16{n + 6} |= rd16(0x4B32 + player_house * 2u);
        uint32_t t = 0x60E8C + a * 8u;
        wr32(n + 8, rd32(t + 4));
        U32{0xFFBF04} = rd32(t) & 0xFFFFFF;
    }
    if (b >= 0 && b != int16_t(U16{0xFFBF64})) {
        U16{0xFFBF64} = uint16_t(b);
        uint32_t t = 0x60E8C + b * 8u;
        wr32(Node(U16{0xFFBF60}) + 8, rd32(t + 4));
        U32{0xFFBF08} = rd32(t) & 0xFFFFFF;
    }
move:
    U32{0xFFBF12} += U32{0xFFBF0E};
    U32{0xFFBF0E} = 0;
}

// 0x504E: a radar column (8 longs from src) into the tiles at 0xD000.
void RadarColumn(uint32_t src) {
    uint16_t i = U16{0xFFBF6A};
    uint16_t a = uint16_t(0xD000 | ((i & 0x1F) << 2) | ((i & 0x20) ? 0x400 : 0));
    for (int r = 0; r < 8; r++, a = uint16_t(a + 0x80), src += 4) {
        display::vram_w16(a, rd16(src));
        display::vram_w16(uint16_t(a + 2), rd16(src + 2));
    }
}

// 0x4F80: the radar cleared to grey with a frame in the house colour.
void ClearRadar() {
    uint32_t buf = 0xFFF3B0;
    uint32_t colour = rd32(0x4FB2 + player_house * 4u);
    for (int16_t i = 0x3F; i >= 0; i--) {
        U16{0xFFBF6A} = uint16_t(i);
        if (i == 0 || i >= 0x3F) {
            for (int k = 0; k < 8; k++) wr32(buf + k * 4u, colour);
        } else {
            for (int k = 0; k < 8; k++) wr32(buf + k * 4u, 0xCCCCCCCC);
            uint16_t c = uint16_t(colour >> 16);
            wr16(buf, uint16_t((c & 0xF000) | 0xCCC));
            wr16(buf + 0x1E, uint16_t((c & 0xF) | 0xCCC0));
        }
        RadarColumn(buf);
    }
}

// 0x50D6: radar zoom (1 full scale), cleared.
void SetRadarMode(uint16_t full) {
    wr8(0xFFBF51, uint8_t(full));
    ClearRadar();
    wr8(0xFFBF6C, 0);
    UpdateRadarFrame();
    FlushDma();
}

// 0x54E8, 0x551C: the radar's view frame shown or hidden, placed over the
// view.
void UpdateRadarFrame() {
    uint16_t a = U16{0xFFBF52};
    if (!a) return;
    uint32_t n = Node(a), m = Node(U16{0xFFBF58});
    if (U8{0xFFBF7C}) {
        U8{n + 7} &= 0x7F;
        U8{m + 7} &= 0x7F;
    } else {
        U8{n + 7} |= 0x80;
        U8{m + 7} |= 0x80;
    }
    PlaceRadarFrame();
}
void PlaceRadarFrame() {
    uint16_t x = uint16_t(((U16{0xFFE3EC} - U16{0xFFBF54} + U16{0xFFBF12}) & 0x7F0) >> 4);
    uint16_t y = uint16_t(((U16{0xFFE3EE} - U16{0xFFBF56} + U16{0xFFBF14}) & 0x7F0) >> 4);
    if (!U8{0xFFBF51}) {
        x >>= 1;
        y >>= 1;
    }
    U16{0xFFBF4C} = uint16_t(x + U16{0xFFBF76});
    U16{0xFFBF4E} = uint16_t(y + U16{0xFFBF78});
}

// 0x579C: the cell is inside the map rectangle of the scale (0x6CE82).
bool CellInMap(uint16_t cell) {
    int16_t x = int16_t(cell & 0x3F), y = int16_t(cell >> 6);
    uint32_t r = 0x6CE82 + map_scale * 8u;
    int16_t x0 = int16_t(rd16(r)), y0 = int16_t(rd16(r + 2));
    return x >= x0 && x < int16_t(x0 + rd16(r + 4)) && y >= y0 && y < int16_t(y0 + rd16(r + 6));
}

// 0x47F6: the original's debug trap ("UNIT" in a1, illegal instruction).
void Assert(uint32_t obj) {
    if (obj) untranslated(0x47F6);
}

// 0xB398: types flagged at 0xB3B4 get their shadow node's bit 2.
void UnitShadowFlag(uint32_t u) {
    if (rd8(0xB3B4 + rd8(u + 2))) U8{Node(U16{u + 0x10}) + 7} |= 4;
}

// 0x199DC: hit points of a fraction of 256, rounded, at most 0xFFFF.
uint16_t ScaleHealth(uint16_t max, uint16_t f) {
    uint32_t v = (uint32_t(max) * f + 0x50) >> 8;
    return v > 0xFFFF ? 0xFFFF : uint16_t(v);
}

// 0x199FA: part * 256 / total, both halved until the dividend fits a word
// (0xFFFF when total becomes 0).
uint16_t PartOf(uint16_t total, uint16_t part) {
    uint32_t n = uint32_t(part) << 8;
    uint16_t d = total;
    while (n > 0xFFFF) {
        n = (n + 1) >> 1;
        d = uint16_t((d + 1) >> 1);
    }
    if (!d) return 0xFFFF;
    return uint16_t(n / d);
}

// 0x1A972, 0x1A938: the structure / unit on a cell (map byte +2 bits 5 /
// 4, byte +3 the index + 1).
uint32_t StructureAt(uint16_t cell) {
    if (int16_t(cell) >= 0x1000) return 0;
    uint32_t c = kMap + int16_t(cell) * 4;
    if (!(rd8(c + 2) & 0x20)) return 0;
    return StructureGet(uint16_t(rd8(c + 3) - 1));
}
uint32_t UnitAt(uint16_t cell) {
    if (int16_t(cell) >= 0x1000) return 0;
    uint32_t c = kMap + int16_t(cell) * 4;
    if (!(rd8(c + 2) & 0x10)) return 0;
    return UnitGet(uint16_t(rd8(c + 3) - 1));
}

// 0x1609C: reads the scenario of a campaign step for a house.
uint16_t LoadScenario(uint16_t step, uint16_t house) {
    if (house == 0xFFFF) return 0;
    campaign = step;
    md::Frame f(0x24);
    uint32_t name = f.base;
    Sprintf(name, 0x1608C, {W(uint16_t(int8_t(rd8(rd32(0x6C75A + house * 30u))))), W(step)});
    bloom_list = 0;
    field_list = 0;
    uint32_t p = FindNamed(name);
    FadeOutScreen();
    SetWindowPos(0, 0);
    SetScreenWidth(1);
    InitSpritePool(0xFFE6A8, kSpriteList);
    InitEffectPool();
    InitMessagePool();
    CreateCursors();
    CreateMarkers();
    CreateRadarSprites();
    CreateMapCursor();
    SetCursorLimits(0x180018, 0x12E00CE);
    U16{0xFFBEA2} = 0x46;
    for (int16_t h : {0, 1, 2, 4, 3}) AllocHouse(h);
    MemSet(0xFFC05C, 0, 0xE6);
    U16{0xFFC05C} = 0x2AB;
    for (int i = 0; i < 16; i++) wr16(0xFFC0A2 + i * 10u, 0xFFFF);
    while (int16_t(rd16(p)) >= 0) {
        uint16_t w = rd16(p);
        p += 2;
        uint16_t key = w & 0xFF;
        int16_t section = int16_t(w) >> 8;
        switch (section) {
        case 0: p = ScenarioBasic(p, key); break;
        case 1: p = ScenarioMap(p, key); break;
        case 2: case 3: case 4: case 5: p = ScenarioHouse(p, uint16_t(section), key); break;
        case 6:
            wr16(0xFFC2A0 + key * 2u, rd16(p));
            p += 2;
            break;
        case 7: p = ScenarioTeam(p); break;
        case 8: p = ScenarioUnit(p); break;
        case 9: p = ScenarioStructure(p, key); break;
        case 10: p = ScenarioReinforcement(p, key); break;
        }
    }
    if (!rd16(player_house_ptr + 8)) {
        uint16_t left = 0x4D;
        for (uint32_t h = FirstHouse(0); h; h = NextHouse(0))
            if (!(rd16(h + 4) & 2)) left = uint16_t(left - rd16(h + 8));
        wr16(player_house_ptr + 8, left);
    }
    for (uint32_t h = FirstHouse(0); h; h = NextHouse(0))
        if (!rd16(h + 6)) FreeHouse(h);
    if (uint32_t b = bloom_list) {
        uint16_t n = rd16(b);
        b += 2;
        do {
            int32_t c = int16_t(uint16_t(rd16(b) + ScaleOffset()));
            b += 2;
            uint32_t cell = rd32(0x4AA24) + c * 4;
            wr16(cell, uint16_t((rd16(cell) & 0xFE00) + (U16{0xFFC228} & 0x1FF)));
        } while (--n);
    }
    U16{0xFFDBEC} = 1;
    U16{0xFFDBEE} = 1;
    U32{0xFFC048} = U32{0xFFC598};
    for (uint16_t i = 0; i < 3; i++)
        if (uint32_t h = HouseGet(i))
            if (rd16(h + 2)) U16{h + 2} -= 1;
    DisplayOff();
    U32{0xFFC288} = LoadScreen(4);
    LoadGameTiles();
    SetCursorCell(tactical_cell);
    SetCursorShape(5, 5);
    U32{0xFFC5A4} = U32{0xFFC598};
    ClearOrderTiles();
    BuildGameSprites();
    UpdateRadarFrame();
    return 1;
}

// 0x26540: a mission: the world reset, the scenario, then played.
void PlayMission(uint16_t house, uint16_t step) {
    PlayVoice(0xFFFE);
    ResetWorld();
    U16{0xFFC148} += 1;
    LoadScenario(step, house);
    RunMission();
    U16{0xFFC148} -= 1;
}

} // namespace game
