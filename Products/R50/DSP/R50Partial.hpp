//
//  R50Partial.hpp
//  One Partial: a source (wave table, sample or noise) through a filter and an
//  amplifier, with its own envelopes, level, pan and tuning.
//
//  This is the smallest complete sound-generating unit in the architecture.
//  Two Partials make a Tone; the Tone structure in R50Voice.hpp decides how
//  they combine. Everything here was previously the whole of Voice, which is
//  why the parameter block needed almost no change: a single-source voice was
//  already a Partial in all but name.
//
//  Filter coefficients are recomputed once per control block (kControlBlock in
//  R50Engine.hpp) rather than per sample — setParams() runs a tan() and is far
//  too costly to call for every Partial of every voice at audio rate.
//

#pragma once

#include "ADSR.hpp"
#include "Filter.hpp"
#include "R50Envelope.hpp"
#include "R50Modulation.hpp"
#include "R50Noise.hpp"
#include "R50SampleFactory.hpp"
#include "R50SamplePlayer.hpp"
#include "R50WaveOscillator.hpp"
#include "R50Waveshaper.hpp"
#include "Utils.hpp"

namespace r50 {

enum class SourceType { Wave = 0, Sample };

/// Denormalised parameter block for one Partial, rebuilt by the engine once per
/// control block. Plain data: the render thread copies it, never allocates it.
struct PartialParams {
    bool  enabled             = true;

    SourceType sourceType     = SourceType::Wave;
    int   sampleInstrument    = 0;      // index into SampleLibrary
    float sampleStart         = 0.0f;   // 0..1 scrub into the asset

    int   waveIndex           = 0;      // index into waveDescriptors()
    float pulseWidth          = 0.5f;   // only read by Difference-mode waves
    int   octave              = 0;
    int   semitone            = 0;
    float fineCents           = 0.0f;

    // Source mix: 0 = oscillator only, 1 = noise only.
    float         noiseMix      = 0.0f;
    NoiseSpectrum noiseSpectrum = NoiseSpectrum::White;
    float         noiseTone     = 0.5f;
    float         noiseRateHz   = 4000.0f;
    bool          noisePitchTrack = false;

    float cutoffHz            = 4000.0f;
    float resonance           = 0.2f;
    float drive               = 0.0f;
    float slope               = 0.0f;   // 0 = 12 dB, 1 = 24 dB
    float keyTrack            = 0.5f;
    float filterEnvAmount     = 0.4f;   // bipolar, +/- 4 octaves at full scale

    // Workstation EG: attack to a level, decay to a break point, slope to
    // sustain, release. A slope time at the minimum skips the break stage and
    // the envelope behaves exactly as a plain ADSR.
    float ampAttack = 0.005f, ampAttackLevel = 1.0f, ampDecay = 0.2f;
    float ampBreak = 1.0f, ampSlope = 0.0f, ampSustain = 0.8f, ampRelease = 0.3f;

    float filterAttack = 0.005f, filterAttackLevel = 1.0f, filterDecay = 0.4f;
    float filterBreak = 1.0f, filterSlope = 0.0f, filterSustain = 0.3f,
          filterRelease = 0.3f;

    // Pitch envelope: a rise and fall applied to this Partial's pitch.
    float pitchAmount = 0.0f;      // semitones, bipolar
    float pitchAttack = 0.001f;
    float pitchDecay  = 0.2f;

    ShaperType     shaperType     = ShaperType::Off;
    float          shaperDrive    = 0.0f;
    ShaperPosition shaperPosition = ShaperPosition::PreFilter;

    float level = 1.0f;
    float pan   = 0.0f;                 // -1 = left, +1 = right
};

class Partial {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        osc_.setSampleRate(sr);
        noise_.setSampleRate(sr);
        filter_.setSampleRate(sr);
        ampEnv_.setSampleRate(sr);
        filterEnv_.setSampleRate(sr);
        pitchEnv_.setSampleRate(sr);
    }

    void setSeed(uint64_t seed) { noise_.setSeed(seed); }

    void reset() {
        sample_.stop();
        noise_.reset();
        filter_.reset();
        shaper_.reset();
        ampEnv_.resetHard();
        filterEnv_.resetHard();
        pitchEnv_.resetHard();
        active_ = false;
    }

