#include "host.hpp"

#include "assets.hpp"
#include "audio.hpp"
#include "display.hpp"
#include "md/mem.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace host {

Options opt;
uint32_t frame_count = 0;

static void (*vblank_handler)() = nullptr;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static SDL_Gamepad* gamepad = nullptr;
static constexpr int kPitch = kWideW;
static uint32_t pixels[kWideW * kWideH];
static Screen shown;
static bool (*wide_renderer)(uint32_t*, int) = nullptr;
static uint64_t next_tick = 0;
static uint8_t plan_held = 0;
static uint8_t plan_mods = 0; // bit 0 Shift
static Mouse mouse_state;
static int digit_key = -1;
static uint32_t digit_frame = 0;
static void (*overlay)(uint32_t*, int) = nullptr;
static bool skippable = false;
static bool real_press = false, pointer_hidden = false;
static uint32_t esc_frame = 0, space_frame = 0, battle_frame = 0;
static bool esc_down = false, space_down = false;
static int wheel = 0;
static int present_x = 0, present_y = 0, present_k = 1; // the frame's place in the window's logical size
static std::string typed;

enum : uint8_t { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, B = 16, C = 32, A = 64, START = 128 };

struct PlanStep {
    std::string cmd, arg;
};
static std::multimap<uint32_t, PlanStep> plan;

static uint8_t plan_button(const std::string& name) {
    static const std::map<std::string, uint8_t> names = {
        {"P1 Up", UP}, {"P1 Down", DOWN}, {"P1 Left", LEFT}, {"P1 Right", RIGHT},
        {"P1 A", A},   {"P1 B", B},       {"P1 C", C},       {"P1 Start", START},
    };
    auto it = names.find(name);
    return it == names.end() ? 0 : it->second;
}

static void quit() {
    audio::shutdown();
    SDL_Quit();
    std::exit(0);
}

void fatal(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    if (!opt.headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dune - The Battle for Arrakis", msg, window);
    audio::shutdown();
    SDL_Quit();
    std::exit(2);
}

static void save_snap(const std::string& path) {
    if (SDL_Surface* s = SDL_CreateSurfaceFrom(shown.w, shown.h, SDL_PIXELFORMAT_XRGB8888, pixels, kPitch * 4)) {
        SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

static void run_plan() {
    auto range = plan.equal_range(frame_count);
    for (auto it = range.first; it != range.second; ++it) {
        const PlanStep& s = it->second;
        if (s.cmd == "press") plan_held |= plan_button(s.arg);
        else if (s.cmd == "release") plan_held &= uint8_t(~plan_button(s.arg));
        else if (s.cmd == "snap") save_snap(s.arg);
        else if (s.cmd == "ram") {
            std::ofstream f(s.arg, std::ios::binary);
            f.write(reinterpret_cast<const char*>(md::ram), sizeof md::ram);
        } else if (s.cmd == "vram") {
            std::ofstream f(s.arg, std::ios::binary);
            f.write(reinterpret_cast<const char*>(display::vram), sizeof display::vram);
        } else if (s.cmd == "w16") {
            unsigned a, v;
            if (std::sscanf(s.arg.c_str(), "%x %x", &a, &v) == 2) md::wr16(a, uint16_t(v));
        } else if (s.cmd == "mouse") {
            std::sscanf(s.arg.c_str(), "%d %d", &mouse_state.x, &mouse_state.y);
            mouse_state.active = mouse_state.inside = true;
        } else if (s.cmd == "mdown") mouse_state.buttons |= uint8_t(std::atoi(s.arg.c_str()));
        else if (s.cmd == "mup") mouse_state.buttons &= uint8_t(~std::atoi(s.arg.c_str()));
        else if (s.cmd == "mods") plan_mods = uint8_t(std::atoi(s.arg.c_str()));
        else if (s.cmd == "digit") {
            digit_key = int(std::strtol(s.arg.c_str(), nullptr, 16));
            digit_frame = frame_count;
        } else if (s.cmd == "esc") {
            esc_down = true;
            esc_frame = frame_count;
        } else if (s.cmd == "space") {
            space_down = true;
            space_frame = frame_count;
        } else if (s.cmd == "wheel") {
            wheel += std::atoi(s.arg.c_str());
        } else if (s.cmd == "anykey") {
            real_press = true;
        } else if (s.cmd == "char") {
            typed += s.arg == "BS" ? std::string("\b") : s.arg;
        } else if (s.cmd == "exit") quit();
    }
}

// Ask for the ROM dump with the file dialog (blocks until it is answered).
static std::string ask_rom_path() {
    struct State {
        bool done = false;
        std::string path;
    } st;
    static const SDL_DialogFileFilter filters[] = {{"Mega Drive ROM", "gen;md;bin"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* ud, const char* const* files, int) {
            auto* s = static_cast<State*>(ud);
            if (files && files[0]) s->path = files[0];
            s->done = true;
        },
        &st, window, filters, 2, nullptr, false);
    while (!st.done) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 50) && e.type == SDL_EVENT_QUIT) quit();
    }
    return st.path;
}

