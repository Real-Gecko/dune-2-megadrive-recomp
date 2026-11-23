#include "host.hpp"

#include "assets.hpp"
#include "audio.hpp"
#include "display.hpp"
#include "md/mem.hpp"

#include <SDL3/SDL.h>

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
static uint32_t pixels[320 * 224];
static uint64_t next_tick = 0;
static uint8_t plan_held = 0;

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

static void save_snap(const std::string& path) {
    int w = display::cfg.width;
    if (SDL_Surface* s = SDL_CreateSurfaceFrom(w, 224, SDL_PIXELFORMAT_XRGB8888, pixels, 320 * 4)) {
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
// extracted from the ROM dump (--rom, or asked for).
static bool load_data() {
    std::string dir = std::string(SDL_GetBasePath()) + "data/";
    std::string err;
    if (assets::load(dir, &err)) return true;
    std::string rom = opt.rom;
    if (rom.empty() && !opt.headless) rom = ask_rom_path();
    if (rom.empty()) {
        std::fprintf(stderr, "%s\nNo ROM dump given (--rom PATH).\n", err.c_str());
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
        if (!SDL_CreateWindowAndRenderer("Dune - The Battle for Arrakis", 320 * opt.scale, 224 * opt.scale,
                                         SDL_WINDOW_RESIZABLE, &window, &renderer)) {
            std::fprintf(stderr, "window: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer, 0);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 224);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    audio::init(!opt.headless, opt.headless ? opt.wav : std::string());
    display::reset();
    if (!load_data()) return false;
    next_tick = SDL_GetTicksNS();
    return true;
}

void set_vblank_handler(void (*handler)()) { vblank_handler = handler; }

// Keyboard: WASD the d-pad, numpad 1/2/3 A/B/C, Space or Enter Start.
static uint8_t key_buttons() {
    const bool* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_W]) b |= UP;
    if (k[SDL_SCANCODE_S]) b |= DOWN;
    if (k[SDL_SCANCODE_A]) b |= LEFT;
    if (k[SDL_SCANCODE_D]) b |= RIGHT;
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN]) b |= START;
    if (k[SDL_SCANCODE_KP_1]) b |= A;
    if (k[SDL_SCANCODE_KP_2]) b |= B;
    if (k[SDL_SCANCODE_KP_3]) b |= C;
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

uint8_t pad() {
    if (opt.headless) return plan_held;
    return uint8_t(plan_held | key_buttons() | pad_buttons());
}

void frame() {
    if (vblank_handler && md::int_mask < 6) vblank_handler();
    display::compose(pixels, 320);
    run_plan();
    frame_count++;
    if (opt.headless) {
        audio::frame();
        return;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit();
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) quit();
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT) && !e.key.repeat)
            SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) gamepad = SDL_OpenGamepad(e.gdevice.which);
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
        }
    }
    int w = display::cfg.width;
    SDL_UpdateTexture(texture, nullptr, pixels, 320 * 4);
    SDL_SetRenderLogicalPresentation(renderer, w, 224, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderClear(renderer);
    SDL_FRect src{0, 0, float(w), 224};
    SDL_RenderTexture(renderer, texture, &src, nullptr);
    SDL_RenderPresent(renderer);
    const uint64_t period = 1000000000ull / 60;
    next_tick += period;
    uint64_t now = SDL_GetTicksNS();
    if (next_tick > now) SDL_DelayNS(next_tick - now);
    else if (now - next_tick > period * 4) next_tick = now;
}

} // namespace host
