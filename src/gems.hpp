// The GEMS 2.5 sound driver the game ran on the Z80 (ROM 0x2952-0x41DC),
// translated. It plays sequences (music and effects) from the ROM's patch,
// envelope, sequence and sample banks on a YM2612 and the PSG.
#pragma once

#include "psg.hpp"
#include "ymfm/ymfm_opn.h"

#include <cstdint>
#include <functional>
#include <vector>

class Gems {
public:
    Gems(ymfm::ym2612& fm, Psg& psg);
    // The Z80 reset and the driver's start-up (main).
    void reset();
    // Command 0B: the 68000 addresses of the four banks.
    void banks(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples);
    void start_seq(uint8_t seq);  // command 10
    void stop_seq(uint8_t seq);   // command 12
    void stop_all();              // command 16
    void mute(uint8_t seq, uint8_t ch, uint8_t on); // command 17
    // VBLINT and the main loop's work for one 60 Hz tick.
    void vblank();
    // One YM2612 sample: the DAC, paced by timer A, and the next queued
    // register writes.
    void timer_a();
    // The bank's sound effects: one-track sequences that play notes and leave
    // the master attenuation alone.
    std::vector<uint8_t> effects() const;
    // When set, sample notes go here (ROM address, bytes, YM2612 samples a
    // byte) instead of the DAC, so they can overlap.
    std::function<void(uint32_t, uint16_t, int)> on_sample;
    // Command 20: the attenuation added to every FM note.
    uint8_t master_atten() const { return masteratn_; }
    void master_atten(uint8_t a) { masteratn_ = a; }

private:
    struct Ccb {
        uint32_t addr = 0;
        uint8_t flags = 0; // 80 sustain, 40 env retrigger, 20 locked, 10 running, 08 sfx timebase, 02 muted, 01 in use
        uint16_t timer = 0, delay = 0, dur = 0;
        uint8_t pnum = 0, snum = 0, vchan = 0;
        struct {
            uint8_t count;
            uint16_t addr;
        } loops[4] = {};
        uint8_t prio = 0, env = 0, atn = 0;
    };
    // A voice table entry. flags: 80 free, 40 release, 20 locked, 10 self-timed, 08 sfx timebase, 07 voice.
    struct Voice {
        uint8_t flags, prio = 0, note = 0x50, ch = 0;
        uint16_t dur = 0;
        uint8_t release = 0;
    };
    struct VoiceList {
        Voice* first;
        int count;
    };
    struct PsgVoice {
        uint8_t com = 0, lev = 0xFF, atk = 0, dky = 0, slv = 0, rrt = 0, mode = 0, dtl = 0, dth = 0, alv = 0, flg = 0;
    };
    struct Write {
        uint8_t bank, reg, val;
    };
    struct Ecb {
        uint8_t chan = 0x40; // 40 free, 20 sfx timebase, 0F channel
        uint8_t ptr = 0, ctr = 0;
        uint16_t delta = 0;
    };

    uint8_t rom(uint32_t a) const;
    uint16_t rom16(uint32_t a) const;
    void fm(int bank, uint8_t reg, uint8_t val);
    void chip_write(const Write& w);
    void fm_voice(int voice, uint8_t reg, uint8_t val);

    void dopsgenv();
    void updseq();
    void sequencer(int ch);
    enum Next { kNextCmd, kDelay, kStop };
    Next seqcmd(int ch, uint8_t cmd);
    uint8_t sbyte(Ccb& c);
    void vtimer(VoiceList list, bool psg);
    void resume_all();
    void pause_seq(uint8_t seq);
    void clip_all();
    void clip(VoiceList list, bool psg);
    void set_tempo(uint8_t bpm);
    void trigenv(Ccb& c, uint8_t ch, uint8_t env);
    void doenvelope();
    void applybend();
    uint16_t getfreq(bool psg, uint8_t note, int ch);
    void noteon(int ch, uint8_t note);
    void noteonpsg(Ccb& c, bool noise);
    void noteondig(Ccb& c);
    void noteonfm(Ccb& c);
    void vtandet(Ccb& c, Voice* v, uint8_t flags);
    void noteoffdig();
    void fetchpatch(Ccb& c);
    uint8_t alloc(Ccb& c, VoiceList list, Voice*& out);
    uint8_t allocspec(Ccb& c, Voice* v, Voice*& out);
    void fill_dac_fifo(bool force);
    void dac_off();

    ymfm::ym2612& fm_;
    Psg& psg_;
    // FM register writes not yet made. The Z80 took a while for each, so
    // they reach the chip a couple per sample: a key off and the next key on
    // never land in the same sample, where the chip would miss the restart.
    std::vector<Write> writes_;
    size_t write_head_ = 0;

    Ccb ccb_[16];
    uint8_t patch_[16][39];
    uint8_t* chpat_ = nullptr;
    Voice fmv_[6], psgv_[3], noisev_[1];
    PsgVoice pv_[4];
    Ecb ecb_[4];
    uint8_t envbuf_[128];
    int16_t pbend_[16];
    uint16_t ebend_[16];
    uint8_t retrig_[16];
    bool needbend_ = false;
    uint8_t mbox_[0x102];
    uint32_t ptbl_ = 0, etbl_ = 0, stbl_ = 0, dtbl_ = 0;
    uint16_t sbpt_ = 204, sbptacc_ = 0;
    uint8_t tbase_ = 0, tickcnt_ = 0, masteratn_ = 0, ch3mode_ = 0x15, carriers_ = 0;
    uint8_t non_note_ = 0, non_ch_ = 0, non_voice_ = 0, non_atten_ = 0;
    uint16_t non_freq_ = 0;

    // The sample being played: its header, and the 256-byte FIFO DACME read
    // from while FILLDACFIFO kept the half it was not reading filled.
    struct {
        uint8_t flags;
        uint32_t ptr;
        uint16_t skip, first, loop, end;
    } samp_ = {};
    bool dacme_on_ = false, fill_on_ = false;
    uint8_t fifo_[256] = {};
    uint8_t fifo_w_ = 0, fifo_r_ = 0, fdfstate_ = 0;
    uint16_t samplectr_ = 0;
    uint32_t sampleptr_ = 0;
    int timer_period_ = 0, timer_count_ = 0;
};
