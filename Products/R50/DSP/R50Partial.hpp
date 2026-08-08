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
#include "R50Envelope.hpp"
#include "R50DigitalFilter.hpp"
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

    /// How much of the key's distance from middle C reaches this Partial's
    /// pitch. 1 is normal tracking; 0 holds one pitch across the keyboard; 2
    /// gives two octaves per octave played.
    ///
    /// This is the D-50's answer to a single sample stretched over the whole
    /// keyboard, and the reason it is worth having: transposition is
    /// destructive, and below 1 the extremes are simply asked to travel less
    /// far. It applies to the wave oscillator as well as the sample, because
    /// the control belongs to the Partial's pitch rather than to one source —
    /// and a key-follow of 0 on a VA Partial is a fixed-pitch drone, which
    /// nothing else here can do.
    float pitchKeyFollow = 1.0f;

    // Pitch envelope, multi-stage: Start -> (attack) -> Amount -> (decay) ->
    // 0 in tune -> note-off -> (release) -> Release level. All levels are
    // semitones, bipolar. The historical pair Amount/Attack/Decay kept their
    // meaning when Start and the release stage were added: with Start at 0
    // the old rise-and-fall is exactly what plays.
    float pitchStartLevel = 0.0f;
    float pitchAmount = 0.0f;      // the attack stage's target level
    float pitchAttack = 0.001f;
    float pitchDecay  = 0.2f;
    float pitchRelease = 0.001f;
    float pitchReleaseLevel = 0.0f;

    ShaperType     shaperType     = ShaperType::Off;
    float          shaperDrive    = 0.0f;
    ShaperPosition shaperPosition = ShaperPosition::PreFilter;

    float level = 1.0f;
    float pan   = 0.0f;                 // -1 = left, +1 = right
    float dryLevel = 1.0f;
    float send[3] = {0.0f, 0.0f, 0.0f};
};

