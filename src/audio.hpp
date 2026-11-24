// Sound: the GEMS driver on an emulated YM2612 (ymfm) and PSG, mixed into an
// SDL audio stream (or, headless, into a WAV file one frame at a time).
#pragma once

#include <cstdint>
#include <string>

namespace audio {

// Sets up the chips; opens the audio device when `device`, records to
// `wav_path` (headless runs) when it is not empty.
void init(bool device, const std::string& wav_path);
void shutdown();
// The device stopped (on) or playing again.
void pause(bool on);
// Headless recording: the sound of one 1/60 s frame.
void frame();

// The 68000's commands to the driver.
void gems_init(uint32_t patches, uint32_t envelopes, uint32_t sequences, uint32_t samples);
void start_seq(uint8_t seq);
void stop_seq(uint8_t seq);
void stop_all();
void mute(uint8_t seq, uint8_t ch, uint8_t on);

} // namespace audio
