//
//  R50Voice.hpp
//  One R50 voice: a single band-limited PCM wave oscillator into the shared
//  ladder/SVF filter, with dedicated amp and filter envelopes.
//
//  Filter coefficients are recomputed once per control block (see
//  kControlBlock in R50Engine.hpp) rather than per sample — setParams() runs a
//  tan() and is far too costly to call 8 times per sample frame.
//

#pragma once

#include "ADSR.hpp"
#include "Filter.hpp"
#include "R50WaveOscillator.hpp"
#include "Utils.hpp"

namespace r50 {

/// Denormalised parameter block shared by every voice, rebuilt by the engine
/// whenever a parameter changes.
struct VoiceParams {
    int   waveIndex           = 0;      // index into waveDescriptors()
    float pulseWidth          = 0.5f;   // only read by Difference-mode waves
    int   octave              = 0;

    float cutoffHz            = 4000.0f;
    float resonance           = 0.2f;
    float drive               = 0.0f;
    float slope               = 0.0f;   // 0 = 12 dB, 1 = 24 dB
    float keyTrack            = 0.5f;
    float filterEnvAmount     = 0.4f;   // bipolar, +/- 4 octaves at full scale

    float ampAttack = 0.005f, ampDecay = 0.2f, ampSustain = 0.8f, ampRelease = 0.3f;
    float filterAttack = 0.005f, filterDecay = 0.4f, filterSustain = 0.3f, filterRelease = 0.3f;
};

class Voice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        osc_.setSampleRate(sr);
        filter_.setSampleRate(sr);
        ampEnv_.setSampleRate(sr);
        filterEnv_.setSampleRate(sr);
    }

    void reset() {
        filter_.reset();
        ampEnv_.resetHard();
        filterEnv_.resetHard();
        active_ = false;
        note_ = -1;
    }

    void noteOn(int note, float velocity, const VoiceParams &p) {
        note_     = note;
        velocity_ = velocity;
        held_     = true;
        active_   = true;
        // A fresh note starts from a known phase so repeated notes are
        // identical — R50 is deliberately a tight, repeatable digital voice.
        osc_.reset(0.0f);
        applyEnvelopeTimes(p);
        ampEnv_.gate(true);
        filterEnv_.gate(true);
    }

    void noteOff() {
        held_ = false;
        ampEnv_.gate(false);
        filterEnv_.gate(false);
    }

    bool isActive() const { return active_; }
    bool isHeld() const { return held_; }
    int  note() const { return note_; }
    /// Rough "how disposable is this voice" score for stealing (lower = safer).
    float releaseProgress() const { return held_ ? 1.0f : ampLevel_; }

    /// Per-control-block update: envelope times and filter coefficients.
    void updateBlock(const VoiceParams &p, double pitchBendSemitones) {
        applyEnvelopeTimes(p);

        osc_.setWave(p.waveIndex);
        osc_.setWidth(p.pulseWidth);
        const double midi = static_cast<double>(note_)
                          + p.octave * 12.0 + pitchBendSemitones;
        osc_.setFrequency(synth::noteToHz(midi));

        // Cutoff in octaves: base + key tracking + bipolar envelope.
        const double keyOctaves = p.keyTrack * (note_ - 60) / 12.0;
        const double envOctaves = p.filterEnvAmount * 4.0 * filterLevel_;
        double cutoff = p.cutoffHz * std::pow(2.0, keyOctaves + envOctaves);
        cutoff = synth::clampf(static_cast<float>(cutoff), 20.0f,
                               static_cast<float>(sampleRate_ * 0.45));
        filter_.setParams(cutoff, p.resonance, p.slope, 0.0f, p.drive);
    }

    /// One sample. Returns mono; the engine handles gain and stereo.
    inline float process() {
        if (!active_) return 0.0f;

        ampLevel_    = ampEnv_.process();
        filterLevel_ = filterEnv_.process();

        if (!ampEnv_.isActive()) {
            active_ = false;
            filter_.reset();
            return 0.0f;
        }

        const float osc = osc_.process();
        return filter_.process(osc) * ampLevel_ * velocity_;
    }

private:
    void applyEnvelopeTimes(const VoiceParams &p) {
        ampEnv_.setAttack(p.ampAttack);
        ampEnv_.setDecay(p.ampDecay);
        ampEnv_.setSustain(p.ampSustain);
        ampEnv_.setRelease(p.ampRelease);
        filterEnv_.setAttack(p.filterAttack);
        filterEnv_.setDecay(p.filterDecay);
        filterEnv_.setSustain(p.filterSustain);
        filterEnv_.setRelease(p.filterRelease);
    }

    WaveOscillator     osc_;
    synth::LadderFilter filter_;
    synth::ADSR        ampEnv_;
    synth::ADSR        filterEnv_;

    double sampleRate_  = 44100.0;
    int    note_        = -1;
    float  velocity_    = 1.0f;
    bool   held_        = false;
    bool   active_      = false;
    float  ampLevel_    = 0.0f;
    float  filterLevel_ = 0.0f;
};

} // namespace r50
