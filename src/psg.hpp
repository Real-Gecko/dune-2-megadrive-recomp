// The SN76489 PSG of the Mega Drive: three square-wave tones and a noise
// channel.
#pragma once

#include <cstdint>

class Psg {
public:
    void write(uint8_t data);
    // Advances by `clocks` PSG clocks (3.58 MHz) and returns the mean output.
    float run(uint32_t clocks);

private:
    uint16_t period_[4] = {0, 0, 0, 0};
    uint16_t counter_[4] = {0, 0, 0, 0};
    uint8_t volume_[4] = {15, 15, 15, 15};
    bool high_[4] = {false, false, false, false};
    uint16_t lfsr_ = 0x8000;
    uint8_t noise_ = 0;
    int latch_ = 0;
    bool latch_volume_ = false;
    uint32_t prescale_ = 0;
};
