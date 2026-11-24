// Port addition, not in the ROM: mouse orders and a selection of several
// units. The first selected unit is the game's own selection (0xFFC25C,
// shown by the side panel); the others get the same order one after the
// other and are stacked below the panel. Mouse clicks latch 'A' like the
// pad, a drag selects the player's units inside it (Shift: adds them),
// Shift+click adds or drops a unit. Ctrl+digit stores the selected units
// as a group, or with no unit selected the player's selected structure; a
// digit selects the group (twice quickly: the view centres on it),
// Shift+digit adds its units. R toggles repair mode: the cursor is the
// repair mark, A (a click) on one of the player's damaged structures
// starts its repair, on a damaged unit sends it to a repair facility; B
// leaves the mode. Q likewise toggles sell mode (the build menu's "$"
// icon as the cursor): a structure clicked is demolished for half its
// price; a unit clicked drives to the nearest factory or repair facility
// and is sold there for half its price. Space centres the view on the last
// event, a click or drag on the radar centres it there, Esc opens the
// options.
#include "game.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace game {

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U16 game_mode{0xFFC264};
constexpr U32 selected_unit{0xFFC25C};
constexpr U32 selected_struct{0xFFC578};
constexpr U16 special_armed{0xFFDC4C};
constexpr U8 sfx_on{0xFFDC51};
constexpr U16 view_x{0xFFE3BE};
constexpr U16 view_y{0xFFE3C0};

struct Input {
    bool click = false, box = false, shift = false, repair = false, sell = false;
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // map pixels
    int digit = -1;
    bool radar = false;
    int16_t rx = 0, ry = 0; // map pixels to centre the view on
    bool event = false;
};

std::vector<uint32_t> sel;
std::vector<uint32_t> groups[10];
uint32_t group_structure[10]; // a group of no units: one of the player's structures
// Units off to be repaired and the group each rejoins when back on the map.
struct Repairing {
    uint32_t unit;
    int group;
};
std::vector<Repairing> repairing;
Input pending, latched;
uint8_t buttons_seen = 0;
uint32_t polled_frame = 0;
bool left_down = false, dragging = false, radar_held = false;
enum Mode { kCommand, kRepair, kSell } mode = kCommand;
// The repair mark's frame (0x659C6) centred on the cursor, kept past the
// end of the cartridge ROM.
constexpr uint32_t kHammer = 0x100000;
bool repair_ok = false;
int16_t drag_x = 0, drag_y = 0, drag_x1 = 0, drag_y1 = 0;
// The last event's place in map pixels (NoteEvent), x -1 for none.
int16_t event_x = -1, event_y = 0;
// The first of the stacked units shown (StackArea).
int stack_top = 0;
constexpr int kStackRow = 36; // a portrait (24), its health bar (8, as the panel's) and a gap

int16_t HouseOf(uint32_t o) { return int16_t(int8_t(rd8(o + 8))); }
uint32_t UnitInfo(uint32_t u) { return rd32(0x6C5BC + int16_t(int8_t(rd8(u + 2))) * 4); }
bool Has(const std::vector<uint32_t>& v, uint32_t u) { return std::find(v.begin(), v.end(), u) != v.end(); }

bool Alive(uint32_t u) {
    for (uint16_t i = 0; i < U16{0xFFDCBC}; i++)
        if (rd32(0xFFDCC0 + i * 4u) == u) return true;
    return false;
}

// A structure on the list and not in ruins.
bool StructureStands(uint32_t s) {
    for (uint16_t i = 0; i < U16{0xFFD318}; i++)
        if (rd32(0xFFD31C + i * 4u) == s) return rd16(s + 0x22) != 1;
    return false;
}

bool OwnStructure(uint32_t s) { return s && StructureStands(s) && HouseOf(s) == int16_t(player_house); }

// One of the player's units on the map that takes orders (not a saboteur,
// nothing flying, not taken off the map as a harvester in a refinery).
bool Selectable(uint32_t u) {
    return u && Alive(u) && HouseOf(u) == int16_t(player_house) && rd8(u + 2) != 6 && (rd16(u + 4) & 6) == 2 &&
           rd16(UnitInfo(u) + 0x3E) != 4;
}

uint16_t CellOf(int16_t x, int16_t y) { return uint16_t(((x >> 5) & 0x3F) | ((y & 0x7E0) << 1)); }

void Deselect() {
    if (uint32_t u = selected_unit) {
        U16{u + 6} &= 0x5FFF;
        selected_unit = 0;
        DropSelection();
    }
}

