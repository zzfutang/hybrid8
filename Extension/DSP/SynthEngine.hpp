//
//  SynthEngine.hpp
//  8-voice polyphonic engine. Owns the voice pool, the global LFO, the
//  lock-free parameter store, MIDI handling and the sample render loop.
//  All render-time methods are real-time safe (no locks, no allocation).
//

#pragma once
#include <atomic>
#include <array>
#include "Voice.hpp"
#include "LFO.hpp"
#include "Params.hpp"
#include "Effects.hpp"
#include "../SynthParameters.h"

namespace synth {

static constexpr int kNumVoices = 8;
// Equal-power normalisation for a full eight-voice stack.
static constexpr float kVoiceSumGain = 0.35f;

class SynthEngine {
public:
    SynthEngine() {
        // Sensible defaults so the instrument makes sound out of the box.
        store_[SynthParamOscWaveform].store(0.0f);
        store_[SynthParamOscPulseWidth].store(0.5f);
        store_[SynthParamOctave].store(0.0f);
        store_[SynthParamOsc1Level].store(1.0f);
        store_[SynthParamOsc2Level].store(0.0f);
        store_[SynthParamNoiseLevel].store(0.0f);
        store_[SynthParamAmpAttack].store(normFromTime(0.005f));
        store_[SynthParamAmpDecay].store(normFromTime(0.15f));
        store_[SynthParamAmpSustain].store(0.8f);
        store_[SynthParamAmpRelease].store(normFromTime(0.25f));
        store_[SynthParamFilterAttack].store(normFromTime(0.01f));
        store_[SynthParamFilterDecay].store(normFromTime(0.3f));
        store_[SynthParamFilterSustain].store(0.4f);
        store_[SynthParamFilterRelease].store(normFromTime(0.4f));
        store_[SynthParamFilterCutoff].store(6000.0f);
        store_[SynthParamFilterResonance].store(0.15f);
        store_[SynthParamFilterEnvAmount].store(0.5f);
        store_[SynthParamFilterSlope].store(0.0f);
        store_[SynthParamFilterKeyTrack].store(0.0f);
        store_[SynthParamLFOWaveform].store(0.0f);
        store_[SynthParamLFORate].store(5.0f);
        store_[SynthParamLFOToOscFreq].store(0.0f);
        store_[SynthParamLFOToPulseWidth].store(0.0f);
        store_[SynthParamLFOToCutoff].store(0.0f);
        store_[SynthParamLFOToResonance].store(0.0f);
        store_[SynthParamMasterGain].store(0.7f);
        store_[SynthParamAnalogAmount].store(0.3f);
        store_[SynthParamPitchBendRange].store(2.0f);
        store_[SynthParamOscPhaseSpread].store(0.5f);
        store_[SynthParamOsc2Waveform].store(0.0f);
        store_[SynthParamOsc2Octave].store(0.0f);
        store_[SynthParamOsc2Detune].store(0.0f);
        store_[SynthParamFilterDrive].store(0.0f);  // no distortion by default
        store_[SynthParamOsc2Semitone].store(0.0f);
        store_[SynthParamOsc2Sync].store(0.0f);     // sync off
        store_[SynthParamOscCrossMod].store(0.0f);  // no cross-mod
        store_[SynthParamOsc2PitchEnv].store(0.0f);
        store_[SynthParamLFOToCrossMod].store(0.0f);
        store_[SynthParamOscCrossModTZ].store(0.0f); // exponential FM
        store_[SynthParamVelToVolume].store(1.0f);   // fully velocity sensitive
        store_[SynthParamVelToCutoff].store(0.0f);
        store_[SynthParamVelToResonance].store(0.0f);
        store_[SynthParamVelToDrive].store(0.0f);
        store_[SynthParamOsc2PulseWidth].store(0.5f);
        store_[SynthParamGlideTime].store(0.0f);
        store_[SynthParamGlideStart].store(0.0f);
        store_[SynthParamLFOKeyTrigger].store(0.0f);
        store_[SynthParamLFODelay].store(0.0f);
        store_[SynthParamVoiceCount].store(static_cast<float>(kNumVoices));
        store_[SynthParamLegato].store(0.0f);
        store_[SynthParamWavetable].store(0.0f);
        store_[SynthParamWTFrame].store(0.0f);
        store_[SynthParamWTLiveness].store(0.25f);
        store_[SynthParamLFOToWTFrame].store(0.0f);
        store_[SynthParamWTFrameEnv].store(0.0f);
        store_[SynthParamLFO2Waveform].store(0.0f);
        store_[SynthParamLFO2Rate].store(2.0f);
        for (int s = 0; s < SYNTH_MOD_SLOTS; ++s) {
            store_[SynthParamMod1Source + s * 3].store(0.0f);
            store_[SynthParamMod1Dest   + s * 3].store(0.0f);
            store_[SynthParamMod1Amount + s * 3].store(0.0f);
        }
        store_[SynthParamArpOn].store(0.0f);
        store_[SynthParamArpMode].store(0.0f);      // Up
        store_[SynthParamArpOctaves].store(1.0f);
        store_[SynthParamArpRate].store(SYNTH_SYNC_DEFAULT_ARP);
        store_[SynthParamArpGate].store(0.5f);
        store_[SynthParamArpHold].store(0.0f);
        store_[SynthParamChorusMix].store(0.0f);
        store_[SynthParamChorusRate].store(SYNTH_SYNC_DEFAULT_CHORUS);
        store_[SynthParamChorusDepth].store(0.35f);
        store_[SynthParamDelayMix].store(0.0f);
        store_[SynthParamDelayTime].store(SYNTH_SYNC_DEFAULT_DELAY);
        store_[SynthParamDelayFeedback].store(0.35f);
        store_[SynthParamDelayTone].store(0.65f);
        store_[SynthParamDelayPingPong].store(1.0f);
        store_[SynthParamStereoSpread].store(0.0f);
        for (int i = 0; i < SynthParamCount; ++i)
            effective_[i].store(store_[i].load(std::memory_order_relaxed),
                                std::memory_order_relaxed);

        // Build the wavetable library up front (off the audio thread) so the
        // first note never triggers generation in the render callback.
        wtLibrary();
    }

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        lfo_.setSampleRate(sr);
        lfo2_.setSampleRate(sr);
        effects_.setup(sr);
        for (int i = 0; i < kNumVoices; ++i) {
            voices_[i].setSampleRate(sr);
            voices_[i].seed(0x1234ULL + 0x9e37U * (i + 1));
        }
        cutoffSmoother_.setSampleRate(sr);   cutoffSmoother_.setTimeConstant(8.0);
        resoSmoother_.setSampleRate(sr);     resoSmoother_.setTimeConstant(15.0);
        pwSmoother_.setSampleRate(sr);       pwSmoother_.setTimeConstant(10.0);
        pw2Smoother_.setSampleRate(sr);      pw2Smoother_.setTimeConstant(10.0);
        wtFrameSmoother_.setSampleRate(sr);  wtFrameSmoother_.setTimeConstant(20.0);
        gainSmoother_.setSampleRate(sr);     gainSmoother_.setTimeConstant(10.0);
        osc1LevelSmoother_.setSampleRate(sr);  osc1LevelSmoother_.setTimeConstant(10.0);
        osc2LevelSmoother_.setSampleRate(sr);  osc2LevelSmoother_.setTimeConstant(10.0);
        noiseLevelSmoother_.setSampleRate(sr); noiseLevelSmoother_.setTimeConstant(10.0);
        driveSmoother_.setSampleRate(sr);    driveSmoother_.setTimeConstant(12.0);
        crossModSmoother_.setSampleRate(sr); crossModSmoother_.setTimeConstant(12.0);
        slopeSmoother_.setSampleRate(sr);    slopeSmoother_.setTimeConstant(15.0);
        stereoSmoother_.setSampleRate(sr);   stereoSmoother_.setTimeConstant(15.0);
        snapSmoothers();
    }

