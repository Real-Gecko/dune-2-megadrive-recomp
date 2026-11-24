// The house-selection screen (0x26B8A) and the scenario start (0x264F8):
// the cursor slides between the three houses on the planet, a press over a
// house confirms it through the mentat, and a confirmed choice starts the
// first briefing.
//
// RAM: 0xFFBEF8 the cursor object (+0 word x, +2 word y); 0xFFBF02 its slide
// velocity (counts to zero); 0xFFBF00 the highlight animation counter;
// 0xFFE6A8/0xFFF3A8 the screen's sprite pool and list.
#include "game.hpp"

namespace game {

namespace {

constexpr U16 cursor_x{0xFFBEF8};
constexpr U16 cursor_vel{0xFFBF02};
constexpr U16 anim_count{0xFFBF00};
constexpr U16 scenario{0xFFC050};
constexpr U8 house_confirmed{0xFFC194};

} // namespace

// 0x48C0: set up the cursor sprite and install the sprite-table VBlank hook.
void HouseSelectInit(uint16_t cls) {
    InitSpritePool(0xFFE6A8, 0xFFF3A8);
    SetSpriteTable(0xF000);
    uint32_t node = CreateSprite(0xFFBEF8, cls, 0);
    U8{node + 7} |= 0x40;
    FlushDma();
    if (rd32(0xFFBEF8) == 0) wr32(0xFFBEF8, 0x200088);
    cursor_vel = 0;
    anim_count = 0;
    wr32(0xFFDBF0, 0);
    BuildSpriteTable(U16{0xFFF3AC});
    vblank_hook = 0x4504;
    md::int_mask = 0;
}

// 0x4934: start a slide towards a neighbour house, or report a house key
// (0x41/0x42/0x43) when a button is pressed over one.
uint16_t HouseSelectPad() {
    uint16_t d0 = uint16_t(ReadPads());
    wr8(0xFFBF2B, uint8_t(d0));
    if (d0 & 4) {
        if (cursor_vel) return d0;
        if (int16_t(cursor_x) > 0x20) { cursor_vel = 0xFFA8; return 0x4C4C; }
    } else if (d0 & 8) {
        if (cursor_vel) return d0;
        if (int16_t(cursor_x) < 0xC8) { cursor_vel = 0x58; return 0x5252; }
    }
    d0 &= 0x70;
    if (!d0) return d0;
    if (int16_t(cursor_x) == 0x20) return 0x4141;
    if (int16_t(cursor_x) == 0x78) return 0x4242;
    if (int16_t(cursor_x) == 0xD0) return 0x4343;
    return d0;
}

// 0x480C: advance a slide (a sound when it arrives) and the pulsing highlight
// built from the template at 0x6424E, then rebuild and upload the sprites.
void HouseSelectAnim() {
    int16_t v = int16_t(cursor_vel);
    if (v != 0) {
        if (v < 0) {
            v = int16_t(v + 4);
            cursor_x = uint16_t(int16_t(cursor_x) - 4);
        } else {
            v = int16_t(v - 4);
            cursor_x = uint16_t(int16_t(cursor_x) + 4);
        }
        if (v == 0) PlaySfx(0x19);
        cursor_vel = uint16_t(v);
    }
    BuildSpriteTable(U16{0xFFF3AC});
    uint16_t c = uint16_t(anim_count + 1);
    anim_count = c;
    uint16_t f = uint16_t(((c & 0x1F) >> 3) + 1);
    uint8_t col = uint8_t(f | (f << 4));
    for (int i = 0; i <= 0xFF; i++) {
        uint8_t b = rd8(0x6424E + i);
        wr8(0xFF0000 + i, b ? col : 0);
    }
    QueueDma(2, 0xFF0000, 0xF7800080);
    FlushDma();
}

// 0x26B8A: pick a house; confirmed through the mentat. Returns the house id.
uint16_t ChooseHouse() {
    FadeOutScreen();
    ClearVram();
    for (;;) {
        int16_t house = -1;
        int16_t key = 0;
        StartSeq29();
        FadeOutScreen();
        DisplayOff();
        HouseSelectInit(0x1B);
        PlayMusicTrack(0x1C);
        StopSeq29();
        FadeIn(LoadScreen(0));
        for (;;) {
            key = int8_t(HouseSelectPad());
            if (key == 0x41) house = 1;
            else if (key == 0x42) house = 2;
            else if (key == 0x43) house = 0;
            else key = 0;
            HouseSelectAnim();
            if (house == -1) continue;
            break;
        }
        PlaySoundEffect(0x26);
        scenario = 0;
        scenario = 0; // 0x47EC clears it again
        if (key == 0) continue;
        if (ConfirmHouse(uint16_t(house))) {
            house_confirmed = 1;
            return uint16_t(house);
        }
    }
}

// 0x264F8: the briefing of a scenario, then the mission (campaign <= 9); the
// out-of-campaign path plays the finale and resets.
void StartScenario(uint16_t house, uint16_t campaign) {
    if (campaign <= 9) {
        SaveMission(house, campaign); // port: the mission reached
        Briefing(house, campaign);
        PlayMission(house, campaign);
    } else {
        Finale(house);
        AttractDemo(0);
    }
}

} // namespace game