// The game's selection set to a unit, as button A selects one (0x27B50).
void MakeLead(uint32_t v, bool loud) {
    if (uint32_t u = selected_unit; u && u != v) U16{u + 6} &= 0x5FFF;
    HidePanelIcon();
    DropSelection();
    wr8(0xFFBF7E, 1);
    if (loud) PlaySoundEffect(0x26);
    U16{v + 6} |= 0x8000;
    U8{v + 6} |= 0x20;
    if (loud) PlaySoundEffect(rd16(UnitInfo(v) + 0x3E) ? 0xF : 0x11);
    selected_unit = v;
    if (uint32_t s = selected_struct) U16{s + 6} &= 0x7FFF;
}

void SelectOnly(uint32_t u) {
    Deselect();
    sel = {u};
    MakeLead(u, true);
}

// Units gone from the map leave the selection; when the game changes its
// own selection, the list follows it.
void Sync() {
    uint32_t old = sel.empty() ? 0 : sel[0];
    size_t n = sel.size();
    std::erase_if(sel, [](uint32_t u) { return !Selectable(u); });
    uint32_t u = selected_unit;
    if (n > 1 && (sel.empty() || sel[0] != old)) {
        if (u == old) {
            Deselect();
            u = 0;
        }
        if (!u && !sel.empty()) MakeLead(sel[0], false);
        u = selected_unit;
    }
    if (u == (sel.empty() ? 0 : sel[0])) return;
    if (u && Selectable(u)) sel = {u};
    else sel.clear();
}

void Toggle(uint32_t t) {
    auto it = std::find(sel.begin(), sel.end(), t);
    if (it == sel.end()) {
        if (sel.empty()) {
            SelectOnly(t);
        } else {
            sel.push_back(t);
            PlaySoundEffect(0x26);
        }
        return;
    }
    bool lead = it == sel.begin();
    sel.erase(it);
    if (!lead) return;
    Deselect();
    if (!sel.empty()) MakeLead(sel[0], false);
}

void Add(const std::vector<uint32_t>& units, bool keep) {
    if (!keep) {
        Deselect();
        sel.clear();
    }
    for (uint32_t u : units)
        if (!Has(sel, u)) sel.push_back(u);
    if (!sel.empty() && selected_unit != sel[0]) MakeLead(sel[0], true);
    else if (keep && !units.empty()) PlaySoundEffect(0x26);
}

void BoxSelect(const Input& c) {
    int16_t x0 = std::min(c.x0, c.x1), x1 = std::max(c.x0, c.x1);
    int16_t y0 = std::min(c.y0, c.y1), y1 = std::max(c.y0, c.y1);
    std::vector<uint32_t> found;
    for (uint16_t i = 0; i < U16{0xFFDCBC}; i++) {
        uint32_t u = rd32(0xFFDCC0 + i * 4u);
        if (!Selectable(u)) continue;
        uint32_t pos = rd32(u + 0xA);
        int16_t x = int16_t(uint16_t(pos) >> 3), y = int16_t(uint16_t(pos >> 16) >> 3);
        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) found.push_back(u);
    }
    Add(found, c.shift);
}

// The view jumps to centre on a map pixel, in steps that draw the
// uncovered rows and columns, kept inside the map's scroll limits.
void CentreAt(int cx, int cy) {
    int x = std::clamp(cx - 0xA0, int(S16{0xFFE3D2}), int(S16{0xFFE3D4}) - 1);
    int y = std::clamp(cy - 0x70, int(S16{0xFFE3CE}), int(S16{0xFFE3D0}) - 1);
    for (;;) {
        int dx = std::clamp(x - int16_t(view_x), -14, 14), dy = std::clamp(y - int16_t(view_y), -14, 14);
        if (!dx && !dy) break;
        ScrollView(int16_t(dx), int16_t(dy));
    }
}

// The view centred on the objects (units or structures, position at +0xA).
void CentreOn(const std::vector<uint32_t>& objects) {
    int32_t sx = 0, sy = 0;
    for (uint32_t o : objects) {
        uint32_t pos = rd32(o + 0xA);
        sx += uint16_t(pos) >> 3;
        sy += uint16_t(pos >> 16) >> 3;
    }
    int n = int(objects.size());
    CentreAt(sx / n, sy / n);
}

// The game's own button A comes first: a special weapon armed or under way
// (0xFFC1A0), placing a structure (game mode 2), or the selected yard
// holding one ready to place.
bool Busy() {
    if (special_armed || U32{0xFFC1A0} || game_mode == 2) return true;
    uint32_t s = selected_struct;
    return s && HouseOf(s) == int16_t(player_house) && rd8(s + 2) == 8 && (rd16(s + 6) & 0xA000) == 0xA000;
}

// One of the player's structures selected as button A on it selects one
// (0x27B50); already selected it stays so (a second A opens its menu).
void SelectStructure(uint32_t s) {
    if (s == selected_struct && (rd16(s + 6) & 0x8000)) return;
    Deselect();
    sel.clear();
    uint8_t key = rd8(0xFFBF2B);
    U16{0xFFC240} = PosCell(rd32(s + 0xA));
    wr8(0xFFBF2B, 'A');
    HandleInput();
    wr8(0xFFBF2B, key);
}

