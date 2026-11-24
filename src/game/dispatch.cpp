// Untranslated routines and code-pointer dispatch.
#include "game.hpp"

#include <cstdio>

namespace game {

void untranslated(uint32_t addr) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "untranslated: %06X (frame %u)", addr, host::frame_count);
    host::fatal(msg);
}

uint32_t call(uint32_t addr) {
    switch (addr) {
    case 0x4504: SpriteTableHook(); return 0;
    case 0x13148: SpriteTableHook(); return 0;
    case 0x17C36: TitleScrollHook(); return 0;
    case 0x24728: MentatHook(); return 0;
    case 0x24816: MentatFadeHook(); return 0;
    case 0x6D10: FreeCursorHook(); return 0;
    case 0x6092: PlacementCursorHook(); return 0;
    case 0xB544: EndSceneHook(); return 0;
    default: untranslated(addr);
    }
}

} // namespace game
