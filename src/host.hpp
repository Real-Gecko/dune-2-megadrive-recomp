// Host side: window, input, frame pacing, and the test plan runner.
#pragma once

#include <cstdint>
#include <string>

namespace host {

struct Options {
    std::string rom;       // the dump to extract the data from when data/ is missing
    int scale = 3;
    std::string plan;      // test plan, same format as tools/mame/harness.lua
    bool headless = false; // no window, no frame pacing
    std::string wav;       // headless: record the sound to this WAV file
};

extern Options opt;
extern uint32_t frame_count;

bool init(const Options& o);
// One VBlank: runs the game's VBlank handler (when the interrupt mask allows
// it), shows the frame, reads input and waits for the next 1/60 s.
// Translated code calls this wherever the original waits for the interrupt.
void frame();
void set_vblank_handler(void (*handler)());
// Pad buttons held, active high: bit 7 Start, 6 A, 5 C, 4 B, 3 Right,
// 2 Left, 1 Down, 0 Up.
uint8_t pad();

} // namespace host