    void setTempo(double bpm) {
        tempoBPM_ = std::min(400.0, std::max(20.0, bpm));
    }

    // --- Parameter access (thread-safe) ------------------------------------
    void setParameter(uint64_t address, float value) {
        if (address < SynthParamCount) store_[address].store(value, std::memory_order_relaxed);
    }
    float getParameter(uint64_t address) const {
        if (address < SynthParamCount) return store_[address].load(std::memory_order_relaxed);
        return 0.0f;
    }
    float getEffectiveParameter(uint64_t address) const {
        if (address < SynthParamCount)
            return effective_[address].load(std::memory_order_relaxed);
        return 0.0f;
    }

    // --- MIDI --------------------------------------------------------------
    // Public note in/out: when the arpeggiator is on, incoming keys feed the
    // arp's held-note set instead of playing directly; the arp clock (advanced
    // in render) triggers the voices.
    void noteOn(int note, int velocity) {
        if (velocity == 0) { noteOff(note); return; }
        if (store_[SynthParamArpOn].load() >= 0.5f) { arpKeyDown(note, velocity); return; }
        directNoteOn(note, velocity);
    }
    void noteOff(int note) {
        if (store_[SynthParamArpOn].load() >= 0.5f) { arpKeyUp(note); return; }
        directNoteOff(note);
    }