// The game data from data/ beside the executable; on the first run it is
// extracted from the ROM dump asked for.
static bool load_data() {
    std::string dir = std::string(SDL_GetBasePath()) + "data/";
    std::string err;
    if (assets::load(dir, &err)) return true;
    std::string rom = !opt.rom.empty() ? opt.rom : opt.headless ? std::string() : ask_rom_path();
    if (rom.empty()) {
        std::fprintf(stderr, "%s\nNo ROM dump given.\n", err.c_str());
        return false;
    }
    if (assets::extract(rom, dir, &err) && assets::load(dir, &err)) return true;
    std::fprintf(stderr, "%s\n", err.c_str());
    if (!opt.headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dune - The Battle for Arrakis", err.c_str(), window);
    return false;
}

bool init(const Options& o) {
    opt = o;
    if (!opt.plan.empty()) {
        std::ifstream pf(opt.plan);
        std::string line;
        while (std::getline(pf, line)) {
            std::istringstream ss(line);
            uint32_t fr;
            PlanStep s;
            if (!(ss >> fr >> s.cmd)) continue;
            std::getline(ss >> std::ws, s.arg);
            plan.emplace(fr, s);
        }
    }
    if (!SDL_Init(opt.headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!opt.headless) {
        if (!SDL_CreateWindowAndRenderer("Dune - The Battle for Arrakis", kWideW * 2, kWideH * 2,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
            std::fprintf(stderr, "window: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer, 1);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kWideW, kWideH);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    audio::init(!opt.headless, opt.headless ? opt.wav : std::string());
    display::reset();
    if (!load_data()) return false;
    if (!opt.headless && opt.saves.empty()) opt.saves = std::string(SDL_GetBasePath()) + "saves.txt";
    next_tick = SDL_GetTicksNS();
    return true;
}

void set_vblank_handler(void (*handler)()) { vblank_handler = handler; }

static bool in_battle() { return frame_count - battle_frame <= 2; }

// Keyboard: the arrows the d-pad, Enter A, Shift C in battle (the faster
// scroll). Esc is taken by the screens that close with it (take_escape).
static uint8_t key_buttons() {
    const bool* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_UP]) b |= UP;
    if (k[SDL_SCANCODE_DOWN]) b |= DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= RIGHT;
    if (k[SDL_SCANCODE_RETURN]) b |= A;
    if (in_battle() && (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT])) b |= C;
    return b;
}

static uint8_t pad_buttons() {
    if (!gamepad) return 0;
    auto btn = [](SDL_GamepadButton x) { return SDL_GetGamepadButton(gamepad, x); };
    int lx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    int ly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    uint8_t b = 0;
    if (btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000) b |= UP;
    if (btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000) b |= DOWN;
    if (btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -16000) b |= LEFT;
    if (btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 16000) b |= RIGHT;
    if (btn(SDL_GAMEPAD_BUTTON_START)) b |= START;
    if (btn(SDL_GAMEPAD_BUTTON_WEST)) b |= A;
    if (btn(SDL_GAMEPAD_BUTTON_SOUTH)) b |= B;
    if (btn(SDL_GAMEPAD_BUTTON_EAST)) b |= C;
    return b;
}

static uint8_t mouse_buttons() {
    return uint8_t((mouse_state.buttons & 1 ? A : 0) | (mouse_state.buttons & 2 ? B : 0));
}

uint8_t pad() {
    if (opt.headless) return uint8_t(plan_held | mouse_buttons());
    uint8_t b = uint8_t(plan_held | key_buttons() | pad_buttons());
    if (b & 0xF) mouse_state.active = false;
    return uint8_t(b | mouse_buttons());
}

const Mouse& mouse() { return mouse_state; }

uint8_t mouse_pad() {
    if (opt.headless) return uint8_t(mouse_buttons() & ~plan_held);
    return uint8_t(mouse_buttons() & ~(plan_held | key_buttons() | pad_buttons()));
}

bool shift_held() { return (plan_mods & 1) || (!opt.headless && (SDL_GetModState() & SDL_KMOD_SHIFT)); }

int take_digit() {
    int d = frame_count - digit_frame <= 1 ? digit_key : -1;
    digit_key = -1;
    return d;
}

void begin_skippable(bool on) {
    skippable = on;
    real_press = false;
}

const Screen& screen() { return shown; }

void set_wide_renderer(bool (*draw)(uint32_t*, int)) { wide_renderer = draw; }

bool press_pending() { return real_press; }

bool take_escape() {
    bool e = esc_down && frame_count - esc_frame <= 1;
    esc_down = false;
    return e;
}

bool take_space() {
    bool s = space_down && frame_count - space_frame <= 1;
    space_down = false;
    return s;
}

int take_wheel() {
    int w = wheel;
    wheel = 0;
    return w;
}

void mark_battle() { battle_frame = frame_count; }
void clear_press() { real_press = false; }

int take_char() {
    if (typed.empty()) return 0;
    int c = typed[0];
    typed.erase(0, 1);
    return c;
}
void clear_chars() { typed.clear(); }

void set_overlay(void (*draw)(uint32_t*, int)) { overlay = draw; }

static void present() {
    // With the wide battle view the window keeps its size: other screens
    // are shown twice as large in its middle.
    int lw = shown.w, lh = shown.h;
    present_k = 1;
    if (opt.wide && shown.w < kWideW) {
        lw = kWideW;
        lh = kWideH;
        present_k = 2;
    }
    present_x = (lw - shown.w * present_k) / 2;
    present_y = (lh - shown.h * present_k) / 2;
    SDL_UpdateTexture(texture, nullptr, pixels, kPitch * 4);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderClear(renderer);
    SDL_FRect src{0, 0, float(shown.w), float(shown.h)};
    SDL_FRect dst{float(present_x), float(present_y), float(shown.w * present_k), float(shown.h * present_k)};
    SDL_RenderTexture(renderer, texture, &src, &dst);
    SDL_RenderPresent(renderer);
}

// The game and its sound stand still while the window is not focused.
static void wait_for_focus() {
    audio::pause(true);
    mouse_state.buttons = 0;
    SDL_Event e;
    while (SDL_WaitEvent(&e) && e.type != SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if (e.type == SDL_EVENT_QUIT) quit();
        if (e.type == SDL_EVENT_WINDOW_EXPOSED) present();
    }
    audio::pause(false);
    next_tick = SDL_GetTicksNS();
}

void frame() {
    if (vblank_handler && md::int_mask < 6) vblank_handler();
    if (skippable && real_press) {
        skippable = false;
        throw Skip{};
    }
    if (opt.wide && wide_renderer && wide_renderer(pixels, kPitch)) {
        shown = {kWideW, kWideH, (kWideW - 320) / 2, (kWideH - 224) / 2};
    } else {
        display::compose(pixels, kPitch);
        shown = {display::cfg.width, 224, 0, 0};
    }
    if (overlay) overlay(pixels, kPitch);
    run_plan();
    frame_count++;
    if (opt.headless) {
        audio::frame();
        return;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit();
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) wait_for_focus();
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT) && !e.key.repeat)
            SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode >= SDL_SCANCODE_1 &&
            e.key.scancode <= SDL_SCANCODE_0) {
            digit_key = (e.key.scancode - SDL_SCANCODE_1 + 1) % 10 | (e.key.mod & SDL_KMOD_CTRL ? 0x100 : 0) |
                        (e.key.mod & SDL_KMOD_SHIFT ? 0x200 : 0);
            digit_frame = frame_count;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            SDL_ConvertEventToRenderCoordinates(renderer, &e);
            float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
            float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            mouse_state.x = std::clamp((int(x) - present_x) / present_k, 0, shown.w - 1);
            mouse_state.y = std::clamp((int(y) - present_y) / present_k, 0, shown.h - 1);
            mouse_state.active = true;
            mouse_state.inside = true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            uint8_t bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
            if (e.button.down) mouse_state.buttons |= bit;
            else mouse_state.buttons &= uint8_t(~bit);
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) wheel -= e.wheel.integer_y;
        if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) mouse_state.inside = false;
        if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            real_press = true;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && !e.key.repeat) {
            esc_down = true;
            esc_frame = frame_count;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE && !e.key.repeat) {
            space_down = true;
            space_frame = frame_count;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && typed.size() < 16) {
            if (e.key.key >= SDLK_A && e.key.key <= SDLK_Z && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)))
                typed += char('A' + (e.key.key - SDLK_A));
            else if (e.key.key == SDLK_BACKSPACE)
                typed += '\b';
        }
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) gamepad = SDL_OpenGamepad(e.gdevice.which);
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
        }
    }
    if (bool hide = in_battle(); hide != pointer_hidden) {
        pointer_hidden = hide;
        if (hide) SDL_HideCursor();
        else SDL_ShowCursor();
    }
    present();
    const uint64_t period = 1000000000ull / 60;
    next_tick += period;
    uint64_t now = SDL_GetTicksNS();
    if (next_tick > now) SDL_DelayNS(next_tick - now);
    else if (now - next_tick > period * 4) next_tick = now;
}

} // namespace host
