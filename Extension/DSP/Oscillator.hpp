//
//  Oscillator.hpp
//  Band-limited (PolyBLEP) oscillator: saw / square / pulse with variable
//  pulse-width. PolyBLEP suppresses the aliasing that a naive discontinuous
//  waveform would produce, giving a cleaner "analogue" tone.
//

#pragma once
#include "Utils.hpp"

namespace synth {

enum class OscWave { Saw = 0, Square = 1, Pulse = 2 };

class Oscillator {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }

    void reset(float phase = 0.0f) { phase_ = phase; }

    inline void setFrequency(double hz) {
        frequency_ = hz;
        phaseInc_ = hz / sampleRate_;
        // Guard magnitude near Nyquist but keep the sign so through-zero FM can
        // drive the phase backwards.
        if (phaseInc_ >  0.49) phaseInc_ =  0.49;
        if (phaseInc_ < -0.49) phaseInc_ = -0.49;
    }

    inline void setWave(OscWave w) { wave_ = w; }
    inline void setPulseWidth(float pw) { pulseWidth_ = clampf(pw, 0.02f, 0.98f); }

    // Returns one sample in roughly [-1, 1].
    inline float process() {
        const double t  = phase_;
        lastPhase_ = t;
        const double dt = phaseInc_;
        const double adt = dt < 0.0 ? -dt : dt; // |dt| for PolyBLEP width
        // PolyBLEP is expressed in phase space and band-limits the waveform's
        // fixed discontinuity at the cycle boundary. Running the phase backward
        // (through-zero FM) already reverses the correction's temporal direction,
        // so no sign flip is applied — flipping it would reinforce the step
        // (driving reverse-running saw/pulse to +/-2 at the wrap) instead of
        // smoothing it.
        float value = 0.0f;

        switch (wave_) {
            case OscWave::Saw: {
                value = static_cast<float>(2.0 * t - 1.0);
                value -= polyBlep(t, adt);
                break;
            }
            case OscWave::Square:
            case OscWave::Pulse: {
                const double w = (wave_ == OscWave::Square) ? 0.5 : pulseWidth_;
                value = (t < w) ? 1.0f : -1.0f;
                value += polyBlep(t, adt);                  // rising edge at 0
                double tw = t - w; if (tw < 0.0) tw += 1.0; // wrapped falling edge
                value -= polyBlep(tw, adt);
                break;
            }
        }

        phase_ += dt;
        wrapped_ = false;
        if (phase_ >= 1.0)      { phase_ -= 1.0; wrapped_ = true; } // forward wrap
        else if (phase_ < 0.0)  { phase_ += 1.0; wrapped_ = true; } // backward wrap
        return value;
    }

    // True if the most recent process() advanced past the cycle boundary.
    inline bool justWrapped() const { return wrapped_; }

    // A pure sine at this oscillator's phase/frequency, aligned with the sample
    // returned by the last process(). Used as a smooth (discontinuity-free)
    // modulator for cross-modulation / FM so the carrier doesn't click at the
    // modulator's waveform wraps.
    inline float phaseSine() const {
        return static_cast<float>(std::sin(kTwoPi * lastPhase_));
    }

    // Hard-sync: force the phase back to the start of the cycle.
    inline void syncReset() { phase_ = 0.0; wrapped_ = false; }

    double frequency() const { return frequency_; }

private:
    // PolyBLEP correction for a step discontinuity of height 2 at t=0/1.
    static inline float polyBlep(double t, double dt) {
        if (dt <= 0.0) return 0.0f;
        if (t < dt) {
            t /= dt;
            return static_cast<float>(t + t - t * t - 1.0);
        } else if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return static_cast<float>(t * t + t + t + 1.0);
        }
        return 0.0f;
    }

    double  sampleRate_ = 44100.0;
    double  frequency_  = 440.0;
    double  phaseInc_   = 0.0;
    double  phase_      = 0.0;
    OscWave wave_       = OscWave::Saw;
    float   pulseWidth_ = 0.5f;
    bool    wrapped_    = false;
    double  lastPhase_  = 0.0;
};

} // namespace synth