    void directNoteOn(int note, int velocity) {
        int maxVoices = clampInt(store_[SynthParamVoiceCount].load(), 1, kNumVoices);
        bool legato = (store_[SynthParamLegato].load() >= 0.5f) && (maxVoices == 1);

        bool wasHeld = heldCount_ > 0;
        pushHeld(note);

        // True legato (mono): a note already down -> glide to the new note
        // without retriggering the envelope.
        if (legato && wasHeld && voices_[0].isActive()) {
            voices_[0].legatoNote(note);
            lastNote_ = static_cast<float>(note);
            hasLastNote_ = true;
            return;
        }

        // Normal trigger. Where the glide starts (semitones):
        //  - fixed start offset  -> begin a set interval from the target note
        //  - start offset == 0   -> glide from the previously played note
        float phaseSpread = store_[SynthParamOscPhaseSpread].load(std::memory_order_relaxed);
        float glideStart  = store_[SynthParamGlideStart].load(std::memory_order_relaxed);
        float startPitch;
        if (std::fabs(glideStart) > 0.01f) {
            startPitch = note + glideStart;
        } else {
            startPitch = hasLastNote_ ? lastNote_ : static_cast<float>(note);
        }
        lastNote_ = static_cast<float>(note);
        hasLastNote_ = true;

        Voice& v = allocateVoice(maxVoices);
        v.noteOn(note, velocity / 127.0f, phaseSpread, startPitch);
    }

    void directNoteOff(int note) {
        removeHeld(note);
        int maxVoices = clampInt(store_[SynthParamVoiceCount].load(), 1, kNumVoices);
        bool legato = (store_[SynthParamLegato].load() >= 0.5f) && (maxVoices == 1);

        if (legato && voices_[0].isActive()) {
            if (heldCount_ > 0) {
                // Glide to the most-recently-held note (last-note priority).
                voices_[0].legatoNote(heldStack_[heldCount_ - 1]);
            } else {
                voices_[0].noteOff();
            }
            // Release any stray held voices (e.g. after a poly->mono switch).
            for (int i = 1; i < kNumVoices; ++i)
                if (voices_[i].isActive() && voices_[i].isHeld() && voices_[i].note() == note)
                    voices_[i].noteOff();
            return;
        }

        for (auto& v : voices_) {
            if (v.isActive() && v.isHeld() && v.note() == note) v.noteOff();
        }
    }

    void allNotesOff() { heldCount_ = 0; arpClear(); for (auto& v : voices_) v.noteOff(); }   // CC123
    void allSoundOff() {
        heldCount_ = 0;
        arpClear();
        for (auto& v : voices_) v.silence();
        effects_.reset(); // CC120 / host reset must also kill effect tails
    }

    // ---- Arpeggiator -----------------------------------------------------
    void arpClear() {
        arpCount_ = 0; arpPhysCount_ = 0;
        for (bool& b : arpPhysDown_) b = false;
        arpPhase_ = 0.0; arpStep_ = 0; arpGateOpen_ = false;
        if (arpCurNote_ >= 0) { directNoteOff(arpCurNote_); arpCurNote_ = -1; }
    }