// Ctrl+digit (0x100) stores the selected units as the group, or with none
// the player's selected structure; a unit or structure is in one group
// only. The digit selects the group, with Shift (0x200) adding its units;
// twice quickly the view centres on it.
void Group(int d) {
    static int last_digit = -1;
    static uint32_t last_frame = 0;
    int k = d & 0xFF;
    if (d & 0x100) {
        uint32_t s = selected_struct;
        if (!sel.empty() || !OwnStructure(s) || !(rd16(s + 6) & 0x8000)) s = 0;
        for (int i = 0; i < 10; i++) {
            std::erase_if(groups[i], [](uint32_t u) { return Has(sel, u); });
            if (s && group_structure[i] == s) group_structure[i] = 0;
        }
        groups[k] = sel;
        group_structure[k] = s;
        last_digit = -1;
        return;
    }
    std::vector<uint32_t> units;
    for (uint32_t u : groups[k])
        if (Selectable(u)) units.push_back(u);
    uint32_t s = units.empty() && OwnStructure(group_structure[k]) ? group_structure[k] : 0;
    if (units.empty() && !s) return;
    bool twice = d == last_digit && host::frame_count - last_frame <= 20;
    last_digit = d;
    last_frame = host::frame_count;
    if (!s) Add(units, (d & 0x200) && !sel.empty());
    else if (!Busy()) SelectStructure(s);
    if (twice) CentreOn(s ? std::vector<uint32_t>{s} : units);
}

// One unit takes the order button A gives at the cell.
void OrderUnit(uint32_t u, uint16_t cell) {
    if (uint32_t v = selected_unit; v && v != u) U16{v + 6} &= 0x5FFF;
    selected_unit = u;
    U16{u + 6} |= 0x8000;
    U8{u + 6} |= 0x20;
    U16{0xFFC240} = cell;
    wr8(0xFFBF2B, 'A');
    HandleInput();
}

// Each selected unit takes the order; the acknowledgements are heard once.
void OrderAll(uint16_t cell) {
    std::vector<uint32_t> units = sel;
    uint8_t on = sfx_on;
    for (uint32_t u : units) {
        if (!Selectable(u)) continue;
        OrderUnit(u, cell);
        sfx_on = 0;
    }
    sfx_on = on;
    sel.clear();
    for (uint32_t u : units)
        if (Selectable(u)) sel.push_back(u);
    if (!sel.empty()) MakeLead(sel[0], false);
}

// Button A (latched) through the selection; false leaves it to the game.
bool Command(const Input& c) {
    if (Busy()) return false;
    if (c.click) U16{0xFFC240} = CellOf(c.x0, c.y0);
    else if (sel.size() < 2) return false;
    uint16_t cell = U16{0xFFC240};
    uint32_t t = UnitAt(cell);
    if (!Selectable(t)) t = 0;
    if (t) {
        if (c.click && c.shift) {
            Toggle(t);
            return true;
        }
        if (sel.size() == 1 && sel[0] == t) return false;
        SelectOnly(t);
        return true;
    }
    if (sel.empty()) return false;
    if (uint32_t s = StructureAt(cell); s && c.click && HouseOf(s) == int16_t(player_house)) {
        bool enter = std::any_of(sel.begin(), sel.end(), [s](uint32_t u) { return CanEnter(u, s) != 0; });
        if (!enter) {
            Deselect();
            sel.clear();
            return false;
        }
    }
    OrderAll(cell);
    return true;
}

void SetMode(Mode m) {
    if (m == mode) return;
    mode = m;
    PlaySoundEffect(0x26);
    if (m != kCommand) return;
    U16{0xFFBF62} = 0x7FFF; // the game's cursor shape set again
    U8{Node(U16{0xFFBF18}) + 7} &= 0x7F;
}

void MakeHammer() {
    if (md::rom.size() >= kHammer + 14) return;
    md::rom.resize(kHammer + 14);
    auto put = [](uint32_t a, uint16_t v) {
        md::rom[a] = uint8_t(v >> 8);
        md::rom[a + 1] = uint8_t(v);
    };
    for (uint32_t k = 0; k < 14; k += 2) put(kHammer + k, rd16(0x659C6 + k));
    put(kHammer + 6, uint16_t(-int16_t(rd16(0x659C8 + 2) / 2)));  // y offset: half the height
    put(kHammer + 12, uint16_t(-int16_t(rd16(0x659C8) / 2)));     // x offset: half the width
}

// Units on their way to be sold, and the structure each drives to.
struct Selling {
    uint32_t unit, structure;
};
std::vector<Selling> selling;

