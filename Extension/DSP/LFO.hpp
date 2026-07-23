//
//  LFO.hpp
//  Low-frequency oscillator: sine / square / saw. Bipolar output in [-1, 1].
//  Runs at control-but-per-sample resolution so it can modulate audio-rate
//  targets (oscillator pitch) without stepping artefacts.
//

#pragma once
#include "Utils.hpp"

namespace synth {

enum class LFOWave { Sine = 0, Square = 1, Saw = 2 };

class LFO {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset() { phase_ = 0.0; }

    inline void setWave(LFOWave w) { wave_ = w; }
    inline void setRate(double hz) {
        rate_ = hz;
        phaseInc_ = hz / sampleRate_;
    }

    inline float process() {
        float v = 0.0f;
        switch (wave_) {
            case LFOWave::Sine:   v = static_cast<float>(std::sin(kTwoPi * phase_)); break;
            case LFOWave::Square: v = (phase_ < 0.5) ? 1.0f : -1.0f; break;
            case LFOWave::Saw:    v = static_cast<float>(2.0 * phase_ - 1.0); break;
        }
        phase_ += phaseInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return v;
    }

    // Current value without advancing (for shared per-block routing peeks).
    float phase() const { return static_cast<float>(phase_); }

private:
    double  sampleRate_ = 44100.0;
    double  rate_ = 5.0;
    double  phaseInc_ = 0.0;
    double  phase_ = 0.0;
    LFOWave wave_ = LFOWave::Sine;
};

} // namespace synth
