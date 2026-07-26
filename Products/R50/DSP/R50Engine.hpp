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
        snapshotParams();
        // Force the generated libraries to build here, on whatever thread
        // constructs the engine — never lazily from the render thread.
        (void)waveLibrary();
        (void)SampleLibrary::shared();
        for (int i = 0; i < kNumVoices; ++i) {
            voices_[i].setSampleRate(sampleRate_);
            // Distinct, fixed per-voice noise seeds: voices decorrelate from
            // one another, but a render stays bit-repeatable across instances.
            voices_[i].setSeed(0x9E3779B97F4A7C15ULL * (i + 1) + 0x165667B1ULL);
        }
    }

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        for (auto &voice : voices_) voice.setSampleRate(sr);
        gainSmoother_.setSampleRate(sr);
        gainSmoother_.setTimeConstant(20.0);
        gainSmoother_.snap(store_[R50ParamMasterGain].load(std::memory_order_relaxed));
    }

    // MARK: - Parameters

    /// Lock-free from any thread: the UI/host writes only into the atomic
    /// store. The denormalised `params_` block that the voices actually read is
    /// derived from it exclusively on the render thread (see snapshotParams),
    /// so no DSP state is ever mutated from two threads.
    void setParameter(uint64_t address, float value) {
        if (address >= R50ParamCount) return;
        store_[address].store(value, std::memory_order_relaxed);
    }

    float getParameter(uint64_t address) const {
        return (address < R50ParamCount)
            ? store_[address].load(std::memory_order_relaxed) : 0.0f;
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

        // Envelope times come from params_, so make sure it reflects any
        // parameter writes that landed since the last control block.
        snapshotParams();

        keyDown_[note & 0x7F] = true;

        // Re-use the voice already playing this note (retrigger) if there is one.
        Voice *voice = findVoice(note);
        if (voice == nullptr) voice = allocateVoice();
        voice->noteOn(note, velocity / 127.0f, params_);
    }

    void noteOff(uint8_t note) {
        // The physical key is up even when CC64 keeps the gate open — pedal-up
        // consults keyDown_, so a note retriggered while the pedal is held is
        // not released out from under the player.
        keyDown_[note & 0x7F] = false;
        if (sustain_) return;

        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld() && voice.note() == note) {
                voice.noteOff();
            }
        }
    }

    void pitchBend(int value14) {
        // 0 .. 16383, centre 8192.
        bendNorm_ = (value14 - 8192) / 8192.0f;
    }

    void sustainPedal(bool down) {
        if (down == sustain_) return;
        sustain_ = down;
        if (down) return;

        // Pedal-up releases only voices whose keys are no longer down.
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld()
                && !keyDown_[voice.note() & 0x7F]) {
                voice.noteOff();
            }
        }
    }

    void allNotesOff() {
        for (auto &voice : voices_) {
            if (voice.isActive()) voice.noteOff();
        }
        std::memset(keyDown_, 0, sizeof(keyDown_));
    }

    void allSoundOff() {
        for (auto &voice : voices_) voice.reset();
        std::memset(keyDown_, 0, sizeof(keyDown_));
    }

    void setTempo(double) {}   // R50 has no tempo-synced sources (yet).

    float outputMeter() const { return meter_.load(std::memory_order_relaxed); }

    // MARK: - Render

    void render(float *outL, float *outR, int frameCount) {
        const double bendSemitones =
            bendNorm_ * store_[R50ParamPitchBendRange].load(std::memory_order_relaxed);
        float peak = 0.0f;
        int offset = 0;

        while (offset < frameCount) {
            const int block = std::min(kControlBlock, frameCount - offset);

            // Re-derive the voice parameter block from the atomic store once
            // per control block — the only place params_ is ever written.
            snapshotParams();

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
    void set(R50Param address, float value) {
        store_[address].store(value, std::memory_order_relaxed);
    }
    float get(R50Param address) const {
        return store_[address].load(std::memory_order_relaxed);
    }

    void setDefaults() {
        set(R50ParamOscWave,         0.0f);   // saw
        set(R50ParamPulseWidth,      0.5f);
        set(R50ParamOctave,          0.0f);
        set(R50ParamCutoff,          3200.0f);
        set(R50ParamResonance,       0.15f);
        set(R50ParamDrive,           0.0f);
        set(R50ParamSlope,           1.0f);   // 24 dB
        set(R50ParamKeyTrack,        0.5f);
        set(R50ParamFilterEnvAmount, 0.45f);
        set(R50ParamAmpAttack,       0.004f);
        set(R50ParamAmpDecay,        0.25f);
        set(R50ParamAmpSustain,      0.75f);
        set(R50ParamAmpRelease,      0.30f);
        set(R50ParamFilterAttack,    0.004f);
        set(R50ParamFilterDecay,     0.45f);
        set(R50ParamFilterSustain,   0.30f);
        set(R50ParamFilterRelease,   0.30f);
        set(R50ParamMasterGain,      0.8f);
        set(R50ParamPitchBendRange,  2.0f);
        set(R50ParamNoiseMix,        0.0f);
        set(R50ParamNoiseSpectrum,   0.0f);   // white
        set(R50ParamNoiseTone,       0.5f);
        set(R50ParamNoiseRate,       4000.0f);
        set(R50ParamNoisePitchTrack, 0.0f);
        set(R50ParamSourceType,       0.0f);   // wave table
        set(R50ParamSampleInstrument, 0.0f);
        set(R50ParamSampleStart,      0.0f);
    }

    /// Derive the denormalised block the voices read from the atomic store.
    ///
    /// RENDER THREAD ONLY (plus the constructor, before any render can run).
    /// Writers on other threads touch nothing but `store_`, which keeps every
    /// piece of live DSP state — params_, the voices and the gain smoother —
    /// single-threaded.
    void snapshotParams() {
        const int wave = static_cast<int>(get(R50ParamOscWave) + 0.5f);
        params_.waveIndex = wave < 0 ? 0
                          : (wave >= kWaveCount ? kWaveCount - 1 : wave);
        params_.pulseWidth      = get(R50ParamPulseWidth);
        params_.octave          = static_cast<int>(std::lround(get(R50ParamOctave)));

        params_.cutoffHz        = get(R50ParamCutoff);
        params_.resonance       = synth::clampf(get(R50ParamResonance), 0.0f, 1.0f);
        params_.drive           = synth::clampf(get(R50ParamDrive), 0.0f, 1.0f);
        params_.slope           = synth::clampf(get(R50ParamSlope), 0.0f, 1.0f);
        params_.keyTrack        = synth::clampf(get(R50ParamKeyTrack), 0.0f, 1.0f);
        params_.filterEnvAmount = synth::clampf(get(R50ParamFilterEnvAmount), -1.0f, 1.0f);

        params_.ampAttack     = std::max(0.0005f, get(R50ParamAmpAttack));
        params_.ampDecay      = std::max(0.0005f, get(R50ParamAmpDecay));
        params_.ampSustain    = synth::clampf(get(R50ParamAmpSustain), 0.0f, 1.0f);
        params_.ampRelease    = std::max(0.0005f, get(R50ParamAmpRelease));
        params_.filterAttack  = std::max(0.0005f, get(R50ParamFilterAttack));
        params_.filterDecay   = std::max(0.0005f, get(R50ParamFilterDecay));
        params_.filterSustain = synth::clampf(get(R50ParamFilterSustain), 0.0f, 1.0f);
        params_.filterRelease = std::max(0.0005f, get(R50ParamFilterRelease));

        params_.sourceType = get(R50ParamSourceType) >= 0.5f
                           ? SourceType::Sample : SourceType::Wave;
        const int instrument = static_cast<int>(get(R50ParamSampleInstrument) + 0.5f);
        params_.sampleInstrument = instrument < 0 ? 0 : instrument;
        params_.sampleStart = synth::clampf(get(R50ParamSampleStart), 0.0f, 1.0f);

        params_.noiseMix = synth::clampf(get(R50ParamNoiseMix), 0.0f, 1.0f);
        const int spectrum = static_cast<int>(get(R50ParamNoiseSpectrum) + 0.5f);
        params_.noiseSpectrum = static_cast<NoiseSpectrum>(
            spectrum < 0 ? 0
                         : (spectrum >= kNoiseSpectrumCount
                                ? kNoiseSpectrumCount - 1 : spectrum));
        params_.noiseTone       = synth::clampf(get(R50ParamNoiseTone), 0.0f, 1.0f);
        params_.noiseRateHz     = get(R50ParamNoiseRate);
        params_.noisePitchTrack = get(R50ParamNoisePitchTrack) >= 0.5f;

        gainSmoother_.setTarget(synth::clampf(get(R50ParamMasterGain), 0.0f, 1.0f));
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

    // Written by the host/UI from any thread; read on the render thread.
    std::atomic<float> store_[R50ParamCount];

    // Live DSP state — render thread only.
    Voice       voices_[kNumVoices];
    VoiceParams params_;
    double sampleRate_ = 44100.0;
    float  bendNorm_   = 0.0f;
    bool   sustain_    = false;
    /// Physical key state, independent of the CC64 gate hold.
    bool   keyDown_[128] = {false};

    synth::OnePoleSmoother gainSmoother_;
    std::atomic<float>     meter_{0.0f};
};

} // namespace r50
