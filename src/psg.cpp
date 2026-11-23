#include "psg.hpp"

#include <cmath>

void Psg::write(uint8_t data) {
    if (data & 0x80) {
        latch_ = (data >> 5) & 3;
        latch_volume_ = data & 0x10;
    }
    if (latch_volume_) {
        volume_[latch_] = data & 0x0F;
    } else if (latch_ == 3) {
        noise_ = data & 7;
        lfsr_ = 0x8000;
    } else if (data & 0x80) {
        period_[latch_] = uint16_t((period_[latch_] & 0x3F0) | (data & 0x0F));
    } else {
        period_[latch_] = uint16_t((period_[latch_] & 0x0F) | (data & 0x3F) << 4);
    }
}

float Psg::run(uint32_t clocks) {
    static const auto levels = [] {
        struct { float v[16]; } t{};
        for (int i = 0; i < 15; i++) t.v[i] = std::pow(10.0f, -0.1f * float(i)); // 2 dB steps
        return t;
    }();
    float sum = 0;
    int steps = 0;
    prescale_ += clocks;
    for (; prescale_ >= 16; prescale_ -= 16, steps++) {
        for (int i = 0; i < 3; i++)
            if (counter_[i] <= 1) {
                counter_[i] = period_[i];
                high_[i] = !high_[i];
            } else {
                counter_[i]--;
            }
        if (counter_[3] <= 1) {
            static constexpr uint16_t rates[3] = {0x10, 0x20, 0x40};
            counter_[3] = (noise_ & 3) == 3 ? period_[2] : rates[noise_ & 3];
            high_[3] = !high_[3];
            if (high_[3]) {
                uint16_t fb = (noise_ & 4) ? ((lfsr_ ^ lfsr_ >> 3) & 1) : (lfsr_ & 1);
                lfsr_ = uint16_t(lfsr_ >> 1 | fb << 15);
            }
        } else {
            counter_[3]--;
        }
        for (int i = 0; i < 4; i++) {
            bool on = i < 3 ? high_[i] : (lfsr_ & 1);
            sum += on ? levels.v[volume_[i]] : -levels.v[volume_[i]];
        }
    }
    return steps ? sum / float(steps) : 0.0f;
}