    void arpKeyDown(int note, int velocity) {
        // Latch: when Hold is on and every key had been released, the next key
        // press starts a fresh chord rather than adding to the latched one.
        bool hold = store_[SynthParamArpHold].load() >= 0.5f;
        if (hold && arpPhysCount_ == 0) arpCount_ = 0;
        if (note >= 0 && note < 128 && !arpPhysDown_[note]) { arpPhysDown_[note] = true; ++arpPhysCount_; }
        arpInsert(note, velocity);
        // First note of a new chord: fire on the next clock tick immediately.
        if (arpCount_ == 1) { arpPhase_ = 1.0; arpStep_ = 0; }
    }

    void arpKeyUp(int note) {
        if (note >= 0 && note < 128 && arpPhysDown_[note]) {
            arpPhysDown_[note] = false; if (arpPhysCount_ > 0) --arpPhysCount_;
        }
        if (store_[SynthParamArpHold].load() < 0.5f) arpRemove(note);
    }

    void modWheel(float v)   { modWheel_.store(clampf(v, 0.0f, 1.0f), std::memory_order_relaxed); }
    void aftertouch(float v) { aftertouch_.store(clampf(v, 0.0f, 1.0f), std::memory_order_relaxed); }

    void pitchBend(int value14) { // 0..16383, centre 8192
        float norm = (value14 - 8192) / 8192.0f; // -1..1
        float range = store_[SynthParamPitchBendRange].load(std::memory_order_relaxed);
        pitchBendSemis_ = norm * range;
    }

