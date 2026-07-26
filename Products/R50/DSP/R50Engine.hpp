//
//  R50Engine.hpp
//  R50 polyphonic engine: parameter store, voice allocation, MIDI handling and
//  the block render loop. Header-only and real-time safe — no allocation,
//  locking or ObjC messaging happens inside render().
//

#pragma once

#include <atomic>
#include <cstring>

#include "R50Parameters.h"
#include "R50Voice.hpp"
#include "Utils.hpp"

namespace r50 {

static constexpr int kNumVoices   = 8;
/// Control-rate block: filter coefficients and pitch update this often.
static constexpr int kControlBlock = 32;

class R50Engine {
public:
    R50Engine() {
        setDefaults();
        rebuildParams();
        for (auto &voice : voices_) voice.setSampleRate(sampleRate_);
    }

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        for (auto &voice : voices_) voice.setSampleRate(sr);
        gainSmoother_.setSampleRate(sr);
        gainSmoother_.setTimeConstant(20.0);
        gainSmoother_.snap(rawParams_[R50ParamMasterGain]);
    }

    // MARK: - Parameters

    void setParameter(uint64_t address, float value) {
        if (address >= R50ParamCount) return;
        rawParams_[address] = value;
        rebuildParams();
    }

    float getParameter(uint64_t address) const {
        return (address < R50ParamCount) ? rawParams_[address] : 0.0f;
    }

    /// Automation ramps are applied as an immediate set. R50's continuous
    /// controls are either smoothed (gain) or only read at control-block
    /// boundaries (filter, pitch), so a per-sample ramp would not be audible.
    void startParameterRamp(uint64_t address, float value, uint32_t /*frames*/) {
        setParameter(address, value);
    }

    // MARK: - MIDI

    void noteOn(uint8_t note, uint8_t velocity) {
        if (velocity == 0) { noteOff(note); return; }

        // Re-use the voice already playing this note (retrigger) if there is one.
        Voice *voice = findVoice(note);
        if (voice == nullptr) voice = allocateVoice();
        voice->noteOn(note, velocity / 127.0f, params_);
    }

    void noteOff(uint8_t note) {
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld() && voice.note() == note) {
                if (sustain_) {
                    sustained_[note & 0x7F] = true;
                } else {
                    voice.noteOff();
                }
            }
        }
    }

    void pitchBend(int value14) {
        // 0 .. 16383, centre 8192.
        bendNorm_ = (value14 - 8192) / 8192.0f;
    }

    void sustainPedal(bool down) {
        sustain_ = down;
        if (down) return;
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld()
                && sustained_[voice.note() & 0x7F]) {
                voice.noteOff();
            }
        }
        std::memset(sustained_, 0, sizeof(sustained_));
    }

    void allNotesOff() {
        for (auto &voice : voices_) {
            if (voice.isActive()) voice.noteOff();
        }
        std::memset(sustained_, 0, sizeof(sustained_));
    }

    void allSoundOff() {
        for (auto &voice : voices_) voice.reset();
        std::memset(sustained_, 0, sizeof(sustained_));
    }

    void setTempo(double) {}   // R50 has no tempo-synced sources (yet).

    float outputMeter() const { return meter_.load(std::memory_order_relaxed); }

    // MARK: - Render

    void render(float *outL, float *outR, int frameCount) {
        const double bendSemitones = bendNorm_
                                   * rawParams_[R50ParamPitchBendRange];
        float peak = 0.0f;
        int offset = 0;

        while (offset < frameCount) {
            const int block = std::min(kControlBlock, frameCount - offset);

            for (auto &voice : voices_) {
                if (voice.isActive()) voice.updateBlock(params_, bendSemitones);
            }

            for (int i = 0; i < block; ++i) {
                float sum = 0.0f;
                for (auto &voice : voices_) sum += voice.process();

                // Headroom for 8 stacked voices, then a gentle safety clip.
                sum *= 0.25f * gainSmoother_.next();
                sum = synth::softClip(sum);

                outL[offset + i] = sum;
                outR[offset + i] = sum;
                const float magnitude = sum < 0.0f ? -sum : sum;
                if (magnitude > peak) peak = magnitude;
            }
            offset += block;
        }

        meter_.store(peak, std::memory_order_relaxed);
    }

