// Dune - The Battle for Arrakis native port: the 68000 game code translated
// to C++, on a native display.
#include "game/game.hpp"
#include "host.hpp"
#include "md/mem.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    host::Options o;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (k == "--rom") o.rom = next();
        else if (k == "--scale") o.scale = std::atoi(next().c_str());
        else if (k == "--plan") o.plan = next();
        else if (k == "--headless") o.headless = true;
        else if (k == "--wav") o.wav = next();
    }
    if (!host::init(o)) return 1;
    game::Reset();
}
