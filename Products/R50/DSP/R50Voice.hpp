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

#include "R50Modulation.hpp"
#include "R50Partial.hpp"
#include "Utils.hpp"

namespace r50 {

static constexpr int kPartialsPerVoice = 2;

/// Must match kControlBlock in R50Engine.hpp. Declared here because the voice
/// needs it to run its LFOs at the block rate and the engine includes the voice
/// rather than the other way round.
static constexpr int kControlBlockForLfo = 32;

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
    ModParams     modulation;

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
        for (ModLfo &lfo : lfos_) lfo.setBlockRate(sr / kControlBlockForLfo);
    }

    void setSeed(uint64_t seed) {
        randomSource_ = synth::FastRandom(seed ^ 0x5DEECE66DULL);
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

    void noteOn(int note, float velocity, const VoiceParams &p,
                float sharedLfoPhase, float modWheel, float aftertouch) {
        note_     = note;
        velocity_ = velocity;
        held_     = true;
        active_   = true;
        blendPosition_ = 0.0;

        // One random value per note, fixed for its lifetime — a source that
        // changed under a held note would be a slow noise generator, not the
        // per-note variation this is for.
        random_ = randomSource_.nextBipolar();

        for (int i = 0; i < kLfoCount; ++i) {
            lfos_[i].noteOn(p.modulation.lfo[i], sharedLfoPhase);
        }
        gatherSources(p, modWheel, aftertouch);
        computeStructureWeights(p);

        for (int i = 0; i < kPartialsPerVoice; ++i) {
            const ModulationBlock mod =
                evaluateMatrix(p.modulation, sources_, i);
            // Sample start is chosen when the note begins, so modulating it is
            // a note-on decision rather than a running one.
            partials_[i].noteOn(note, velocity, p.partial[i], mod.sampleStart);
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

    void updateBlock(const VoiceParams &p, double pitchBendSemitones,
                     float modWheel, float aftertouch) {
        structure_ = p.structure;
        blendRate_ = (p.blendTime > 0.0001f)
                   ? 1.0 / (p.blendTime * sampleRate_) : 1.0;
        computeStructureWeights(p);

        // Advance the LFOs once per block whether or not anything is routed —
        // a free-running LFO that stalls when unrouted would jump when a slot
        // is assigned mid-note.
        for (int i = 0; i < kLfoCount; ++i) {
            sources_.lfo[i] = lfos_[i].process(p.modulation.lfo[i]);
        }
        gatherSources(p, modWheel, aftertouch);

        ringLevel_ = p.ringLevel;
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            sources_.ampEnv    = partials_[i].ampLevel();
            sources_.filterEnv = partials_[i].filterEnvLevel();
            sources_.pitchEnv  = partials_[i].pitchEnvLevel();
            const ModulationBlock mod =
                evaluateMatrix(p.modulation, sources_, i);
            ringLevel_ += mod.ringLevel;
            partials_[i].updateBlock(p.partial[i], pitchBendSemitones, mod);
        }
        ringLevel_ = synth::clampf(ringLevel_, 0.0f, 8.0f);
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
    /// Voice-wide source values. Envelope levels are filled in per Partial by
    /// the caller, since each Partial has its own.
    void gatherSources(const VoiceParams &p, float modWheel, float aftertouch) {
        sources_.velocity   = velocity_;
        sources_.keyTrack   = synth::clampf((note_ - 60) / 60.0f, -1.0f, 1.0f);
        sources_.modWheel   = modWheel;
        sources_.aftertouch = aftertouch;
        sources_.random     = random_;
        for (int i = 0; i < kMacroCount; ++i) {
            sources_.macros[i] = p.modulation.macros[i];
        }
    }

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
    ModLfo  lfos_[kLfoCount];
    ModSourceValues sources_;
    synth::FastRandom randomSource_{0x853C49E6748FEA9BULL};
    float   random_ = 0.0f;

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