// The cells around a structure (16 offsets from its origin cell by layout,
// 0x6B83C, 0 unused).
uint32_t Ring(uint32_t s) {
    return 0x6B83C + uint32_t(rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x3C) << 5);
}
uint16_t Origin(uint32_t s) { return PosCell((rd32(s + 0xA) & 0xFF00FF00) + 0x800080); }

// A unit sent to be sold: infantry to the nearest of the player's barracks
// or WOR, vehicles to the nearest vehicle factory, high-tech factory or
// repair facility.
bool SellUnit(uint32_t u) {
    uint16_t at = PosCell(rd32(u + 0xA));
    bool foot = rd16(UnitInfo(u) + 0x3E) == 0;
    uint32_t best = 0;
    uint16_t best_d = 0xFFFF;
    for (uint32_t s = FirstStructure(0, player_house, 0xFFFF); s; s = NextStructure(0)) {
        uint8_t t = rd8(s + 2);
        bool barracks = t == 7 || t == 0xA;
        if (foot ? !barracks : (t != 3 && t != 4 && t != 5 && t != 0xD)) continue;
        if (rd16(s + 0x22) == 1) continue;
        uint16_t d = CellDistance(at, Origin(s));
        if (d < best_d) {
            best_d = d;
            best = s;
        }
    }
    if (!best) return false;
    std::erase_if(selling, [u](const Selling& e) { return e.unit == u; });
    selling.push_back({u, best});
    LeaveGroups(u);
    uint32_t ring = Ring(best);
    uint16_t dest = Origin(best), dest_d = 0xFFFF;
    for (int k = 0; k < 16; k++) {
        if (!rd16(ring + k * 2u)) continue;
        uint16_t c = uint16_t(Origin(best) + rd16(ring + k * 2u));
        if (!CellInMap(c) || StructureAt(c)) continue;
        if (uint16_t d = CellDistance(at, c); d < dest_d) {
            dest_d = d;
            dest = c;
        }
    }
    if (rd8(u + 2) != 0x10) wr16(u + 0x5A, 0); // as HandleInput's move order (0xE264)
    wr16(u + 0x5C, 0);
    wr8(u + 0x7C, 0xFF);
    SetUnitAction(u, 1);
    UnitMoveTo(u, MakeTarget(dest, 3));
    PlaySoundEffect(rd16(UnitInfo(u) + 0x3E) ? 0x12 : 0x10);
    return true;
}

// Units that reached their structure (a cell around it) are sold for half
// their price (0x16 of their type info) and taken off the map.
void SellArrived() {
    std::erase_if(selling, [](const Selling& e) {
        if (!Selectable(e.unit) || !StructureStands(e.structure)) return true;
        uint16_t at = PosCell(rd32(e.unit + 0xA)), ring = 0;
        uint32_t r = Ring(e.structure);
        bool near = false;
        for (int k = 0; k < 16 && !near; k++)
            if ((ring = rd16(r + k * 2u))) near = uint16_t(Origin(e.structure) + ring) == at;
        if (!near) return false;
        U32{HouseGet(player_house) + 0x12} += rd16(UnitInfo(e.unit) + 0x16) / 2u;
        if (e.unit == selected_unit) {
            U16{e.unit + 6} &= 0x5FFF;
            selected_unit = 0;
            DropSelection();
        }
        LeaveGroups(e.unit);
        LiftUnit(e.unit);
        CrushUnit(e.unit);
        PlaySoundEffect(0x26);
        return true;
    });
}

// A structure sold for half its price (0x16 of its type info): what it
// builds or holds is cancelled (0x103AA), it leaves the map as when its
// ruins appear (0xF71E), and its cells become concrete as a slab makes
// them (0xE6D0); one of the player's units is sent off to be sold
// (SellUnit).
void Sell(uint16_t cell) {
    if (uint32_t u = UnitAt(cell); Selectable(u) && !StructureAt(cell)) {
        if (!SellUnit(u)) PlaySoundEffect(0x2F);
        return;
    }
    uint32_t s = StructureAt(cell);
    if (!s || HouseOf(s) != int16_t(player_house) || rd16(s + 0x22) == 1) {
        PlaySoundEffect(0x2F);
        return;
    }
    uint32_t info = rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4);
    // A refinery 100: its price (400) less the harvester it came with (300)
    // that stays.
    U32{HouseGet(player_house) + 0x12} += rd8(s + 2) == 0xC ? 100u : rd16(info + 0x16) / 2u;
    // A refinery's harvester or a repair facility's unit comes out first
    // (0xC81C) instead of being lost with it.
    if (rd8(s + 2) == 0xC || rd8(s + 2) == 0xD) {
        uint32_t st = U32{0xFFD5AC}, obj = U32{0xFFC208};
        U32{0xFFD5AC} = s;
        U32{0xFFC208} = s;
        while (rd8(s + 3) != 0xFF && StructReleaseUnit(0)) {
        }
        U32{0xFFD5AC} = st;
        U32{0xFFC208} = obj;
    }
    CancelBuild(s);
    if (rd8(s + 2) == 9) U16{HouseGet(player_house) + 0x1E} -= 1;
    uint16_t origin = PosCell(rd32(s + 0xA)), layout = rd16(info + 0x3C);
    StructureLost(s);
    RemoveCellEffect(origin); // its ruins
    for (uint16_t i = 0; i < rd16(0x6B82E + layout * 2u); i++) {
        uint16_t c = uint16_t(origin + rd16(0x6B740 + layout * 18u + i * 2u));
        uint32_t m = 0xFF7D9C + int16_t(c) * 4;
        wr16(m, uint16_t((rd16(m) & 0xFE00) + (U16{0xFFC230} & 0x1FF)));
        wr8(m + 2, uint8_t((rd8(m + 2) & 0xF8) | player_house));
        RedrawCell(c);
    }
    PlaySoundEffect(0x26);
}

