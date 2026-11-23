// Native 2D display: the tile and map memory the game draws into, palette,
// scroll tables and the screen settings, composed into a frame by the host.
//
// This stands in for the Mega Drive VDP without emulating it: there are no
// ports, registers or command words. The game's DMA queue and its direct
// writes become calls to the copy/fill functions below, and its register
// writes become the setters. Memory keeps the console's layout (tiles,
// nametables and the sprite table at the same VRAM addresses) so the
// translated code's addresses stay valid.
#pragma once

#include <cstdint>

namespace display {

extern uint8_t vram[0x10000];
extern uint16_t cram[64];
extern uint16_t vsram[40];

struct Config {
    bool enabled = false;      // display on (blanked when off)
    uint16_t plane_a = 0xE000; // nametable addresses
    uint16_t plane_b = 0xC000;
    uint16_t window = 0x6000;
    uint16_t sprites = 0xF000; // sprite attribute table
    uint16_t hscroll = 0xF400; // horizontal scroll table
    int plane_w = 64, plane_h = 32; // plane size in cells
    int hmode = 0;             // 0 whole screen, 2 per 8 lines, 3 per line
    bool vcolumns = false;     // vertical scroll per 2-cell column
    int width = 320;           // 320 or 256
    uint8_t win_h = 0, win_v = 0; // window position (VDP register values)
    int backdrop = 0;          // palette index of the backdrop
};
extern Config cfg;

void reset();

// Video memory writes (what the game's DMA and data port writes did).
void vram_w16(uint16_t a, uint16_t v);
uint16_t vram_r16(uint16_t a);
void vram_copy(uint16_t dest, const uint8_t* src, uint32_t bytes, int step = 2);
void vram_fill(uint16_t dest, uint16_t v, uint32_t bytes, int step = 1);
void vram_move(uint16_t dest, uint16_t src, uint32_t bytes, int step = 1);
void cram_w16(int i, uint16_t v); // i = colour index 0-63
void vsram_w16(int i, uint16_t v); // i = word index 0-39

// Composes the frame: width cfg.width x 224, ARGB8888, rows pitch apart.
void compose(uint32_t* out, int pitch);
uint32_t color(uint16_t c);

} // namespace display
