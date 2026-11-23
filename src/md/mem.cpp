#include "mem.hpp"

#include <cstdio>
#include <set>

namespace md {

uint8_t ram[0x10000];
std::vector<uint8_t> rom;
int int_mask = 7;
uint32_t sp = 0xFFFFFFFA;

static void io(uint32_t a, bool write) {
    static std::set<uint32_t> seen;
    if (seen.insert(a << 1 | write).second)
        std::fprintf(stderr, "md: unhandled %s at %06X\n", write ? "write" : "read", a);
}

uint8_t rd8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) return ram[a & 0xFFFF];
    if (a < rom.size()) return rom[a];
    io(a, false);
    return 0;
}

uint16_t rd16(uint32_t a) { return uint16_t(rd8(a) << 8 | rd8(a + 1)); }
uint32_t rd32(uint32_t a) { return uint32_t(rd16(a)) << 16 | rd16(a + 2); }

void wr8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) ram[a & 0xFFFF] = v;
    else io(a, true);
}

void wr16(uint32_t a, uint16_t v) {
    wr8(a, uint8_t(v >> 8));
    wr8(a + 1, uint8_t(v));
}

void wr32(uint32_t a, uint32_t v) {
    wr16(a, uint16_t(v >> 16));
    wr16(a + 2, uint16_t(v));
}

const uint8_t* ptr(uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) return ram + (a & 0xFFFF);
    return rom.data() + a;
}

uint8_t* ram_ptr(uint32_t a) { return ram + (a & 0xFFFF); }

} // namespace md