private:
    void setDefaults() {
        rawParams_[R50ParamOscWave]          = 0.0f;   // saw
        rawParams_[R50ParamPulseWidth]       = 0.5f;
        rawParams_[R50ParamOctave]           = 0.0f;
        rawParams_[R50ParamCutoff]           = 3200.0f;
        rawParams_[R50ParamResonance]        = 0.15f;
        rawParams_[R50ParamDrive]            = 0.0f;
        rawParams_[R50ParamSlope]            = 1.0f;   // 24 dB
        rawParams_[R50ParamKeyTrack]         = 0.5f;
        rawParams_[R50ParamFilterEnvAmount]  = 0.45f;
        rawParams_[R50ParamAmpAttack]        = 0.004f;
        rawParams_[R50ParamAmpDecay]         = 0.25f;
        rawParams_[R50ParamAmpSustain]       = 0.75f;
        rawParams_[R50ParamAmpRelease]       = 0.30f;
        rawParams_[R50ParamFilterAttack]     = 0.004f;
        rawParams_[R50ParamFilterDecay]      = 0.45f;
        rawParams_[R50ParamFilterSustain]    = 0.30f;
        rawParams_[R50ParamFilterRelease]    = 0.30f;
        rawParams_[R50ParamMasterGain]       = 0.8f;
        rawParams_[R50ParamPitchBendRange]   = 2.0f;
    }

    /// Translate the flat AU parameter array into the denormalised block the
    /// voices read. Called off the render thread (parameter set) and from the
    /// render thread (automation events) — both only write plain floats.
    void rebuildParams() {
        const int wave = static_cast<int>(rawParams_[R50ParamOscWave] + 0.5f);
        params_.wave = static_cast<synth::OscWave>(
            wave < 0 ? 0 : (wave > 2 ? 2 : wave));
        params_.pulseWidth      = rawParams_[R50ParamPulseWidth];
        params_.octave          = static_cast<int>(std::lround(rawParams_[R50ParamOctave]));

        params_.cutoffHz        = rawParams_[R50ParamCutoff];
        params_.resonance       = synth::clampf(rawParams_[R50ParamResonance], 0.0f, 1.0f);
        params_.drive           = synth::clampf(rawParams_[R50ParamDrive], 0.0f, 1.0f);
        params_.slope           = synth::clampf(rawParams_[R50ParamSlope], 0.0f, 1.0f);
        params_.keyTrack        = synth::clampf(rawParams_[R50ParamKeyTrack], 0.0f, 1.0f);
        params_.filterEnvAmount = synth::clampf(rawParams_[R50ParamFilterEnvAmount], -1.0f, 1.0f);

        params_.ampAttack     = std::max(0.0005f, rawParams_[R50ParamAmpAttack]);
        params_.ampDecay      = std::max(0.0005f, rawParams_[R50ParamAmpDecay]);
        params_.ampSustain    = synth::clampf(rawParams_[R50ParamAmpSustain], 0.0f, 1.0f);
        params_.ampRelease    = std::max(0.0005f, rawParams_[R50ParamAmpRelease]);
        params_.filterAttack  = std::max(0.0005f, rawParams_[R50ParamFilterAttack]);
        params_.filterDecay   = std::max(0.0005f, rawParams_[R50ParamFilterDecay]);
        params_.filterSustain = synth::clampf(rawParams_[R50ParamFilterSustain], 0.0f, 1.0f);
        params_.filterRelease = std::max(0.0005f, rawParams_[R50ParamFilterRelease]);

        gainSmoother_.setTarget(synth::clampf(rawParams_[R50ParamMasterGain], 0.0f, 1.0f));
    }

    Voice *findVoice(int note) {
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld() && voice.note() == note) {
                return &voice;
            }
        }
        return nullptr;
    }

    /// Prefer a free voice; otherwise steal the quietest releasing one, and
    /// failing that the quietest held one.
    Voice *allocateVoice() {
        for (auto &voice : voices_) {
            if (!voice.isActive()) return &voice;
        }
        Voice *quietest = &voices_[0];
        for (auto &voice : voices_) {
            if (voice.releaseProgress() < quietest->releaseProgress()) {
                quietest = &voice;
            }
        }
        return quietest;
    }

    Voice       voices_[kNumVoices];
    VoiceParams params_;
    float       rawParams_[R50ParamCount] = {0.0f};

    double sampleRate_ = 44100.0;
    float  bendNorm_   = 0.0f;
    bool   sustain_    = false;
    bool   sustained_[128] = {false};

    synth::OnePoleSmoother gainSmoother_;
    std::atomic<float>     meter_{0.0f};
};

} // namespace r50
