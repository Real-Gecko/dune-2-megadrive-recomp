// Boot: reset, hardware init, the logo and the game's main.
#include "game.hpp"

#include <cstring>

namespace game {

// 0x41E0
static void GameMain() {
    md::int_mask = 0;
    fade_delay = 4;
    display::cfg.win_h = 0x14; // window over the whole screen
    display::cfg.win_v = 0x1C;
    FrontEnd();
    CMain();
}

// 0x200 and 0x17A4. The game's main returns to the reset vector.
void Reset() {
    host::set_vblank_handler(VBlank);
    for (;;) {
        std::memset(md::ram, 0, sizeof md::ram);
        md::int_mask = 7;
        InitHardware();
        display::cfg.enabled = true;
        vdp_mode2 = 0x8164;
        dma_enable = 0xFF;
        display::cfg.win_h = 0;
        display::cfg.win_v = 0;
        pad_repeat_mask = 0xE0;
        InitSpritePool(0xFFE6A8, 0xFFF3A8);
        wr8(0xFFE000, 0); // 0x282A: not a PAL console
        md::int_mask = 0;
        BootLogo();
        GameMain();
    }
}

} // namespace game
