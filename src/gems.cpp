#include "gems.hpp"

#include "md/mem.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr uint16_t kFmFreq[13] = {644, 682, 723, 766, 811, 859, 910, 965, 1022, 1083, 1147, 1215, 1288};
// Wavelengths of A2..B7, plus one for interpolating above B7.
constexpr uint16_t kPsgFreq[64] = {
    0x3F9, 0x3C0, 0x38A, 0x357, 0x327, 0x2FA, 0x2CF, 0x2A7, 0x281, 0x25D, 0x23B, 0x21B, 0x1FC, 0x1E0, 0x1C5, 0x1AC,
    0x194, 0x17D, 0x168, 0x153, 0x140, 0x12E, 0x11D, 0x10D, 0x0FE, 0x0F0, 0x0E2, 0x0D6, 0x0CA, 0x0BE, 0x0B4, 0x0AA,
    0x0A0, 0x097, 0x08F, 0x087, 0x07F, 0x078, 0x071, 0x06B, 0x065, 0x05F, 0x05A, 0x055, 0x050, 0x04C, 0x047, 0x043,
    0x040, 0x03C, 0x039, 0x035, 0x032, 0x02F, 0x02D, 0x02A, 0x028, 0x026, 0x023, 0x021, 0x020, 0x01E, 0x01C, 0x01C,
};
// Carrier operators per algorithm, bit n for the nth total level in kFmRegs.
constexpr uint8_t kCarriers[8] = {0x08, 0x08, 0x08, 0x08, 0x0A, 0x0E, 0x0E, 0x0F};
// FMADDRTBL: register and the offset of its value in the patch (past the type
// byte); 0 writes 0, bit 7 marks a total level.
constexpr uint8_t kFmRegs[][2] = {
    {0xB0, 2},   {0xB4, 3},  {0x30, 4},   {0x40, 133}, {0x50, 6},  {0x60, 7},  {0x70, 8},  {0x80, 9},
    {0x90, 0},   {0x38, 16}, {0x48, 145}, {0x58, 18},  {0x68, 19}, {0x78, 20}, {0x88, 21}, {0x98, 0},
    {0x34, 10},  {0x44, 139}, {0x54, 12}, {0x64, 13},  {0x74, 14}, {0x84, 15}, {0x94, 0},  {0x3C, 22},
    {0x4C, 151}, {0x5C, 24}, {0x6C, 25},  {0x7C, 26},  {0x8C, 27}, {0x9C, 0},
};

} // namespace

Gems::Gems(ymfm::ym2612& fm, Psg& psg) : fm_(fm), psg_(psg) { reset(); }

uint8_t Gems::rom(uint32_t a) const {
    a &= 0xFFFFFF;
    return a < md::rom.size() ? md::rom[a] : 0;
}

uint16_t Gems::rom16(uint32_t a) const { return uint16_t(rom(a) | rom(a + 1) << 8); }

void Gems::fm(int bank, uint8_t reg, uint8_t val) {
    writes_.push_back({uint8_t(bank), reg, val});
}

void Gems::chip_write(const Write& w) {
    if (w.bank) {
        fm_.write_address_hi(w.reg);
        fm_.write_data_hi(w.val);
    } else {
        fm_.write_address(w.reg);
        fm_.write_data(w.val);
    }
}

// A register of voice 0-2 or 4-6 (the key on/off numbering).
void Gems::fm_voice(int voice, uint8_t reg, uint8_t val) { fm(voice > 3, uint8_t(reg + (voice & 3)), val); }

