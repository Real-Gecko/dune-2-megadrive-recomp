// The game's C main (0x124DC): set up a game, pick a house when needed,
// start scenarios and run the in-game loop; and the game mode switch
// (0x203E0).
#include "game.hpp"

namespace game {

void GameInit();
uint16_t SwapFlag1(uint16_t on);
uint16_t SwapFlag2(uint16_t on);

namespace {

constexpr U16 player_house{0xFFC274};
constexpr U16 scenario{0xFFC050};
constexpr U16 campaign{0xFFC04C};
constexpr U16 game_mode{0xFFC264};
constexpr U16 next_mode{0xFFC268};
constexpr U16 restart{0xFFC14C}; // 1 start the next scenario, 2 choose a house first
constexpr U16 music_left{0xFFC574};
constexpr U16 tick_skip{0xFFC02C};
constexpr U16 playing{0xFFC19C};
constexpr U32 selected{0xFFC25C};


void PlayBattleMusic() { PlayMusicTrack(uint16_t(RandomRange(0, 4) + 8)); }

} // namespace

// 0x203E0
uint16_t SetGameMode(uint16_t mode) {
    if (mode == 3 && !selected) mode = 4;
    if (mode == 4 && selected) {
        DropSelection();
        selected = 0;
    }
    if (mode == game_mode) return 0;
    uint16_t old = game_mode;
    SwapFlag2(0);
    game_mode = mode;
    next_mode = mode;
    if (old == 1 || old == 2) SetCursorAt(U16{0xFFC188});
    if (old >= 1 && old <= 3 && selected && mode != 3 && mode != 1) {
        CancelOrder(2, selected);
        selected = 0;
        DropSelection();
    }
    switch (game_mode) {
    case 1: U16{0xFFC188} = U16{0xFFC248}; [[fallthrough]];
    case 2: case 3: case 4: SwapFlag2(1); break;
    }
    return 0;
}

// 0x124DC
void CMain() {
    uint8_t tick = 0;
    wr8(0xFFC03C, rd8(0x6CFAA));
    GameInit();
    SwapFlag2(1);
    SetFadeDelay(3);
    FadeIn(U32{0xFFC288});
    ResetFrameDelta();
    U32{rd32(0xFFC278) + 0x12} += 1;
    U32{0xFFC054} += 1;
    music_left = 0xFFFF;
    do {
        if (S16{0xFFC574} < 0) PlayBattleMusic();
        UpdateFrameDelta();
        UpdateWorld(rd32(0x4AA28));
        tick_skip = 0;
        if (restart == 2) {
            player_house = 5;
            player_house = ChooseHouse();
            campaign = uint16_t(scenario + 1);
            wr8(0xFFBF2B, 0);
            restart = 1;
        }
        if (next_mode != game_mode) SetGameMode(next_mode);
        if (restart == 1) {
            SetGameMode(0);
            StartScenario(player_house, campaign);
            restart = 0;
            SetGameMode(4);
            FadeIn(U32{0xFFC288});
            SyncFrameDelta();
            PlayBattleMusic();
        } else {
            GameTick();
            if (!tick_skip) {
                switch (tick) {
                case 0: TeamTick(); break;
                case 1: StructureTick(); break;
                case 2: HouseTick(); break;
                case 3: StructureTick(); tick = 0xFF; break;
                }
                tick++;
            }
            UpdateRadar();
            UpdateHouse(player_house, 0);
            UpdateEffects();
            UpdateCellEffects();
        }
        if (playing) playing = CheckGameEnd();
    } while (playing);
}

} // namespace game