// A damaged unit to the nearest repair facility: carried by a free
// carryall when there is one (its pick-up takes it to the nearest facility,
// 0x43FE8; not harvesters, which it takes to a refinery), else driven
// there as button A on the facility orders it.
bool RepairUnit(uint32_t u) {
    if (rd16(u + 0x12) >= rd16(UnitInfo(u) + 0x10)) return false;
    uint32_t best = 0;
    uint16_t best_d = 0xFFFF;
    for (uint32_t s = FirstStructure(0, player_house, 0xD); s; s = NextStructure(0)) {
        uint16_t d = Distance(rd32(s + 0xA), rd32(u + 0xA));
        if (d < best_d) {
            best_d = d;
            best = s;
        }
    }
    if (!best) return false;
    LeaveGroupsForRepair(u);
    uint16_t t = MakeTarget(rd16(u), 1);
    if (rd8(u + 2) != 0x10) {
        if (uint32_t w = NearestUnit(0, player_house, t, 0)) {
            LinkTargets(t, MakeTarget(rd16(w), 1));
            PlaySoundEffect(rd16(UnitInfo(u) + 0x3E) ? 0x12 : 0x10);
            return true;
        }
    }
    OrderUnit(u, PosCell(rd32(best + 0xA)));
    if (!sel.empty() && Selectable(sel[0])) MakeLead(sel[0], false);
    return true;
}

// What a repair click on the cell mends: one of the player's damaged units
// (when there is a repair facility) or structures (not under repair yet);
// else 0.
uint32_t RepairTarget(uint16_t cell) {
    if (uint32_t u = UnitAt(cell); Selectable(u) && !StructureAt(cell))
        return rd16(u + 0x12) < rd16(UnitInfo(u) + 0x10) && FirstStructure(0, player_house, 0xD) ? u : 0;
    uint32_t s = StructureAt(cell);
    if (!s || HouseOf(s) != int16_t(player_house) || (rd8(s + 4) & 0x20)) return 0;
    return rd16(rd32(0x6B954 + int16_t(int8_t(rd8(s + 2))) * 4) + 0x10) > rd16(s + 0x12) ? s : 0;
}

// A repair started as the structure menu's repair button does (0x28F3C);
// on one of the player's units, RepairUnit. Nothing elsewhere.
void Repair(uint16_t cell) {
    uint32_t o = RepairTarget(cell);
    if (!o) return;
    if (o != StructureAt(cell)) {
        RepairUnit(o);
        return;
    }
    U8{o + 4} |= 0x20;
    SetRepairMark(o, rd16(rd32(0x6B954 + int16_t(int8_t(rd8(o + 2))) * 4) + 0x3C));
    PlaySoundEffect(0x26);
}

// The stack of the selected units after the first (the panel shows that
// one): in the wide view in the panel's column, going on below the first
// one's portrait and health bar (and a harvester's spice bar) down to the
// radar when it shows, else in a column left of the panel. Left, top and
// the rows it has room for.
struct Area {
    int x, y, rows;
};
Area StackArea() {
    const host::Screen& scr = host::screen();
    uint32_t portrait = rd32(Node(U16{0xFFC732}));
    int x = int16_t(rd16(portrait));
    if (!scr.ox && !scr.oy) return {x - 36, 0x30, (scr.h - 0x30) / kStackRow};
    int y = int16_t(rd16(portrait + 2)) + kStackRow, bottom = scr.h;
    if (uint32_t bar = Node(U16{0xFFC740}); !(rd8(bar + 7) & 0x80)) y = std::max(y, int16_t(rd16(rd32(bar) + 2)) + 12);
    if (uint16_t rn = U16{0xFFBF58}; rn && !(rd8(Node(rn) + 7) & 0x80)) bottom = S16{0xFFBF78} + scr.h - 224;
    return {x + scr.w - 320, y, std::max(0, (bottom - y) / kStackRow)};
}

} // namespace