void Gems::reset() {
    for (Ccb& c : ccb_) c = Ccb{};
    std::memset(patch_, 0, sizeof patch_);
    for (auto& p : patch_) p[0] = 0xFF; // undefined
    chpat_ = patch_[0];
    static constexpr uint8_t fm_voices[6] = {0, 1, 4, 5, 6, 2};
    for (int i = 0; i < 6; i++) fmv_[i] = Voice{uint8_t(0x80 | fm_voices[i])};
    for (int i = 0; i < 3; i++) psgv_[i] = Voice{uint8_t(0x80 | i)};
    noisev_[0] = Voice{0x83};
    for (PsgVoice& p : pv_) p = PsgVoice{};
    for (Ecb& e : ecb_) e = Ecb{};
    std::memset(envbuf_, 0, sizeof envbuf_);
    std::memset(pbend_, 0, sizeof pbend_);
    std::memset(ebend_, 0, sizeof ebend_);
    std::memset(retrig_, 0, sizeof retrig_);
    needbend_ = false;
    std::memset(mbox_, 0, sizeof mbox_);
    ptbl_ = etbl_ = stbl_ = dtbl_ = 0;
    sbpt_ = 204;
    sbptacc_ = 0;
    tbase_ = tickcnt_ = masteratn_ = carriers_ = 0;
    ch3mode_ = 0x15;
    non_note_ = non_ch_ = non_voice_ = non_atten_ = 0;
    non_freq_ = 0;
    samp_ = {};
    dacme_on_ = fill_on_ = false;
    fifo_w_ = fifo_r_ = fdfstate_ = 0;
    samplectr_ = 0;
    sampleptr_ = 0;
    timer_period_ = timer_count_ = 0;
    for (uint8_t b : {0x9F, 0xBF, 0xDF, 0xFF}) psg_.write(b); // silence the PSG
}

void Gems::banks(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples) {
    ptbl_ = patches & 0xFFFFFF;
    etbl_ = envelopes & 0xFFFFFF;
    stbl_ = sequences & 0xFFFFFF;
    dtbl_ = samples & 0xFFFFFF;
}

// --- PSG envelopes (60 Hz) ---

void Gems::dopsgenv() {
    uint8_t cmdbits = 0x80;
    for (int i = 0; i < 4; i++, cmdbits += 0x20) {
        PsgVoice& p = pv_[i];
        uint8_t c = p.com;
        p.com = 0;
        if (c & 4) { // stop
            p.lev = 0xFF;
            p.flg = 1;
            p.mode = 0;
            if (i == 3) psgv_[2].flags &= ~0x20;
        }
        if ((c & 2) && p.mode) { // key off
            p.flg = 1;
            p.mode = 4;
        }
        if (c & 1) { // key on
            p.lev = 0xFF;
            psg_.write(p.dtl | cmdbits);
            if (i != 3) psg_.write(p.dth);
            p.flg = 1;
            p.mode = 1;
        }
        switch (p.mode) {
        case 1: { // attack
            p.flg = 1;
            int a = p.lev - p.atk;
            if (a <= 0 || a <= p.alv) {
                p.lev = p.alv;
                p.mode = 2;
            } else {
                p.lev = uint8_t(a);
            }
            break;
        }
        case 2: { // decay
            p.flg = 1;
            int a = p.lev;
            bool end;
            if (a < p.slv) {
                a += p.dky;
                end = a > 0xFF || a >= p.slv;
            } else if (a == p.slv) {
                end = true;
            } else {
                a -= p.dky;
                end = a < 0 || a < p.slv;
            }
            if (end) {
                p.lev = p.slv;
                p.mode = 3;
            } else {
                p.lev = uint8_t(a);
            }
            break;
        }
        case 4: { // release
            p.flg = 1;
            int a = p.lev + p.rrt;
            if (a > 0xFF) {
                p.lev = 0xFF;
                p.mode = 0;
                if (i == 3) psgv_[2].flags &= ~0x20;
            } else {
                p.lev = uint8_t(a);
            }
            break;
        }
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!(pv_[i].flg & 1)) continue;
        pv_[i].flg = 0;
        psg_.write(uint8_t(pv_[i].lev >> 4 | (0x90 + 0x20 * i)));
    }
}

// --- Sequencer ---

uint8_t Gems::sbyte(Ccb& c) {
    uint8_t b = rom(c.addr);
    c.addr = (c.addr + 1) & 0xFFFFFF;
    return b;
}

void Gems::updseq() {
    for (int ch = 0; ch < 16; ch++) {
        chpat_ = patch_[ch];
        uint8_t f = ccb_[ch].flags;
        if (!(f & 0x10)) continue;
        if (tbase_ & ((f & 0x08) ? 1 : 2)) sequencer(ch);
    }
}

