// The game's side of the GEMS sound driver (0x1596-0x1788 and the C
// wrappers at 0x2DCA0-0x2DDE8): commands that went to the Z80 through its
// FIFO now call the translated driver (src/gems.cpp).
#include "game.hpp"

#include "../audio.hpp"

namespace game {

// 0x1624: Z80 reset, then the four bank addresses (command 0B).
void GemsInit(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples) {
    audio::gems_init(patches, envelopes, sequences, samples);
}

// 0x1664, 0x2DDC2: start sequence (command 10).
void GemsStartSeq(uint32_t seq) {
    audio::start_seq(uint8_t(seq));
}

// 0x167A, 0x2DDCC: stop sequence (command 12).
void GemsStopSeq(uint32_t seq) {
    audio::stop_seq(uint8_t(seq));
}

// 0x16A6: stop everything (command 16).
void GemsStopAll() {
    audio::stop_all();
}

// 0x1736: GEMS command 0x17 - mute (1) or unmute (0) a channel of a sequence.
void GemsSeqCommand3(uint32_t seq, uint32_t ch, uint32_t on) {
    audio::mute(uint8_t(seq), uint8_t(ch), uint8_t(on));
}

// 0x2DCF0: the title screen's banks.
void LoadTitleSoundBanks() {
    GemsInit(0x2DCEC, 0x2DCEC, 0x2DCEC, 0x2DCEC);
    GemsInit(0xD02AE, 0xD0251, 0xCF970, 0xC8160);
}

// 0x2DCA0: the game's banks; clears the key state.
void LoadGameSoundBanks() {
    GemsStopAll();
    GemsInit(0x2DCEC, 0x2DCEC, 0x2DCEC, 0x2DCEC);
    GemsInit(0xFE2B6, 0xFE185, 0xF6363, 0xD06BF);
    wr8(0xFFBF2A, 0);
    wr8(0xFFBF2B, 0);
    wr16(0xFFE41A, 0);
}

// 0x2DD26: music; ids the table at 0x2DD5C marks start now, the others are
// queued (one at a time) for VBlank.
void PlayMusic(uint32_t id) {
    if (rd8(0x2DD5C + uint16_t(id))) {
        GemsStartSeq(id);
        return;
    }
    uint16_t n = uint16_t(rd8(kSoundQueue) + 1);
    if (n >= 2) return;
    wr8(kSoundQueue + n, uint8_t(id));
    wr8(kSoundQueue, uint8_t(n));
}

constexpr U8 sfx_on{0xFFDC51};

// 0x00A7DC: a sound effect (id <= 0x40) via the table at 0x6A87E, when
// effects are on.
void PlaySoundEffect(uint32_t id) {
    if (!sfx_on) return;
    if (int16_t(id) > 0x40) return;
    int8_t v = int8_t(rd8(0x6A87E + uint16_t(id)));
    if (v < 0) return;
    PlayMusic(uint32_t(int32_t(v)));
}

constexpr U16 music_cur{0xFFC8BC};
constexpr U16 music_last{0xFFC8B8};
constexpr U16 music_flag{0xFFC574};
constexpr U16 music_queued{0xFFDC52};
constexpr U8 music_on{0xFFDC50};

// 0x00A80A: play/stop a music track (id 0 stops; 8..0xC pick a random
// battle track avoiding an immediate repeat). Gated by music_on.
void PlayMusicTrack(uint32_t track) {
    uint16_t d3 = uint16_t(track);
    if (d3 == 0) {
        GemsStopSeq(uint32_t(int32_t(int16_t(music_cur))));
        music_cur = 0;
        return;
    }
    if (!music_on) {
        if (int16_t(music_flag) < 0) return;
        GemsStopSeq(uint32_t(int32_t(int16_t(music_cur))));
        music_cur = 0;
        music_flag = 0xFFFF;
        return;
    }
    if (d3 >= 8 && d3 <= 0xC) {
        while (d3 == uint16_t(music_last)) d3 = uint16_t(RandomRange(0, 4) + 8);
        music_last = d3;
    }
    int16_t seq = int16_t(rd16(0x6A8C0 + d3 * 4u));
    if (seq < 0) return;
    GemsStopSeq(uint32_t(int32_t(int16_t(music_cur))));
    music_cur = uint16_t(seq);
    music_flag = rd16(0x6A8C2 + d3 * 4u);
    PlayMusic(uint32_t(int32_t(seq)));
    music_queued = uint16_t(seq);
}

// 0x2DDB6: an effect when effects are on.
void PlaySfx(uint32_t id) {
    if (sfx_on) GemsStartSeq(id);
}

// 0x2DDD2, 0x2DDDE
void StartSeq29() { GemsStartSeq(0x29); }
void StopSeq29() { GemsStopSeq(0x29); }

} // namespace game