// In place of HandleInput (0xE264) in the game tick.
uint16_t PlayerInput() {
    std::erase_if(repairing, [](const Repairing& e) {
        if (!Selectable(e.unit) || rd16(e.unit + 0x12) < rd16(UnitInfo(e.unit) + 0x10)) return false;
        if (std::none_of(std::begin(groups), std::end(groups), [&](const auto& g) { return Has(g, e.unit); }))
            groups[e.group].push_back(e.unit);
        return true;
    });
    Sync();
    SellArrived();
    Input c = latched;
    latched = pending;
    pending = {};
    if (c.repair) SetMode(mode == kRepair ? kCommand : kRepair);
    if (c.sell) SetMode(mode == kSell ? kCommand : kSell);
    if (vblank_hook == 0x6092) SetMode(kCommand);
    if (c.digit >= 0) Group(c.digit);
    if (c.radar) CentreAt(c.rx, c.ry);
    if (c.event && event_x >= 0) CentreAt(event_x, event_y);
    uint8_t key = rd8(0xFFBF2B);
    uint16_t r = 1;
    if (mode != kCommand) {
        uint16_t cell = c.click ? CellOf(c.x0, c.y0) : U16{0xFFC240};
        if (key == 'A') mode == kRepair ? Repair(cell) : Sell(cell);
        if (key == 'B') SetMode(kCommand);
        if (key == 'A' || key == 'B') wr8(0xFFBF2B, 0);
        r = HandleInput();
        uint32_t n = Node(U16{0xFFBF18});
        if (mode == kRepair) {
            MakeHammer();
            wr32(n + 8, kHammer);
            U16{n + 6} &= 0x9FFF;
            // Over nothing to mend the mark is crossed out (DrawSelection).
            repair_ok = RepairTarget(U16{0xFFC240}) != 0;
            U8{n + 7} &= 0x7F;
        } else if (mode == kSell) {
            U8{n + 7} |= 0x80;
        }
        Sync();
        return r;
    }
    if (c.box) BoxSelect(c);
    if (key == 'A') // a new order calls off a sale
        std::erase_if(selling, [](const Selling& e) { return Has(sel, e.unit) || e.unit == selected_unit; });
    if (key != 'A' || !Command(c)) {
        if (key == 'B' && sel.size() > 1) sel.resize(1);
        r = HandleInput();
    }
    Sync();
    return r;
}

// From the map cursor's VBlank hooks: the mouse, digit keys, R and Q, Esc
// (latches Start: the options), Space and the wheel (over the stack).
// Left click latches A, a left drag (not when placing) makes a box, right
// press latches B. Buttons already held when the hooks resume (a menu
// closed by a click) do nothing.
void PollMouse(bool placement) {
    if (int d = host::take_digit(); d >= 0) pending.digit = d;
    const host::Mouse& m = host::mouse();
    if (host::frame_count - polled_frame > 10) {
        buttons_seen = m.buttons;
        left_down = dragging = false;
        host::clear_chars();
    }
    while (int ch = host::take_char())
        if (ch == 'R') pending.repair = !pending.repair;
        else if (ch == 'Q') pending.sell = !pending.sell;
    host::mark_battle();
    // Several units selected: all get the brackets (DrawSelection), not the
    // game's mark on the first.
    if (uint16_t n = U16{0xFFBF60}; n && sel.size() > 1) U8{Node(n) + 7} |= 0x80;
    if (host::take_escape()) {
        wr8(0xFFBF2A, 'S');
        U16{0xFFC02C} = 1;
    }
    if (host::take_space()) pending.event = true;
    polled_frame = host::frame_count;
    uint8_t now = m.buttons, pressed = uint8_t(now & ~buttons_seen), released = uint8_t(buttons_seen & ~now);
    buttons_seen = now;
    const host::Screen& scr = host::screen();
    if (int d = host::take_wheel()) {
        Area a = StackArea();
        if (m.x >= a.x && m.x < a.x + 32 && m.y >= a.y && m.y < a.y + a.rows * kStackRow) stack_top += d;
    }
    int16_t x = int16_t(m.x - scr.ox + view_x), y = int16_t(m.y - scr.oy + view_y);
    if (pressed & 2) {
        left_down = dragging = false;
        wr8(0xFFBF2A, 'B');
        U16{0xFFC02C} = 1;
    }
    // The radar (64 x 64 at 0xFFBF76, the bottom right corner in the wide
    // view): a left press or drag on it centres the view there (a pixel a
    // cell, two when zoomed in, from the map rectangle at 0xFFBF54).
    {
        uint16_t rn = U16{0xFFBF58};
        int rx0 = S16{0xFFBF76} + (scr.ox ? scr.w - 320 : 0), ry0 = S16{0xFFBF78} + (scr.oy ? scr.h - 224 : 0);
        int px = m.x - rx0, py = m.y - ry0;
        bool on = rn && !(rd8(Node(rn) + 7) & 0x80) && px >= 0 && px < 64 && py >= 0 && py < 64;
        if ((pressed & 1) && on) radar_held = true;
        if (!(now & 1)) radar_held = false;
        if (radar_held) {
            int k = rd8(0xFFBF51) ? 16 : 32;
            pending.radar = true;
            pending.rx = int16_t(U16{0xFFBF54} + std::clamp(px, 0, 63) * k + k / 2);
            pending.ry = int16_t(U16{0xFFBF56} + std::clamp(py, 0, 63) * k + k / 2);
            left_down = dragging = false;
            return;
        }
    }
    if (pressed & 1) {
        left_down = true;
        dragging = false;
        drag_x = x;
        drag_y = y;
    }
    if (left_down && !placement && (std::abs(x - drag_x) > 4 || std::abs(y - drag_y) > 4)) dragging = true;
    drag_x1 = x;
    drag_y1 = y;
    if (!(released & 1) || !left_down) return;
    left_down = false;
    pending.shift = host::shift_held();
    if (dragging) {
        dragging = false;
        pending.box = true;
        pending.x0 = drag_x;
        pending.y0 = drag_y;
        pending.x1 = x;
        pending.y1 = y;
        return;
    }
    wr8(0xFFBF2A, 'A');
    U16{0xFFC02C} = 1;
    if (placement) return;
    pending.click = true;
    pending.x0 = x;
    pending.y0 = y;
}