void Gems::sequencer(int ch) {
    Ccb& c = ccb_[ch];
    if (++c.timer != 0) return;
    uint8_t a = sbyte(c);
    for (;;) {
        if (a & 0x80) { // delay (C0) or duration (80): 6 bits per byte, msbs first
            uint8_t kind = a & 0xC0;
            uint16_t v = a & 0x3F;
            for (;;) {
                a = sbyte(c);
                if ((a & 0xC0) != kind) break;
                v = uint16_t(v << 6 | (a & 0x3F));
            }
            (kind == 0xC0 ? c.delay : c.dur) = uint16_t(-v);
            continue;
        }
        if (a < 96) {
            if (!(c.flags & 2)) noteon(ch, a);
        } else {
            Next n = seqcmd(ch, a);
            if (n == kStop) return;
            if (n == kNextCmd) {
                a = sbyte(c);
                continue;
            }
        }
        if (c.delay) {
            c.timer = c.delay;
            return;
        }
        a = sbyte(c);
    }
}

Gems::Next Gems::seqcmd(int ch, uint8_t cmd) {
    Ccb& c = ccb_[ch];
    switch (cmd) {
    case 96: // end of sequence
        c.flags = 0;
        c.dur = 0;
        return kStop;
    case 97: // program change
        c.pnum = sbyte(c);
        fetchpatch(c);
        return kDelay;
    case 98: // envelope
        c.env = sbyte(c);
        if (!(c.flags & 0x40)) trigenv(c, uint8_t(ch), c.env);
        return kDelay;
    case 99: // nop
        return kDelay;
    case 100: { // loop start
        int i = 0;
        while (i < 3 && c.loops[i].count) i++;
        c.loops[i].count = sbyte(c);
        c.loops[i].addr = uint16_t(c.addr);
        return kNextCmd;
    }
    case 101: { // loop end
        int i = 3;
        while (i > 0 && !c.loops[i].count) i--;
        if (c.loops[i].count != 127 && --c.loops[i].count == 0) return kNextCmd;
        uint16_t cur = uint16_t(c.addr);
        c.addr = (c.addr & 0xFF0000) | c.loops[i].addr;
        if (c.loops[i].addr >= cur) c.addr = (c.addr - 0x10000) & 0xFFFFFF;
        return kNextCmd;
    }
    case 102: // retrigger mode
        if (sbyte(c)) c.flags |= 0x40;
        else c.flags &= ~0x40;
        return kDelay;
    case 103: // sustain
        if (sbyte(c)) c.flags |= 0x80;
        else c.flags &= ~0x80;
        return kDelay;
    case 104: // tempo
        set_tempo(uint8_t(sbyte(c) + 40));
        return kDelay;
    case 105: { // mute: bit 4 unmutes, bits 0-3 the channel of this sequence
        uint8_t h = sbyte(c);
        for (Ccb& o : ccb_) {
            if (!(o.flags & 1) || o.snum != c.snum || (h & 15) != o.vchan) continue;
            if (h & 0x10) o.flags &= ~2;
            else o.flags |= 2;
            break;
        }
        return kDelay;
    }
    case 106: // priority
        c.prio = sbyte(c);
        return kDelay;
    case 107: // start sequence
        start_seq(sbyte(c));
        return kDelay;
    case 108: { // pitch bend, semitones with 8 fraction bits
        uint8_t lo = sbyte(c);
        pbend_[ch] = int16_t(lo | sbyte(c) << 8);
        retrig_[ch] |= 1;
        needbend_ = true;
        return kDelay;
    }
    case 109: // use the sfx timebase
        c.flags |= 0x08;
        return kDelay;
    case 110: { // sample playback rate
        uint8_t d = sbyte(c);
        if (chpat_[0] == 1) chpat_[1] = d;
        return kDelay;
    }
    case 111: { // goto, 16-bit signed offset
        uint8_t lo = sbyte(c);
        int16_t off = int16_t(lo | sbyte(c) << 8);
        c.addr = (c.addr + uint32_t(int32_t(off))) & 0xFFFFFF;
        return kNextCmd;
    }
    case 112: { // store to a mailbox
        uint8_t n = sbyte(c);
        mbox_[2 + n] = sbyte(c);
        return kDelay;
    }
    case 113: { // if mailbox <relation> value, else skip forward
        uint8_t m = mbox_[2 + sbyte(c)];
        uint8_t rel = sbyte(c);
        uint8_t v = sbyte(c);
        bool t;
        switch (rel) {
        case 1: t = m != v; break;
        case 2: t = m > v; break;
        case 3: t = m >= v; break;
        case 4: t = m < v; break;
        case 5: t = m <= v; break;
        default: t = m == v; break;
        }
        uint8_t off = sbyte(c);
        if (!t) c.addr = (c.addr + off) & 0xFFFFFF;
        return kNextCmd;
    }
    case 114: { // extra functions
        uint8_t code = sbyte(c);
        uint8_t v = sbyte(c);
        switch (code) {
        case 0: stop_seq(v); break;
        case 1: pause_seq(v); break;
        case 2: resume_all(); break;
        case 3: pause_seq(mbox_[2]); break;
        case 4: masteratn_ = v; break;
        case 5: c.atn = v; break;
        }
        return kDelay;
    }
    }
    return kNextCmd;
}

