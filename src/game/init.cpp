// Game start (0x16BA8) and the data tables it sets up.
#include "game.hpp"

namespace game {

// 0x203B8, 0x20390: one-shot flags 1 and 2: set (on) or take (off); the
// old value.
uint16_t SwapFlag1(uint16_t on) { return on ? TestAndSetFlag(1) : TakeFlag(1); }
uint16_t SwapFlag2(uint16_t on) { return on ? TestAndSetFlag(2) : TakeFlag(2); }

// 0x4A34: number (1 up) of a named file of the table at 0x52C34, 0 if none.
uint16_t FindFile(uint32_t name) {
    uint16_t n = 1;
    for (uint32_t t = 0x52C34;; t += 8, n++) {
        uint32_t s = rd32(t);
        if (!s) return 0;
        uint32_t a = name;
        for (;;) {
            uint8_t c = rd8(a++);
            if (c != rd8(s++)) break;
            if (!c) return n;
        }
    }
}

// 0x4A70: the size (padded to even) of a chunk of file n, 0 if absent.
uint32_t ChunkSize(int16_t file, uint32_t id) {
    uint32_t a = rd32(0x52C2C + int32_t(file) * 8 + 4) + 0xC;
    for (;;) {
        uint32_t c = a;
        a += 8;
        uint32_t cid = rd32(c);
        if (int32_t(cid) < 0) return 0;
        uint32_t size = (rd32(c + 4) + 1) & 0xFFFFFFFE;
        if (cid == id) return size;
        a += size;
    }
}

// Script info: +0 text, +4 data, +8 order, +0xC order words, +0xE data
// words, +0x10 functions table, +0x14 1 unless loaded with a flag.
// 0x16D6C
void ClearScriptInfo(uint32_t info) {
    if (!info) return;
    wr32(info, 0);
    wr32(info + 8, 0);
    wr32(info + 4, 0);
}

// 0x16D82: points a script info at the TEXT, ORDR and DATA chunks of an
// EMC file; their total size, 0 when the file or a chunk is missing.
uint16_t LoadScriptFile(uint32_t name, uint32_t info, uint32_t functions, uint32_t flag) {
    if (!info || !name) return 0;
    ClearScriptInfo(info);
    uint32_t total = 0;
    wr16(info + 0x14, flag ? 0 : 1);
    wr32(info + 0x10, functions);
    if (!FindFile(name)) return 0;
    int16_t f = int16_t(FindFile(name));
    uint32_t n = ChunkSize(f, 0x54455854); // TEXT
    total += n;
    if (n) wr32(info, FindChunk(f, 0x54455854));
    n = ChunkSize(f, 0x4F524452); // ORDR
    total += n;
    if (n) {
        wr32(info + 8, FindChunk(f, 0x4F524452));
        wr16(info + 0xC, uint16_t(n >> 1));
        n = ChunkSize(f, 0x44415441); // DATA
        total += n;
        if (n) {
            wr32(info + 4, FindChunk(f, 0x44415441));
            wr16(info + 0xE, uint16_t(n >> 1));
            return uint16_t(total);
        }
    }
    ClearScriptInfo(info);
    return 0;
}

// 0x16D34: loads "NAME.EMC".
uint16_t LoadScript(uint32_t name, uint32_t info, uint32_t functions, uint32_t flag) {
    Sprintf(0xFFC5B0, 0x16D2C, {L(name)});
    return LoadScriptFile(0xFFC5B0, info, functions, flag);
}

// 0x43144, 0x2DE30, 0x227B8, 0xB802: the object arrays (units, teams,
// houses, structures) cleared, at a new address when given.
uint32_t InitUnitArray(uint32_t a) {
    U16{0xFFDCBC} = 0;
    if (a) U32{0xFFDE58} = a;
    if (uint32_t p = U32{0xFFDE58}) MemSet(p, 0, 0x37C8);
    U16{0xFFC728} = 1;
    U16{0xFFC72C} = 0;
    return 0x37C8;
}

uint32_t InitTeamArray(uint32_t a) {
    U16{0xFFDC5C} = 0;
    if (a) U32{0xFFDCA0} = a;
    if (uint32_t p = U32{0xFFDCA0}) MemSet(p, 0, 0x540);
    return 0x540;
}

constexpr U32 houses{0xFFDC24}; // 6 houses of 0x46 bytes

// 0x22922: house n when it is in use (flags +4 bit 0), else 0.
uint32_t HouseGet(uint16_t n) {
    uint32_t h = houses + int32_t(int16_t(n)) * 0x46;
    return rd16(h + 4) & 1 ? h : 0;
}

uint32_t InitHouseArray(uint32_t a) {
    U16{0xFFDC08} = 0;
    if (a) houses = a;
    if (uint32_t p = houses) MemSet(p, 0, 0x1A4);
    for (uint16_t i = 0; i < 6; i++)
        if (uint32_t h = HouseGet(i)) wr16(h + 8, 10);
    return 0x1A4;
}

uint32_t InitStructureArray(uint32_t a) {
    U16{0xFFD318} = 0;
    if (a) U32{0xFFD4B4} = a;
    if (uint32_t p = U32{0xFFD4B4}) MemSet(p, 0, 0x1BF2);
    return 0x1BF2;
}

// 0x126CC: a file name with the language letter (0xFFC03C indexes the
// suffixes at 0x6C6EC) in a static buffer; 0 for none.
uint32_t LocalizedName(uint32_t name) {
    if (!name) return 0;
    StrNCpy(0xFFD6D8, name, 0xE);
    wr8(0xFFD6E0, 0);
    StrCat(0xFFD6D8, 0x126CA);
    StrCat(0xFFD6D8, 0x6C6EC + int16_t(int8_t(rd8(0xFFC03C))) * 4);
    return 0xFFD6D8;
}

constexpr U16 game_loaded{0xFFC024};
constexpr U16 scenario{0xFFC050};
constexpr U16 campaign{0xFFC04C};
constexpr U16 player_house{0xFFC274};

// 0x16BA8
void GameInit() {
    SwapFlag1(1);
    SetPadRepeatMask(0xF0);
    SetScreenWidth(1);
    if (!game_loaded) {
        scenario = 0;
        campaign = 1;
        player_house = 0xFFFF;
    }
    U16{0xFFC264} = 0;
    U16{0xFFC268} = 0;
    U32{0xFFC168} = 0xFF0000;
    U32{0xFFC16C} = 0x2EE0;
    LoadScript(0x16B8A, 0xFFC1B0, 0xFED7C, 0); // UNIT
    LoadScript(0x16B8F, 0xFFC1E0, 0xFED40, 0); // TEAM
    LoadScript(0x16B94, 0xFFC1C8, 0x6B9A0, 0); // BUILD
    InitUnitArray(0xFF1000);
    InitTeamArray(0xFF47CC);
    InitHouseArray(0xFF4D10);
    InitStructureArray(0xFF4EB8);
    U32{0xFFC15C} = FindNamed(LocalizedName(0x16B9A)); // DUNE
    U32{0xFFC158} = FindNamed(LocalizedName(0x16B9F)); // MESSAGE
    wr16(0xFFE41A, 0);
    if (player_house == 0xFFFF) {
        player_house = 5;
        player_house = ChooseHouse();
    }
    campaign = uint16_t(scenario + 1);
    StartScenario(player_house, campaign);
    SetGameMode(4);
}

} // namespace game
