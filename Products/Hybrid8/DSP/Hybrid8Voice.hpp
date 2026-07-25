//
//  Hybrid8Voice.hpp — Hybrid8 product voice composition
//  A single synth voice: oscillator + noise blend -> analogue-modelled
//  low-pass filter -> VCA. Two independent ADSR envelopes drive the VCA and
//  the filter cutoff. Per-voice pitch drift and phase randomisation give the
//  subtle instability of stacked analogue voices.
//

#pragma once
#include "../../../Shared/DSPCore/Oscillator.hpp"
#include "Hybrid8Wavetable.hpp"
#include "../../../Shared/DSPCore/ADSR.hpp"
#include "../../../Shared/DSPCore/Filter.hpp"
#include "../../../Shared/DSPCore/Decimator.hpp"
#include "Hybrid8VoiceParameters.hpp"

namespace synth {

// Fixed component tolerances for one physical-style VCF voice card. Values are
// deterministic for a given voice seed, so sessions and offline bounces remain
// repeatable. Summing three uniform draws gives a compact bell-shaped spread:
// most filters cluster near nominal and very few reach the stated limits.
struct VCFTolerance {
    float cutoffCents = 0.0f;       // maximum +/-18 cents at Analog = 1
    float resonanceScale = 0.0f;    // maximum +/-3%
    float saturationScale = 0.0f;   // maximum +/-4%

    static VCFTolerance fromSeed(uint64_t seed) {
        FastRandom random(seed ^ 0xd6e8feb86659fd93ULL);
        auto normalish = [&]() {
            return (random.nextBipolar() + random.nextBipolar()
                  + random.nextBipolar()) / 3.0f;
        };
        return {normalish() * 18.0f,
                normalish() * 0.03f,
                normalish() * 0.04f};
    }
};

struct VoiceTelemetry {
    float osc1Octave = 0.0f, osc2Octave = 0.0f;
    float pulseWidth = 0.5f, osc2PulseWidth = 0.5f;
    float wtFrame = 0.0f, wtLiveness = 0.0f;
    float crossMod = 0.0f, cutoff = 6000.0f;
    float resonance = 0.0f, drive = 0.0f, amplitude = 1.0f;
    float osc1Level = 1.0f, osc2Level = 0.0f, noiseLevel = 0.0f;
    float filterSlope = 0.0f, filterMode = 0.0f, panOffset = 0.0f;
};

class Voice {
public:
    static constexpr int kOversample = 2; // oscillator/FM/sync run at 2x

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        // Oscillators and the complete nonlinear filter path run oversampled.
        // The decimator band-limits their combined output before returning to
        // the host rate.
        osc_.setSampleRate(sr * kOversample);
        osc2_.setSampleRate(sr * kOversample);
        wtOsc1_.setSampleRate(sr * kOversample);
        wtOsc2_.setSampleRate(sr * kOversample);
        wtLibrary();           // force factory construction off the audio thread
        decimator_.setup(sr);
        ampEnv_.setSampleRate(sr);
        filtEnv_.setSampleRate(sr);
        filter_.setSampleRate(sr * kOversample);
        lfoLocal_.setSampleRate(sr);
        lfo2Local_.setSampleRate(sr);
        lfo3Local_.setSampleRate(sr);
        lfoFadeSamples_ = static_cast<int>(sr * 0.005); // 5 ms click-free start
        // Drift updated ~ every 32 samples; convert to a slow random-walk step.
        driftInc_ = 32.0;
    }

    void seed(uint64_t s) {
        rng_ = FastRandom(s);
        vcfTolerance_ = VCFTolerance::fromSeed(s);
    }

    bool  isActive() const { return pendingNote_ || ampEnv_.isActive(); }
    int   note() const { return pendingNote_ ? pendingNoteNumber_ : note_; }
    bool  isHeld() const { return held_; }
    uint64_t age() const { return age_; }
    const VoiceTelemetry& telemetry() const { return telemetry_; }
    float panModulation() const { return telemetry_.panOffset; }