// --- Voice timers ---

void Gems::vtimer(VoiceList list, bool psg) {
    for (int i = 0; i < list.count; i++) {
        Voice& v = list.first[i];
        if (!(tbase_ & ((v.flags & 0x08) ? 1 : 2))) continue;
        if (v.flags & 0x40) {
            if (--v.release == 0) v.flags &= ~0x40;
            continue;
        }
        if (!(v.flags & 0x10)) continue;
        if (++v.dur != 0) continue;
        v.flags &= ~0x18;
        if ((v.flags & 0x2F) == 0x26) {
            noteoffdig();
            continue;
        }
        v.flags |= 0xC0;
        if (psg) pv_[v.flags & 7].com |= 2;
        else fm(0, 0x28, v.flags & 7);
    }
}

// --- Sequence control ---

void Gems::resume_all() {
    for (Ccb& c : ccb_)
        if (!(c.flags & 0x20) && (c.flags & 1)) c.flags |= 0x10;
}

void Gems::start_seq(uint8_t seq) {
    uint32_t hdr = stbl_ + rom16(stbl_ + uint8_t(seq * 2));
    uint8_t tracks = rom(hdr);
    if (!tracks) return;
    uint8_t n = 0;
    for (int ch = 0; ch < 16; ch++) {
        Ccb& c = ccb_[ch];
        if (c.flags & 0x21) continue;
        c.flags = 0x11;
        c.addr = (stbl_ + rom16(hdr + 1 + 2 * n)) & 0xFFFFFF;
        c.timer = 0xFFFF;
        c.snum = seq;
        c.vchan = n;
        for (auto& l : c.loops) l.count = 0;
        c.env = c.prio = c.atn = 0;
        pbend_[ch] = 0;
        ebend_[ch] = 0;
        if (++n == tracks) return;
    }
}

std::vector<uint8_t> Gems::effects() const {
    // Argument bytes of the commands 97-114.
    static constexpr uint8_t kArgs[18] = {1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 2, 0, 1, 2, 2, 4, 2};
    std::vector<uint8_t> out;
    // The sequence bank comes right before the envelope bank in this ROM.
    uint32_t limit = etbl_ > stbl_ ? etbl_ - stbl_ : 0;
    for (uint32_t i = 0; 2 * i < limit && i < 256; i++) {
        uint32_t hdr = stbl_ + rom16(stbl_ + 2 * i);
        limit = std::min(limit, hdr - stbl_);
        if (rom(hdr) != 1) continue;
        uint32_t p = stbl_ + rom16(hdr + 1);
        bool note = false, master = false;
        for (uint8_t a; (a = rom(p)) != 96; p += 1 + (a > 96 && a < 115 ? kArgs[a - 97] : 0)) {
            if (a < 96) note = true;
            if (a == 114 && rom(p + 1) == 4) master = true;
        }
        if (note && !master) out.push_back(uint8_t(i));
    }
    return out;
}

void Gems::stop_seq(uint8_t seq) {
    for (Ccb& c : ccb_) {
        if (!(c.flags & 1) || (c.flags & 0x20)) continue;
        if (seq != 255 && c.snum != seq) continue;
        c.flags = 0;
        c.dur = 0;
    }
    clip_all();
}

void Gems::pause_seq(uint8_t seq) {
    for (Ccb& c : ccb_)
        if ((c.flags & 1) && !(c.flags & 0x20) && c.snum == seq) c.flags &= ~0x10;
    clip_all();
}