    void noteOn(int note, float velocity, const PartialParams &p,
                float startOffset = 0.0f) {
        note_     = note;
        velocity_ = velocity;
        active_   = p.enabled;
        if (!p.enabled) return;

        // A fresh note starts from a known phase so repeated notes are
        // identical — R50 is deliberately a tight, repeatable digital voice.
        osc_.reset(0.0f);

        // Resolve the region once, here: the scan is bounded but it has no
        // business running per sample, and the asset pointer must stay fixed
        // for the life of the note so a library publish cannot tear it.
        sample_.stop();
        if (p.sourceType == SourceType::Sample) {
            const Multisample *instrument =
                SampleLibrary::shared().instrument(p.sampleInstrument);
            const SampleRegion *region = instrument
                ? instrument->find(note, static_cast<int>(velocity * 127.0f + 0.5f))
                : nullptr;
            if (region != nullptr) {
                sample_.start(SampleLibrary::shared().sample(region->slot),
                              region,
                              synth::clampf(p.sampleStart + startOffset, 0.0f, 1.0f));
                sampleRootKey_   = region->rootKey;
                sampleTuneCents_ = region->tuneCents;
            }
        }

        applyEnvelopeTimes(p);
        ampEnv_.gate(true);
        filterEnv_.gate(true);
        pitchEnv_.gate(true);
    }

    void noteOff() {
        ampEnv_.gate(false);
        filterEnv_.gate(false);
        pitchEnv_.gate(false);
    }

    bool  isActive() const { return active_; }
    float ampLevel() const { return ampLevel_; }
    float filterEnvLevel() const { return filterLevel_; }
    float pitchEnvLevel() const { return pitchLevel_; }

    /// Per-control-block update: envelope times, pitch and filter coefficients.
    /// Modulation is applied here, on top of the snapshotted parameters, in the
    /// same place the fixed detune and cutoff offsets are already combined.
    void updateBlock(const PartialParams &p, double pitchBendSemitones,
                     const ModulationBlock &mod) {
        if (!p.enabled) { active_ = false; return; }
        applyEnvelopeTimes(p);

        const int waveIndex = static_cast<int>(
            std::lround(p.waveIndex + mod.waveIndex));
        osc_.setWave(waveIndex);
        osc_.setWidth(p.pulseWidth + mod.pulseWidth);
        noiseMix_ = synth::clampf(p.noiseMix + mod.noiseMix, 0.0f, 1.0f);
        level_    = synth::clampf(p.level + mod.level, 0.0f, 2.0f);
        // Equal-power pan, so a centred Partial is not louder than a panned one.
        const float panPosition = synth::clampf(p.pan + mod.pan, -1.0f, 1.0f);
        const float angle = (panPosition + 1.0f) * 0.25f
                          * static_cast<float>(synth::kPi);
        panLeft_  = std::cos(angle);
        panRight_ = std::sin(angle);

        shaper_.setParams(p.shaperType,
                          synth::clampf(p.shaperDrive + mod.shaperDrive, 0.0f, 1.0f),
                          p.shaperPosition);

        const double detune = p.octave * 12.0 + p.semitone
                            + p.fineCents / 100.0 + pitchBendSemitones
                            + p.pitchAmount * pitchLevel_
                            + mod.pitchSemitones;
        const double noteHz = synth::noteToHz(note_ + detune);
        osc_.setFrequency(noteHz);
        noise_.updateBlock(p.noiseSpectrum, p.noiseTone, p.noiseRateHz,
                           p.noisePitchTrack, noteHz);

        // Sample playback rate follows the same pitch as the oscillator, but
        // relative to the region's root key rather than to concert pitch.
        sourceType_ = p.sourceType;
        if (sample_.isActive()) {
            const double semitones = (note_ - sampleRootKey_) + detune
                                   + sampleTuneCents_ / 100.0;
            sample_.setPlaybackRatio(std::pow(2.0, semitones / 12.0), sampleRate_);
        }

        // Cutoff in octaves: base + key tracking + bipolar envelope.
        const double keyOctaves = p.keyTrack * (note_ - 60) / 12.0;
        const double envOctaves = p.filterEnvAmount * 4.0 * filterLevel_;
        double cutoff = p.cutoffHz
                      * std::pow(2.0, keyOctaves + envOctaves + mod.cutoffOctaves);
        cutoff = synth::clampf(static_cast<float>(cutoff), 20.0f,
                               static_cast<float>(sampleRate_ * 0.45));
        const float resonance = synth::clampf(p.resonance + mod.resonance, 0.0f, 1.0f);
        filter_.setParams(cutoff, resonance, p.slope, 0.0f, p.drive);
    }