// Where the player last heard of (an enemy unit sighted, one of their
// structures hit, a structure destroyed): Space centres the view there.
void NoteEvent(uint32_t pos) {
    event_x = int16_t(uint16_t(pos) >> 3);
    event_y = int16_t(uint16_t(pos >> 16) >> 3);
}

// A unit sent to be sold, sold or freed leaves its group and the
// selection; a freed structure its group.
void LeaveGroups(uint32_t u) {
    for (auto& g : groups) std::erase(g, u);
    std::erase(sel, u);
    std::replace(std::begin(group_structure), std::end(group_structure), u, 0u);
    std::erase_if(repairing, [u](const Repairing& e) { return e.unit == u; });
}

// A unit sent for repair (by the player, or carried off by the game)
// leaves its group and the selection until it is back on the map repaired.
void LeaveGroupsForRepair(uint32_t u) {
    if (std::any_of(repairing.begin(), repairing.end(), [u](const Repairing& e) { return e.unit == u; })) return;
    int k = 0;
    while (k < 10 && !Has(groups[k], u)) k++;
    LeaveGroups(u);
    if (k < 10) repairing.push_back({u, k});
}

void ResetSelection() {
    sel.clear();
    selling.clear();
    repairing.clear();
    for (auto& g : groups) g.clear();
    std::fill(std::begin(group_structure), std::end(group_structure), 0);
    pending = latched = {};
    left_down = dragging = false;
    mode = kCommand;
    event_x = -1;
}