void Gems::stop_all() {
    for (Ccb& c : ccb_) {
        c.flags = 0;
        c.dur = 0;
    }
    clip_all();
}

void Gems::mute(uint8_t seq, uint8_t ch, uint8_t on) {
    for (Ccb& c : ccb_) {
        if (!(c.flags & 1) || c.snum != seq || c.vchan != ch) continue;
        if (on & 1) c.flags |= 2;
        else c.flags &= ~2;
    }
}

// Cuts the voices and envelopes of channels that are not running.
void Gems::clip_all() {
    clip({fmv_, 6}, false);
    clip({psgv_, 3}, true);
    clip({noisev_, 1}, true);
    for (Ecb& e : ecb_) {
        if (e.chan & 0x40) continue;
        if (ccb_[e.chan & 15].flags & 0x10) continue;
        e.chan |= 0x40;
    }
}

void Gems::clip(VoiceList list, bool psg) {
    for (int i = 0; i < list.count; i++) {
        Voice& v = list.first[i];
        if (ccb_[v.ch & 15].flags & 0x10) continue;
        uint8_t old = v.flags;
        int vn = old & 7;
        v.flags = uint8_t(vn | 0x80);
        v.dur = 0;
        v.release = 0;
        if (psg) {
            pv_[vn].com = 4;
        } else if (old & 0x20) {
            dac_off();
        } else {
            for (uint8_t reg : {0x40, 0x44, 0x48, 0x4C}) fm_voice(vn, reg, 0x7F);
            fm(0, 0x28, uint8_t(vn));
        }
    }
}

void Gems::set_tempo(uint8_t bpm) { sbpt_ = uint16_t(uint16_t(bpm * 218) >> 7); }

// --- Pitch envelopes and bends ---

void Gems::trigenv(Ccb& c, uint8_t ch, uint8_t env) {
    Ecb* e = nullptr;
    for (Ecb& x : ecb_)
        if (x.chan == ch) {
            e = &x;
            break;
        }
    if (!e)
        for (Ecb& x : ecb_)
            if (x.chan & 0x40) {
                e = &x;
                break;
            }
    if (!e) return;
    int buf = int(e - ecb_) * 32;
    e->chan = uint8_t((c.flags & 0x08) ? ch | 0x20 : ch);
    e->ctr = 0;
    uint32_t src = etbl_ + rom16(etbl_ + uint8_t(env * 2));
    for (int i = 0; i < 32; i++) envbuf_[buf + i] = rom(src + i);
    ebend_[ch] = uint16_t(envbuf_[buf] | envbuf_[buf + 1] << 8);
    e->ptr = uint8_t(buf + 2);
}

void Gems::doenvelope() {
    for (Ecb& e : ecb_) {
        if (e.chan & 0x40) continue;
        if (!(tbase_ & ((e.chan & 0x20) ? 1 : 2))) continue;
        int ch = e.chan & 0x0F;
        bool done = false;
        uint8_t ctr;
        if (e.ctr) {
            ctr = uint8_t(e.ctr - 1);
        } else {
            uint8_t n = envbuf_[e.ptr & 127];
            if (n == 0) {
                done = true;
                ctr = 0;
            } else {
                ctr = uint8_t(n - 1);
                e.delta = uint16_t(envbuf_[(e.ptr + 1) & 127] | envbuf_[(e.ptr + 2) & 127] << 8);
                e.ptr = uint8_t(e.ptr + 3);
            }
        }
        if (done) {
            ebend_[ch] = 0;
            e.chan = 0x40;
        } else {
            e.ctr = ctr;
            ebend_[ch] = uint16_t(ebend_[ch] + e.delta);
        }
        retrig_[ch] |= 1;
        needbend_ = true;
    }
}

void Gems::applybend() {
    if (!needbend_) return;
    needbend_ = false;
    for (Voice& v : fmv_) {
        int ch = v.ch & 15;
        if (!(retrig_[ch] & 1)) continue;
        non_freq_ = getfreq(false, v.note, ch);
        int vn = v.flags & 7;
        fm_voice(vn, 0xA4, uint8_t(non_freq_ >> 8));
        fm_voice(vn, 0xA0, uint8_t(non_freq_));
    }
    for (Voice& v : psgv_) {
        int ch = v.ch & 15;
        if (!(retrig_[ch] & 1)) continue;
        uint16_t f = getfreq(true, v.note, ch);
        psg_.write(uint8_t((f & 0x0F) | 0x80 | (v.flags & 7) << 5));
        psg_.write(uint8_t(f >> 4));
    }
    std::memset(retrig_, 0, sizeof retrig_);
}