    void noteOn(int note, float velocity, float phaseSpread, float startPitch,
                float tuningOffsetSemis = 0.0f, float voiceGain = 1.0f) {
        if (ampEnv_.isActive()) {
            // Voice steal: preserve the old voice for a very short fade before
            // resetting any discontinuous oscillator/filter state.
            pendingNote_ = true;
            pendingNoteNumber_ = note;
            pendingVelocity_ = velocity;
            pendingPhaseSpread_ = phaseSpread;
            pendingStartPitch_ = startPitch;
            pendingTuningOffsetSemis_ = tuningOffsetSemis;
            pendingVoiceGain_ = voiceGain;
            pendingHeld_ = true;
            held_ = true;
            age_ = 0;
            stealFadeTotal_ = std::max(1, static_cast<int>(sampleRate_ * 0.003));
            stealFadeRemaining_ = stealFadeTotal_;
            return;
        }
        startNote(note, velocity, phaseSpread, startPitch, tuningOffsetSemis,
                  voiceGain, true);
    }

    void startNote(int note, float velocity, float phaseSpread, float startPitch,
                   float tuningOffsetSemis, float voiceGain,
                   bool resetEnvelopes) {
        note_ = note;
        targetPitch_ = static_cast<float>(note);
        glidePitch_ = startPitch;   // where the glide begins (semitones)
        velocity_ = velocity;
        tuningOffsetSemis_ = tuningOffsetSemis;
        voiceGain_ = voiceGain;
        held_ = true;
        age_ = 0;
        // Per-voice start-phase randomisation ("un-sync"), independently
        // controllable. At 0 every voice starts phase-aligned (hard sync).
        osc_.reset(rng_.nextUnipolar() * phaseSpread);
        osc2_.reset(rng_.nextUnipolar() * phaseSpread);
        // Wavetable oscillators: random start phase and liveness offset per voice.
        wtOsc1_.reset(rng_.nextUnipolar() * phaseSpread, rng_.nextUnipolar());
        wtOsc2_.reset(rng_.nextUnipolar() * phaseSpread, rng_.nextUnipolar());
        decimator_.reset();
        filter_.reset();
        lfoLocal_.reset();      // start LFO phase at 0 (used when key-triggered)
        lfo2Local_.reset();
        lfo3Local_.reset();
        lfoElapsed_ = 0;        // (re)start the LFO delay from this key press
        if (resetEnvelopes) {
            ampEnv_.resetHard();
            filtEnv_.resetHard();
        }
        ampEnv_.gate(true);
        filtEnv_.gate(true);
        drift_ = 0.0f;
        drift2_ = 0.0f;
        random_ = rng_.nextBipolar();   // per-note sample & hold for the matrix
    }

    void noteOff() {
        if (pendingNote_) pendingHeld_ = false;
        held_ = false;
        ampEnv_.gate(false);
        filtEnv_.gate(false);
    }

    // Immediate silence for MIDI "All Sound Off" (CC120): hard-reset the
    // envelopes, filter and decimator so the voice stops now rather than
    // continuing through a (possibly long) release stage.
    void silence() {
        held_ = false;
        ampEnv_.resetHard();
        filtEnv_.resetHard();
        filter_.reset();
        decimator_.reset();
        pendingNote_ = false;
        stealFadeRemaining_ = 0;
    }

    // Legato: retarget the pitch (glides from the current pitch) WITHOUT
    // re-gating the envelopes or resetting the oscillators.
    void legatoNote(int note) {
        note_ = note;
        targetPitch_ = static_cast<float>(note);
        held_ = true;
    }

    // Apply per-envelope times etc. from the current param snapshot.
    void updateEnvelopes(const Params& p) {
        ampEnv_.setAttack(p.ampA);  ampEnv_.setDecay(p.ampD);
        ampEnv_.setSustain(p.ampS); ampEnv_.setRelease(p.ampR);
        filtEnv_.setAttack(p.filtA);  filtEnv_.setDecay(p.filtD);
        filtEnv_.setSustain(p.filtS); filtEnv_.setRelease(p.filtR);
    }

