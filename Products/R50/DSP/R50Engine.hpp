//
//  R50Engine.hpp
//  R50 polyphonic engine: parameter store, voice allocation, MIDI handling and
//  the block render loop. Header-only and real-time safe — no allocation,
//  locking or ObjC messaging happens inside render().
//

#pragma once

#include <atomic>
#include <cstring>

#include "R50EffectsRack.hpp"
#include "R50Parameters.h"
#include "R50Voice.hpp"
#include "Utils.hpp"

namespace r50 {

static constexpr int kNumVoices   = 8;
/// Control-rate block: filter coefficients and pitch update this often.
static constexpr int kControlBlock = 32;

/// Ceiling of the output stage: the gain rider in render() holds the
/// steady-state peak at or below this, and outputLimit()'s knee starts here.
static constexpr float kLimiterCeiling = 0.92f;

/// The output safety clip. Bit-transparent below the knee and a smooth,
/// monotonic bend into 1.0 above it — a safety device, not a colour. The
/// tanh-style softClip it replaced bent the curve at every level.
///
/// Since the gain rider took over sustained-level control, this only ever
/// sees the first control block of a transient the rider has not caught up
/// with, so the knee sits at the rider's ceiling rather than at the 0.75
/// that used to shave every chord continuously — waveshaping a held chord
/// is exactly the distortion the rider exists to remove.
inline float outputLimit(float x) {
    constexpr float knee = kLimiterCeiling;
    const float magnitude = std::fabs(x);
    if (magnitude <= knee) return x;
    const float span = 1.0f - knee;
    const float t = (magnitude - knee) / span;   // 0.. as the input rises
    const float shaped = knee + span * (t / (1.0f + t));  // asymptote at 1.0
    return std::copysign(shaped, x);
}

class R50Engine {
public:
    R50Engine() {
        setDefaults();
        snapshotParams();
        for (int partial = 0; partial < kPartialsPerVoice; ++partial) {
            dryLevel_[partial] = dryLevelTarget_[partial];
            for (int slot = 0; slot < kEffectSlotCount; ++slot)
                sendLevel_[partial][slot] = sendLevelTarget_[partial][slot];
        }
        for (int tone = 0; tone < kTonesPerVoice; ++tone) {
            ringDryLevel_[tone] = ringDryLevelTarget_[tone];
            for (int slot = 0; slot < kEffectSlotCount; ++slot)
                ringSendLevel_[tone][slot] = ringSendLevelTarget_[tone][slot];
        }
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
        auditionVoice_.setSampleRate(sr);
        effects_.setup(sr);
        routingSmoothCoef_ =
            static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate_)));
        // Rider timing: the hold must outlast not just one cycle of the
        // lowest note (A0 is 36 ms) but a chord's beat cycle — with a short
        // hold the gain chases every equal-temperament beat swell, and each
        // fast re-attack edge sprays sidebands measured at -34 dB. Held past
        // the beats, the gain over a sustained chord is close to static and
        // the rider adds almost nothing; the price is a slow, breath-like
        // recovery after an accent, which is what a limiter should sound like.
        limiterHoldSamples_ = static_cast<int>(0.400 * sampleRate_);
        limiterReleaseCoef_ = static_cast<float>(
            std::exp(-kControlBlock / (0.300 * sampleRate_)));
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

        // Fingered portamento wants to know whether another key was still
        // down when this one arrived — checked before this key is marked.
        bool legato = false;
        for (int key = 0; key < 128; ++key) {
            if (keyDown_[key] && key != (note & 0x7F)) { legato = true; break; }
        }
        keyDown_[note & 0x7F] = true;

        if (get(R50ParamVoiceMode) >= 0.5f) {
            monoNoteOn(note, velocity, legato);
            return;
        }

        // Re-use the voice already playing this note (retrigger) if there is one.
        Voice *voice = findVoice(note);
        if (voice == nullptr) voice = allocateVoice();
        startVoice(*voice, note, velocity, legato);
    }

    void noteOff(uint8_t note) {
        // The physical key is up even when CC64 keeps the gate open — pedal-up
        // consults keyDown_, so a note retriggered while the pedal is held is
        // not released out from under the player.
        keyDown_[note & 0x7F] = false;

        if (get(R50ParamVoiceMode) >= 0.5f) {
            monoNoteOff(note);
            return;
        }

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

    /// Matrix sources, so they only do anything once something routes them.
    void modWheel(float value)   { modWheel_ = synth::clampf(value, 0.0f, 1.0f); }
    void aftertouch(float value) { aftertouch_ = synth::clampf(value, 0.0f, 1.0f); }

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
        monoDepth_ = 0;
        lastPlayedNote_ = -1;
    }

    void allSoundOff() {
        for (auto &voice : voices_) voice.reset();
        auditionVoice_.reset();
        auditionHold_ = 0;
        limiterEnv_  = 0.0f;
        limiterGain_ = 1.0f;
        limiterHold_ = 0;
        limiterDelayPos_ = 0;
        std::memset(limiterDelayL_, 0, sizeof(limiterDelayL_));
        std::memset(limiterDelayR_, 0, sizeof(limiterDelayR_));
        effects_.reset();
        std::memset(keyDown_, 0, sizeof(keyDown_));
        monoDepth_ = 0;
        lastPlayedNote_ = -1;
    }

    void setTempo(double) {}   // R50 has no tempo-synced sources (yet).

    /// Preview one instrument from the sample browser, called from the UI
    /// thread. It deliberately does not go through the patch: the browser is
    /// for judging the raw content, and a preview filtered by whatever the
    /// current patch happens to do would tell you about the patch instead. It
    /// also stays out of the voice pool, so auditioning never steals a note
    /// that is playing.
    ///
    /// The request is one atomic word rather than a lock: the render thread
    /// picks it up on the next control block and nothing here can block it. The
    /// sequence number in the top bits is what makes two identical requests
    /// distinguishable, so auditioning the same sample twice retriggers.
    void requestAudition(int instrument, uint8_t note, uint8_t velocity) {
        const uint64_t sequence = auditionSequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        const uint64_t packed = (sequence << 32)
                              | (static_cast<uint64_t>(instrument & 0xFFFF) << 16)
                              | (static_cast<uint64_t>(note) << 8)
                              | static_cast<uint64_t>(velocity);
        auditionRequest_.store(packed, std::memory_order_release);
    }

    float outputMeter() const { return meter_.load(std::memory_order_relaxed); }

    /// Peak of the signal entering the output stage since the last read.
    /// Reading resets the hold, so a poll sees every excursion exactly once.
    /// Above kLimiterCeiling the gain rider is pulling the level down; above
    /// 1.0 it would have clipped outright without the output stage.
    float readHeadroomPeak() {
        return headroomPeak_.exchange(0.0f, std::memory_order_relaxed);
    }

    // MARK: - Render

    void render(float *outL, float *outR, int frameCount) {
        const double bendSemitones =
            bendNorm_ * store_[R50ParamPitchBendRange].load(std::memory_order_relaxed);
        float peak = 0.0f;
        float prePeak = 0.0f;
        int offset = 0;

        while (offset < frameCount) {
            const int block = std::min(kControlBlock, frameCount - offset);

            // Re-derive the voice parameter block from the atomic store once
            // per control block — the only place params_ is ever written.
            snapshotParams();

            // Effect coefficients are control-rate too; several of them run
            // trig or allocate delay reads that have no business per sample.
            effects_.setCompressorParams(
                compressorAmount_ > 0.001f ? 1.0f : 0.0f,
                -6.0f - 24.0f * compressorAmount_,   // threshold
                1.0f + 7.0f * compressorAmount_,     // ratio
                0.010f, 0.120f,
                6.0f * compressorAmount_);           // makeup
            for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                const auto slotValue = [&](R50FxSlotField field) {
                    return get(r50FxSlotParam(slot, field));
                };
                const int algorithm =
                    static_cast<int>(slotValue(R50FxFieldAlgorithm) + 0.5f);
                EffectSlotDescriptor descriptor;
                descriptor.algorithm = static_cast<EffectAlgorithm>(
                    algorithm < 0 ? 0 : (algorithm >= kEffectAlgorithmCount
                        ? kEffectAlgorithmCount - 1 : algorithm));
                descriptor.bypass = slotValue(R50FxFieldBypass) >= 0.5f;
                descriptor.send = slotValue(R50FxFieldRouting) >= 0.5f;
                descriptor.mix = synth::clampf(slotValue(R50FxFieldMix), 0.0f, 1.0f);
                descriptor.width =
                    synth::clampf(slotValue(R50FxFieldWidth), 0.0f, 2.0f);
                for (int control = 0; control < 8; ++control) {
                    descriptor.control[control] = synth::clampf(
                        slotValue(static_cast<R50FxSlotField>(
                            R50FxFieldControl1 + control)), 0.0f, 1.0f);
                }
                descriptor.mode[0] = static_cast<int>(
                    std::lround(slotValue(R50FxFieldMode1)));
                descriptor.mode[1] = static_cast<int>(
                    std::lround(slotValue(R50FxFieldMode2)));
                effects_.setRackSlot(slot, descriptor);
            }

            // One free-running phase per LFO, so voices whose LFO is not set to
            // retrigger all agree with each other across a held chord.
            for (int i = 0; i < kLfoCount; ++i) {
                sharedLfoPhase_[i] += params_.modulation.lfo[i].rateHz
                                    * kControlBlock / sampleRate_;
                sharedLfoPhase_[i] -= std::floor(sharedLfoPhase_[i]);
            }
            sharedVectorPhase_ += params_.vectorLfo.rateHz
                                * kControlBlock / sampleRate_;
            sharedVectorPhase_ -= std::floor(sharedVectorPhase_);

            serviceAudition();

            for (auto &voice : voices_) {
                if (voice.isActive()) {
                    voice.updateBlock(params_, bendSemitones,
                                      modWheel_, aftertouch_);
                }
            }
            if (auditionVoice_.isActive()) {
                auditionVoice_.updateBlock(auditionParams_, 0.0, 0.0f, 0.0f);
            }

            for (int i = 0; i < block; ++i) {
                EffectRackInput rackInput;
                for (int partial = 0; partial < kPartialsPerVoice; ++partial) {
                    dryLevel_[partial] = dryLevelTarget_[partial]
                        + (dryLevel_[partial] - dryLevelTarget_[partial])
                        * routingSmoothCoef_;
                    for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                        sendLevel_[partial][slot] = sendLevelTarget_[partial][slot]
                            + (sendLevel_[partial][slot]
                               - sendLevelTarget_[partial][slot])
                            * routingSmoothCoef_;
                    }
                }
                for (int tone = 0; tone < kTonesPerVoice; ++tone) {
                    ringDryLevel_[tone] = ringDryLevelTarget_[tone]
                        + (ringDryLevel_[tone] - ringDryLevelTarget_[tone])
                        * routingSmoothCoef_;
                    for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                        ringSendLevel_[tone][slot] = ringSendLevelTarget_[tone][slot]
                            + (ringSendLevel_[tone][slot]
                               - ringSendLevelTarget_[tone][slot])
                            * routingSmoothCoef_;
                    }
                }
                for (auto &voice : voices_) {
                    VoiceOutput voiceOutput;
                    voice.processPartials(voiceOutput);
                    for (int partial = 0; partial < kPartialsPerVoice; ++partial) {
                        const float left = voiceOutput.partial[partial].l;
                        const float right = voiceOutput.partial[partial].r;
                        rackInput.dry.l += left * dryLevel_[partial];
                        rackInput.dry.r += right * dryLevel_[partial];
                        for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                            rackInput.send[slot].l +=
                                left * sendLevel_[partial][slot];
                            rackInput.send[slot].r +=
                                right * sendLevel_[partial][slot];
                        }
                    }
                    for (int tone = 0; tone < kTonesPerVoice; ++tone) {
                        const float left = voiceOutput.ring[tone].l;
                        const float right = voiceOutput.ring[tone].r;
                        rackInput.dry.l += left * ringDryLevel_[tone];
                        rackInput.dry.r += right * ringDryLevel_[tone];
                        for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                            rackInput.send[slot].l +=
                                left * ringSendLevel_[tone][slot];
                            rackInput.send[slot].r +=
                                right * ringSendLevel_[tone][slot];
                        }
                    }
                }

                // Headroom for stacked voices, then a gentle safety clip.
                // 0.25 was sized for eight voices all peaking together, which
                // left a single note at -20 dBFS; the soft clip exists exactly
                // to catch the rare moment when a dense chord does line up.
                // 0.55 in turn measured a four-note chord at -10 dBFS median
                // across the factory bank — quiet enough to read as a fault —
                // so the scale trusts the clip a little further still.
                rackInput.dry.l *= 0.9f;
                rackInput.dry.r *= 0.9f;
                for (StereoSample &send : rackInput.send) {
                    send.l *= 0.9f;
                    send.r *= 0.9f;
                }

                // Effects run before the master trim, so moving the output
                // level does not change how hard the compressor works or how
                // loud the reverb tail sits against the dry signal.
                const StereoSample wet = effects_.processRack(rackInput);

                // The preview joins after the effects and before the master
                // trim: it is not part of the patch, so the patch's reverb and
                // compressor have no business acting on it, but the output
                // level control still has to.
                float auditionL = 0.0f, auditionR = 0.0f;
                if (auditionVoice_.isActive()) {
                    auditionVoice_.process(auditionL, auditionR);
                    if (auditionHold_ > 0 && --auditionHold_ == 0) {
                        auditionVoice_.noteOff();
                    }
                }

                const float gain = gainSmoother_.next();
                preBlockL_[i] = (wet.l + auditionL * kAuditionLevel) * gain;
                preBlockR_[i] = (wet.r + auditionR * kAuditionLevel) * gain;
            }

            // Output limiting is a gain rider with one control block of
            // lookahead rather than a per-sample waveshaper: a memoryless
            // clip at the old 0.75 knee coloured every ordinary four-note
            // chord continuously (measured at -43 dB THD at the default
            // master gain, -17 dB at fortissimo). Riding a single gain
            // leaves a held chord's waveform untouched. The output plays
            // kControlBlock samples late, so by the time a fresh peak
            // reaches the speaker the gain has already ramped down for it —
            // no overshoot to shave; the peak-hold keeps the gain from
            // rippling inside a low note's cycle, and the release is slow
            // enough that beats breathe rather than pump.
            float blockPeak = 0.0f;
            for (int i = 0; i < block; ++i) {
                blockPeak = std::max(blockPeak,
                    std::max(std::fabs(preBlockL_[i]), std::fabs(preBlockR_[i])));
            }
            if (blockPeak >= limiterEnv_) {
                limiterEnv_ = blockPeak;
                limiterHold_ = limiterHoldSamples_;
            } else if (blockPeak >= limiterEnv_ * 0.85f) {
                // A beat swell that comes back close to the held peak pins
                // the envelope where it is. Without this (and without the
                // hold outlasting a chord's slowest beat trough), the hold
                // lapses in the troughs and every re-attack is a block-fast
                // gain edge — sidebands at -41 dB, audible as a 10 Hz
                // shimmer on high chords. Held over a sustained chord, the
                // gain is simply static.
                limiterHold_ = limiterHoldSamples_;
            } else if (limiterHold_ > 0) {
                limiterHold_ -= block;
            } else {
                limiterEnv_ = blockPeak
                    + (limiterEnv_ - blockPeak) * limiterReleaseCoef_;
            }
            const float riderTarget = limiterEnv_ > kLimiterCeiling
                ? kLimiterCeiling / limiterEnv_ : 1.0f;

            for (int i = 0; i < block; ++i) {
                // Ramp across the block so a gain step never lands mid-buffer.
                // The delayed sample being played was already inside the
                // envelope's hold window when its gain was computed, so
                // outputLimit() below is a true last resort.
                const float rider = limiterGain_
                    + (riderTarget - limiterGain_) * (i + 1) / block;
                const float delayedL = limiterDelayL_[limiterDelayPos_];
                const float delayedR = limiterDelayR_[limiterDelayPos_];
                limiterDelayL_[limiterDelayPos_] = preBlockL_[i];
                limiterDelayR_[limiterDelayPos_] = preBlockR_[i];
                if (++limiterDelayPos_ == kControlBlock) limiterDelayPos_ = 0;

                const float sumL = outputLimit(delayedL * rider);
                const float sumR = outputLimit(delayedR * rider);

                outL[offset + i] = sumL;
                outR[offset + i] = sumR;
                const float magnitude = std::max(std::fabs(sumL), std::fabs(sumR));
                if (magnitude > peak) peak = magnitude;
                // The clip indicator watches the signal ENTERING the output
                // stage: above the ceiling the rider is pulling the level
                // down, which is what "limiting" means here.
                const float preMagnitude =
                    std::max(std::fabs(preBlockL_[i]), std::fabs(preBlockR_[i]));
                if (preMagnitude > prePeak) prePeak = preMagnitude;
            }
            limiterGain_ = riderTarget;
            offset += block;
        }

        meter_.store(peak, std::memory_order_relaxed);
        if (prePeak > headroomPeak_.load(std::memory_order_relaxed)) {
            headroomPeak_.store(prePeak, std::memory_order_relaxed);
        }
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
        // 0.74 is played-in, not derived: 0.9 left the all-wave synth
        // presets leaning on the output limiter on every chord.
        set(R50ParamMasterGain,      0.74f);
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
        for (int partial = 1; partial < kPartialsPerVoice; ++partial) {
            setPartial(partial, R50FieldEnabled,          0.0f);
            setPartial(partial, R50FieldSourceType,       0.0f);
            setPartial(partial, R50FieldSampleInstrument, 0.0f);
            setPartial(partial, R50FieldSampleStart,      0.0f);
            setPartial(partial, R50FieldOscWave,          0.0f);
            setPartial(partial, R50FieldPulseWidth,       defaults.pulseWidth);
            setPartial(partial, R50FieldOctave,           0.0f);
            setPartial(partial, R50FieldSemitone,         0.0f);
            setPartial(partial, R50FieldFine,             0.0f);
            setPartial(partial, R50FieldNoiseMix,         0.0f);
            setPartial(partial, R50FieldNoiseSpectrum,    0.0f);
            setPartial(partial, R50FieldNoiseTone,        0.5f);
            setPartial(partial, R50FieldNoiseRate,        4000.0f);
            setPartial(partial, R50FieldNoisePitchTrack,  0.0f);
            setPartial(partial, R50FieldCutoff,           3200.0f);
            setPartial(partial, R50FieldResonance,        0.15f);
            setPartial(partial, R50FieldSlope,            1.0f);
            setPartial(partial, R50FieldKeyTrack,         0.5f);
            setPartial(partial, R50FieldFilterEnvAmount,  0.45f);
            setPartial(partial, R50FieldAmpAttack,        0.004f);
            setPartial(partial, R50FieldAmpDecay,         0.25f);
            setPartial(partial, R50FieldAmpSustain,       0.75f);
            setPartial(partial, R50FieldAmpRelease,       0.30f);
            setPartial(partial, R50FieldFilterAttack,     0.004f);
            setPartial(partial, R50FieldFilterDecay,      0.45f);
            setPartial(partial, R50FieldFilterSustain,    0.30f);
            setPartial(partial, R50FieldFilterRelease,    0.30f);
            setPartial(partial, R50FieldLevel,            1.0f);
            setPartial(partial, R50FieldPan,              0.0f);
            setPartial(partial, R50FieldDryLevel,         1.0f);
            setPartial(partial, R50FieldSend1,            0.0f);
            setPartial(partial, R50FieldSend2,            0.0f);
            setPartial(partial, R50FieldSend3,            0.0f);
        }

        // Defaults that reduce the EG to the plain ADSR it replaces: full
        // attack level, and a slope time at the minimum so the break stage is
        // skipped entirely.
        for (int partial = 0; partial < kPartialsPerVoice; ++partial) {
            setPartial(partial, R50FieldAmpAttackLevel,    1.0f);
            setPartial(partial, R50FieldAmpBreak,          1.0f);
            setPartial(partial, R50FieldAmpSlope,          0.0f);
            setPartial(partial, R50FieldFilterAttackLevel, 1.0f);
            setPartial(partial, R50FieldFilterBreak,       1.0f);
            setPartial(partial, R50FieldFilterSlope,       0.0f);
            setPartial(partial, R50FieldPitchKeyFollow,    1.0f);
            setPartial(partial, R50FieldPitchStartLevel,   0.0f);
            setPartial(partial, R50FieldPitchAmount,       0.0f);
            setPartial(partial, R50FieldPitchAttack,       0.001f);
            setPartial(partial, R50FieldPitchDecay,        0.2f);
            setPartial(partial, R50FieldPitchRelease,      0.001f);
            setPartial(partial, R50FieldPitchReleaseLevel, 0.0f);
            setPartial(partial, R50FieldShaperType,        0.0f);
            setPartial(partial, R50FieldShaperDrive,       0.0f);
            setPartial(partial, R50FieldShaperPosition,    0.0f);
        }

        set(R50ParamToneStructure,     0.0f);   // Mix
        set(R50ParamToneRingLevel,     1.0f);
        set(R50ParamToneBlendTime,     0.25f);
        set(R50ParamToneCrossfadeLow,  48.0f);
        set(R50ParamToneCrossfadeHigh, 72.0f);

        set(R50ParamToneBStructure,     0.0f);
        set(R50ParamToneBRingLevel,     1.0f);
        set(R50ParamToneBBlendTime,     0.25f);
        set(R50ParamToneBCrossfadeLow,  48.0f);
        set(R50ParamToneBCrossfadeHigh, 72.0f);
        set(R50ParamPatchStructure,     0.0f);
        set(R50ParamPatchSplitPoint,    60.0f);
        set(R50ParamPatchVelocitySplit, 0.5f);
        set(R50ParamPatchVectorMix,     0.0f);
        set(R50ParamToneALevel,         1.0f);
        set(R50ParamToneBLevel,         1.0f);
        set(R50ParamVectorLfoWave,       0.0f);
        set(R50ParamVectorLfoRate,       0.25f);
        set(R50ParamVectorLfoDepth,      0.0f);
        set(R50ParamVectorLfoRetrigger,  0.0f);
        set(R50ParamVectorLfoPhase,      0.0f);
        set(R50ParamToneRingPan,         0.0f);
        set(R50ParamToneRingDry,         1.0f);
        set(R50ParamToneRingSend1,       0.0f);
        set(R50ParamToneRingSend2,       0.0f);
        set(R50ParamToneRingSend3,       0.0f);
        set(R50ParamToneBRingPan,        0.0f);
        set(R50ParamToneBRingDry,        1.0f);
        set(R50ParamToneBRingSend1,      0.0f);
        set(R50ParamToneBRingSend2,      0.0f);
        set(R50ParamToneBRingSend3,      0.0f);

        // The compressor is the sole processor outside the three-slot rack.
        set(R50ParamFxCompressor,    0.0f);

        // Modulation defaults to nothing routed, so an existing preset — which
        // names no slot — sounds exactly as it did.
        set(R50ParamLfo1Wave, 0.0f);  set(R50ParamLfo1Rate, 5.0f);
        set(R50ParamLfo1Delay, 0.0f); set(R50ParamLfo1Fade, 0.0f);
        set(R50ParamLfo1Retrigger, 1.0f); set(R50ParamLfo1Phase, 0.0f);
        set(R50ParamLfo2Wave, 0.0f);  set(R50ParamLfo2Rate, 0.6f);
        set(R50ParamLfo2Delay, 0.0f); set(R50ParamLfo2Fade, 0.0f);
        set(R50ParamLfo2Retrigger, 1.0f); set(R50ParamLfo2Phase, 0.0f);
        for (int slot = 0; slot < kModSlots; ++slot) {
            store_[r50ModSlotParam(slot, R50ModFieldSource)].store(0.0f);
            store_[r50ModSlotParam(slot, R50ModFieldDestination)].store(0.0f);
            store_[r50ModSlotParam(slot, R50ModFieldTarget)].store(0.0f);
            store_[r50ModSlotParam(slot, R50ModFieldAmount)].store(0.0f);
        }
        set(R50ParamMacro1, 0.0f); set(R50ParamMacro2, 0.0f);
        set(R50ParamMacro3, 0.0f); set(R50ParamMacro4, 0.0f);
        set(R50ParamVoiceMode, 0.0f);
        set(R50ParamGlideTime, 0.0f);
        set(R50ParamGlideMode, 0.0f);
        set(R50ParamGlideShape, 0.0f);

        for (int partial = 0; partial < kPartialsPerVoice; ++partial) {
            // Every source feeds the main path at unity; the main path runs
            // through the insert slots, and Off inserts are transparent. So
            // the default patch is a plain dry path, and selecting an
            // algorithm is immediately audible with no routing edit — a send
            // knob only matters once a slot is switched to Send.
            setPartial(partial, R50FieldDryLevel, 1.0f);
            setPartial(partial, R50FieldSend1, 0.0f);
            setPartial(partial, R50FieldSend2, 0.0f);
            setPartial(partial, R50FieldSend3, 0.0f);
        }
        for (int slot = 0; slot < kEffectSlotCount; ++slot) {
            set(r50FxSlotParam(slot, R50FxFieldAlgorithm), 0.0f);
            set(r50FxSlotParam(slot, R50FxFieldBypass), 0.0f);
            set(r50FxSlotParam(slot, R50FxFieldRouting), 0.0f);
            set(r50FxSlotParam(slot, R50FxFieldReserved), 0.0f);
            set(r50FxSlotParam(slot, R50FxFieldMix), 1.0f);
            set(r50FxSlotParam(slot, R50FxFieldWidth), 1.0f);
            for (int control = 0; control < 8; ++control) {
                set(r50FxSlotParam(slot, static_cast<R50FxSlotField>(
                    R50FxFieldControl1 + control)), 0.5f);
            }
            set(r50FxSlotParam(slot, R50FxFieldMode1), 0.0f);
            set(r50FxSlotParam(slot, R50FxFieldMode2), 0.0f);
        }
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

        out.ampAttackLevel    = synth::clampf(field(R50FieldAmpAttackLevel), 0.0f, 1.0f);
        out.ampBreak          = synth::clampf(field(R50FieldAmpBreak), 0.0f, 1.0f);
        out.ampSlope          = std::max(0.0f, field(R50FieldAmpSlope));
        out.filterAttackLevel = synth::clampf(field(R50FieldFilterAttackLevel), 0.0f, 1.0f);
        out.filterBreak       = synth::clampf(field(R50FieldFilterBreak), 0.0f, 1.0f);
        out.filterSlope       = std::max(0.0f, field(R50FieldFilterSlope));

        out.pitchKeyFollow = synth::clampf(field(R50FieldPitchKeyFollow), 0.0f, 2.0f);
        out.pitchStartLevel = synth::clampf(
            field(R50FieldPitchStartLevel), -24.0f, 24.0f);
        out.pitchAmount = synth::clampf(field(R50FieldPitchAmount), -24.0f, 24.0f);
        out.pitchAttack = std::max(0.0005f, field(R50FieldPitchAttack));
        out.pitchDecay  = std::max(0.0005f, field(R50FieldPitchDecay));
        out.pitchRelease = std::max(0.0005f, field(R50FieldPitchRelease));
        out.pitchReleaseLevel = synth::clampf(
            field(R50FieldPitchReleaseLevel), -24.0f, 24.0f);

        const int shaper = static_cast<int>(field(R50FieldShaperType) + 0.5f);
        out.shaperType = static_cast<ShaperType>(
            shaper < 0 ? 0 : (shaper >= kShaperTypeCount ? kShaperTypeCount - 1 : shaper));
        out.shaperDrive = synth::clampf(field(R50FieldShaperDrive), 0.0f, 1.0f);
        out.shaperPosition = field(R50FieldShaperPosition) >= 0.5f
                           ? ShaperPosition::PostFilter : ShaperPosition::PreFilter;

        out.level = synth::clampf(field(R50FieldLevel), 0.0f, 1.0f);
        out.pan   = synth::clampf(field(R50FieldPan), -1.0f, 1.0f);
        out.dryLevel = synth::clampf(field(R50FieldDryLevel), 0.0f, 1.0f);
        out.send[0] = synth::clampf(field(R50FieldSend1), 0.0f, 1.0f);
        out.send[1] = synth::clampf(field(R50FieldSend2), 0.0f, 1.0f);
        out.send[2] = synth::clampf(field(R50FieldSend3), 0.0f, 1.0f);
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
            dryLevelTarget_[i] = params_.partial[i].dryLevel;
            for (int slot = 0; slot < kEffectSlotCount; ++slot)
                sendLevelTarget_[i][slot] = params_.partial[i].send[slot];
        }

        const int structure = static_cast<int>(get(R50ParamToneStructure) + 0.5f);
        params_.structure = static_cast<ToneStructure>(
            structure < 0 ? 0
                          : (structure >= kToneStructureCount
                                 ? kToneStructureCount - 1 : structure));
        params_.ringLevel = synth::clampf(get(R50ParamToneRingLevel), 0.0f, 8.0f);
        params_.ringPan = synth::clampf(get(R50ParamToneRingPan), -1.0f, 1.0f);
        params_.blendTime = std::max(0.001f, get(R50ParamToneBlendTime));
        params_.crossfadeLow =
            static_cast<int>(std::lround(get(R50ParamToneCrossfadeLow)));
        params_.crossfadeHigh =
            static_cast<int>(std::lround(get(R50ParamToneCrossfadeHigh)));

        const int structureB =
            static_cast<int>(get(R50ParamToneBStructure) + 0.5f);
        params_.toneB.structure = static_cast<ToneStructure>(
            structureB < 0 ? 0
                           : (structureB >= kToneStructureCount
                                  ? kToneStructureCount - 1 : structureB));
        params_.toneB.ringLevel =
            synth::clampf(get(R50ParamToneBRingLevel), 0.0f, 8.0f);
        params_.toneB.ringPan =
            synth::clampf(get(R50ParamToneBRingPan), -1.0f, 1.0f);
        params_.toneB.blendTime =
            std::max(0.001f, get(R50ParamToneBBlendTime));
        params_.toneB.crossfadeLow =
            static_cast<int>(std::lround(get(R50ParamToneBCrossfadeLow)));
        params_.toneB.crossfadeHigh =
            static_cast<int>(std::lround(get(R50ParamToneBCrossfadeHigh)));

        const R50Param ringDry[kTonesPerVoice] = {
            R50ParamToneRingDry, R50ParamToneBRingDry
        };
        const R50Param ringSend[kTonesPerVoice][kEffectSlotCount] = {
            {R50ParamToneRingSend1, R50ParamToneRingSend2, R50ParamToneRingSend3},
            {R50ParamToneBRingSend1, R50ParamToneBRingSend2, R50ParamToneBRingSend3}
        };
        for (int tone = 0; tone < kTonesPerVoice; ++tone) {
            ringDryLevelTarget_[tone] =
                synth::clampf(get(ringDry[tone]), 0.0f, 1.0f);
            for (int slot = 0; slot < kEffectSlotCount; ++slot) {
                ringSendLevelTarget_[tone][slot] =
                    synth::clampf(get(ringSend[tone][slot]), 0.0f, 1.0f);
            }
        }

        const int patch =
            static_cast<int>(get(R50ParamPatchStructure) + 0.5f);
        params_.patchStructure = static_cast<PatchStructure>(
            patch < 0 ? 0 : (patch >= kPatchStructureCount
                ? kPatchStructureCount - 1 : patch));
        params_.patchSplitPoint =
            static_cast<int>(std::lround(get(R50ParamPatchSplitPoint)));
        params_.patchVelocitySplit =
            synth::clampf(get(R50ParamPatchVelocitySplit), 0.0f, 1.0f);
        params_.patchVectorMix =
            synth::clampf(get(R50ParamPatchVectorMix), 0.0f, 1.0f);
        params_.toneLevel[0] =
            synth::clampf(get(R50ParamToneALevel), 0.0f, 1.0f);
        params_.toneLevel[1] =
            synth::clampf(get(R50ParamToneBLevel), 0.0f, 1.0f);
        const int vectorWave =
            static_cast<int>(get(R50ParamVectorLfoWave) + 0.5f);
        params_.vectorLfo.wave = static_cast<synth::LFOWave>(
            vectorWave < 0 ? 0 : (vectorWave > 4 ? 4 : vectorWave));
        params_.vectorLfo.rateHz =
            std::max(0.01f, get(R50ParamVectorLfoRate));
        params_.vectorLfo.delaySeconds = 0.0f;
        params_.vectorLfo.fadeSeconds = 0.0f;
        params_.vectorLfo.retrigger =
            get(R50ParamVectorLfoRetrigger) >= 0.5f;
        params_.vectorLfo.phase =
            synth::clampf(get(R50ParamVectorLfoPhase), 0.0f, 1.0f);
        params_.vectorLfoDepth =
            synth::clampf(get(R50ParamVectorLfoDepth), 0.0f, 1.0f);

        snapshotModulation();
        compressorAmount_ = synth::clampf(get(R50ParamFxCompressor), 0.0f, 1.0f);
        gainSmoother_.setTarget(synth::clampf(get(R50ParamMasterGain), 0.0f, 1.0f));
    }

    void snapshotModulation() {
        ModParams &mod = params_.modulation;
        for (int i = 0; i < kLfoCount; ++i) {
            const R50Param base = (i == 0) ? R50ParamLfo1Wave : R50ParamLfo2Wave;
            const int wave = static_cast<int>(
                get(static_cast<R50Param>(base + 0)) + 0.5f);
            mod.lfo[i].wave = static_cast<synth::LFOWave>(
                wave < 0 ? 0 : (wave > 4 ? 4 : wave));
            mod.lfo[i].rateHz       = get(static_cast<R50Param>(base + 1));
            mod.lfo[i].delaySeconds = get(static_cast<R50Param>(base + 2));
            mod.lfo[i].fadeSeconds  = get(static_cast<R50Param>(base + 3));
            mod.lfo[i].retrigger    = get(static_cast<R50Param>(base + 4)) >= 0.5f;
            mod.lfo[i].phase        = get(static_cast<R50Param>(base + 5));
        }

        for (int slot = 0; slot < kModSlots; ++slot) {
            const auto field = [&](R50ModSlotField f) {
                return store_[r50ModSlotParam(slot, f)]
                           .load(std::memory_order_relaxed);
            };
            const int source = static_cast<int>(field(R50ModFieldSource) + 0.5f);
            const int destination =
                static_cast<int>(field(R50ModFieldDestination) + 0.5f);
            const int target = static_cast<int>(field(R50ModFieldTarget) + 0.5f);
            mod.slots[slot].source = static_cast<ModSource>(
                source < 0 ? 0 : (source >= kModSourceCount ? 0 : source));
            mod.slots[slot].destination = static_cast<ModDestination>(
                destination < 0 ? 0
                                : (destination >= kModDestinationCount ? 0 : destination));
            mod.slots[slot].target = static_cast<ModTarget>(
                target < 0 ? 0 : (target >= kModTargetCount ? 0 : target));
            mod.slots[slot].amount = synth::clampf(field(R50ModFieldAmount), -1.0f, 1.0f);
        }

        mod.macros[0] = synth::clampf(get(R50ParamMacro1), 0.0f, 1.0f);
        mod.macros[1] = synth::clampf(get(R50ParamMacro2), 0.0f, 1.0f);
        mod.macros[2] = synth::clampf(get(R50ParamMacro3), 0.0f, 1.0f);
        mod.macros[3] = synth::clampf(get(R50ParamMacro4), 0.0f, 1.0f);
    }

    Voice *findVoice(int note) {
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld() && voice.note() == note) {
                return &voice;
            }
        }
        return nullptr;
    }

    /// Pick up a pending browser preview, if one arrived since the last block.
    void serviceAudition() {
        const uint64_t packed = auditionRequest_.load(std::memory_order_acquire);
        if (packed == auditionSeen_) return;
        auditionSeen_ = packed;

        const int     instrument = static_cast<int>((packed >> 16) & 0xFFFF);
        const uint8_t note       = static_cast<uint8_t>((packed >> 8) & 0xFF);
        const uint8_t velocity   = static_cast<uint8_t>(packed & 0xFF);

        // A deliberately plain patch: one Partial, the sample straight through
        // an open filter, no shaping of any kind. What you hear is the asset.
        auditionParams_ = VoiceParams{};
        auditionParams_.structure = ToneStructure::Mix;
        PartialParams &p = auditionParams_.partial[0];
        p = PartialParams{};
        p.sourceType        = SourceType::Sample;
        p.sampleInstrument  = instrument;
        p.cutoffHz          = 20000.0f;
        p.resonance         = 0.0f;
        p.keyTrack          = 0.0f;
        p.filterEnvAmount   = 0.0f;
        p.ampAttack         = 0.002f;
        p.ampDecay          = 0.01f;
        p.ampSustain        = 1.0f;
        p.ampRelease        = 0.35f;
        auditionParams_.partial[1].enabled = false;
        auditionParams_.partial[2].enabled = false;
        auditionParams_.partial[3].enabled = false;

        // Held for a fixed time rather than by the mouse: a looped sustain
        // would otherwise drone until the user thought to stop it, and a
        // one-shot ends on its own well inside the window.
        auditionHold_ = static_cast<int>(kAuditionSeconds * sampleRate_);
        auditionVoice_.reset();
        auditionVoice_.setSampleRate(sampleRate_);
        auditionVoice_.noteOn(note, velocity / 127.0f, auditionParams_,
                              0.0f, 0.0f, 0.0f);
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

    void startVoice(Voice &voice, uint8_t note, uint8_t velocity, bool legato) {
        voice.noteOn(note, velocity / 127.0f, params_,
                     static_cast<float>(sharedLfoPhase_[0]),
                     modWheel_, aftertouch_,
                     static_cast<float>(sharedVectorPhase_));

        // Glide: the new note starts at the previously played pitch and
        // travels home over exactly the stated time — a fifth and an octave
        // both land together, which is what fingers expect — along the
        // trajectory Glide Shape picks. In Legato mode only fingered
        // transitions slide; a detached note starts on pitch, which is the
        // playable portamento of a mono lead.
        const float glideTime = get(R50ParamGlideTime);
        const bool wantsGlide = get(R50ParamGlideMode) < 0.5f || legato;
        if (glideTime > 0.001f && wantsGlide && lastPlayedNote_ >= 0
            && lastPlayedNote_ != static_cast<int>(note)) {
            const double step = kControlBlock
                              / (sampleRate_ * static_cast<double>(glideTime));
            voice.setGlide(static_cast<double>(lastPlayedNote_) - note, step,
                           synth::clampf(get(R50ParamGlideShape), 0.0f, 1.0f));
        }
        lastPlayedNote_ = note;
    }

    // ---- Monophonic voice handling ---------------------------------------
    //
    // Last-note priority with a held-key stack: a new key replaces the
    // sounding pitch, and releasing it returns to the most recent key still
    // down — the trill behaviour every mono bass and lead has taught players
    // to expect. Fixed storage; note events run on the render thread.

    Voice *monoVoice() {
        for (auto &voice : voices_) {
            if (voice.isActive() && voice.isHeld()) return &voice;
        }
        return nullptr;
    }

    void monoRemove(uint8_t note) {
        for (int i = 0; i < monoDepth_; ++i) {
            if (monoStack_[i].note != note) continue;
            for (int j = i; j + 1 < monoDepth_; ++j) {
                monoStack_[j] = monoStack_[j + 1];
            }
            --monoDepth_;
            return;
        }
    }

    void monoNoteOn(uint8_t note, uint8_t velocity, bool legato) {
        monoRemove(note);
        if (monoDepth_ == kMonoStackDepth) {   // full: forget the oldest key
            for (int i = 0; i + 1 < monoDepth_; ++i) {
                monoStack_[i] = monoStack_[i + 1];
            }
            --monoDepth_;
        }
        monoStack_[monoDepth_++] = {note, velocity};
        Voice *voice = monoVoice();
        if (voice == nullptr) voice = allocateVoice();
        startVoice(*voice, note, velocity, legato);
    }

    void monoNoteOff(uint8_t note) {
        const bool wasSounding = monoDepth_ > 0
            && monoStack_[monoDepth_ - 1].note == note;
        monoRemove(note);
        if (sustain_) return;      // CC64 keeps the gate open, as in poly
        if (!wasSounding) return;  // an inner held key released silently
        Voice *voice = monoVoice();
        if (monoDepth_ > 0) {
            // Returning to a held key is legato by definition.
            const MonoKey key = monoStack_[monoDepth_ - 1];
            if (voice == nullptr) voice = allocateVoice();
            startVoice(*voice, key.note, key.velocity, true);
        } else if (voice != nullptr) {
            voice->noteOff();
        }
    }

    // Written by the host/UI from any thread; read on the render thread.
    std::atomic<float> store_[R50ParamCount];

    // Live DSP state — render thread only.
    Voice       voices_[kNumVoices];
    VoiceParams params_;
    double sampleRate_ = 44100.0;
    float  bendNorm_   = 0.0f;
    float  modWheel_   = 0.0f;
    float  aftertouch_ = 0.0f;
    double sharedLfoPhase_[kLfoCount] = {0.0, 0.0};
    double sharedVectorPhase_ = 0.0;
    bool   sustain_    = false;
    /// Physical key state, independent of the CC64 gate hold.
    bool   keyDown_[128] = {false};

    struct MonoKey { uint8_t note; uint8_t velocity; };
    static constexpr int kMonoStackDepth = 32;
    MonoKey monoStack_[kMonoStackDepth] = {};
    int     monoDepth_ = 0;
    /// Where a glide departs from: the last pitch any voice was started at.
    int     lastPlayedNote_ = -1;

    // Sample-browser preview. The request word is the only thing crossing
    // threads; everything derived from it lives on the render thread, exactly
    // as the parameter store does.
    static constexpr double kAuditionSeconds = 1.5;
    static constexpr float  kAuditionLevel   = 0.9f;    // matches the voice sum
    std::atomic<uint64_t> auditionRequest_{0};
    std::atomic<uint64_t> auditionSequence_{0};
    uint64_t    auditionSeen_ = 0;
    Voice       auditionVoice_;
    VoiceParams auditionParams_;
    int         auditionHold_ = 0;

    GlobalEffects          effects_;
    float                  compressorAmount_ = 0.0f;
    float dryLevel_[kPartialsPerVoice] = {1.0f, 1.0f};
    float dryLevelTarget_[kPartialsPerVoice] = {1.0f, 1.0f};
    float sendLevel_[kPartialsPerVoice][kEffectSlotCount] = {};
    float sendLevelTarget_[kPartialsPerVoice][kEffectSlotCount] = {};
    float ringDryLevel_[kTonesPerVoice] = {1.0f, 1.0f};
    float ringDryLevelTarget_[kTonesPerVoice] = {1.0f, 1.0f};
    float ringSendLevel_[kTonesPerVoice][kEffectSlotCount] = {};
    float ringSendLevelTarget_[kTonesPerVoice][kEffectSlotCount] = {};
    float routingSmoothCoef_ = 0.0f;
    // Output gain rider (render thread only).
    float preBlockL_[kControlBlock] = {};
    float preBlockR_[kControlBlock] = {};
    float limiterDelayL_[kControlBlock] = {};   // the lookahead
    float limiterDelayR_[kControlBlock] = {};
    int   limiterDelayPos_ = 0;
    float limiterEnv_  = 0.0f;
    float limiterGain_ = 1.0f;
    int   limiterHold_ = 0;
    int   limiterHoldSamples_ = 2205;      // 50 ms at the default rate
    float limiterReleaseCoef_ = 0.9952f;   // 150 ms at the default rate
    synth::OnePoleSmoother gainSmoother_;
    std::atomic<float>     meter_{0.0f};
    std::atomic<float>     headroomPeak_{0.0f};
};

} // namespace r50