// The FM block/F-number or PSG wavelength of a note (0 = C0) plus the
// channel's pitch and envelope bends.
uint16_t Gems::getfreq(bool psg, uint8_t note, int ch) {
    uint16_t bend = uint16_t(pbend_[ch] + ebend_[ch]);
    uint8_t frac = uint8_t(bend);
    uint8_t a = uint8_t(note + (bend >> 8));
    if (a >= 96) {
        if (bend & 0x8000) {
            frac = 0;
            a = 0;
        } else {
            frac = 0xFF;
            a = 95;
        }
    }
    const uint16_t* tbl;
    uint8_t oct = 0;
    if (!psg) {
        oct = uint8_t(a / 12);
        a = uint8_t(a % 12);
        tbl = kFmFreq;
    } else {
        if (a < 33) {
            a = 0;
            frac = 0;
        } else {
            a = uint8_t(a - 33);
        }
        tbl = kPsgFreq;
    }
    uint16_t delta = uint16_t(tbl[a + 1] - tbl[a]);
    int8_t step = int8_t(uint16_t(frac * delta) >> 8);
    uint16_t v = uint16_t(tbl[a] + step);
    if (!psg) v |= uint16_t(oct << 11);
    return v;
}

// --- Note on ---

void Gems::noteon(int ch, uint8_t note) {
    Ccb& c = ccb_[ch];
    uint8_t atten = uint8_t(masteratn_ + c.atn);
    non_atten_ = (atten & 0x80) ? 127 : atten;
    non_note_ = note;
    non_ch_ = uint8_t(ch);
    switch (chpat_[0]) {
    case 0: noteonfm(c); break;
    case 1: noteondig(c); break;
    case 2: noteonpsg(c, false); break;
    case 3: noteonpsg(c, true); break;
    }
}

void Gems::noteonpsg(Ccb& c, bool noise) {
    Voice* v;
    uint8_t f = noise ? allocspec(c, noisev_, v) : alloc(c, {psgv_, 3}, v);
    if (f == 0xFF) return;
    vtandet(c, v, f);
    uint16_t freq = getfreq(true, non_note_, non_ch_);
    const uint8_t* p = chpat_ + 1;
    PsgVoice& pv = pv_[non_voice_];
    pv.dtl = freq & 0x0F;
    pv.dth = uint8_t(freq >> 4);
    if (non_voice_ == 3) {
        psgv_[2].flags &= ~0x20;
        // The driver tests the patch type here, which is always 3, so every
        // noise note takes over TG3 (for the noise modes clocked by it).
        if ((p[-1] & 3) == 3) {
            psg_.write(pv.dtl | 0xC0);
            psg_.write(pv.dth);
            psgv_[2].flags = 0xA2;
            psgv_[2].note = non_note_;
            psgv_[2].ch = non_ch_;
            pv_[2].com = 4;
        }
        pv.dtl = p[0];
    }
    pv.atk = p[1];
    pv.slv = uint8_t(p[2] << 4);
    pv.alv = uint8_t(p[3] << 4);
    pv.dky = p[4];
    pv.rrt = p[5];
    pv.com |= 1;
}