    // Render one sample. globalLfo is the free-running LFO from the engine,
    // used when key-trigger is off.
    inline float render(const Params& p, float globalLfo, float globalLfo2,
                        float globalLfo3) {
        if (pendingNote_ && stealFadeRemaining_ <= 0) {
            const int note = pendingNoteNumber_;
            const float velocity = pendingVelocity_;
            const float phaseSpread = pendingPhaseSpread_;
            const float startPitch = pendingStartPitch_;
            const float tuningOffset = pendingTuningOffsetSemis_;
            const float voiceGain = pendingVoiceGain_;
            const bool shouldHold = pendingHeld_;
            pendingNote_ = false;
            startNote(note, velocity, phaseSpread, startPitch, tuningOffset,
                      voiceGain, true);
            if (!shouldHold) noteOff();
        }
        ++age_;

        // --- Per-voice analogue pitch drift (two independent random walks so
        // the oscillators beat against each other for analogue thickness) ----
        if (--driftCounter_ <= 0) {
            driftCounter_ = 32;
            drift_  += rng_.nextBipolar() * 0.0015f;
            drift2_ += rng_.nextBipolar() * 0.0015f;
            drift_  = clampf(drift_,  -0.06f, 0.06f);
            drift2_ = clampf(drift2_, -0.06f, 0.06f);
        }

        // Filter envelope is advanced once here and reused for both the
        // oscillator-2 pitch modulation and the filter cutoff below. The amp
        // envelope is advanced here too so it can act as a modulation source.
        float env    = filtEnv_.process();
        float ampVal = ampEnv_.process();

        // --- Glide (portamento): move the pitch toward the target note ------
        glidePitch_ += (targetPitch_ - glidePitch_) * p.glideCoef;

        // --- LFO (per-voice): optional key-trigger reset + delayed fade-in ---
        auto delayedAmplitude = [&](float seconds) {
            const int delaySamples = static_cast<int>(seconds * sampleRate_);
            if (lfoElapsed_ < delaySamples) return 0.0f;
            return lfoFadeSamples_ > 0
                ? clampf(static_cast<float>(lfoElapsed_ - delaySamples)
                         / static_cast<float>(lfoFadeSamples_), 0.0f, 1.0f)
                : 1.0f;
        };
        // Each LFO: Loop (0) uses the shared free-running engine LFO; Trig (1)
        // and One-Shot (2) use a per-voice copy reset on this key press (Trig
        // loops, One-Shot plays a single cycle then holds).
        auto voiceLfo = [&](LFO& local, int mode, LFOWave wave, double rate,
                            bool uni, float phase, float globalVal, float delaySec) -> float {
            int delaySamp = static_cast<int>(delaySec * sampleRate_);
            float amp = delayedAmplitude(delaySec);
            float src;
            if (mode >= 1) {
                local.setWave(wave); local.setRate(rate);
                local.setPolarity(uni); local.setPhase(phase);
                local.setOneShot(mode == 2);
                // Hold at the reset phase until the delay elapses, then run.
                src = (lfoElapsed_ >= delaySamp) ? local.process() : 0.0f;
            } else {
                src = globalVal;    // free-running, shared across voices
            }
            return src * amp;
        };
        float lfo  = voiceLfo(lfoLocal_,  p.lfo1Mode, p.lfoWave,  p.lfoRate,
                              p.lfo1Unipolar, p.lfo1Phase, globalLfo,  p.lfoDelay);
        float lfo2 = voiceLfo(lfo2Local_, p.lfo2Mode, p.lfo2Wave, p.lfo2Rate,
                              p.lfo2Unipolar, p.lfo2Phase, globalLfo2, p.lfo2Delay);
        float lfo3 = voiceLfo(lfo3Local_, p.lfo3Mode, p.lfo3Wave, p.lfo3Rate,
                              p.lfo3Unipolar, p.lfo3Phase, globalLfo3, p.lfo3Delay);
        if (lfoElapsed_ < (1 << 30)) ++lfoElapsed_;

        // --- Modulation matrix: gather the sources, then accumulate each
        //     slot's (source * amount) onto its destination. Applied below at
        //     the matching point in the signal path. --------------------------
        float srcv[ModSrcCount];
        srcv[ModSrcNone]       = 0.0f;
        srcv[ModSrcLFO1]       = lfo;
        srcv[ModSrcLFO2]       = lfo2;
        srcv[ModSrcFilterEnv]  = env;
        srcv[ModSrcAmpEnv]     = ampVal;
        srcv[ModSrcVelocity]   = velocity_;
        srcv[ModSrcKeyTrack]   = (note_ - 60) / 48.0f;   // ~ -1 .. +1 over range
        srcv[ModSrcModWheel]   = p.modWheel;
        srcv[ModSrcAftertouch] = p.aftertouch;
        srcv[ModSrcRandom]     = random_;                // per-note S&H, -1..1
        srcv[ModSrcLFO3]       = lfo3;
        float md[ModDstCount] = {0.0f};
        bool matrixLfo1ToWTFrame = false;
        bool matrixFilterEnvToWTFrame = false;
        for (int s = 0; s < SYNTH_MOD_SLOTS; ++s) {
            int sr = p.modSource[s], ds = p.modDest[s];
            if (sr > 0 && ds > 0) {
                md[ds] += srcv[sr] * p.modAmount[s];
                if (ds == ModDstWTFrame) {
                    matrixLfo1ToWTFrame |= sr == ModSrcLFO1;
                    matrixFilterEnvToWTFrame |= sr == ModSrcFilterEnv;
                }
            }
        }

        // --- Shared pitch modulation: octave + bend + vibrato(LFO) + matrix --
        const float vibratoSource =
            p.vibratoLFO == 1 ? lfo2 : (p.vibratoLFO == 2 ? lfo3 : lfo);
        const float vibrato =
            vibratoSource * p.lfoToOscFreq * 2.0f; // up to +/-2 semis
        const double baseSemis = static_cast<double>(glidePitch_)
                               + tuningOffsetSemis_
                               + p.pitchBendSemis
                               + vibrato
                               + md[ModDstOscPitch] * 12.0;   // matrix -> pitch

        // Oscillator 1 — the CARRIER: its pitch tracks the played note, and it
        // is the oscillator that cross-mod (FM) and hard sync act on. Because
        // the carrier sets the perceived pitch, tuning osc 2 changes timbre,
        // not pitch.
        double semis1 = baseSemis + p.octave * 12.0
                      + md[ModDstOsc1Pitch] * 12.0
                      + drift_ * p.analogAmount;          // +/-6 cents at analog=1
        double baseF1 = noteToHz(semis1);
        osc_.setWave(p.oscWave);

        // Oscillator 2 — the MODULATOR / sync master reference. Its octave +
        // semitone + detune (+ pitch env) set the FM ratio / sync timbre.
        double semis2 = baseSemis + p.osc2Octave * 12.0
                      + std::round(p.osc2Semitone)        // coarse semitones
                      + p.osc2Detune * 0.01               // cents -> semitones
                      + drift2_ * p.analogAmount
                      + p.osc2PitchEnv * env * 36.0       // env -> +/-3 octaves
                      + md[ModDstOsc2Pitch] * 36.0;       // matrix -> osc2 pitch
        double baseF2 = noteToHz(semis2);
        osc2_.setWave(p.osc2Wave);
        osc2_.setFrequency(baseF2);                       // modulator: own freq

        // --- Pulse width (each oscillator has its own; LFO PWM hits both) ---
        float pwm = lfo * p.lfoToPulseWidth * 0.45f
                  + md[ModDstPulseWidth] * 0.45f;
        const float pw1 = clampf(p.pulseWidth + pwm
                               + md[ModDstOsc1PW] * 0.45f, 0.02f, 0.98f);
        const float pw2 = clampf(p.osc2PulseWidth + pwm
                               + md[ModDstOsc2PW] * 0.45f, 0.02f, 0.98f);
        osc_.setPulseWidth(pw1);
        osc2_.setPulseWidth(pw2);

        // --- Wavetable setup (per-osc; frequency is constant across the block
        //     because a wavetable oscillator does not support FM). The morph
        //     frame is modulated by the LFO and the filter envelope. ---------
        // The two fixed WT-frame routes predate the modulation matrix and are
        // retained for old sessions. An explicit equivalent matrix route takes
        // precedence so a hidden legacy amount cannot be summed accidentally.
        const float legacyLfoFrame =
            matrixLfo1ToWTFrame ? 0.0f : lfo * p.lfoToWTFrame;
        const float legacyEnvFrame =
            matrixFilterEnvToWTFrame ? 0.0f : env * p.wtFrameEnv;
        const float wtFrameMod = clampf(p.wtFrame
                                        + legacyLfoFrame
                                        + legacyEnvFrame
                                        + md[ModDstWTFrame], 0.0f, 1.0f);
        const float wtLivenessMod = clampf(p.wtLiveness + md[ModDstWTLiveness],
                                           0.0f, 1.0f);
        if (p.osc1IsWT) {
            wtOsc1_.setTable(wtTableAt(p.wtTable));
            wtOsc1_.setFrame(wtFrameMod);
            wtOsc1_.setLiveness(wtLivenessMod);
            wtOsc1_.setFrequency(baseF1);
        }
        if (p.osc2IsWT) {
            wtOsc2_.setTable(wtTableAt(p.wtTable));
            wtOsc2_.setFrame(wtFrameMod);
            wtOsc2_.setLiveness(wtLivenessMod);
            wtOsc2_.setFrequency(baseF2);
        }

        // Cross-mod (FM) and hard sync require both oscillators to be analog;
        // a wavetable oscillator supports neither.
        const bool analogPair = !p.osc1IsWT && !p.osc2IsWT;
        const float cm = clampf(p.crossMod + lfo * p.lfoToCrossMod
                                + md[ModDstCrossMod], 0.0f, 1.0f);
        const bool  fm = analogPair && cm > 0.0001f;
        const bool  doSync = analogPair && p.osc2Sync;

        // --- Filter cutoff modulation (envelope + keytrack + LFO + matrix) --
        float envOct  = p.filterEnvAmt * env * 6.0f;                 // +/-6 oct range
        float lfoOct  = lfo * p.lfoToCutoff * 4.0f;                  // +/-4 oct
        float keyOct  = p.filterKeyTrack * (note_ - 60) / 12.0f;     // 1 oct / octave
        float velOct  = p.velToCutoff * velocity_ * 5.0f;            // velocity opens filter
        float matOct  = md[ModDstCutoff] * 4.0f;                     // matrix -> cutoff
        double cutoff = p.cutoff * std::pow(2.0, envOct + lfoOct + keyOct + velOct + matOct);
        // Each voice card has a fixed, subtle VCF calibration. Analog controls
        // how much of the component spread is heard; at zero every voice is
        // mathematically identical.
        const float analog = clampf(p.analogAmount, 0.0f, 1.0f);
        cutoff *= std::exp2(static_cast<double>(vcfTolerance_.cutoffCents * analog)
                            / 1200.0);
        // Keep the audible cutoff below the host Nyquist even though the
        // internal filter itself runs at twice the host sample rate.
        cutoff = clampf(static_cast<float>(cutoff), 20.0f,
                        static_cast<float>(sampleRate_ * 0.45));

        float reso = clampf(p.resonance + lfo * p.lfoToResonance * 0.5f
                          + p.velToResonance * velocity_ * 0.7f
                          + md[ModDstResonance] * 0.7f, 0.0f, 0.98f);
        reso = clampf(reso * (1.0f + vcfTolerance_.resonanceScale * analog),
                      0.0f, 0.98f);

        float drive = clampf(p.filterDrive + p.velToDrive * velocity_
                           + md[ModDstDrive], 0.0f, 1.0f);
        drive = clampf(drive * (1.0f + vcfTolerance_.saturationScale * analog),
                       0.0f, 1.0f);
        const float filterAnalog = clampf(
            analog * (1.0f + vcfTolerance_.saturationScale), 0.0f, 1.0f);
        const float filterSlope = clampf(
            p.filterSlopeMix + md[ModDstFilterSlope], 0.0f, 1.0f);
        const float filterMode = clampf(
            p.filterModeMix + md[ModDstFilterMode] * 2.0f, 0.0f, 2.0f);
        filter_.setParams(cutoff, reso, filterSlope, filterAnalog, drive,
                          filterMode);

        const float osc1Level = clampf(
            p.osc1Level + md[ModDstOsc1Level], 0.0f, 1.0f);
        const float osc2Level = clampf(
            p.osc2Level + md[ModDstOsc2Level], 0.0f, 1.0f);
        const float noiseLevel = clampf(
            p.noiseLevel + md[ModDstNoiseLevel], 0.0f, 1.0f);

        float filtered = 0.0f;
        for (int os = 0; os < kOversample; ++os) {
            // Process the modulator (osc 2) first so its sine can FM the carrier.
            float o2 = p.osc2IsWT ? wtOsc2_.process() : osc2_.process();

            double f1 = baseF1;
            if (fm) {
                // Modulate with a *sine* of osc 2's phase (smooth; a raw
                // saw/pulse would click at every wrap). Roland/DX-style FM.
                float mod = osc2_.phaseSine();
                if (p.crossModTZ) {
                    f1 = baseF1 * (1.0 + static_cast<double>(cm) * mod * 4.0);
                } else {
                    f1 = baseF1 * std::exp2(static_cast<double>(cm) * mod * 2.0);
                }
            }

            float o1;
            if (p.osc1IsWT) {
                o1 = wtOsc1_.process();
            } else {
                osc_.setFrequency(f1);
                o1 = osc_.process();
            }

            // Hard sync: master osc 1 wraps -> reset slave osc 2.
            if (doSync && osc_.justWrapped()) osc2_.syncReset();

            float m = o1 * osc1Level + o2 * osc2Level;
            // Noise and every nonlinear filter operation also run at 2x.
            float src = m + rng_.nextBipolar() * noiseLevel;
            filtered = decimator_.process(filter_.process(src));
        }

        // --- VCA (velocity depth + matrix amplitude modulation / tremolo) ----
        float velGain = (1.0f - p.velToVolume) + p.velToVolume * velocity_;
        float ampMod  = clampf(1.0f + md[ModDstAmp], 0.0f, 2.0f);
        float amp = ampVal * velGain * ampMod;
        telemetry_.osc1Octave = p.octave
                              + (vibrato + md[ModDstOscPitch] * 12.0f) / 12.0f;
        telemetry_.osc2Octave = p.osc2Octave
                              + (vibrato + p.osc2PitchEnv * env * 36.0f
                                 + md[ModDstOsc2Pitch] * 36.0f) / 12.0f;
        telemetry_.pulseWidth = pw1;
        telemetry_.osc2PulseWidth = pw2;
        telemetry_.wtFrame = wtFrameMod;
        telemetry_.wtLiveness = wtLivenessMod;
        telemetry_.crossMod = cm;
        telemetry_.cutoff = static_cast<float>(cutoff);
        telemetry_.resonance = reso;
        telemetry_.drive = drive;
        telemetry_.amplitude = clampf(p.masterGain * ampMod, 0.0f, 1.0f);
        telemetry_.osc1Level = osc1Level;
        telemetry_.osc2Level = osc2Level;
        telemetry_.noiseLevel = noiseLevel;
        telemetry_.filterSlope = filterSlope;
        telemetry_.filterMode = filterMode;
        telemetry_.panOffset = clampf(md[ModDstVoicePan], -1.0f, 1.0f);
        float output = filtered * amp * voiceGain_;
        if (stealFadeRemaining_ > 0) {
            output *= static_cast<float>(stealFadeRemaining_)
                    / static_cast<float>(stealFadeTotal_);
            --stealFadeRemaining_;
        }
        return output;
    }

private:
    double sampleRate_ = 44100.0;

