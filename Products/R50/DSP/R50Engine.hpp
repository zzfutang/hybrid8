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
                float sumL = 0.0f, sumR = 0.0f;
                for (auto &voice : voices_) {
                    float voiceL = 0.0f, voiceR = 0.0f;
                    voice.process(voiceL, voiceR);
                    sumL += voiceL;
                    sumR += voiceR;
                }

                // Headroom for stacked voices, then a gentle safety clip.
                const float gain = 0.25f * gainSmoother_.next();
                sumL = synth::softClip(sumL * gain);
                sumR = synth::softClip(sumR * gain);

                outL[offset + i] = sumL;
                outR[offset + i] = sumR;
                const float magnitude = std::max(std::fabs(sumL), std::fabs(sumR));
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
    void setPartial(int partial, R50PartialField field, float value) {
        store_[r50PartialParam(partial, field)]
            .store(value, std::memory_order_relaxed);
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

        // Partial 1 mixing controls, and Partial 2 off by default so an
        // existing one-Partial preset sounds exactly as it did before.
        set(R50ParamP1Enabled,  1.0f);
        set(R50ParamP1Level,    1.0f);
        set(R50ParamP1Pan,      0.0f);
        set(R50ParamP1Semitone, 0.0f);
        set(R50ParamP1Fine,     0.0f);

        PartialParams defaults;
        setPartial(1, R50FieldEnabled,         0.0f);
        setPartial(1, R50FieldSourceType,      0.0f);
        setPartial(1, R50FieldSampleInstrument,0.0f);
        setPartial(1, R50FieldSampleStart,     0.0f);
        setPartial(1, R50FieldOscWave,         0.0f);
        setPartial(1, R50FieldPulseWidth,      defaults.pulseWidth);
        setPartial(1, R50FieldOctave,          0.0f);
        setPartial(1, R50FieldSemitone,        0.0f);
        setPartial(1, R50FieldFine,            0.0f);
        setPartial(1, R50FieldNoiseMix,        0.0f);
        setPartial(1, R50FieldNoiseSpectrum,   0.0f);
        setPartial(1, R50FieldNoiseTone,       0.5f);
        setPartial(1, R50FieldNoiseRate,       4000.0f);
        setPartial(1, R50FieldNoisePitchTrack, 0.0f);
        setPartial(1, R50FieldCutoff,          3200.0f);
        setPartial(1, R50FieldResonance,       0.15f);
        setPartial(1, R50FieldDrive,           0.0f);
        setPartial(1, R50FieldSlope,           1.0f);
        setPartial(1, R50FieldKeyTrack,        0.5f);
        setPartial(1, R50FieldFilterEnvAmount, 0.45f);
        setPartial(1, R50FieldAmpAttack,       0.004f);
        setPartial(1, R50FieldAmpDecay,        0.25f);
        setPartial(1, R50FieldAmpSustain,      0.75f);
        setPartial(1, R50FieldAmpRelease,      0.30f);
        setPartial(1, R50FieldFilterAttack,    0.004f);
        setPartial(1, R50FieldFilterDecay,     0.45f);
        setPartial(1, R50FieldFilterSustain,   0.30f);
        setPartial(1, R50FieldFilterRelease,   0.30f);
        setPartial(1, R50FieldLevel,           1.0f);
        setPartial(1, R50FieldPan,             0.0f);

        set(R50ParamToneStructure,     0.0f);   // Mix
        set(R50ParamToneRingLevel,     1.0f);
        set(R50ParamToneBlendTime,     0.25f);
        set(R50ParamToneCrossfadeLow,  48.0f);
        set(R50ParamToneCrossfadeHigh, 72.0f);
    }

    /// Derive the denormalised block the voices read from the atomic store.
    ///
    /// RENDER THREAD ONLY (plus the constructor, before any render can run).
    /// Writers on other threads touch nothing but `store_`, which keeps every
    /// piece of live DSP state — params_, the voices and the gain smoother —
    /// single-threaded.
    /// Read one Partial's block out of the atomic store. Addresses come from
    /// r50PartialParam(), the single authority for the mapping — Partial 1 uses
    /// the original scattered addresses so old presets keep their meaning.
    void snapshotPartial(int index, PartialParams &out) const {
        const auto field = [&](R50PartialField f) {
            return store_[r50PartialParam(index, f)]
                       .load(std::memory_order_relaxed);
        };

        out.enabled = field(R50FieldEnabled) >= 0.5f;

        out.sourceType = field(R50FieldSourceType) >= 0.5f
                       ? SourceType::Sample : SourceType::Wave;
        const int instrument = static_cast<int>(field(R50FieldSampleInstrument) + 0.5f);
        out.sampleInstrument = instrument < 0 ? 0 : instrument;
        out.sampleStart = synth::clampf(field(R50FieldSampleStart), 0.0f, 1.0f);

        const int wave = static_cast<int>(field(R50FieldOscWave) + 0.5f);
        out.waveIndex = wave < 0 ? 0 : (wave >= kWaveCount ? kWaveCount - 1 : wave);
        out.pulseWidth = field(R50FieldPulseWidth);
        out.octave     = static_cast<int>(std::lround(field(R50FieldOctave)));
        out.semitone   = static_cast<int>(std::lround(field(R50FieldSemitone)));
        out.fineCents  = field(R50FieldFine);

        out.noiseMix = synth::clampf(field(R50FieldNoiseMix), 0.0f, 1.0f);
        const int spectrum = static_cast<int>(field(R50FieldNoiseSpectrum) + 0.5f);
        out.noiseSpectrum = static_cast<NoiseSpectrum>(
            spectrum < 0 ? 0
                         : (spectrum >= kNoiseSpectrumCount
                                ? kNoiseSpectrumCount - 1 : spectrum));
        out.noiseTone       = synth::clampf(field(R50FieldNoiseTone), 0.0f, 1.0f);
        out.noiseRateHz     = field(R50FieldNoiseRate);
        out.noisePitchTrack = field(R50FieldNoisePitchTrack) >= 0.5f;

        out.cutoffHz        = field(R50FieldCutoff);
        out.resonance       = synth::clampf(field(R50FieldResonance), 0.0f, 1.0f);
        out.drive           = synth::clampf(field(R50FieldDrive), 0.0f, 1.0f);
        out.slope           = synth::clampf(field(R50FieldSlope), 0.0f, 1.0f);
        out.keyTrack        = synth::clampf(field(R50FieldKeyTrack), 0.0f, 1.0f);
        out.filterEnvAmount = synth::clampf(field(R50FieldFilterEnvAmount), -1.0f, 1.0f);

        out.ampAttack     = std::max(0.0005f, field(R50FieldAmpAttack));
        out.ampDecay      = std::max(0.0005f, field(R50FieldAmpDecay));
        out.ampSustain    = synth::clampf(field(R50FieldAmpSustain), 0.0f, 1.0f);
        out.ampRelease    = std::max(0.0005f, field(R50FieldAmpRelease));
        out.filterAttack  = std::max(0.0005f, field(R50FieldFilterAttack));
        out.filterDecay   = std::max(0.0005f, field(R50FieldFilterDecay));
        out.filterSustain = synth::clampf(field(R50FieldFilterSustain), 0.0f, 1.0f);
        out.filterRelease = std::max(0.0005f, field(R50FieldFilterRelease));

        out.level = synth::clampf(field(R50FieldLevel), 0.0f, 1.0f);
        out.pan   = synth::clampf(field(R50FieldPan), -1.0f, 1.0f);
    }

    /// Derive the denormalised block the voices read from the atomic store.
    ///
    /// RENDER THREAD ONLY (plus the constructor, before any render can run).
    /// Writers on other threads touch nothing but `store_`, which keeps every
    /// piece of live DSP state — params_, the voices and the gain smoother —
    /// single-threaded.
    void snapshotParams() {
        for (int i = 0; i < kPartialsPerVoice; ++i) {
            snapshotPartial(i, params_.partial[i]);
        }

        const int structure = static_cast<int>(get(R50ParamToneStructure) + 0.5f);
        params_.structure = static_cast<ToneStructure>(
            structure < 0 ? 0
                          : (structure >= kToneStructureCount
                                 ? kToneStructureCount - 1 : structure));
        params_.ringLevel = synth::clampf(get(R50ParamToneRingLevel), 0.0f, 1.0f);
        params_.blendTime = std::max(0.001f, get(R50ParamToneBlendTime));
        params_.crossfadeLow =
            static_cast<int>(std::lround(get(R50ParamToneCrossfadeLow)));
        params_.crossfadeHigh =
            static_cast<int>(std::lround(get(R50ParamToneCrossfadeHigh)));

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
