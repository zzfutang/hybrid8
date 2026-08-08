//
//  R50Waveshaper.hpp
//  Per-Partial waveshaping, before or after the filter.
//
//  Position matters more than it looks. Shaping before the filter lets the
//  filter tame the harmonics that shaping created; shaping after it puts them
//  back on top of a filtered signal, which is a coarser and brighter sound.
//  Both are useful, so it is a control rather than a fixed choice.
//
//  Gain compensation keeps each type roughly level-matched with Off, so
//  auditioning types is a comparison of character rather than of loudness.
//
//  The stage runs at 4x through two cascaded high-quality 129-tap half-band
//  stages. Folding and rectification create harmonics far above anything the
//  source contained, so both the old 2x rate and short decimator's ~-45 dB
//  rejection were insufficient for the production effects architecture.
//

#pragma once

#include <cmath>

#include "R50Oversampling.hpp"
#include "Utils.hpp"

namespace r50 {

enum class ShaperType {
    Off = 0,
    SoftClip,
    HardClip,
    Fold,
    Rectify
};

static constexpr int kShaperTypeCount = 5;

enum class ShaperPosition { PreFilter = 0, PostFilter };

class Waveshaper {
public:
    void setParams(ShaperType type, float drive, ShaperPosition position) {
        type_ = type;
        position_ = position;
        // Exponential taper: the useful range of a shaper is bunched at the
        // bottom.
        //
        // A Partial reaches this stage at roughly half full scale, so the
        // folder needs enough gain to fold that several times over — a range
        // sized for a unit-amplitude signal barely folds a real one at all.
        const float amount = synth::clampf(drive, 0.0f, 1.0f);
        const float depth = (type == ShaperType::Fold) ? 18.0f : 24.0f;
        gain_ = 1.0f + amount * amount * depth;

        // No makeup. Every type here is bounded to +/-1 by construction, so
        // driving one harder redistributes energy rather than adding level.
        // The filter's drive compensation, which this was copied from, exists
        // because a tanh stage genuinely does get louder; applying it to a
        // wavefolder just made folding quieter the harder it folded.
        makeup_ = 1.0f;
    }

    ShaperType type() const { return type_; }
    ShaperPosition position() const { return position_; }
    bool isActive() const { return type_ != ShaperType::Off; }

    void reset() {
        oversampler_.reset();
        std::fill(dryDelay_, dryDelay_ + Oversampler4x::latencySamples(), 0.0f);
        dryPosition_ = 0;
    }

    /// Shape one sample at 4x. The inexpensive Off path has the same fixed
    /// latency as the active path, keeping Partials aligned when only some use
    /// shaping and avoiding a timing jump when the type changes.
    inline float process(float input) {
        const float delayedDry = dryDelay_[dryPosition_];
        dryDelay_[dryPosition_] = input;
        if (++dryPosition_ == Oversampler4x::latencySamples()) dryPosition_ = 0;

        if (type_ == ShaperType::Off) return delayedDry;

        return oversampler_.process(input, [this](float sample) {
            return shapeOne(sample);
        });
    }

private:
    inline float shapeOne(float input) const {
        const float driven = input * gain_;
        float shaped = 0.0f;
        switch (type_) {
            case ShaperType::Off:
                return input;

            case ShaperType::SoftClip:
                shaped = synth::softClip(driven);
                break;

            case ShaperType::HardClip:
                shaped = synth::clampf(driven, -1.0f, 1.0f);
                break;

            case ShaperType::Fold:
                shaped = fold(driven);
                break;

            case ShaperType::Rectify:
                // Full-wave rectification doubles the fundamental and removes
                // the DC the naive form would leave behind.
                shaped = 2.0f * std::fabs(synth::softClip(driven)) - 1.0f;
                break;
        }
        return shaped * makeup_;
    }

    /// Triangle wavefolder: reflect back at ±1 as many times as needed. Bounded
    /// by construction, so a large drive folds rather than clipping.
    static inline float fold(float x) {
        if (x >= -1.0f && x <= 1.0f) return x;
        float folded = std::fmod(x + 1.0f, 4.0f);
        if (folded < 0.0f) folded += 4.0f;
        folded -= 1.0f;                       // now in [-1, 3)
        if (folded > 1.0f) folded = 2.0f - folded;
        return folded;
    }

    ShaperType     type_     = ShaperType::Off;
    ShaperPosition position_ = ShaperPosition::PreFilter;
    float gain_   = 1.0f;
    float makeup_ = 1.0f;
    Oversampler4x oversampler_;
    float dryDelay_[Oversampler4x::latencySamples()] = {};
    int dryPosition_ = 0;
};

} // namespace r50
