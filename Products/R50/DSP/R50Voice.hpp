//
//  R50Voice.hpp
//  One voice: four Partials form two Tones, then a Patch structure combines
//  those Tones into stereo.
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

static constexpr int kTonesPerVoice = 2;
static constexpr int kPartialsPerTone = 2;
static constexpr int kPartialsPerVoice = kTonesPerVoice * kPartialsPerTone;

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

enum class PatchStructure {
    Layer = 0,
    KeySplit,
    VelocitySplit,
    VelocityCrossfade,
    VectorMix
};

static constexpr int kPatchStructureCount = 5;

struct ToneParams {
    ToneStructure structure     = ToneStructure::Mix;
    float         ringLevel     = 1.0f;
    float         ringPan       = 0.0f;
    float         blendTime     = 0.25f;
    int           crossfadeLow  = 48;
    int           crossfadeHigh = 72;
};

struct VoicePartialOutput {
    float l = 0.0f;
    float r = 0.0f;
};

struct VoiceOutput {
    VoicePartialOutput partial[kPartialsPerVoice];
    VoicePartialOutput ring[kTonesPerVoice];
};

struct VoiceParams {
    PartialParams partial[kPartialsPerVoice];
    ModParams     modulation;

    // Tone A keeps the original members so existing tests and callers retain
    // source compatibility. Tone B is the appended second structure.
    ToneStructure structure     = ToneStructure::Mix;
    float         ringLevel     = 1.0f;
    float         ringPan       = 0.0f;
    float         blendTime     = 0.25f;   // AttackSustain handover, seconds
    int           crossfadeLow  = 48;
    int           crossfadeHigh = 72;

    ToneParams toneB;
    PatchStructure patchStructure = PatchStructure::Layer;
    int   patchSplitPoint = 60;
    float patchVelocitySplit = 0.5f;
    float patchVectorMix = 0.0f;
    float toneLevel[kTonesPerVoice] = {1.0f, 1.0f};
    LfoParams vectorLfo;
    float vectorLfoDepth = 0.0f;
};