void Gems::noteondig(Ccb& c) {
    if (on_sample) {
        uint8_t s = uint8_t(non_note_ - 48);
        if (non_note_ < 48) s = uint8_t(s + 96);
        uint32_t h = dtbl_ + s * 12u;
        uint8_t n = uint8_t(-(chpat_[1] != 4 ? chpat_[1] : rom(h) & 0x0F));
        int period = 1024 - ((uint8_t(int8_t(n) >> 2) << 2) | (n & 3)); // as timer A
        uint32_t ptr = uint32_t(rom(h + 1) | rom(h + 2) << 8 | rom(h + 3) << 16) + rom16(h + 4);
        if (rom16(h + 6)) on_sample((dtbl_ + ptr) & 0xFFFFFF, rom16(h + 6), period);
        return;
    }
    Voice* v;
    uint8_t f = allocspec(c, &fmv_[4], v);
    if (f == 0xFF) return;
    if (!(f & 0x80)) {
        dacme_on_ = false;
        if (!(f & 0x20)) fm(0, 0x28, f & 7);
    }
    vtandet(c, v, f);
    fmv_[4].flags |= 0x20;
    // C4..B7 are samples 0-47, C0..B3 48-95.
    uint8_t s = uint8_t(non_note_ - 48);
    if (non_note_ < 48) s = uint8_t(s + 96);
    uint32_t h = dtbl_ + s * 12u;
    samp_.flags = rom(h);
    samp_.ptr = uint32_t(rom(h + 1) | rom(h + 2) << 8 | rom(h + 3) << 16);
    samp_.skip = rom16(h + 4);
    samp_.first = rom16(h + 6);
    samp_.loop = rom16(h + 8);
    samp_.end = rom16(h + 10);
    if (samp_.first == 0) {
        fmv_[4].flags = 0xC6;
        return;
    }
    if (chpat_[1] != 4) samp_.flags = uint8_t((samp_.flags & 0xF0) | chpat_[1]);
    fifo_r_ = 0;
    // Timer A counts the rate value in YM2612 samples.
    uint8_t n = uint8_t(-(samp_.flags & 0x0F));
    fm(0, 0x24, uint8_t(int8_t(n) >> 2));
    fm(0, 0x25, n & 3);
    fm(0, 0x2B, 0x80);
    fm(0, 0x27, ch3mode_);
    fm(1, 0xB6, 0xC0);
    timer_period_ = 1024 - ((uint8_t(int8_t(n) >> 2) << 2) | (n & 3));
    timer_count_ = 0;
    sampleptr_ = (dtbl_ + samp_.ptr + samp_.skip) & 0xFFFFFF;
    samplectr_ = samp_.first;
    fill_on_ = true;
    fifo_w_ = 0;
    fdfstate_ = (samp_.flags & 0x10) ? 5 : 4;
    fill_dac_fifo(true);
    dacme_on_ = true;
}

void Gems::noteonfm(Ccb& c) {
    bool ch3 = chpat_[2] & 0x40;
    Voice* v;
    uint8_t f = ch3 ? allocspec(c, &fmv_[5], v) : alloc(c, {fmv_, 6}, v);
    if (f == 0xFF) return;
    if (!(f & 0x80)) fm(0, 0x28, f & 7);
    vtandet(c, v, f);
    if (!ch3) non_freq_ = getfreq(false, non_note_, non_ch_);
    const uint8_t* p = chpat_ + 1;
    int vn = non_voice_;
    if (vn == 2) {
        ch3mode_ = p[1] | 0x15;
        fm(0, 0x27, p[1] | 0x05);
    }
    if (p[0] & 0x08) fm(0, 0x22, p[0]);
    carriers_ = kCarriers[p[2] & 7];
    for (const auto& [reg, off] : kFmRegs) {
        uint8_t val = 0;
        if (off & 0x80) {
            val = p[off & 0x7F];
            bool carrier = carriers_ & 1;
            carriers_ >>= 1;
            if (carrier) {
                uint8_t level = uint8_t(127 - val);
                uint8_t cut = uint8_t(uint16_t(uint8_t(level << 1) * non_atten_) >> 8);
                val = uint8_t(127 - uint8_t(level - cut));
            }
        } else if (off) {
            val = p[off];
        }
        fm_voice(vn, reg, val);
    }
    if (!(p[1] & 0x40)) {
        fm_voice(vn, 0xA4, uint8_t(non_freq_ >> 8));
        fm_voice(vn, 0xA0, uint8_t(non_freq_));
    } else {
        static constexpr uint8_t ch3regs[8] = {0xA6, 0xA2, 0xAC, 0xA8, 0xAD, 0xA9, 0xAE, 0xAA};
        for (int i = 0; i < 8; i++) fm(0, ch3regs[i], p[28 + i]);
    }
    fm(0, 0x28, uint8_t(p[36] << 4 | non_voice_));
}

