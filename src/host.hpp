// Host side: window, input, frame pacing, and the test plan runner.
#pragma once

#include <cstdint>
#include <string>

namespace host {

struct Options {
    bool wide = true;      // the battle in kWideW x kWideH (off headless)
    std::string plan;      // test plan, same format as tools/mame/harness.lua
    bool headless = false; // no window, no frame pacing
    std::string wav;       // headless: record the sound to this WAV file
    std::string saves;     // the missions reached (window: saves.txt beside the exe)
    std::string rom;       // ROM dump to extract data/ from, instead of asking
};

extern Options opt;

// The battle view's size, and the frame last shown: its size and where the
// console's 320 x 224 screen sits in it.
constexpr int kWideW = 800, kWideH = 448;
struct Screen {
    int w = 320, h = 224, ox = 0, oy = 0;
};
const Screen& screen();
extern uint32_t frame_count;

bool init(const Options& o);
// One VBlank: runs the game's VBlank handler (when the interrupt mask allows
// it), shows the frame, reads input and waits for the next 1/60 s.
// Translated code calls this wherever the original waits for the interrupt.
void frame();
void set_vblank_handler(void (*handler)());
// Reports an error (stderr, and a message box with a window), stops the
// sound and exits with status 2.
[[noreturn]] void fatal(const char* msg);
// Pad buttons held, active high: bit 7 Start, 6 A, 5 C, 4 B, 3 Right,
// 2 Left, 1 Down, 0 Up. From a gamepad, the test plan, the mouse buttons
// and the keyboard (arrows, Enter A, Shift C in battle).
uint8_t pad();

// The mouse in screen pixels; active from its last move or click until the
// d-pad is used. Left button 1, right 2; they also count as pad A and B.
struct Mouse {
    int x = 0, y = 0;
    uint8_t buttons = 0;
    bool active = false, inside = false;
};
const Mouse& mouse();
// The pad bits held by the mouse buttons alone.
uint8_t mouse_pad();
bool shift_held();
// A digit key pressed this frame or the last: digit | 0x100 with Ctrl |
// 0x200 with Shift; else -1.
int take_digit();
// A skippable stretch: from begin_skippable(true) until (false), a key,
// mouse or pad button press in the window makes the next frame() throw Skip.
struct Skip {};
void begin_skippable(bool on);
// A key, mouse or pad button pressed in the window (or the plan's
// "anykey") since clear_press().
bool press_pending();
// Esc pressed this frame or the last (taken once): it closes menus and
// opens the options in battle.
bool take_escape();
// Space pressed this frame or the last (taken once).
bool take_space();
// Mouse wheel steps since the last call, positive towards the user.
int take_wheel();
// Called each battle frame: the system mouse pointer is hidden meanwhile.
void mark_battle();
void clear_press();
// Letters typed (upper case) and Backspace ('\b'), oldest first; 0 when
// none. clear_chars() drops what is queued.
int take_char();
void clear_chars();
// Drawn over each composed frame.
void set_overlay(void (*draw)(uint32_t* pixels, int pitch));
// Draws the frame kWideW x kWideH instead of the console screen; false
// when it does not apply.
void set_wide_renderer(bool (*draw)(uint32_t* pixels, int pitch));

} // namespace host
