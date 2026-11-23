// The side panel: the portrait and health bar of what is selected, its
// item icon with a progress bar, and the "no power" marker. Sprite nodes:
// 0xFFC732 portrait, 0xFFC736 health bar, 0xFFC73C item icon, 0xFFC740
// progress bar, 0xFFC750 marker; caches of what they show at 0xFFC734,
// 0xFFC738, 0xFFC73E, 0xFFC742.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U32 selected_struct{0xFFC578};
constexpr U32 selected_unit{0xFFC25C};

void Hide(uint16_t n) { U8{Node(n) + 7} |= 0x80; }
void Show(uint16_t n) { U8{Node(n) + 7} &= 0x7F; }
int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t StructInfo(int16_t type) { return rd32(0x6B954 + type * 4); }
uint32_t UnitTypeInfo(int16_t type) { return rd32(0x6C5BC + type * 4); }

// 0x9772: a bar of 32 steps (q, 0..31) drawn into four tiles of the buffer
// in a table (buffer, DMA destination and length, sprite node word): rows
// 2-7, full tiles in the pattern, a 0xC frame, the partial tile masked.
void DrawBar(uint16_t q, uint32_t pattern, uint32_t table) {
    uint16_t off = (q & 7) * 4;
    int tile = (q & 0x18) >> 3;
    uint32_t mask = rd32(0x9752 + off), edge = rd32(0x979E + off);
    uint32_t fill[4], cap[4];
    for (int k = 0; k < 4; k++) {
        if (k < tile) {
            fill[k] = pattern;
            cap[k] = 0xCCCCCCCC;
        } else if (k == tile) {
            fill[k] = (pattern & mask) | edge;
            cap[k] = (0xCCCCCCCC & mask) | edge;
        } else {
            fill[k] = cap[k] = 0;
        }
    }
    fill[0] = (fill[0] & 0x0FFFFFFF) | 0xC0000000;
    uint32_t buf = rd32(table);
    for (int k = 0; k < 4; k++) {
        uint32_t t = buf + 8 + k * 0x20u;
        wr32(t, cap[k]);
        for (int r = 1; r <= 4; r++) wr32(t + r * 4u, fill[k]);
        wr32(t + 20, cap[k]);
    }
    QueueDma(2, buf, rd32(table + 4));
}

// value * 32 / max in 16 bits, one less when exact.
uint16_t BarSteps(uint16_t value, uint16_t max) {
    uint16_t n = uint16_t(value << 5);
    uint16_t q = n / max;
    if (!(n % max)) q--;
    return q;
}

// 0x9866: the progress bar (frame pos: its position object).
void ProgressBar(uint32_t pos, uint32_t pattern, uint16_t max, uint16_t value) {
    if (int16_t(value) <= 0) {
        HideProgressBar();
        return;
    }
    if (int16_t(max) <= 0) return;
    uint32_t key = uint32_t(value) << 16 | max;
    if (key == U32{0xFFC742}) return;
    U32{0xFFC742} = key;
    wr32(Node(U16{0xFFC740}), pos);
    if (int16_t(max) < int16_t(value)) value = max;
    DrawBar(BarSteps(value, max), pattern, 0x98D4);
    Show(rd16(rd32(0x98DC)));
}

// 0x9946, 0x98E0: the item icon and the portrait show a class's graphics
// (0x60E8C; palette line by the +4 byte, 0xFE or 0xFF).
void ShowGraphic(int16_t cls, U16 cache, U16 node, uint32_t dest) {
    if (cls <= 0) return;
    if (cls != int16_t(cache)) {
        cache = uint16_t(cls);
        uint32_t t = 0x60E8C + uint16_t(cls << 3);
        uint16_t pal;
        if (rd8(t + 4) == 0xFE) pal = 0x2000;
        else if (rd8(t + 4) == 0xFF) pal = 0x6000;
        else return;
        uint32_t n = Node(node);
        wr16(n + 6, uint16_t((rd16(n + 6) & 0x9FFF) | pal));
        QueueDma(2, rd32(t), dest);
    }
    Show(node);
}
void ShowPortrait(int16_t cls) { ShowGraphic(cls, U16{0xFFC734}, U16{0xFFC732}, 0xF28000C0); }

