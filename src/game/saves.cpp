// Port addition, not in the ROM: the missions reached, kept in a file
// (host::opt.saves, one "house scenario" line each) so a game can go on
// from any of them as a password would start it.
#include "game.hpp"

#include <algorithm>
#include <fstream>

namespace game {

std::vector<Save> ReadSaves() {
    std::vector<Save> v;
    if (host::opt.saves.empty()) return v;
    std::ifstream f(host::opt.saves);
    int h, s;
    while (f >> h >> s)
        if (h >= 0 && h <= 2 && s >= 1 && s <= 9) v.push_back({uint16_t(h), uint16_t(s)});
    std::sort(v.begin(), v.end(), [](Save a, Save b) { return a.house != b.house ? a.house < b.house : a.scenario < b.scenario; });
    v.erase(std::unique(v.begin(), v.end(), [](Save a, Save b) { return a.house == b.house && a.scenario == b.scenario; }),
            v.end());
    return v;
}

void SaveMission(uint16_t house, uint16_t scenario) {
    if (host::opt.saves.empty() || house > 2 || scenario < 1 || scenario > 9) return;
    for (Save s : ReadSaves())
        if (s.house == house && s.scenario == scenario) return;
    std::ofstream f(host::opt.saves, std::ios::app);
    f << house << ' ' << scenario << '\n';
}

} // namespace game