/// The multi-stage pitch envelope, shaped like the classic workstation one:
/// pitch starts at Start, travels to the Attack level, settles to zero — in
/// tune — for the held note, and drifts to the Release level after note-off.
/// Levels are semitones and bipolar throughout; the envelope's output IS the
/// pitch offset, there is no separate amount to scale it by.
class PitchEnvelope {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }

    void configure(float startLevel, float attackTime, float attackLevel,
                   float decayTime, float releaseTime, float releaseLevel) {
        start_       = startLevel;
        attackTime_  = attackTime;
        attackLevel_ = attackLevel;
        decayTime_   = decayTime;
        releaseTime_ = releaseTime;
        releaseLevel_ = releaseLevel;
    }

    void resetHard() {
        stage_ = Stage::Idle;
        current_ = 0.0f;
        remaining_ = 0;
        delta_ = 0.0f;
    }

    void gate(bool on) {
        if (on) {
            current_ = start_;
            stage_ = Stage::Attack;
            beginRamp(attackLevel_, attackTime_);
        } else if (stage_ != Stage::Idle) {
            stage_ = Stage::Release;
            beginRamp(releaseLevel_, releaseTime_);
        }
    }

    /// The current pitch offset in semitones.
    inline float process() {
        if (stage_ == Stage::Idle || stage_ == Stage::Done) return current_;
        if (remaining_ > 0) {
            current_ += delta_;
            --remaining_;
            return current_;
        }
        switch (stage_) {
            case Stage::Attack:
                current_ = attackLevel_;
                stage_ = Stage::Decay;
                beginRamp(0.0f, decayTime_);
                break;
            case Stage::Decay:
                current_ = 0.0f;
                stage_ = Stage::Sustain;   // in tune while the note holds
                break;
            case Stage::Release:
                current_ = releaseLevel_;
                stage_ = Stage::Done;
                break;
            default:
                break;
        }
        return current_;
    }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release, Done };

    void beginRamp(float target, float seconds) {
        remaining_ = static_cast<int>(
            std::max(1.0, seconds * sampleRate_));
        delta_ = (target - current_) / static_cast<float>(remaining_);
    }

    double sampleRate_ = 44100.0;
    Stage  stage_ = Stage::Idle;
    float  current_ = 0.0f, delta_ = 0.0f;
    int    remaining_ = 0;
    float  start_ = 0.0f, attackLevel_ = 0.0f, releaseLevel_ = 0.0f;
    float  attackTime_ = 0.001f, decayTime_ = 0.2f, releaseTime_ = 0.001f;
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
        tail_   = 0.0f;
    }

    void noteOn(int note, float velocity, const PartialParams &p,
                float startOffset = 0.0f) {
        note_     = note;
        velocity_ = velocity;
        // Loudness follows a squared curve — about -12 dB per halving of
        // velocity — because amplitude linear in velocity is dynamically
        // flat: mezzo-forte sat only 6 dB under fortissimo. Everything else
        // that consumes velocity (region selection, velocity splits and
        // crossfades, the matrix source) stays linear; only loudness is
        // perceptual.
        ampVelocity_ = velocity * velocity;
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

                // An imported instrument is one region, and its recorded pitch
                // is a guess until someone corrects it, so the library holds an
                // editable root for it. Generated content has a root per zone
                // and one number cannot describe them, so it is left alone.
                RootTuning tuning;
                if (instrument->regionCount == 1
                 && SampleLibrary::shared().rootTuning(p.sampleInstrument, tuning)) {
                    sampleRootKey_   = tuning.rootKey;
                    sampleTuneCents_ = tuning.tuneCents;
                }
            }
        }

        applyEnvelopeTimes(p);
        ampEnv_.gate(true);
        filterEnv_.gate(true);
        pitchEnv_.gate(true);
        tail_ = 0.0f;
    }

    void noteOff() {
        ampEnv_.gate(false);
        filterEnv_.gate(false);
        pitchEnv_.gate(false);
    }

    bool  isActive() const { return active_; }
    float ampLevel() const { return ampLevel_; }
    float filterEnvLevel() const { return filterLevel_; }
    /// Matrix source: the envelope's semitone output normalised to -1..1.
    float pitchEnvLevel() const { return pitchLevel_ / 24.0f; }

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
        // The discontinuous folder/rectifier need spectral headroom even at
        // 4x. Half an octave of mip bias removes only the shaper input's top
        // band; clean Partials retain the new near-20 kHz oscillator bandwidth.
        osc_.setMipBias(shaper_.isActive()
                            ? 0.5f * r50::kWaveLevelsPerOctave : 0.0f);

        const double detune = p.octave * 12.0 + p.semitone
                            + p.fineCents / 100.0 + pitchBendSemitones
                            + pitchLevel_
                            + mod.pitchSemitones;
        // Key follow pivots on middle C rather than on note zero, so turning it
        // down pulls the keyboard in around the middle of its range instead of
        // collapsing it onto the bottom note.
        const double tracked = 60.0 + (note_ - 60.0) * p.pitchKeyFollow;
        const double noteHz = synth::noteToHz(tracked + detune);
        osc_.setFrequency(noteHz);
        noise_.updateBlock(p.noiseSpectrum, p.noiseTone, p.noiseRateHz,
                           p.noisePitchTrack, noteHz);

        // Sample playback rate follows the same pitch as the oscillator, but
        // relative to the region's root key rather than to concert pitch.
        sourceType_ = p.sourceType;
        if (sample_.isActive()) {
            const double semitones = (tracked - sampleRootKey_) + detune
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
        filter_.setParams(cutoff, resonance, p.slope);
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

        float out;
        if (shaper_.position() == ShaperPosition::PreFilter) {
            source = shaper_.process(source);
            out = filter_.process(source) * ampLevel_ * ampVelocity_;
        } else {
            out = shaper_.process(filter_.process(source)) * ampLevel_ * ampVelocity_;
        }

        // A one-shot sample that has played to its end can never produce
        // another frame, so once it finishes — and nothing else is feeding the
        // chain — the note is over no matter what the amp envelope still
        // intends. Without this the Partial stays active for as long as the
        // envelope sustains, which on a held key is forever: the voice sits
        // there rendering silence, and allocateVoice() ranks held voices as the
        // *last* to steal, so a held one-shot chord quietly eats the polyphony.
        //
        // The end is detected from the output rather than from the sample's
        // last frame, because a resonant filter fed a marimba transient is
        // still ringing after the source has stopped and cutting at the final
        // frame would truncate it. Following the rectified output through a
        // one-pole lets that ring finish and makes the cut click-free by
        // construction — it only ever fires on silence.
        // The follower runs unconditionally: if it only started once the sample
        // had finished it would read silence on its first update and cut the
        // ring it exists to preserve.
        //
        // It is also the only thing guarding the mix. Noise is a source in its
        // own right and can outlive an exhausted one-shot, but that needs no
        // separate test here — noise that is audible holds the follower up, and
        // noise that is not is not worth a voice.
        tail_ += (std::fabs(out) - tail_) * kTailCoefficient;
        if (tail_ < kTailSilence
         && sourceType_ == SourceType::Sample && sample_.isFinished()) {
            active_ = false;
            filter_.reset();
        }
        return out;
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
        pitchEnv_.configure(p.pitchStartLevel, p.pitchAttack, p.pitchAmount,
                            p.pitchDecay, p.pitchRelease,
                            p.pitchReleaseLevel);
    }

    WaveOscillator      osc_;
    SamplePlayer        sample_;
    NoiseSource         noise_;
    DigitalLowPassFilter filter_;
    Waveshaper          shaper_;
    R50Envelope         ampEnv_;
    R50Envelope         filterEnv_;
    PitchEnvelope       pitchEnv_;

    double sampleRate_  = 44100.0;
    int    note_        = -1;
    float  velocity_    = 1.0f;
    float  ampVelocity_ = 1.0f;   // velocity_^2: the loudness curve
    bool   active_      = false;
    SourceType sourceType_ = SourceType::Wave;

    /// Follower for the exhausted-one-shot check above. The coefficient is a
    /// ~5 ms time constant at 44.1 kHz, long enough to bridge a low note's zero
    /// crossings and short enough that the voice is back in the pool before the
    /// player could ask for it again. The threshold is -80 dBFS.
    static constexpr float kTailCoefficient = 0.0045f;
    static constexpr float kTailSilence     = 1.0e-4f;
    float tail_ = 0.0f;
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
