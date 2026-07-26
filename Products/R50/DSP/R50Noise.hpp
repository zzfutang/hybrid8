//
//  R50Noise.hpp
//  R50's second sound source. With no virtual-analog oscillator in the design,
//  noise carries every non-pitched element: attack transients, breath and bow
//  air under a sustained tone, and the stepped digital textures that a
//  band-limited table cannot produce.
//
//  Real-time safe: no allocation, no locking, and the generator state is per
//  voice. Each voice is seeded deterministically from its index, so voices
//  decorrelate from one another while a render stays bit-repeatable.
//
//  The per-spectrum gains are trimmed so every colour lands at the same output
//  level as a wave table. They are not equal, and cannot be: each filter shapes
//  a different amount of the spectrum away, and the voice filter then removes a
//  different share again depending on where the energy sits. Measured before
//  trimming, the colours spanned 11 dB — violet and the band-pass nearly 9 dB
//  below a saw while blue sat 2 dB above it.
//

#pragma once

#include "Filter.hpp"
#include "Utils.hpp"

namespace r50 {

enum class NoiseSpectrum {
    White = 0,   //  0 dB/oct
    Pink,        // -3 dB/oct
    Brown,       // -6 dB/oct
    Blue,        // +3 dB/oct
    Violet,      // +6 dB/oct
    Filtered,    // band-passed, optionally tracking the note
    SampleHold   // stepped — aliased on purpose
};

static constexpr int kNoiseSpectrumCount = 7;

class NoiseSource {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        bandpass_.setSampleRate(sr);
        updateStepRate();
    }

    /// Distinct per voice so simultaneous notes do not produce identical noise.
    void setSeed(uint64_t seed) { rng_ = synth::FastRandom(seed); }

    void reset() {
        bandpass_.reset();
        for (float &state : pink_) state = 0.0f;
        brown_ = 0.0f;
        lastWhite_ = 0.0f;
        lastPink_  = 0.0f;
        stepValue_ = 0.0f;
        stepPhase_ = 0.0;
    }

    /// Control-rate update. Everything costly (the band-pass coefficients, the
    /// step-rate division) happens here rather than per sample.
    void updateBlock(NoiseSpectrum spectrum, float tone, float rateHz,
                     bool pitchTrack, double noteHz) {
        spectrum_   = spectrum;
        tone_       = synth::clampf(tone, 0.0f, 1.0f);
        rateHz_     = rateHz;
        pitchTrack_ = pitchTrack;
        noteHz_     = noteHz;

        if (spectrum_ == NoiseSpectrum::Filtered) {
            // Tracking puts the band on a harmonic of the note, which is what
            // makes filtered noise read as breath belonging to the tone rather
            // than as a separate hiss layer.
            double centre = pitchTrack_
                ? noteHz_ * (0.5 + tone_ * 6.0)
                : 100.0 * std::pow(2.0, tone_ * 7.0);
            centre = synth::clampf(static_cast<float>(centre), 30.0f,
                                   static_cast<float>(sampleRate_ * 0.45));
            bandpass_.setCoefficients(centre, 4.0, 0.0f, 0.0f);
        }
        updateStepRate();
    }

    inline float process() {
        const float white = rng_.nextBipolar();

        switch (spectrum_) {
            case NoiseSpectrum::White:
                return white * 1.06f;

            case NoiseSpectrum::Pink:
                return processPink(white) * 0.31f;

            case NoiseSpectrum::Brown: {
                // Leaky integrator: -6 dB/oct without unbounded DC drift.
                brown_ = (brown_ + 0.02f * white) / 1.02f;
                return brown_ * 8.4f;
            }

            case NoiseSpectrum::Blue: {
                // Differentiated pink is +3 dB/oct.
                const float pink = processPink(white);
                const float blue = pink - lastPink_;
                lastPink_ = pink;
                return blue * 1.33f;
            }

            case NoiseSpectrum::Violet: {
                // Differentiated white is +6 dB/oct.
                const float violet = white - lastWhite_;
                lastWhite_ = white;
                return violet * 0.98f;
            }

            case NoiseSpectrum::Filtered:
                return bandpass_.process(white).bp * 1.35f;

            case NoiseSpectrum::SampleHold: {
                stepPhase_ += stepInc_;
                if (stepPhase_ >= 1.0) {
                    stepPhase_ -= std::floor(stepPhase_);
                    stepValue_ = white;
                }
                return stepValue_ * 0.81f;
            }
        }
        return 0.0f;
    }

private:
    /// Paul Kellet's pink filter — a fixed cascade whose summed response holds
    /// -3 dB/oct across the audible band.
    inline float processPink(float white) {
        pink_[0] = 0.99886f * pink_[0] + white * 0.0555179f;
        pink_[1] = 0.99332f * pink_[1] + white * 0.0750759f;
        pink_[2] = 0.96900f * pink_[2] + white * 0.1538520f;
        pink_[3] = 0.86650f * pink_[3] + white * 0.3104856f;
        pink_[4] = 0.55000f * pink_[4] + white * 0.5329522f;
        pink_[5] = -0.7616f * pink_[5] - white * 0.0168980f;
        const float out = pink_[0] + pink_[1] + pink_[2] + pink_[3]
                        + pink_[4] + pink_[5] + pink_[6] + white * 0.5362f;
        pink_[6] = white * 0.115926f;
        return out;
    }

    void updateStepRate() {
        double rate = pitchTrack_ ? noteHz_ * (1.0 + tone_ * 8.0) : rateHz_;
        rate = synth::clampf(static_cast<float>(rate), 10.0f,
                             static_cast<float>(sampleRate_ * 0.5));
        stepInc_ = rate / sampleRate_;
    }

    synth::FastRandom rng_{0x2545F4914F6CDD1DULL};
    synth::SVFStage   bandpass_;

    double sampleRate_ = 44100.0;
    NoiseSpectrum spectrum_ = NoiseSpectrum::White;
    float  tone_       = 0.5f;
    float  rateHz_     = 4000.0f;
    bool   pitchTrack_ = false;
    double noteHz_     = 440.0;

    float  pink_[7]   = {0.0f};
    float  brown_     = 0.0f;
    float  lastWhite_ = 0.0f;
    float  lastPink_  = 0.0f;
    float  stepValue_ = 0.0f;
    double stepPhase_ = 0.0;
    double stepInc_   = 0.1;
};

} // namespace r50