// 0x96CA: the health bar, red, yellow or green by how full.
void HealthBar(uint16_t value, uint16_t max) {
    if (!max || !value) return;
    uint32_t key = uint32_t(value) << 16 | max;
    if (key == U32{0xFFC738}) return;
    U32{0xFFC738} = key;
    if (int16_t(max) < int16_t(value)) value = max;
    uint16_t q = BarSteps(value, max);
    DrawBar(q, rd32(0x9742 + ((q & 0x18) >> 1)), 0x9736);
    Show(rd16(rd32(0x973E)));
}

// 0x9B84: portrait and health bar hidden.
void HidePortrait() {
    Hide(U16{0xFFC732});
    Hide(U16{0xFFC736});
    U32{0xFFC738} = 0xFFFFFFFF;
}

// 0x12148: the status message (0xFFC030) for the structure on the
// tactical cell.
uint16_t StructureStatusText() {
    uint16_t id = 0;
    if (uint32_t s = StructureAt(U16{0xFFC248})) {
        if (rd16(StructInfo(int8_t(rd8(s + 2))) + 0xC) & 2) {
            if (rd16(s + 6) & 2) {
                U16{0xFFC030} = 0x90;
                return 0x90;
            }
            if (rd8(s + 3) == 0xFF) {
                id = 0x29;
            } else if (rd16(s + 4) & 0x4000) {
                id = 0x28;
            } else if (rd16(s + 0x56)) {
                id = 0x2E;
            } else {
                id = rd8(s + 2) == 8 ? 0x26 : 0x27;
                U8{s + 6} &= uint8_t(~0x40);
                U8{s + 6} |= 0x20;
            }
        } else if (rd8(s + 2) == 2) {
            id = uint16_t(rd16(0x6C76C + HouseOf(s) * 30) + 0x29);
        }
    }
    U16{0xFFC030} = id;
    return id;
}

} // namespace

void ShowIcon(int16_t cls) { ShowGraphic(cls, U16{0xFFC73E}, U16{0xFFC73C}, 0xF4C000C0); }

// 0x9BA2
void HideIcon() { Hide(U16{0xFFC73C}); }

// 0x9BB8
void HideProgressBar() {
    Hide(U16{0xFFC740});
    U32{0xFFC742} = 0xFFFFFFFF;
}

// 0x9BAE
void HidePanelIcon() {
    HideIcon();
    HideProgressBar();
}

// 0x9CE0, 0x9CF4: the "no power" marker.
void HideBuildMarker() {
    Hide(U16{0xFFC750});
    U16{0xFFC724} = 0;
}
void ShowBuildMarker() {
    Show(U16{0xFFC750});
    U16{0xFFC724} = 1;
}

