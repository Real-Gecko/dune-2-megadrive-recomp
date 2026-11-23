// The 68000 address space as the translated code sees it: cartridge ROM and
// 64 KB of work RAM. Pointers stay 68000 addresses (uint32_t); values are
// big-endian in memory, as on the console.
#pragma once

#include <cstdint>
#include <vector>

namespace md {

extern uint8_t ram[0x10000];
extern std::vector<uint8_t> rom;
// The 68000 interrupt mask the game code sets (SR bits 8-10).
extern int int_mask;

uint8_t rd8(uint32_t a);
uint16_t rd16(uint32_t a);
uint32_t rd32(uint32_t a);
void wr8(uint32_t a, uint8_t v);
void wr16(uint32_t a, uint16_t v);
void wr32(uint32_t a, uint32_t v);
// Host pointer to n bytes at a (ROM or RAM, not crossing the region's end).
const uint8_t* ptr(uint32_t a);
uint8_t* ram_ptr(uint32_t a);

// A variable in memory at a fixed address.
template <class T> struct Var {
    uint32_t a;
    constexpr explicit Var(uint32_t addr) : a(addr) {}
    constexpr Var(const Var&) = default;
    T get() const {
        if constexpr (sizeof(T) == 1) return T(rd8(a));
        else if constexpr (sizeof(T) == 2) return T(rd16(a));
        else return T(rd32(a));
    }
    void set(T v) const {
        if constexpr (sizeof(T) == 1) wr8(a, uint8_t(v));
        else if constexpr (sizeof(T) == 2) wr16(a, uint16_t(v));
        else wr32(a, uint32_t(v));
    }
    operator T() const { return get(); }
    const Var& operator=(T v) const { set(v); return *this; }
    const Var& operator=(const Var& o) const { set(o.get()); return *this; }
    const Var& operator+=(T v) const { set(T(get() + v)); return *this; }
    const Var& operator-=(T v) const { set(T(get() - v)); return *this; }
    const Var& operator|=(T v) const { set(T(get() | v)); return *this; }
    const Var& operator&=(T v) const { set(T(get() & v)); return *this; }
    const Var& operator^=(T v) const { set(T(get() ^ v)); return *this; }
    T operator++(int) const { T v = get(); set(T(v + 1)); return v; }
    T operator--(int) const { T v = get(); set(T(v - 1)); return v; }
    const Var& operator++() const { set(T(get() + 1)); return *this; }
    const Var& operator--() const { set(T(get() - 1)); return *this; }
    Var operator[](int i) const { return Var{uint32_t(a + i * int(sizeof(T)))}; }
};
using U8 = Var<uint8_t>;
using U16 = Var<uint16_t>;
using U32 = Var<uint32_t>;
using S8 = Var<int8_t>;
using S16 = Var<int16_t>;
using S32 = Var<int32_t>;

// The 68000 stack in work RAM, for locals whose address the code passes on.
extern uint32_t sp;
struct Frame {
    uint32_t base, saved;
    explicit Frame(uint32_t bytes) : saved(sp) { sp -= (bytes + 1) & ~1u; base = sp; }
    ~Frame() { sp = saved; }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

} // namespace md
