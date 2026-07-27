//
//  R50Modulation.hpp
//  Modulation sources, destinations and the matrix that connects them.
//
//  Everything here is evaluated once per control block, per voice, per Partial.
//  Per Partial matters: routing an amp envelope to cutoff on both Partials
//  should give each Partial its *own* envelope shaping its *own* filter, not
//  one Partial's envelope driving both.
//
//  Amounts are bipolar and each destination declares what full scale means, so
//  a single amount control reads sensibly whether it is bending pitch by an
//  octave or nudging a pan.
//

#pragma once

#include <cstdint>

#include "LFO.hpp"
#include "Utils.hpp"

namespace r50 {

enum class ModSource {
    None = 0,
    Lfo1, Lfo2,
    AmpEnv, FilterEnv, PitchEnv,
    Velocity, KeyTrack, ModWheel, Aftertouch, Random,
    Macro1, Macro2, Macro3, Macro4
};
static constexpr int kModSourceCount = 15;

enum class ModDestination {
    None = 0,
    Pitch, Cutoff, Resonance, Level, Pan,
    WaveIndex, PulseWidth, NoiseMix, SampleStart, ShaperDrive, RingLevel,
    VectorMix
};
static constexpr int kModDestinationCount = 13;

/// Which Partial a slot acts on. With two Partials, forcing a slot per Partial
/// for the common "both" case would waste half the matrix.
enum class ModTarget { Both = 0, Partial1, Partial2 };
static constexpr int kModTargetCount = 3;

static constexpr int kModSlots = 6;
static constexpr int kMacroCount = 4;
static constexpr int kLfoCount = 2;

struct ModSlot {
    ModSource      source      = ModSource::None;
    ModDestination destination = ModDestination::None;
    ModTarget      target      = ModTarget::Both;
    float          amount      = 0.0f;      // -1 .. +1
};

struct LfoParams {
    synth::LFOWave wave      = synth::LFOWave::Sine;
    float rateHz             = 5.0f;
    float delaySeconds       = 0.0f;
    float fadeSeconds        = 0.0f;
    bool  retrigger          = true;
    float phase              = 0.0f;
};

struct ModParams {
    ModSlot   slots[kModSlots];
    LfoParams lfo[kLfoCount];
    float     macros[kMacroCount] = {0.0f, 0.0f, 0.0f, 0.0f};

    /// True when nothing is routed, which lets the voice skip the whole stage
    /// and stay bit-identical to an engine without a matrix.
    bool anyRouted() const {
        for (const ModSlot &slot : slots) {
            if (slot.source != ModSource::None
             && slot.destination != ModDestination::None
             && slot.amount != 0.0f) {
                return true;
            }
        }
        return false;
    }
};

/// Summed modulation for one Partial, in each destination's own units.
struct ModulationBlock {
    float pitchSemitones = 0.0f;   // +/- 12 at full scale
    float cutoffOctaves  = 0.0f;   // +/- 4
    float resonance      = 0.0f;   // +/- 1
    float level          = 0.0f;   // +/- 1
    float pan            = 0.0f;   // +/- 1
    float waveIndex      = 0.0f;   // +/- the whole wave list
    float pulseWidth     = 0.0f;   // +/- 0.5
    float noiseMix       = 0.0f;   // +/- 1
    float sampleStart    = 0.0f;   // +/- 1
    float shaperDrive    = 0.0f;   // +/- 1
    float ringLevel      = 0.0f;   // +/- 8, applied at the Tone
    float vectorMix      = 0.0f;   // +/- 1, applied at the Patch

    bool active = false;
};

/// The value every source currently has, gathered once per voice per block.
/// Envelope levels are per Partial, so they are passed separately.
struct ModSourceValues {
    float lfo[kLfoCount] = {0.0f, 0.0f};
    float velocity  = 0.0f;
    float keyTrack  = 0.0f;
    float modWheel  = 0.0f;
    float aftertouch = 0.0f;
    float random    = 0.0f;
    float macros[kMacroCount] = {0.0f, 0.0f, 0.0f, 0.0f};

    float ampEnv    = 0.0f;
    float filterEnv = 0.0f;
    float pitchEnv  = 0.0f;

