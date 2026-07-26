//
//  R50Voice.hpp
//  One voice: two Partials combined by a Tone structure, producing stereo.
//
//  The structure is what makes two Partials a Tone rather than two sounds
//  playing at once. AttackSustain in particular — a sampled transient handing
//  over to a sustaining source — is the structure the whole instrument is
//  built around.
//

#pragma once

#include <cmath>

#include "R50Partial.hpp"
#include "Utils.hpp"

namespace r50 {

static constexpr int kPartialsPerVoice = 2;

enum class ToneStructure {
    Mix = 0,
    RingMod,
    AttackSustain,
    VelocityCrossfade,
    KeyCrossfade
};

static constexpr int kToneStructureCount = 5;

struct VoiceParams {
    PartialParams partial[kPartialsPerVoice];

    ToneStructure structure     = ToneStructure::Mix;
    float         ringLevel     = 1.0f;
    float         blendTime     = 0.25f;   // AttackSustain handover, seconds
    int           crossfadeLow  = 48;
    int           crossfadeHigh = 72;
};

class Voice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        for (Partial &partial : partials_) partial.setSampleRate(sr);
    }

    void setSeed(uint64_t seed) {
        // Distinct per Partial as well as per voice, so two Partials both using
        // noise do not produce the same stream and sum to 6 dB of one source.
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            partials_[i].setSeed(seed + 0x9E3779B9ULL * (i + 1));
        }
    }

    void reset() {
        for (Partial &partial : partials_) partial.reset();
        active_ = false;
        note_ = -1;
    }

    void noteOn(int note, float velocity, const VoiceParams &p) {
        note_     = note;
        velocity_ = velocity;
        held_     = true;
        active_   = true;
        blendPosition_ = 0.0;
        computeStructureWeights(p);
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            partials_[i].noteOn(note, velocity, p.partial[i]);
        }
    }

    void noteOff() {
        held_ = false;
        for (Partial &partial : partials_) partial.noteOff();
    }

    bool isActive() const { return active_; }
    bool isHeld() const { return held_; }
    int  note() const { return note_; }

    /// Rough "how disposable is this voice" score for stealing (lower = safer).
    float releaseProgress() const {
        if (held_) return 1.0f;
        float loudest = 0.0f;
        for (const Partial &partial : partials_) {
            loudest = std::max(loudest, partial.ampLevel());
        }
        return loudest;
    }

    void updateBlock(const VoiceParams &p, double pitchBendSemitones) {
        structure_ = p.structure;
        ringLevel_ = p.ringLevel;
        blendRate_ = (p.blendTime > 0.0001f)
                   ? 1.0 / (p.blendTime * sampleRate_) : 1.0;
        computeStructureWeights(p);
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            partials_[i].updateBlock(p.partial[i], pitchBendSemitones);
        }
    }

    /// One stereo sample.
    inline void process(float &outL, float &outR) {
        outL = 0.0f;
        outR = 0.0f;
        if (!active_) return;

        const float a = partials_[0].process();
        const float b = partials_[1].process();

        // A voice lives as long as either Partial is still sounding.
        if (!partials_[0].isActive() && !partials_[1].isActive()) {
            active_ = false;
            return;
        }

        // Structure weight times the Partial's own level. Kept separate from
        // the ring product below, which deliberately uses the unscaled
        // signals so level behaves as a dry amount.
        float weightA = weightA_ * partials_[0].level();
        float weightB = weightB_ * partials_[1].level();
        float extra = 0.0f;

        switch (structure_) {
            case ToneStructure::Mix:
            case ToneStructure::VelocityCrossfade:
            case ToneStructure::KeyCrossfade:
                break;   // weights already resolved per block

            case ToneStructure::RingMod:
                // Bandwidth doubles here, so the product aliases against
                // Nyquist without oversampling both Partials. Measured rather
                // than assumed — see the ring-mod test.
                extra = a * b * ringLevel_;
                break;

            case ToneStructure::AttackSustain: {
                // Explicit timed handover: the transient Partial gives way to
                // the sustaining one over blendTime.
                const float x = static_cast<float>(blendPosition_);
                weightA = (1.0f - x) * partials_[0].level();
                weightB = x * partials_[1].level();
                if (blendPosition_ < 1.0) {
                    blendPosition_ += blendRate_;
                    if (blendPosition_ > 1.0) blendPosition_ = 1.0;
                }
                break;
            }
        }

        const float left = a * weightA * partials_[0].panLeft()
                         + b * weightB * partials_[1].panLeft()
                         + extra * 0.7071f;
        const float right = a * weightA * partials_[0].panRight()
                          + b * weightB * partials_[1].panRight()
                          + extra * 0.7071f;
        outL = left;
        outR = right;
    }

private:
    /// Equal-power crossfade weights, resolved once per block rather than per
    /// sample: velocity is fixed for the note and key position cannot change.
    void computeStructureWeights(const VoiceParams &p) {
        float position = -1.0f;
        if (p.structure == ToneStructure::VelocityCrossfade) {
            position = synth::clampf(velocity_, 0.0f, 1.0f);
        } else if (p.structure == ToneStructure::KeyCrossfade) {
            const float low = static_cast<float>(p.crossfadeLow);
            const float high = static_cast<float>(p.crossfadeHigh);
            position = (high > low)
                ? synth::clampf((note_ - low) / (high - low), 0.0f, 1.0f)
                : 0.0f;
        }

        if (position < 0.0f) {          // Mix, RingMod, AttackSustain
            weightA_ = 1.0f;
            weightB_ = 1.0f;
            return;
        }
        const float angle = position * 0.5f * static_cast<float>(synth::kPi);
        weightA_ = std::cos(angle);
        weightB_ = std::sin(angle);
    }

    Partial partials_[kPartialsPerVoice];

    double sampleRate_ = 44100.0;
    int    note_       = -1;
    float  velocity_   = 1.0f;
    bool   held_       = false;
    bool   active_     = false;

    ToneStructure structure_ = ToneStructure::Mix;
    float  ringLevel_     = 1.0f;
    float  weightA_       = 1.0f;
    float  weightB_       = 1.0f;
    double blendPosition_ = 0.0;
    double blendRate_     = 1.0;
};

} // namespace r50