    // --- Render ------------------------------------------------------------
    // Writes `frames` samples into outL / outR (outR may equal outL for mono).
    void render(float* outL, float* outR, int frames) {
        // Snapshot discrete/enum params once per block.
        Params p;
        int w1 = clampInt(store_[SynthParamOscWaveform].load(), 0, 3);
        p.osc1IsWT = (w1 == 3);
        p.oscWave  = static_cast<OscWave>(std::min(w1, 2));
        p.octave    = clampInt(store_[SynthParamOctave].load(), -4, 4);
        int w2 = clampInt(store_[SynthParamOsc2Waveform].load(), 0, 3);
        p.osc2IsWT = (w2 == 3);
        p.osc2Wave = static_cast<OscWave>(std::min(w2, 2));
        p.osc2Octave = clampInt(store_[SynthParamOsc2Octave].load(), -4, 4);
        p.wtTable      = clampInt(store_[SynthParamWavetable].load(), 0, WT_NUM_SETS - 1);
        p.wtLiveness   = store_[SynthParamWTLiveness].load();
        p.lfoToWTFrame = store_[SynthParamLFOToWTFrame].load();
        p.wtFrameEnv   = store_[SynthParamWTFrameEnv].load();
        p.osc2Semitone = store_[SynthParamOsc2Semitone].load();
        p.osc2Detune = store_[SynthParamOsc2Detune].load();
        p.osc2Sync   = store_[SynthParamOsc2Sync].load() >= 0.5f;
        p.osc2PitchEnv = store_[SynthParamOsc2PitchEnv].load();
        p.crossModTZ = store_[SynthParamOscCrossModTZ].load() >= 0.5f;
        p.lfoToCrossMod = store_[SynthParamLFOToCrossMod].load();
        // Envelope times are stored normalised 0..1 and mapped to seconds here.
        p.ampA = timeFromNorm(store_[SynthParamAmpAttack].load());
        p.ampD = timeFromNorm(store_[SynthParamAmpDecay].load());
        p.ampS = store_[SynthParamAmpSustain].load();
        p.ampR = timeFromNorm(store_[SynthParamAmpRelease].load());
        p.filtA = timeFromNorm(store_[SynthParamFilterAttack].load());
        p.filtD = timeFromNorm(store_[SynthParamFilterDecay].load());
        p.filtS = store_[SynthParamFilterSustain].load();
        p.filtR = timeFromNorm(store_[SynthParamFilterRelease].load());
        p.filterEnvAmt   = store_[SynthParamFilterEnvAmount].load();
        p.filterKeyTrack = store_[SynthParamFilterKeyTrack].load();
        p.lfoWave    = static_cast<LFOWave>(clampInt(store_[SynthParamLFOWaveform].load(), 0, 2));
        p.lfoRate    = store_[SynthParamLFORate].load();
        p.lfoKeyTrigger = store_[SynthParamLFOKeyTrigger].load() >= 0.5f;
        p.lfoDelay   = store_[SynthParamLFODelay].load();
        p.lfoToOscFreq    = store_[SynthParamLFOToOscFreq].load();
        p.lfoToPulseWidth = store_[SynthParamLFOToPulseWidth].load();
        p.lfoToCutoff     = store_[SynthParamLFOToCutoff].load();
        p.lfoToResonance  = store_[SynthParamLFOToResonance].load();
        p.analogAmount = store_[SynthParamAnalogAmount].load();
        p.masterGain = store_[SynthParamMasterGain].load();
        p.pitchBendSemis = pitchBendSemis_;
        // Glide: exponential approach reaching ~99% of the target in the set
        // time. 0 (or near) -> coefficient 1 -> instant (glide off).
        float glideT = store_[SynthParamGlideTime].load();
        p.glideCoef = (glideT < 0.001f)
                        ? 1.0f
                        : 1.0f - std::exp(-4.6f / (glideT * static_cast<float>(sampleRate_)));
        p.velToVolume    = store_[SynthParamVelToVolume].load();
        p.velToCutoff    = store_[SynthParamVelToCutoff].load();
        p.velToResonance = store_[SynthParamVelToResonance].load();
        p.velToDrive     = store_[SynthParamVelToDrive].load();

        // Modulation matrix snapshot + global mod sources.
        for (int s = 0; s < SYNTH_MOD_SLOTS; ++s) {
            p.modSource[s] = clampInt(store_[SynthParamMod1Source + s * 3].load(), 0, ModSrcCount - 1);
            p.modDest[s]   = clampInt(store_[SynthParamMod1Dest   + s * 3].load(), 0, ModDstCount - 1);
            p.modAmount[s] = store_[SynthParamMod1Amount + s * 3].load();
        }
        p.modWheel   = modWheel_.load(std::memory_order_relaxed);
        p.aftertouch = aftertouch_.load(std::memory_order_relaxed);

        lfo_.setWave(p.lfoWave);
        lfo_.setRate(p.lfoRate);
        lfo2_.setWave(static_cast<LFOWave>(clampInt(store_[SynthParamLFO2Waveform].load(), 0, 2)));
        lfo2_.setRate(store_[SynthParamLFO2Rate].load());
        const double chorusSeconds = syncSeconds(store_[SynthParamChorusRate].load());
        const double delaySeconds = syncSeconds(store_[SynthParamDelayTime].load());
        effects_.setParams(store_[SynthParamChorusMix].load(),
                           static_cast<float>(1.0 / chorusSeconds),
                           store_[SynthParamChorusDepth].load(),
                           store_[SynthParamDelayMix].load(),
                           static_cast<float>(delaySeconds),
                           store_[SynthParamDelayFeedback].load(),
                           store_[SynthParamDelayTone].load(),
                           store_[SynthParamDelayPingPong].load());

        // Smoothed continuous params: set targets, ramp per sample.
        cutoffSmoother_.setTarget(store_[SynthParamFilterCutoff].load());
        resoSmoother_.setTarget(store_[SynthParamFilterResonance].load());
        pwSmoother_.setTarget(store_[SynthParamOscPulseWidth].load());
        pw2Smoother_.setTarget(store_[SynthParamOsc2PulseWidth].load());
        wtFrameSmoother_.setTarget(store_[SynthParamWTFrame].load());
        gainSmoother_.setTarget(store_[SynthParamMasterGain].load());
        osc1LevelSmoother_.setTarget(store_[SynthParamOsc1Level].load());
        osc2LevelSmoother_.setTarget(store_[SynthParamOsc2Level].load());
        noiseLevelSmoother_.setTarget(store_[SynthParamNoiseLevel].load());
        driveSmoother_.setTarget(store_[SynthParamFilterDrive].load());
        crossModSmoother_.setTarget(store_[SynthParamOscCrossMod].load());
        slopeSmoother_.setTarget(store_[SynthParamFilterSlope].load());
        stereoSmoother_.setTarget(store_[SynthParamStereoSpread].load());

        // Update envelope times on active voices once per block.
        for (auto& v : voices_) v.updateEnvelopes(p);

        // Arpeggiator settings for this block.
        bool   arpOn   = store_[SynthParamArpOn].load() >= 0.5f;
        int    arpMode = clampInt(store_[SynthParamArpMode].load(), 0, 3);
        int    arpOct  = clampInt(store_[SynthParamArpOctaves].load(), 1, 4);
        double arpStepInc = 1.0 / (syncSeconds(store_[SynthParamArpRate].load()) * sampleRate_);
        double arpGateFrac = clampf(store_[SynthParamArpGate].load(), 0.05f, 1.0f);
        if (arpOn != arpWasOn_) {                       // arp switched on/off
            arpWasOn_ = arpOn;
            if (arpCurNote_ >= 0) { directNoteOff(arpCurNote_); arpCurNote_ = -1; }
            arpPhase_ = (arpOn && arpCount_ > 0) ? 1.0 : 0.0;   // fire at once if notes held
            arpStep_ = 0; arpGateOpen_ = false;
            if (!arpOn) { arpCount_ = 0; arpPhysCount_ = 0; for (bool& b : arpPhysDown_) b = false; }
        }

        for (int n = 0; n < frames; ++n) {
            if (arpOn) arpTick(arpStepInc, arpGateFrac, arpMode, arpOct);
            p.cutoff     = cutoffSmoother_.next();
            p.resonance  = resoSmoother_.next();
            p.pulseWidth = pwSmoother_.next();
            p.osc2PulseWidth = pw2Smoother_.next();
            p.wtFrame    = wtFrameSmoother_.next();
            p.osc1Level  = osc1LevelSmoother_.next();
            p.osc2Level  = osc2LevelSmoother_.next();
            p.noiseLevel = noiseLevelSmoother_.next();
            p.filterDrive = driveSmoother_.next();
            p.crossMod   = crossModSmoother_.next();
            p.filterSlopeMix = slopeSmoother_.next();
            float stereoSpread = stereoSmoother_.next();
            float gain   = gainSmoother_.next();

            float lfoVal  = lfo_.process();
            float lfo2Val = lfo2_.process();

            // Physical-style voice-card panning. The fixed, alternating
            // positions keep successive notes balanced across the stage.
            static constexpr float voicePan[kNumVoices] = {
                -1.0f, 1.0f, -0.714f, 0.714f,
                -0.429f, 0.429f, -0.143f, 0.143f
            };
            float mixL = 0.0f, mixR = 0.0f;
            for (int i = 0; i < kNumVoices; ++i) {
                Voice& v = voices_[i];
                if (!v.isActive()) continue;
                const float sample = v.render(p, lfoVal, lfo2Val);
                const float pan = voicePan[i] * stereoSpread;
                const float angle =
                    (pan + 1.0f) * static_cast<float>(kPi * 0.25);
                // sqrt(2) keeps the existing centre position at unity per
                // channel while maintaining constant total stereo power.
                mixL += sample * std::cos(angle) * 1.41421356f;
                mixR += sample * std::sin(angle) * 1.41421356f;
            }
            // Equal-power scaling for eight voices, followed by a bounded soft
            // ceiling for correlated stacks and resonant transients.
            StereoSample fx = effects_.process(mixL * kVoiceSumGain,
                                               mixR * kVoiceSumGain);
            outL[n] = softClip(fx.l) * gain;
            if (outR != outL) outR[n] = softClip(fx.r) * gain;
        }

        // Publish one representative voice once per block. Atomics keep this
        // audio-thread write lock-free while the editor polls independently.
        const Voice* displayVoice = nullptr;
        for (const auto& voice : voices_) {
            if (voice.isActive()) { displayVoice = &voice; break; }
        }
        for (int i = 0; i < SynthParamCount; ++i)
            effective_[i].store(store_[i].load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
        if (displayVoice) {
            const VoiceTelemetry& t = displayVoice->telemetry();
            effective_[SynthParamOctave].store(t.osc1Octave);
            effective_[SynthParamOsc2Octave].store(t.osc2Octave);
            effective_[SynthParamOscPulseWidth].store(t.pulseWidth);
            effective_[SynthParamOsc2PulseWidth].store(t.pulseWidth);
            effective_[SynthParamWTFrame].store(t.wtFrame);
            effective_[SynthParamWTLiveness].store(t.wtLiveness);
            effective_[SynthParamOscCrossMod].store(t.crossMod);
            effective_[SynthParamFilterCutoff].store(t.cutoff);
            effective_[SynthParamFilterResonance].store(t.resonance);
            effective_[SynthParamFilterDrive].store(t.drive);
            effective_[SynthParamMasterGain].store(t.amplitude);
        }
    }

private:
    // Allocate within the first `maxVoices` voices only (1 = monophonic).
    Voice& allocateVoice(int maxVoices) {
        // 1) An idle voice.
        for (int i = 0; i < maxVoices; ++i) if (!voices_[i].isActive()) return voices_[i];
        // 2) Prefer stealing a released (not held) voice — oldest first.
        Voice* best = nullptr;
        for (int i = 0; i < maxVoices; ++i) {
            Voice& v = voices_[i];
            if (!v.isHeld()) {
                if (!best || v.age() > best->age()) best = &v;
            }
        }
        if (best) return *best;
        // 3) Otherwise steal the oldest voice in range.
        best = &voices_[0];
        for (int i = 0; i < maxVoices; ++i)
            if (voices_[i].age() > best->age()) best = &voices_[i];
        return *best;
    }

    static int clampInt(float f, int lo, int hi) {
        int i = static_cast<int>(std::lround(f));
        return std::min(hi, std::max(lo, i));
    }

    // Ordered stack of currently-held notes (for mono legato / last-note priority).
    void pushHeld(int note) {
        if (heldCount_ < 128) heldStack_[heldCount_++] = note;
    }
    void removeHeld(int note) {
        for (int i = heldCount_ - 1; i >= 0; --i) {
            if (heldStack_[i] == note) {
                for (int j = i; j < heldCount_ - 1; ++j) heldStack_[j] = heldStack_[j + 1];
                --heldCount_;
                return;
            }
        }
    }

    // ---- Arpeggiator internals -------------------------------------------
    void arpInsert(int note, int vel) {
        for (int i = 0; i < arpCount_; ++i) {           // already present -> refresh velocity
            if (arpNotes_[i] == note) { arpVels_[i] = vel; return; }
        }
        if (arpCount_ >= kArpMax) return;
        int i = arpCount_ - 1;                           // keep ascending order
        while (i >= 0 && arpNotes_[i] > note) { arpNotes_[i + 1] = arpNotes_[i]; arpVels_[i + 1] = arpVels_[i]; --i; }
        arpNotes_[i + 1] = note; arpVels_[i + 1] = vel; ++arpCount_;
    }
    void arpRemove(int note) {
        for (int i = 0; i < arpCount_; ++i) {
            if (arpNotes_[i] == note) {
                for (int j = i; j < arpCount_ - 1; ++j) { arpNotes_[j] = arpNotes_[j + 1]; arpVels_[j] = arpVels_[j + 1]; }
                --arpCount_;
                return;
            }
        }
    }
    // Position in the expanded (note x octave) list for the current step.
    int arpPatternIndex(int mode, int L) {
        if (L <= 1) return 0;
        long s = arpStep_;
        switch (mode) {
            case 1: return (int)(L - 1 - (s % L));                    // Down
            case 2: { long period = 2 * L - 2;                       // Up/Down (ping-pong)
                      long m = s % period; return (int)((m < L) ? m : (period - m)); }
            case 3: { arpRng_ = arpRng_ * 1664525u + 1013904223u;    // Random
                      return (int)((arpRng_ >> 9) % (uint32_t)L); }
            default: return (int)(s % L);                            // Up
        }
    }
    inline void arpTick(double stepInc, double gateFrac, int mode, int octaves) {
        if (arpCount_ == 0) {                            // nothing held
            if (arpCurNote_ >= 0) { directNoteOff(arpCurNote_); arpCurNote_ = -1; }
            arpGateOpen_ = false;
            return;
        }
        // Close the gate partway through the step (staccato).
        if (arpGateOpen_ && arpPhase_ >= gateFrac) {
            if (arpCurNote_ >= 0) directNoteOff(arpCurNote_);
            arpGateOpen_ = false;
        }
        arpPhase_ += stepInc;
        if (arpPhase_ >= 1.0) {                          // next step
            arpPhase_ -= 1.0;
            if (arpCurNote_ >= 0) directNoteOff(arpCurNote_);
            int L = arpCount_ * octaves;
            int idx = arpPatternIndex(mode, L);
            int note = arpNotes_[idx % arpCount_] + 12 * (idx / arpCount_);
            note = std::min(127, std::max(0, note));
            directNoteOn(note, arpVels_[idx % arpCount_]);
            arpCurNote_ = note;
            arpGateOpen_ = true;
            ++arpStep_;
        }
    }

    void snapSmoothers() {
        cutoffSmoother_.snap(store_[SynthParamFilterCutoff].load());
        resoSmoother_.snap(store_[SynthParamFilterResonance].load());
        pwSmoother_.snap(store_[SynthParamOscPulseWidth].load());
        pw2Smoother_.snap(store_[SynthParamOsc2PulseWidth].load());
        wtFrameSmoother_.snap(store_[SynthParamWTFrame].load());
        gainSmoother_.snap(store_[SynthParamMasterGain].load());
        osc1LevelSmoother_.snap(store_[SynthParamOsc1Level].load());
        osc2LevelSmoother_.snap(store_[SynthParamOsc2Level].load());
        noiseLevelSmoother_.snap(store_[SynthParamNoiseLevel].load());
        driveSmoother_.snap(store_[SynthParamFilterDrive].load());
        crossModSmoother_.snap(store_[SynthParamOscCrossMod].load());
        slopeSmoother_.snap(store_[SynthParamFilterSlope].load());
        stereoSmoother_.snap(store_[SynthParamStereoSpread].load());
    }

    double syncSeconds(float rawIndex) const {
        // Duration of each division in quarter-note beats.
        static constexpr double beats[SYNTH_SYNC_DIVISION_COUNT] = {
            4.0, 3.0, 2.0, 4.0 / 3.0,
            1.5, 1.0, 2.0 / 3.0,
            0.75, 0.5, 1.0 / 3.0,
            0.375, 0.25, 1.0 / 6.0, 0.125
        };
        const int index = clampInt(rawIndex, 0, SYNTH_SYNC_DIVISION_COUNT - 1);
        return beats[index] * 60.0 / tempoBPM_;
    }

    double sampleRate_ = 44100.0;
    double tempoBPM_ = 120.0;
    std::array<Voice, kNumVoices> voices_;
    LFO lfo_;
    LFO lfo2_;
    GlobalEffects effects_;
    std::atomic<float> modWheel_{0.0f};
    std::atomic<float> aftertouch_{0.0f};

    OnePoleSmoother cutoffSmoother_, resoSmoother_, pwSmoother_, gainSmoother_;
    OnePoleSmoother pw2Smoother_, wtFrameSmoother_;
    OnePoleSmoother osc1LevelSmoother_, osc2LevelSmoother_, noiseLevelSmoother_;
    OnePoleSmoother driveSmoother_, crossModSmoother_, slopeSmoother_;
    OnePoleSmoother stereoSmoother_;
    float pitchBendSemis_ = 0.0f;
    float lastNote_ = 60.0f;
    bool  hasLastNote_ = false;
    int   heldStack_[128] = {0};
    int   heldCount_ = 0;

    // Arpeggiator state.
    static constexpr int kArpMax = 16;
    int      arpNotes_[kArpMax] = {0};
    int      arpVels_[kArpMax]  = {0};
    int      arpCount_ = 0;
    bool     arpPhysDown_[128] = {false};
    int      arpPhysCount_ = 0;
    double   arpPhase_ = 0.0;
    long     arpStep_  = 0;
    int      arpCurNote_ = -1;
    bool     arpGateOpen_ = false;
    bool     arpWasOn_ = false;
    uint32_t arpRng_ = 0x9e3779b9u;

    std::atomic<float> store_[SynthParamCount];
    std::atomic<float> effective_[SynthParamCount];
};

} // namespace synth