// 0x294C2: the panel for the selected structure or unit.
void UpdatePanel() {
    uint32_t s = selected_struct;
    uint32_t u = selected_unit;
    if (s && !u && (rd16(s + 6) & 0x8000)) {
        uint8_t type = rd8(s + 2);
        uint32_t info = StructInfo(int8_t(type));
        ShowPortrait(int16_t(rd16(info + 0x14)));
        HealthBar(rd16(s + 0x12), rd16(info + 0x10));
        if (HouseOf(s) != int16_t(player_house)) {
            HidePanelIcon();
            HideProgressBar();
            HideBuildMarker();
            return;
        }
        if (rd16(s + 6) & 2) {
            ShowIcon(0x40);
            ProgressBar(0x6A7E2, 0x77777777, 100, uint16_t(100 + int8_t(-rd8(s + 0x55))));
            if (!(rd16(s + 6) & 0x100)) HideBuildMarker();
            return;
        }
        if (rd16(s + 0x4C) != player_house) {
            if (int8_t(rd8(s + 3)) >= 0 && (rd16(info + 0xC) & 2)) {
                uint16_t i = uint16_t(int8_t(rd8(s + 3)));
                uint32_t o = type == 8 ? StructureGet(i) : UnitGet(i);
                wr16(s + 0x52, uint16_t(int8_t(rd8(o + 2))));
            } else {
                wr8(s + 3, 0xFF);
            }
        }
        bool marker;
        if (type == 8) marker = (rd16(s + 6) & 0x100) && rd16(s + 0x52) != 1 && rd16(s + 0x52) != 0xE;
        else if (type != 0xB && (rd16(s + 6) & 0x100)) marker = true;
        else marker = type == 5 && !U16{0xFFC728};
        if (marker) ShowBuildMarker();
        else HideBuildMarker();

        if (type == 0xB) {
            HidePanelIcon();
            uint32_t h = HouseGet(uint16_t(HouseOf(s)));
            if (int16_t(rd16(h + 0x30)) > -1) {
                uint16_t max = rd16(0x6C768 + HouseOf(s) * 30);
                ProgressBar(0x6A7E8, 0x77777777, max, uint16_t(max - rd16(h + 0x2E)));
            }
            return;
        }
        if (rd16(info + 0xC) & 2) {
            int16_t item = int16_t(rd16(s + 0x52));
            uint32_t ii = type == 8 ? StructInfo(item) : UnitTypeInfo(item);
            if (item >= 0) ShowIcon(int16_t(rd16(ii + 0x14)));
            if (!(rd16(s + 6) & 0x4000)) {
                HideProgressBar();
                return;
            }
            uint16_t cell = U16{0xFFC248};
            U16{0xFFC248} = PosCell(rd32(s + 0xA));
            StructureStatusText();
            U16{0xFFC248} = cell;
            uint16_t max = rd16(ii + 0x18);
            ProgressBar(0x6A7E2, 0x77777777, max, uint16_t(max - (rd16(s + 0x56) >> 8)));
            return;
        }
        if (type == 0xD && int8_t(rd8(s + 3)) >= 0) {
            uint32_t v = UnitGet(uint16_t(int8_t(rd8(s + 3))));
            ShowIcon(int16_t(rd16(UnitTypeInfo(int8_t(rd8(v + 2))) + 0x14)));
            ProgressBar(0x6A7E2, 0x77777777, rd16(v + 0x5A), uint16_t(rd16(v + 0x5A) - rd16(s + 0x56)));
            return;
        }
        if (type == 2) {
            ShowIcon(rd8(s + 8) == 1 ? 0x5E : int16_t(rd16(UnitTypeInfo(int16_t(rd16(s + 0x52))) + 0x14)));
            if (rd16(s + 0x4E)) {
                ShowBuildMarker();
                HideProgressBar();
                return;
            }
            HideBuildMarker();
            uint16_t max = rd16(0x6C766 + HouseOf(s) * 30);
            if (rd16(s + 0x56)) ProgressBar(0x6A7E2, 0x77777777, max, uint16_t(max - rd16(s + 0x56)));
            else HideProgressBar();
            return;
        }
        HideIcon();
        uint32_t h = U32{0xFFC278};
        if (type == 9) {
            uint16_t v = rd16(h + 0x1C);
            ProgressBar(0x6A7E8, 0x77777777, rd16(h + 0x1A), v ? v : 1);
        } else if (type == 0xC || type == 0x11) {
            uint32_t v = rd32(h + 0x12), max = rd32(h + 0x16);
            while (max > 0x7FF) {
                max >>= 1;
                v >>= 1;
            }
            ProgressBar(0x6A7E8, 0x33333333, uint16_t(max), uint16_t(v ? v : 1));
        } else {
            HideProgressBar();
        }
        return;
    }
    HideBuildMarker();
    if (!u || !(rd16(u + 6) & 0x8000)) {
        HidePortrait();
        HidePanelIcon();
        return;
    }
    uint8_t type = rd8(u + 2);
    uint32_t info = UnitTypeInfo(int8_t(type));
    if (rd8(u + 8) == 3 && type != 0x19) ShowPortrait(type == 3 ? 0x5E : 0x68);
    else ShowPortrait(int16_t(rd16(info + 0x14)));
    uint16_t hp = rd16(u + 0x12);
    if (!hp) {
        HidePortrait();
        HidePanelIcon();
        DropSelection();
        return;
    }
    HealthBar(hp, rd16(info + 0x10));
    if (type == 0x10) ProgressBar(0x6A7E8, 0x33333333, 100, uint16_t(int8_t(rd8(u + 0x5E))));
    else HidePanelIcon();
    if (HouseOf(u) != int16_t(player_house)) ShowCursorShadow();
}

} // namespace game
