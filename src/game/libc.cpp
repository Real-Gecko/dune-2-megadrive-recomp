// The C runtime (0x491CA-0x49F3E): strings, memory, sprintf.
#include "game.hpp"

#include <string>

namespace game {

// 0x49A48
uint32_t StrLen(uint32_t s) {
    uint32_t n = 0;
    while (rd8(s + n)) n++;
    return n;
}

// 0x49A60: strncpy (pads with zeros up to n).
uint32_t StrNCpy(uint32_t dst, uint32_t src, int32_t n) {
    uint32_t d = dst;
    while (--n >= 0) {
        uint8_t c = rd8(src++);
        wr8(d++, c);
        if (!c) break;
    }
    while (--n >= 0) wr8(d++, 0);
    return dst;
}

// 0x49AD4
uint32_t StrCat(uint32_t dst, uint32_t src) {
    uint32_t d = dst;
    while (rd8(d)) d++;
    uint8_t c;
    do {
        c = rd8(src++);
        wr8(d++, c);
    } while (c);
    return dst;
}

// 0x49AF8
int32_t StrCmp(uint32_t a, uint32_t b) {
    for (;;) {
        uint8_t c = rd8(b++);
        if (!c) return int8_t(rd8(a));
        uint8_t d = rd8(a++);
        if (d != c) return int8_t(uint8_t(d - c));
    }
}

// 0x49CDC
uint32_t MemSet(uint32_t p, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) wr8(p + i, v);
    return p;
}

// 0x49DB8 (negative lengths copy nothing)
uint32_t MemCpy(uint32_t dst, uint32_t src, int32_t n) {
    for (int32_t i = 0; i < n; i++) wr8(dst + i, rd8(src + i));
    return dst;
}

// 0x49EE8
uint32_t MemMove(uint32_t dst, uint32_t src, uint32_t n) {
    if (src > dst)
        for (uint32_t i = 0; i < n; i++) wr8(dst + i, rd8(src + i));
    else
        for (uint32_t i = n; i-- > 0;) wr8(dst + i, rd8(src + i));
    return dst;
}

// 0x49364: the printf engine on arguments laid out as the 68000 C compiler
// passes varargs: every argument 32 bits ('h' takes the low word).
static std::string Format(uint32_t fmt, uint32_t args) {
    std::string out;
    for (;;) {
        char c = char(rd8(fmt++));
        if (!c) return out;
        if (c != '%') {
            out += c;
            continue;
        }
        bool left = false, zero = false, plus = false, space = false;
        for (;; fmt++) {
            char f = char(rd8(fmt));
            if (f == '-') left = true;
            else if (f == '0') zero = true;
            else if (f == '+') plus = true;
            else if (f == ' ') space = true;
            else break;
        }
        auto number = [&]() {
            int v = 0;
            if (rd8(fmt) == '*') {
                fmt++;
                v = int32_t(rd32(args));
                args += 4;
                return v;
            }
            while (rd8(fmt) >= '0' && rd8(fmt) <= '9') v = v * 10 + (rd8(fmt++) - '0');
            return v;
        };
        int width = number(), prec = -1;
        if (rd8(fmt) == '.') {
            fmt++;
            prec = number();
        }
        bool half = false;
        if (rd8(fmt) == 'l') {
            fmt++;
        } else if (rd8(fmt) == 'h') {
            half = true;
            fmt++;
        }
        char conv = char(rd8(fmt++));
        std::string s;
        auto arg = [&](bool is_signed) -> int64_t {
            uint32_t v = rd32(args);
            args += 4;
            if (half) return is_signed ? int64_t(int16_t(v)) : int64_t(uint16_t(v));
            return is_signed ? int64_t(int32_t(v)) : int64_t(v);
        };
        switch (conv) {
        case 'd':
        case 'i': {
            int64_t v = arg(true);
            s = std::to_string(v < 0 ? -v : v);
            if (prec >= 0) while (int(s.size()) < prec) s = "0" + s;
            if (v < 0) s = "-" + s;
            else if (plus) s = "+" + s;
            else if (space) s = " " + s;
            break;
        }
        case 'u': s = std::to_string(arg(false)); break;
        case 'x':
        case 'X': {
            uint64_t v = uint64_t(arg(false));
            const char* dig = conv == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            do {
                s = dig[v & 15] + s;
                v >>= 4;
            } while (v);
            break;
        }
        case 'c': s = char(arg(false)); break;
        case 's': {
            uint32_t p = rd32(args);
            args += 4;
            for (int i = 0; (prec < 0 || i < prec) && rd8(p + i); i++) s += char(rd8(p + i));
            zero = false;
            break;
        }
        case '%': s = "%"; break;
        default: s = conv; break;
        }
        while (int(s.size()) < width) {
            if (left) s += ' ';
            else if (zero && (s[0] == '-' || s[0] == '+')) s.insert(1, "0");
            else s = (zero ? "0" : " ") + s;
        }
        out += s;
    }
}

// 0x491CA: sprintf with the arguments at args.
uint16_t SprintfAt(uint32_t dst, uint32_t fmt, uint32_t args) {
    std::string s = Format(fmt, args);
    for (size_t i = 0; i < s.size(); i++) wr8(dst + uint32_t(i), uint8_t(s[i]));
    wr8(dst + uint32_t(s.size()), 0);
    return uint16_t(s.size());
}

uint16_t Sprintf(uint32_t dst, uint32_t fmt, std::initializer_list<PArg> args) {
    md::Frame f(uint32_t(args.size()) * 4);
    uint32_t p = f.base;
    for (const PArg& a : args) {
        wr32(p, a.v);
        p += 4;
    }
    return SprintfAt(dst, fmt, f.base);
}

} // namespace game