void Gems::vtandet(Ccb& c, Voice* v, uint8_t flags) {
    non_voice_ = flags & 7;
    v->flags = non_voice_;
    if (c.dur) v->flags |= 0x10;
    if (c.flags & 0x08) v->flags |= 0x08;
    v->prio = c.prio;
    v->note = non_note_;
    v->ch = non_ch_;
    v->dur = c.dur;
    v->release = 254;
    if (c.flags & 0x40) trigenv(c, non_ch_, c.env);
}

void Gems::noteoffdig() {
    if (samp_.flags & 0x20) fdfstate_ = 7;
    else if (samp_.flags & 0x10) fdfstate_ = 6;
}

void Gems::fetchpatch(Ccb& c) {
    uint32_t src = ptbl_ + rom16(ptbl_ + uint8_t(c.pnum * 2));
    for (int i = 0; i < 39; i++) chpat_[i] = rom(src + i);
}

// --- Voice allocation ---

// A free voice last used by this channel (unless it sustains), else the one
// free the longest, else the lowest priority one in use if it is not above
// the channel's.
uint8_t Gems::alloc(Ccb& c, VoiceList list, Voice*& out) {
    uint8_t lowest = 0xFF, freest = 0xFF;
    Voice *lowp = nullptr, *freep = nullptr;
    for (int i = 0; i < list.count; i++) {
        Voice& v = list.first[i];
        if (v.flags & 0x20) continue;
        if (v.flags & 0x80) {
            if (v.ch == non_ch_ && !(c.flags & 0x80)) {
                out = &v;
                return v.flags;
            }
            if (v.release < freest) {
                freest = v.release;
                freep = &v;
            }
        } else if (v.prio < lowest) {
            lowest = v.prio;
            lowp = &v;
        }
    }
    if (freest != 0xFF) {
        out = freep;
        return freep->flags;
    }
    if (lowest > c.prio) return 0xFF;
    out = lowp;
    return lowp->flags;
}

uint8_t Gems::allocspec(Ccb& c, Voice* v, Voice*& out) {
    if (!(v->flags & 0x80) && v->prio > c.prio) return 0xFF;
    out = v;
    return v->flags;
}

// --- DAC ---

void Gems::dac_off() {
    dacme_on_ = false;
    fill_on_ = false;
    fm(0, 0x2B, 0);
}

// FILLDACFIFO: refills the half of the FIFO DACME is not reading. A sample
// ends when its counter runs out, cutting off what is still in the FIFO.
void Gems::fill_dac_fifo(bool force) {
    if (!force && (!fill_on_ || !((fifo_w_ ^ fifo_r_) & 0x80))) return;
    if (fdfstate_ < 7 && samplectr_ > 128) {
        samplectr_ = uint16_t(samplectr_ - 128);
        for (int i = 0; i < 128; i++) fifo_[uint8_t(fifo_w_ + i)] = rom(sampleptr_ + i);
        fifo_w_ = uint8_t(fifo_w_ + 128);
        sampleptr_ = (sampleptr_ + 128) & 0xFFFFFF;
        return;
    }
    // Looped samples (state 5) are not used by this game.
    dac_off();
    fmv_[4].flags = 0xC6;
    fmv_[4].dur = 0;
}

void Gems::timer_a() {
    for (int i = 0; i < 2 && write_head_ < writes_.size(); i++) chip_write(writes_[write_head_++]);
    if (write_head_ == writes_.size()) {
        writes_.clear();
        write_head_ = 0;
    }
    if (!dacme_on_ || timer_period_ <= 0) return;
    if (++timer_count_ < timer_period_) return;
    timer_count_ = 0;
    chip_write({0, 0x2A, fifo_[fifo_r_++]});
    fill_dac_fifo(false);
}

// --- Main loop ---


void Gems::vblank() {
    tickcnt_++;
    sbptacc_ = uint16_t(sbptacc_ + sbpt_);
    for (;;) {
        uint8_t b = 0;
        if (tickcnt_) {
            tickcnt_--;
            dopsgenv();
            b = 1;
        }
        if (sbptacc_ >= 0x100) {
            sbptacc_ = uint16_t(sbptacc_ - 0x100);
            b |= 2;
        }
        if (!b) break;
        tbase_ = b;
        doenvelope();
        vtimer({fmv_, 6}, false);
        vtimer({psgv_, 3}, true);
        vtimer({noisev_, 1}, true);
        updseq();
        applybend();
    }
}