class Voice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        for (Partial &partial : partials_) partial.setSampleRate(sr);
        for (ModLfo &lfo : lfos_) lfo.setBlockRate(sr / kControlBlockForLfo);
        vectorLfo_.setBlockRate(sr / kControlBlockForLfo);
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
                float sharedLfoPhase, float modWheel, float aftertouch,
                float sharedVectorPhase = 0.0f) {
        note_     = note;
        velocity_ = velocity;
        held_     = true;
        active_   = true;
        blendPosition_[0] = blendPosition_[1] = 0.0;
        structure_[0] = p.structure;
        structure_[1] = p.toneB.structure;
        ringLevel_[0] = p.ringLevel;
        ringLevel_[1] = p.toneB.ringLevel;
        ringPan_[0] = p.ringPan;
        ringPan_[1] = p.toneB.ringPan;
        blendRate_[0] = (p.blendTime > 0.0001f)
                      ? 1.0 / (p.blendTime * sampleRate_) : 1.0;
        blendRate_[1] = (p.toneB.blendTime > 0.0001f)
                      ? 1.0 / (p.toneB.blendTime * sampleRate_) : 1.0;

        // One random value per note, fixed for its lifetime — a source that
        // changed under a held note would be a slow noise generator, not the
        // per-note variation this is for.
        random_ = randomSource_.nextBipolar();

        for (int i = 0; i < kLfoCount; ++i) {
            lfos_[i].noteOn(p.modulation.lfo[i], sharedLfoPhase);
        }
        vectorLfo_.noteOn(p.vectorLfo, sharedVectorPhase);
        gatherSources(p, modWheel, aftertouch);
        computeStructureWeights(p);
        computePatchWeights(p, 0.0f);

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
        structure_[0] = p.structure;
        structure_[1] = p.toneB.structure;
        ringPan_[0] = p.ringPan;
        ringPan_[1] = p.toneB.ringPan;
        blendRate_[0] = (p.blendTime > 0.0001f)
                      ? 1.0 / (p.blendTime * sampleRate_) : 1.0;
        blendRate_[1] = (p.toneB.blendTime > 0.0001f)
                      ? 1.0 / (p.toneB.blendTime * sampleRate_) : 1.0;
        computeStructureWeights(p);

        // Advance the LFOs once per block whether or not anything is routed —
        // a free-running LFO that stalls when unrouted would jump when a slot
        // is assigned mid-note.
        for (int i = 0; i < kLfoCount; ++i) {
            sources_.lfo[i] = lfos_[i].process(p.modulation.lfo[i]);
        }
        gatherSources(p, modWheel, aftertouch);
        const float vectorMod = evaluatePatchVector(p.modulation, sources_)
                              + vectorLfo_.process(p.vectorLfo)
                                * p.vectorLfoDepth;
        computePatchWeights(p, vectorMod);

        ringLevel_[0] = p.ringLevel;
        ringLevel_[1] = p.toneB.ringLevel;
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            sources_.ampEnv    = partials_[i].ampLevel();
            sources_.filterEnv = partials_[i].filterEnvLevel();
            sources_.pitchEnv  = partials_[i].pitchEnvLevel();
            const ModulationBlock mod =
                evaluateMatrix(p.modulation, sources_, i);
            ringLevel_[i / kPartialsPerTone] += mod.ringLevel;
            partials_[i].updateBlock(p.partial[i], pitchBendSemitones, mod);
        }
        for (float &level : ringLevel_)
            level = synth::clampf(level, 0.0f, 8.0f);
    }

    /// One stereo sample.
    inline void process(float &outL, float &outR) {
        VoiceOutput output;
        processPartials(output);
        outL = outR = 0.0f;
        for (const auto &partial : output.partial) {
            outL += partial.l;
            outR += partial.r;
        }
        for (const auto &ring : output.ring) {
            outL += ring.l;
            outR += ring.r;
        }
    }

    /// Render the two Partial contributions separately so the engine can
    /// accumulate independent dry and global-send buses without allocating.
    inline void processPartials(VoiceOutput &output) {
        output = {};
        if (!active_) return;

        float signal[kPartialsPerVoice];
        for (int i = 0; i < kPartialsPerVoice; ++i)
            signal[i] = partials_[i].process();

        bool anyActive = false;
        for (const Partial &partial : partials_) anyActive |= partial.isActive();
        if (!anyActive) {
            active_ = false;
            return;
        }

        for (int tone = 0; tone < kTonesPerVoice; ++tone) {
            const int ia = tone * kPartialsPerTone;
            const int ib = ia + 1;
            float weightA = weightA_[tone] * partials_[ia].level();
            float weightB = weightB_[tone] * partials_[ib].level();
            float extra = 0.0f;

            switch (structure_[tone]) {
                case ToneStructure::Mix:
                case ToneStructure::VelocityCrossfade:
                case ToneStructure::KeyCrossfade:
                    break;
                case ToneStructure::RingMod:
                    extra = signal[ia] * signal[ib] * ringLevel_[tone];
                    break;
                case ToneStructure::AttackSustain: {
                    const float x = static_cast<float>(blendPosition_[tone]);
                    weightA = (1.0f - x) * partials_[ia].level();
                    weightB = x * partials_[ib].level();
                    if (blendPosition_[tone] < 1.0) {
                        blendPosition_[tone] += blendRate_[tone];
                        if (blendPosition_[tone] > 1.0) blendPosition_[tone] = 1.0;
                    }
                    break;
                }
            }

            const float toneGain = toneWeight_[tone];
            output.partial[ia].l =
                signal[ia] * weightA * partials_[ia].panLeft() * toneGain;
            output.partial[ia].r =
                signal[ia] * weightA * partials_[ia].panRight() * toneGain;
            output.partial[ib].l =
                signal[ib] * weightB * partials_[ib].panLeft() * toneGain;
            output.partial[ib].r =
                signal[ib] * weightB * partials_[ib].panRight() * toneGain;
            const float ringAngle = (ringPan_[tone] + 1.0f) * 0.25f
                                  * static_cast<float>(synth::kPi);
            output.ring[tone].l = extra * std::cos(ringAngle) * toneGain;
            output.ring[tone].r = extra * std::sin(ringAngle) * toneGain;
        }
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
        const ToneParams tones[kTonesPerVoice] = {
            {p.structure, p.ringLevel, p.ringPan, p.blendTime,
             p.crossfadeLow, p.crossfadeHigh},
            p.toneB
        };
        for (int tone = 0; tone < kTonesPerVoice; ++tone) {
        float position = -1.0f;
        if (tones[tone].structure == ToneStructure::VelocityCrossfade) {
            position = synth::clampf(velocity_, 0.0f, 1.0f);
        } else if (tones[tone].structure == ToneStructure::KeyCrossfade) {
            const float low = static_cast<float>(tones[tone].crossfadeLow);
            const float high = static_cast<float>(tones[tone].crossfadeHigh);
            position = (high > low)
                ? synth::clampf((note_ - low) / (high - low), 0.0f, 1.0f)
                : 0.0f;
        }

        if (position < 0.0f) {          // Mix, RingMod, AttackSustain
            weightA_[tone] = 1.0f;
            weightB_[tone] = 1.0f;
            continue;
        }
        const float angle = position * 0.5f * static_cast<float>(synth::kPi);
        weightA_[tone] = std::cos(angle);
        weightB_[tone] = std::sin(angle);
        }
    }

    void computePatchWeights(const VoiceParams &p, float vectorMod) {
        float a = p.toneLevel[0], b = p.toneLevel[1];
        switch (p.patchStructure) {
            case PatchStructure::Layer:
                break;
            case PatchStructure::KeySplit:
                if (note_ < p.patchSplitPoint) b = 0.0f; else a = 0.0f;
                break;
            case PatchStructure::VelocitySplit:
                if (velocity_ < p.patchVelocitySplit) b = 0.0f; else a = 0.0f;
                break;
            case PatchStructure::VelocityCrossfade: {
                const float angle = velocity_ * 0.5f
                                  * static_cast<float>(synth::kPi);
                a *= std::cos(angle); b *= std::sin(angle);
                break;
            }
            case PatchStructure::VectorMix: {
                const float angle =
                    synth::clampf(p.patchVectorMix + vectorMod, 0.0f, 1.0f)
                                  * 0.5f * static_cast<float>(synth::kPi);
                a *= std::cos(angle); b *= std::sin(angle);
                break;
            }
        }
        toneWeight_[0] = a;
        toneWeight_[1] = b;
    }

    Partial partials_[kPartialsPerVoice];
    ModLfo  lfos_[kLfoCount];
    ModLfo  vectorLfo_;
    ModSourceValues sources_;
    synth::FastRandom randomSource_{0x853C49E6748FEA9BULL};
    float   random_ = 0.0f;

    double sampleRate_ = 44100.0;
    int    note_       = -1;
    float  velocity_   = 1.0f;
    bool   held_       = false;
    bool   active_     = false;

    ToneStructure structure_[kTonesPerVoice] = {
        ToneStructure::Mix, ToneStructure::Mix
    };
    float  ringLevel_[kTonesPerVoice] = {1.0f, 1.0f};
    float  ringPan_[kTonesPerVoice] = {0.0f, 0.0f};
    float  weightA_[kTonesPerVoice] = {1.0f, 1.0f};
    float  weightB_[kTonesPerVoice] = {1.0f, 1.0f};
    float  toneWeight_[kTonesPerVoice] = {1.0f, 0.0f};
    double blendPosition_[kTonesPerVoice] = {0.0, 0.0};
    double blendRate_[kTonesPerVoice] = {1.0, 1.0};
};

} // namespace r50