    /// One sample, before level, pan and the Tone structure. Level is applied
    /// by the caller, not here: ring modulation multiplies two Partials, and if
    /// level were already folded in the product would be quadratic in it — so
    /// turning a Partial down would attenuate the ring far faster than the dry
    /// signal. Level is the dry amount, exactly as the architecture's
    /// dry1*p1 + dry2*p2 + ring*(p1*p2) implies.
    inline float process() {
        if (!active_) return 0.0f;

        ampLevel_    = ampEnv_.process();
        filterLevel_ = filterEnv_.process();
        pitchLevel_  = pitchEnv_.process();

        if (!ampEnv_.isActive()) {
            active_ = false;
            filter_.reset();
            return 0.0f;
        }

        // Crossfade source and noise before the filter, so the filter and its
        // envelope shape noise exactly as they shape the oscillator.
        const float tonal = (sourceType_ == SourceType::Sample)
                          ? sample_.process()
                          : osc_.process();
        const float noise = noise_.process();
        float source = tonal + (noise - tonal) * noiseMix_;

        if (shaper_.position() == ShaperPosition::PreFilter) {
            source = shaper_.process(source);
            return filter_.process(source) * ampLevel_ * velocity_;
        }
        return shaper_.process(filter_.process(source)) * ampLevel_ * velocity_;
    }

    float level() const { return level_; }
    float panLeft() const { return panLeft_; }
    float panRight() const { return panRight_; }

private:
    void applyEnvelopeTimes(const PartialParams &p) {
        ampEnv_.setAttack(p.ampAttack);
        ampEnv_.setAttackLevel(p.ampAttackLevel);
        ampEnv_.setDecay(p.ampDecay);
        ampEnv_.setBreakPoint(p.ampBreak);
        ampEnv_.setSlope(p.ampSlope);
        ampEnv_.setSustain(p.ampSustain);
        ampEnv_.setRelease(p.ampRelease);

        filterEnv_.setAttack(p.filterAttack);
        filterEnv_.setAttackLevel(p.filterAttackLevel);
        filterEnv_.setDecay(p.filterDecay);
        filterEnv_.setBreakPoint(p.filterBreak);
        filterEnv_.setSlope(p.filterSlope);
        filterEnv_.setSustain(p.filterSustain);
        filterEnv_.setRelease(p.filterRelease);

        // A pitch envelope wants exactly a rise then a fall to nothing, so the
        // seven-stage machinery would be wasted on it.
        pitchEnv_.setAttack(p.pitchAttack);
        pitchEnv_.setDecay(p.pitchDecay);
        pitchEnv_.setSustain(0.0f);
        pitchEnv_.setRelease(0.001f);
    }

    WaveOscillator      osc_;
    SamplePlayer        sample_;
    NoiseSource         noise_;
    synth::LadderFilter filter_;
    Waveshaper          shaper_;
    R50Envelope         ampEnv_;
    R50Envelope         filterEnv_;
    synth::ADSR         pitchEnv_;

    double sampleRate_  = 44100.0;
    int    note_        = -1;
    float  velocity_    = 1.0f;
    bool   active_      = false;
    SourceType sourceType_ = SourceType::Wave;
    int    sampleRootKey_   = 60;
    float  sampleTuneCents_ = 0.0f;
    float  noiseMix_    = 0.0f;
    float  level_       = 1.0f;
    float  panLeft_     = 0.7071f;
    float  panRight_    = 0.7071f;
    float  ampLevel_    = 0.0f;
    float  filterLevel_ = 0.0f;
    float  pitchLevel_  = 0.0f;
};

} // namespace r50