    Oscillator osc_;
    Oscillator osc2_;
    WavetableOscillator wtOsc1_;
    WavetableOscillator wtOsc2_;
    Decimator2x decimator_;
    ADSR       ampEnv_;
    ADSR       filtEnv_;
    LadderFilter filter_;
    LFO        lfoLocal_;
    LFO        lfo2Local_;
    LFO        lfo3Local_;
    int        lfoElapsed_ = 0;
    int        lfoFadeSamples_ = 240;
    FastRandom rng_{0x9e3779b97f4a7c15ULL};

    int   note_ = 60;
    float random_ = 0.0f;
    VCFTolerance vcfTolerance_;
    VoiceTelemetry telemetry_;
    float targetPitch_ = 60.0f;
    float glidePitch_ = 60.0f;
    float velocity_ = 1.0f;
    float tuningOffsetSemis_ = 0.0f;
    float voiceGain_ = 1.0f;
    bool  held_ = false;
    uint64_t age_ = 0;

    bool  pendingNote_ = false;
    bool  pendingHeld_ = false;
    int   pendingNoteNumber_ = 60;
    float pendingVelocity_ = 1.0f;
    float pendingPhaseSpread_ = 0.0f;
    float pendingStartPitch_ = 60.0f;
    float pendingTuningOffsetSemis_ = 0.0f;
    float pendingVoiceGain_ = 1.0f;
    int   stealFadeRemaining_ = 0;
    int   stealFadeTotal_ = 1;

    float drift_ = 0.0f;
    float drift2_ = 0.0f;
    int   driftCounter_ = 32;
    double driftInc_ = 32.0;
};

} // namespace synth
