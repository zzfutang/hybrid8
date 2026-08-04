//
//  Reusable Decimator.hpp
//  2x -> 1x decimation filter. The oscillator / cross-mod / hard-sync path runs
//  at double rate; this low-pass band-limits before dropping to the host rate,
//  removing the images that sync and audio-rate FM push above the host Nyquist.
//
//  It is an 11-tap linear-phase half-band FIR (Hamming-windowed sinc). Unlike a
//  resonant IIR (which overshoots and rings at every oscillator discontinuity —
//  audible as a per-cycle click on bright tones) a short windowed-sinc FIR has
//  only a tiny, symmetric ripple, yet gives a real stopband (~-45 dB) above the
//  fold instead of the mere -6 dB of a 3-tap binomial kernel.
//

#pragma once
#include "Utils.hpp"

namespace synth {

class Decimator2x {
public:
    void setup(double /*baseSampleRate*/) {}
    void reset() { for (int i = 0; i < K; ++i) z_[i] = 0.0f; pos_ = 0; }

    // Feed both oversampled sub-samples per host sample; the value returned
    // after the 2nd feed is the decimated host-rate output.
    inline float process(float x) {
        z_[pos_] = x;
        float acc = 0.0f;
        int idx = pos_;
        for (int k = 0; k < K; ++k) {
            acc += h_[k] * z_[idx];
            idx = (idx == 0) ? K - 1 : idx - 1;
        }
        pos_ = (pos_ + 1 == K) ? 0 : pos_ + 1;
        return acc;
    }

private:
    static constexpr int K = 11;
    // Half-band FIR: h[0]=0.5 centre, even taps zero, Hamming-windowed sinc,
    // normalised to unity DC gain. Symmetric (linear phase).
    static constexpr float h_[K] = {
        0.005061f, 0.0f, -0.041966f, 0.0f, 0.288460f,
        0.496890f,
        0.288460f, 0.0f, -0.041966f, 0.0f, 0.005061f
    };
    float z_[K] = {0};
    int   pos_ = 0;
};

// Sharper 2x->1x half-band for the FINAL decimation stage. The 11-tap filter
// above has a transition band so wide that a saw's harmonics between ~28 kHz
// and Nyquist pass nearly unattenuated and fold straight into the audible
// band — measured at -32 dB below the fundamental on top-octave notes, and
// the dominant source of the "hash" up there (not the oscillator's PolyBLEP
// residual). This 63-tap Kaiser-windowed half-band (beta 8) is flat to
// 20 kHz and at least -80 dB everywhere that can fold below 20 kHz after
// the drop to the host rate. Keep the short filter for earlier oversampled
// stages, whose leakage a later stage still removes; this one is for the
// stage whose output nothing cleans up.
class Decimator2xSharp {
public:
    void setup(double /*baseSampleRate*/) {}
    void reset() { for (int i = 0; i < K; ++i) z_[i] = 0.0f; pos_ = 0; }

    inline float process(float x) {
        z_[pos_] = x;
        float acc = 0.0f;
        int idx = pos_;
        for (int k = 0; k < K; ++k) {
            acc += h_[k] * z_[idx];
            idx = (idx == 0) ? K - 1 : idx - 1;
        }
        pos_ = (pos_ + 1 == K) ? 0 : pos_ + 1;
        return acc;
    }

private:
    static constexpr int K = 63;
    static constexpr float h_[K] = {
        -0.00002402f, 0.00000000f, 0.00010904f, 0.00000000f, -0.00029356f,
        0.00000000f, 0.00063811f, 0.00000000f, -0.00122208f, 0.00000000f,
        0.00214591f, 0.00000000f, -0.00353441f, 0.00000000f, 0.00554365f,
        0.00000000f, -0.00837621f, 0.00000000f, 0.01231558f, 0.00000000f,
        -0.01780470f, 0.00000000f, 0.02563769f, 0.00000000f, -0.03748938f,
        0.00000000f, 0.05772404f, 0.00000000f, -0.10244251f, 0.00000000f,
        0.31707287f, 0.49999997f, 0.31707287f, 0.00000000f, -0.10244251f,
        0.00000000f, 0.05772404f, 0.00000000f, -0.03748938f, 0.00000000f,
        0.02563769f, 0.00000000f, -0.01780470f, 0.00000000f, 0.01231558f,
        0.00000000f, -0.00837621f, 0.00000000f, 0.00554365f, 0.00000000f,
        -0.00353441f, 0.00000000f, 0.00214591f, 0.00000000f, -0.00122208f,
        0.00000000f, 0.00063811f, 0.00000000f, -0.00029356f, 0.00000000f,
        0.00010904f, 0.00000000f, -0.00002402f
    };
    float z_[K] = {0};
    int   pos_ = 0;
};

} // namespace synth
