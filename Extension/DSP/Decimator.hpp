//
//  Decimator.hpp
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

} // namespace synth
