#include "audio.hpp"

#include "gems.hpp"
#include "md/mem.hpp"
#include "psg.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace audio {
namespace {

constexpr uint32_t kFmClock = 7670453;  // NTSC 68000 clock
constexpr uint32_t kPsgClock = 3579545; // NTSC Z80 clock

struct Core {
    ymfm::ymfm_interface intf;
    ymfm::ym2612 fm{intf};
    Psg psg;
    Gems gems{fm, psg};
    uint32_t rate = 0;        // the YM2612's sample rate, clock / 144
    uint32_t tick_phase = 0;  // VBlank at 60 Hz
    uint32_t psg_phase = 0;
};

// The music and the sound effects each on their own driver and chips, so
// effects never take voices from the music.
std::unique_ptr<Core> music, sfx;
std::vector<bool> is_effect(256);
SDL_Mutex* mutex = nullptr;
SDL_AudioStream* stream = nullptr;
std::FILE* wav = nullptr;
uint32_t wav_frames = 0, wav_phase = 0;
std::vector<float> buf;

// The effects' samples, mixed so they overlap (the console's one DAC
// channel cut a playing sample off).
struct Sample {
    uint32_t addr, len;
    int period, t = 0;
};
std::vector<Sample> samples;

float mix_samples() {
    float v = 0;
    for (size_t i = 0; i < samples.size();) {
        Sample& s = samples[i];
        uint32_t k = uint32_t(s.t++ / s.period);
        if (k >= s.len) {
            samples.erase(samples.begin() + long(i));
            continue;
        }
        int d = int8_t(md::rom[s.addr + k] ^ 0x80) * 2; // as ymfm's DAC
        v += float(d < 0 ? d - 3 : d + 4) / 780.0f;
        i++;
    }
    return v;
}

std::unique_ptr<Core> make_core() {
    auto c = std::make_unique<Core>();
    c->fm.reset();
    c->rate = c->fm.sample_rate(kFmClock);
    c->gems.reset();
    return c;
}

void render(Core& c, float out[2]) {
    if ((c.tick_phase += 60) >= c.rate) {
        c.tick_phase -= c.rate;
        c.gems.vblank();
    }
    c.gems.timer_a();
    ymfm::ym2612::output_data o;
    c.fm.generate(&o);
    c.psg_phase += kPsgClock;
    float p = c.psg.run(c.psg_phase / c.rate) * 0.125f;
    c.psg_phase %= c.rate;
    out[0] = float(o.data[0]) / 16384.0f + p;
    out[1] = float(o.data[1]) / 16384.0f + p;
}

void render(float* out, int frames) {
    for (int i = 0; i < frames; i++) {
        float m[2], s[2];
        render(*music, m);
        render(*sfx, s);
        float d = mix_samples();
        s[0] += d;
        s[1] += d;
        out[2 * i] = std::clamp(m[0] + s[0], -1.0f, 1.0f);
        out[2 * i + 1] = std::clamp(m[1] + s[1], -1.0f, 1.0f);
    }
}

void SDLCALL callback(void*, SDL_AudioStream* s, int additional, int) {
    int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    buf.resize(size_t(frames) * 2);
    SDL_LockMutex(mutex);
    render(buf.data(), frames);
    SDL_UnlockMutex(mutex);
    SDL_PutAudioStreamData(s, buf.data(), int(buf.size() * sizeof(float)));
}

void put32(uint32_t v) { std::fwrite(&v, 4, 1, wav); }
void put16(uint16_t v) { std::fwrite(&v, 2, 1, wav); }

void wav_header() {
    std::fseek(wav, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, wav);
    put32(36 + wav_frames * 4);
    std::fwrite("WAVEfmt ", 1, 8, wav);
    put32(16);
    put16(1);
    put16(2);
    put32(music->rate);
    put32(music->rate * 4);
    put16(4);
    put16(16);
    std::fwrite("data", 1, 4, wav);
    put32(wav_frames * 4);
}

} // namespace

void init(bool device, const std::string& wav_path) {
    music = make_core();
    sfx = make_core();
    sfx->gems.on_sample = [](uint32_t addr, uint16_t len, int period) { samples.push_back({addr, len, period}); };
    mutex = SDL_CreateMutex();
    if (!wav_path.empty()) {
        wav = std::fopen(wav_path.c_str(), "wb");
        if (wav) wav_header();
        else std::fprintf(stderr, "cannot write %s\n", wav_path.c_str());
    }
    if (!device) return;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(music->rate)};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, nullptr);
    if (!stream) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
}

void pause(bool on) {
    if (!stream) return;
    if (on) SDL_PauseAudioStreamDevice(stream);
    else SDL_ResumeAudioStreamDevice(stream);
}

void shutdown() {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
    if (wav) {
        wav_header();
        std::fclose(wav);
        wav = nullptr;
    }
}

void frame() {
    if (!wav) return;
    wav_phase += music->rate;
    int n = int(wav_phase / 60);
    wav_phase %= 60;
    buf.resize(size_t(n) * 2);
    render(buf.data(), n);
    std::vector<int16_t> pcm(buf.size());
    for (size_t i = 0; i < buf.size(); i++) pcm[i] = int16_t(buf[i] * 32767.0f);
    std::fwrite(pcm.data(), 2, pcm.size(), wav);
    wav_frames += uint32_t(n);
}

namespace {

struct Lock {
    Lock() { SDL_LockMutex(mutex); }
    ~Lock() { SDL_UnlockMutex(mutex); }
};

} // namespace

// The Z80 reset and driver start, then command 0B.
void gems_init(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples) {
    Lock l;
    for (Core* c : {music.get(), sfx.get()}) {
        c->gems.reset();
        c->gems.banks(patches, envelopes, sequences, samples);
    }
    is_effect.assign(256, false);
    for (uint8_t seq : music->gems.effects()) is_effect[seq] = true;
}

void start_seq(uint8_t seq) {
    Lock l;
    if (!is_effect[seq]) {
        music->gems.start_seq(seq);
        return;
    }
    // On the one driver the music's master attenuation applied to effects too.
    sfx->gems.master_atten(music->gems.master_atten());
    sfx->gems.start_seq(seq);
}

void stop_seq(uint8_t seq) {
    Lock l;
    music->gems.stop_seq(seq);
    sfx->gems.stop_seq(seq);
}

void stop_all() {
    Lock l;
    music->gems.stop_all();
    sfx->gems.stop_all();
    samples.clear();
}

void mute(uint8_t seq, uint8_t ch, uint8_t on) {
    Lock l;
    music->gems.mute(seq, ch, on);
    sfx->gems.mute(seq, ch, on);
}

} // namespace audio