// Brackets around the selected units when there are several (else the
// game marks the one), the stack of those after the first, the sell mode's cursor, the repair mark in grey over
// nothing to mend, and the drag box.
void DrawSelection(uint32_t* px, int pitch) {
    if (vblank_hook != 0x6D10 && vblank_hook != 0x6092) return;
    const host::Screen& scr = host::screen();
    int w = scr.w, h = scr.h, vx = int16_t(view_x) - scr.ox, vy = int16_t(view_y) - scr.oy;
    auto dot = [&](int x, int y) {
        if (x >= 0 && x < w && y >= 0 && y < h) px[y * pitch + x] = 0xFFFFFFFF;
    };
    auto hline = [&](int x0, int x1, int y) {
        for (int x = x0; x <= x1; x++) dot(x, y);
    };
    auto vline = [&](int x, int y0, int y1) {
        for (int y = y0; y <= y1; y++) dot(x, y);
    };
    for (size_t i = 0; sel.size() > 1 && i < sel.size(); i++) {
        if (!Selectable(sel[i])) continue;
        uint32_t pos = rd32(sel[i] + 0xA);
        int x = (uint16_t(pos) >> 3) - vx, y = (uint16_t(pos >> 16) >> 3) - vy;
        int x0 = x - 12, x1 = x + 11, y0 = y - 12, y1 = y + 11;
        hline(x0, x0 + 4, y0);
        hline(x1 - 4, x1, y0);
        hline(x0, x0 + 4, y1);
        hline(x1 - 4, x1, y1);
        vline(x0, y0, y0 + 4);
        vline(x1, y0, y0 + 4);
        vline(x0, y1 - 4, y1);
        vline(x1, y1 - 4, y1);
    }
    // Several units selected: those after the first each with its portrait
    // (4 x 3 tiles as the panel's, 0x60E8C) and its health bar (as the
    // panel's) stacked in rows; the wheel scrolls them.
    if (sel.size() > 1) {
        Area a = StackArea();
        int n = int(sel.size()) - 1;
        stack_top = std::clamp(stack_top, 0, std::max(0, n - a.rows));
        for (int r = 0; r < a.rows && stack_top + r < n; r++) {
            uint32_t u = sel[size_t(stack_top + r + 1)];
            if (!Selectable(u)) continue;
            int x0 = a.x, y0 = a.y + r * kStackRow;
            uint32_t t = 0x60E8C + uint16_t(rd16(UnitInfo(u) + 0x14) << 3), src = rd32(t);
            int line = rd8(t + 4) == 0xFE ? 1 : 3;
            for (int y = 0; y < 24; y++)
                for (int x = 0; x < 32; x++) {
                    uint8_t b = rd8(src + uint32_t(((x >> 3) * 3 + (y >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)));
                    int c = x & 1 ? b & 15 : b >> 4;
                    if (c && y0 + y < h) px[(y0 + y) * pitch + x0 + x] = display::color(display::cram[line * 16 + c]);
                }
            // The bar's four tiles as DrawBar (0x9772) fills them: rows 2
            // and 7 the frame, 3-6 the pattern; palette line 2.
            uint16_t hp = rd16(u + 0x12), max = std::max<uint16_t>(rd16(UnitInfo(u) + 0x10), 1);
            uint16_t n32 = uint16_t(std::min(hp, max) << 5), q = uint16_t(n32 / max - (n32 % max ? 0 : 1));
            uint32_t pattern = rd32(0x9742 + ((q & 0x18) >> 1));
            uint32_t mask = rd32(0x9752 + (q & 7) * 4), edge = rd32(0x979E + (q & 7) * 4);
            for (int k = 0; k < 4; k++) {
                int part = (q & 0x18) >> 3;
                uint32_t fill = k < part ? pattern : k == part ? (pattern & mask) | edge : 0;
                uint32_t cap = k < part ? 0xCCCCCCCC : k == part ? (0xCCCCCCCC & mask) | edge : 0;
                if (k == 0) fill = (fill & 0x0FFFFFFF) | 0xC0000000;
                for (int y = 2; y < 8; y++) {
                    uint32_t v = y == 2 || y == 7 ? cap : fill;
                    int sy = y0 + 24 + y;
                    for (int x = 0; x < 8; x++)
                        if (int c = (v >> (28 - 4 * x)) & 15; c && sy < h)
                            px[sy * pitch + x0 + k * 8 + x] = display::color(display::cram[2 * 16 + c]);
                }
            }
        }
    }
    if (mode == kSell) { // the build menu's cost icon (tiles 0x4A31C, palette line 1 of 0xA6C98; 12 its backdrop)
        int cx = S16{0xFFBF12} + scr.ox - 8, cy = S16{0xFFBF14} + scr.oy - 8;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                uint8_t b = rd8(0x4A31C + uint32_t(((x >> 3) * 2 + (y >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)));
                int c = x & 1 ? b & 15 : b >> 4;
                int sx = cx + x, sy = cy + y;
                if (c && c != 12 && sx >= 0 && sx < w && sy >= 0 && sy < h)
                    px[sy * pitch + sx] = display::color(rd16(0xA6C98 + 0x20 + c * 2u));
            }
    }
    if (mode == kRepair && !repair_ok) { // a red circle and slash over the repair mark (24 x 24, centred)
        int cx = S16{0xFFBF12} + scr.ox, cy = S16{0xFFBF14} + scr.oy;
        for (int y = -12; y < 12; y++)
            for (int x = -12; x < 12; x++) {
                int d = (2 * x + 1) * (2 * x + 1) + (2 * y + 1) * (2 * y + 1); // (2r)^2 from the centre
                bool ring = d >= 18 * 18 && d < 24 * 24, slash = d < 24 * 24 && std::abs(x - y) <= 1;
                int sx = cx + x, sy = cy + y;
                if ((ring || slash) && sx >= 0 && sx < w && sy >= 0 && sy < h) px[sy * pitch + sx] = 0xFFE02020;
            }
    }
    if (dragging) {
        int x0 = std::min(drag_x, drag_x1) - vx, x1 = std::max(drag_x, drag_x1) - vx;
        int y0 = std::min(drag_y, drag_y1) - vy, y1 = std::max(drag_y, drag_y1) - vy;
        hline(x0, x1, y0);
        hline(x0, x1, y1);
        vline(x0, y0, y1);
        vline(x1, y0, y1);
    }
}

} // namespace game
