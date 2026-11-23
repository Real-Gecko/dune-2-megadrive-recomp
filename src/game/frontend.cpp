// The front end loop (0x9320): game defaults, then title, options,
// password and tutorial until a game starts.
#include "game.hpp"

namespace game {

// 0x4A04: back to the reset vector.
void AttractDemo(uint32_t) { Reset(); }

constexpr U16 game_loaded{0xFFC024}; // a password started a game

// 0x9320
void FrontEnd() {
    FlushDma();
    WaitVBlank();
    for (uint32_t a : {0xFFC154u, 0xFFC24Cu, 0xFFC250u, 0xFFC19Cu, 0xFFC26Cu, 0xFFC270u, 0xFFC180u, 0xFFC184u})
        wr16(a, 1);
    wr16(0xFFC170, 0xFFFF);
    wr16(0xFFC190, 0xFFFF);
    wr16(0xFFC274, 0xFFFF);
    wr16(0xFFC1F8, 10);
    wr16(0xFFC350, 0xFFFF);
    wr16(0xFFC178, 0xFFFF);
    wr16(0xFFC17C, 0xFFFF);
    wr16(0xFFC280, 0xB);
    wr16(0xFFC282, 4);
    wr16(0xFFC284, 8);
    wr16(0xFFC286, 3);
    wr8(0xFFDC50, 1); // music on
    wr8(0xFFDC51, 1); // effects on
    wr8(0xFFBF7C, 1);
    wr16(0xFFDBFC, 0);
    wr16(0xFFC568, 1);
    wr16(0xFFC56A, 1);
    wr16(0xFFC56C, 2);
    wr16(0xFFC1AC, 2);
    wr16(0xFFC56E, 1);
    uint32_t t = rd32(0x4AA2C);
    wr16(0xFFC224, rd16(t + 0x1E + rd16(t + 0xE) * 2u));
    wr16(0xFFC228, rd16(t + rd16(t + 0x14) * 2u));
    wr16(0xFFC230, rd16(t + 4 + rd16(t + 0x10) * 2u));
    wr16(0xFFC22C, rd16(t + rd16(t + 0x12) * 2u));
    wr16(0xFFC234, rd16(t + rd16(t + 0xC) * 2u));
    wr16(0xFFC608, 0x400);
    wr32(0xFFC60C, 0xFF0400);
    uint16_t intro = 1;
    game_loaded = 0;
    while (!game_loaded) {
        uint16_t choice = TitleScreen(intro);
        if (!choice) break;
        intro = 0;
        if (choice == 1) {
            do {
                choice = OptionsScreen(0);
                if (choice == 3) {
                    game_loaded = PasswordScreen();
                    if (game_loaded) choice = 0;
                }
            } while (choice == 3);
        } else if (choice == 2) {
            Tutorial();
        } else if (choice == 3) {
            if (!Tutorial()) AttractDemo(0);
        }
    }
}

} // namespace game