    float value(ModSource source) const {
        switch (source) {
            case ModSource::Lfo1:       return lfo[0];
            case ModSource::Lfo2:       return lfo[1];
            case ModSource::AmpEnv:     return ampEnv;
            case ModSource::FilterEnv:  return filterEnv;
            case ModSource::PitchEnv:   return pitchEnv;
            case ModSource::Velocity:   return velocity;
            case ModSource::KeyTrack:   return keyTrack;
            case ModSource::ModWheel:   return modWheel;
            case ModSource::Aftertouch: return aftertouch;
            case ModSource::Random:     return random;
            case ModSource::Macro1:     return macros[0];
            case ModSource::Macro2:     return macros[1];
            case ModSource::Macro3:     return macros[2];
            case ModSource::Macro4:     return macros[3];
            case ModSource::None:       break;
        }
        return 0.0f;
    }
};

/// Sum every slot that applies to `partialIndex` into a modulation block.
/// Full-scale amounts per destination are declared here and nowhere else.
inline ModulationBlock evaluateMatrix(const ModParams &params,
                                      const ModSourceValues &sources,
                                      int partialIndex) {
    ModulationBlock block;
    const int withinTone = partialIndex % 2;
    for (const ModSlot &slot : params.slots) {
        if (slot.source == ModSource::None
         || slot.destination == ModDestination::None
         || slot.amount == 0.0f) {
            continue;
        }
        if (slot.target == ModTarget::Partial1 && withinTone != 0) continue;
        if (slot.target == ModTarget::Partial2 && withinTone != 1) continue;

        const float amount = slot.amount * sources.value(slot.source);
        block.active = true;

        switch (slot.destination) {
            case ModDestination::Pitch:       block.pitchSemitones += amount * 12.0f; break;
            case ModDestination::Cutoff:      block.cutoffOctaves  += amount * 4.0f;  break;
            case ModDestination::Resonance:   block.resonance      += amount;         break;
            case ModDestination::Level:       block.level          += amount;         break;
            case ModDestination::Pan:         block.pan            += amount;         break;
            case ModDestination::WaveIndex:   block.waveIndex      += amount * 10.0f; break;
            case ModDestination::PulseWidth:  block.pulseWidth     += amount * 0.5f;  break;
            case ModDestination::NoiseMix:    block.noiseMix       += amount;         break;
            case ModDestination::SampleStart: block.sampleStart    += amount;         break;
            case ModDestination::ShaperDrive: block.shaperDrive    += amount;         break;
            case ModDestination::RingLevel:   block.ringLevel      += amount * 8.0f;  break;
            case ModDestination::VectorMix:   block.vectorMix      += amount;         break;
            case ModDestination::None:        break;
        }
    }
    return block;
}

/// Patch destinations are evaluated once per voice, above both Tones. The
/// Partial target column is intentionally irrelevant for these routes.
inline float evaluatePatchVector(const ModParams &params,
                                 const ModSourceValues &sources) {
    float value = 0.0f;
    for (const ModSlot &slot : params.slots) {
        if (slot.destination != ModDestination::VectorMix
            || slot.source == ModSource::None || slot.amount == 0.0f) {
            continue;
        }
        value += slot.amount * sources.value(slot.source);
    }
    return value;
}

/// An LFO plus the delay and fade-in that make it usable musically. Neither is
/// in Shared/DSPCore's LFO, and both are what let a vibrato arrive after the
/// note rather than on top of it.
class ModLfo {
public:
    /// Advanced once per control block, so it runs at the block rate rather
    /// than the sample rate. At LFO frequencies per-sample resolution buys
    /// nothing and costs a call per sample per voice.
    void setBlockRate(double blocksPerSecond) {
        lfo_.setSampleRate(blocksPerSecond);
        blockRate_ = blocksPerSecond;
    }

    void noteOn(const LfoParams &params, float sharedPhase) {
        elapsed_ = 0.0f;
        lfo_.reset();
        lfo_.setPhase(params.retrigger ? params.phase
                                       : params.phase + sharedPhase);
    }

    /// One block. Returns the bipolar value after delay and fade.
    inline float process(const LfoParams &params) {
        lfo_.setWave(params.wave);
        lfo_.setRate(params.rateHz);
        const float raw = lfo_.process();

        elapsed_ += static_cast<float>(1.0 / blockRate_);
        if (elapsed_ < params.delaySeconds) return 0.0f;
        if (params.fadeSeconds <= 0.0001f) return raw;

        const float faded = (elapsed_ - params.delaySeconds) / params.fadeSeconds;
        return raw * synth::clampf(faded, 0.0f, 1.0f);
    }

private:
    synth::LFO lfo_;
    double blockRate_ = 1378.125;   // 44100 / 32
    float  elapsed_   = 0.0f;
};

} // namespace r50
