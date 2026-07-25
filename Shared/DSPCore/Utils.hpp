//
//  Utils.hpp
//  Small real-time-safe helpers: fast RNG, one-pole smoother, math utils.
//

#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace synth {

static constexpr double kPi   = 3.14159265358979323846;
static constexpr double kTwoPi = 2.0 * kPi;

// --- Fast, allocation-free white-noise / random source (xorshift64) --------
class FastRandom {
public:
    explicit FastRandom(uint64_t seed = 0x123456789abcdefULL) : state_(seed ? seed : 1) {}

    inline uint64_t nextU64() {
        uint64_t x = state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state_ = x;
        return x;
    }

    // Uniform in [-1, 1)
    inline float nextBipolar() {
        // Take top 24 bits for a float mantissa.
        uint32_t r = static_cast<uint32_t>(nextU64() >> 40); // 24 bits
        return (static_cast<float>(r) / 8388608.0f) - 1.0f;  // /2^23 -> [0,2) - 1
    }

    // Uniform in [0, 1)
    inline float nextUnipolar() {
        uint32_t r = static_cast<uint32_t>(nextU64() >> 40);
        return static_cast<float>(r) / 16777216.0f; // /2^24
    }

private:
    uint64_t state_;
};

// --- One-pole smoother to remove zipper noise on control changes -----------
class OnePoleSmoother {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; setTimeConstant(timeMs_); }
    void setTimeConstant(double ms) {
        timeMs_ = ms;
        const double tc = std::max(0.0001, ms * 0.001);
        coef_ = std::exp(-1.0 / (tc * sampleRate_));
    }
    inline void setTarget(float t) { target_ = t; }
    inline void snap(float v) { target_ = v; value_ = v; }
    inline float next() {
        value_ = target_ + (value_ - target_) * coef_;
        return value_;
    }
    inline float value() const { return value_; }

private:
    double sampleRate_ = 44100.0;
    double timeMs_ = 5.0;
    float  coef_ = 0.0f;
    float  value_ = 0.0f;
    float  target_ = 0.0f;
};

inline float clampf(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

// Fast soft-clip / analog-ish saturation.
inline float softClip(float x) {
    // Cheap tanh approximation, monotonic and bounded in [-1,1].
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// MIDI note -> frequency (A4 = 440 Hz at note 69).
inline double noteToHz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

} // namespace synth
