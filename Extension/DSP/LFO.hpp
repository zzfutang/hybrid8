//
//  LFO.hpp
//  Low-frequency oscillator with phase offset, polarity and sample-and-hold.
//  Runs at control-but-per-sample resolution so it can modulate audio-rate
//  targets (oscillator pitch) without stepping artefacts.
//

#pragma once
#include "Utils.hpp"

namespace synth {

enum class LFOWave {
    Sine = 0, Square = 1, SawUp = 2, SawDown = 3, SampleHold = 4
};

class LFO {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset() { phase_ = 0.0; finished_ = false; hold_ = nextRandom(); }

    inline void setWave(LFOWave w) { wave_ = w; }
    // One-shot ("play once"): after a single cycle the LFO holds its end value
    // instead of looping.
    inline void setOneShot(bool o) { oneShot_ = o; }
    inline void setPolarity(bool unipolar) { unipolar_ = unipolar; }
    inline void setPhase(float phase) {
        phaseOffset_ = clampf(phase, 0.0f, 1.0f);
    }
    inline void setRate(double hz) {
        rate_ = hz;
        phaseInc_ = hz / sampleRate_;
    }

    inline float process() {
        // One-shot has completed: hold the final value forever.
        if (oneShot_ && finished_)
            return unipolar_ ? 0.5f * (heldRaw_ + 1.0f) : heldRaw_;

        double p = phase_ + phaseOffset_;
        p -= std::floor(p);
        float v = 0.0f;
        switch (wave_) {
            case LFOWave::Sine:
                v = static_cast<float>(std::sin(kTwoPi * p)); break;
            case LFOWave::Square:
                v = (p < 0.5) ? 1.0f : -1.0f; break;
            case LFOWave::SawUp:
                v = static_cast<float>(2.0 * p - 1.0); break;
            case LFOWave::SawDown:
                v = static_cast<float>(1.0 - 2.0 * p); break;
            case LFOWave::SampleHold:
                v = hold_; break;
        }
        heldRaw_ = v;
        phase_ += phaseInc_;
        // The small tolerance prevents decimal rates such as 10/100 from
        // gaining an extra sample per cycle through binary rounding.
        if (phase_ >= 1.0 - 1.0e-12) {
            if (oneShot_) {
                finished_ = true;      // hold heldRaw_ (end-of-cycle value)
            } else {
                phase_ -= std::floor(phase_);
                if (phase_ >= 1.0 - 1.0e-12)
                    phase_ = 0.0;
                hold_ = nextRandom();
            }
        }
        return unipolar_ ? 0.5f * (v + 1.0f) : v;
    }

    // Current value without advancing (for shared per-block routing peeks).
    float phase() const { return static_cast<float>(phase_); }

private:
    double  sampleRate_ = 44100.0;
    double  rate_ = 5.0;
    double  phaseInc_ = 0.0;
    double  phase_ = 0.0;
    float   phaseOffset_ = 0.0f;
    float   hold_ = 0.0f;
    float   heldRaw_ = 0.0f;        // last bipolar value (one-shot hold)
    uint32_t randomState_ = 0x9e3779b9u;
    bool    unipolar_ = false;
    bool    oneShot_ = false;
    bool    finished_ = false;
    LFOWave wave_ = LFOWave::Sine;

    inline float nextRandom() {
        randomState_ ^= randomState_ << 13;
        randomState_ ^= randomState_ >> 17;
        randomState_ ^= randomState_ << 5;
        return static_cast<float>(randomState_) * (2.0f / 4294967295.0f)
             - 1.0f;
    }
};

} // namespace synth
