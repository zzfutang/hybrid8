//
//  Decimator.hpp
//  2x -> 1x decimation filter (4th-order Butterworth low-pass, two biquads).
//  Used to run the oscillator/cross-mod/sync path at double rate and then
//  band-limit before dropping back to the host rate, which substantially
//  reduces aliasing from hard sync and audio-rate FM.
//

#pragma once
#include "Utils.hpp"

namespace synth {

// RBJ biquad low-pass, transposed direct-form II (double state for stability).
class Biquad {
public:
    void setLowpass(double fc, double sampleRate, double q) {
        double w0 = kTwoPi * fc / sampleRate;
        double cs = std::cos(w0);
        double sn = std::sin(w0);
        double alpha = sn / (2.0 * q);
        double b0 = (1.0 - cs) * 0.5;
        double b1 = 1.0 - cs;
        double b2 = (1.0 - cs) * 0.5;
        double a0 = 1.0 + alpha;
        double a1 = -2.0 * cs;
        double a2 = 1.0 - alpha;
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
        a1_ = a1 / a0; a2_ = a2 / a0;
    }
    void reset() { z1_ = 0.0; z2_ = 0.0; }

    inline float process(float x) {
        double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return static_cast<float>(y);
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

// 2x -> 1x decimator. Uses an all-positive (ring-free) FIR half-band-style
// kernel instead of a resonant IIR: a Butterworth/elliptic decimator overshoots
// at every oscillator discontinuity, which is audible as a per-cycle click on
// bright waveforms. This kernel never overshoots. The base oscillators are
// already band-limited by PolyBLEP; the decimator's remaining job is to knock
// down the extra images from sync / FM before dropping to the host rate.
class Decimator2x {
public:
    void setup(double /*baseSampleRate*/) {}
    void reset() { z1_ = z2_ = 0.0f; }

    // Feed both oversampled sub-samples per host sample; the value returned
    // after the 2nd feed is the decimated output. Kernel is a 3-tap binomial
    // low-pass (1 2 1)/4 — monotonic step response (no ringing / overshoot),
    // a null at the 2x Nyquist, and only gentle high-end roll-off so bright
    // tones stay bright.
    inline float process(float x) {
        float y = (x + 2.0f * z1_ + z2_) * 0.25f;
        z2_ = z1_; z1_ = x;
        return y;
    }

private:
    float z1_ = 0.0f, z2_ = 0.0f;
};

} // namespace synth
