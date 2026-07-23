//
//  Voice.hpp
//  A single synth voice: oscillator + noise blend -> analogue-modelled
//  low-pass filter -> VCA. Two independent ADSR envelopes drive the VCA and
//  the filter cutoff. Per-voice pitch drift and phase randomisation give the
//  subtle instability of stacked analogue voices.
//

#pragma once
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "ADSR.hpp"
#include "Filter.hpp"
#include "Decimator.hpp"
#include "Params.hpp"

namespace synth {

class Voice {
public:
    static constexpr int kOversample = 2; // oscillator/FM/sync run at 2x

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        // Oscillators (and their sync/FM) run oversampled, everything else at
        // the host rate. Decimator band-limits before dropping back down.
        osc_.setSampleRate(sr * kOversample);
        osc2_.setSampleRate(sr * kOversample);
        wtOsc1_.setSampleRate(sr * kOversample);
        wtOsc2_.setSampleRate(sr * kOversample);
        lib_ = &wtLibrary();   // built on first call, off the audio thread
        decimator_.setup(sr);
        ampEnv_.setSampleRate(sr);
        filtEnv_.setSampleRate(sr);
        filter_.setSampleRate(sr);
        lfoLocal_.setSampleRate(sr);
        lfoFadeSamples_ = static_cast<int>(sr * 0.005); // 5 ms click-free start
        // Drift updated ~ every 32 samples; convert to a slow random-walk step.
        driftInc_ = 32.0;
    }

    void seed(uint64_t s) { rng_ = FastRandom(s); }

    bool  isActive() const { return ampEnv_.isActive(); }
    int   note() const { return note_; }
    bool  isHeld() const { return held_; }
    uint64_t age() const { return age_; }

    void noteOn(int note, float velocity, float phaseSpread, float startPitch) {
        note_ = note;
        targetPitch_ = static_cast<float>(note);
        glidePitch_ = startPitch;   // where the glide begins (semitones)
        velocity_ = velocity;
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
        lfoElapsed_ = 0;        // (re)start the LFO delay from this key press
        ampEnv_.gate(true);
        filtEnv_.gate(true);
        drift_ = 0.0f;
        drift2_ = 0.0f;
        random_ = rng_.nextBipolar();   // per-note sample & hold for the matrix
    }

    void noteOff() {
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
    inline float render(const Params& p, float globalLfo, float globalLfo2) {
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
        float lfo;
        {
            int delaySamp = static_cast<int>(p.lfoDelay * sampleRate_);
            float amp;
            if (lfoElapsed_ < delaySamp) {
                amp = 0.0f;
            } else if (lfoFadeSamples_ > 0) {
                amp = clampf(static_cast<float>(lfoElapsed_ - delaySamp)
                             / static_cast<float>(lfoFadeSamples_), 0.0f, 1.0f);
            } else {
                amp = 1.0f;
            }
            float src;
            if (p.lfoKeyTrigger) {
                lfoLocal_.setWave(p.lfoWave);
                lfoLocal_.setRate(p.lfoRate);
                // Hold at the reset phase until the delay elapses, then run.
                src = (lfoElapsed_ >= delaySamp) ? lfoLocal_.process() : 0.0f;
            } else {
                src = globalLfo;    // free-running, shared across voices
            }
            if (lfoElapsed_ < (1 << 30)) ++lfoElapsed_;
            lfo = src * amp;
        }

        // --- Modulation matrix: gather the sources, then accumulate each
        //     slot's (source * amount) onto its destination. Applied below at
        //     the matching point in the signal path. --------------------------
        float srcv[ModSrcCount];
        srcv[ModSrcNone]       = 0.0f;
        srcv[ModSrcLFO1]       = lfo;
        srcv[ModSrcLFO2]       = globalLfo2;
        srcv[ModSrcFilterEnv]  = env;
        srcv[ModSrcAmpEnv]     = ampVal;
        srcv[ModSrcVelocity]   = velocity_;
        srcv[ModSrcKeyTrack]   = (note_ - 60) / 48.0f;   // ~ -1 .. +1 over range
        srcv[ModSrcModWheel]   = p.modWheel;
        srcv[ModSrcAftertouch] = p.aftertouch;
        srcv[ModSrcRandom]     = random_;                // per-note S&H, -1..1
        float md[ModDstCount] = {0.0f};
        for (int s = 0; s < SYNTH_MOD_SLOTS; ++s) {
            int sr = p.modSource[s], ds = p.modDest[s];
            if (sr > 0 && ds > 0) md[ds] += srcv[sr] * p.modAmount[s];
        }

        // --- Shared pitch modulation: octave + bend + vibrato(LFO) + matrix --
        const float vibrato = lfo * p.lfoToOscFreq * 2.0f; // up to +/-2 semis
        const double baseSemis = static_cast<double>(glidePitch_)
                               + p.pitchBendSemis
                               + vibrato
                               + md[ModDstOscPitch] * 12.0;   // matrix -> pitch

        // Oscillator 1 — the CARRIER: its pitch tracks the played note, and it
        // is the oscillator that cross-mod (FM) and hard sync act on. Because
        // the carrier sets the perceived pitch, tuning osc 2 changes timbre,
        // not pitch.
        double semis1 = baseSemis + p.octave * 12.0
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
        float pwm = lfo * p.lfoToPulseWidth * 0.45f + md[ModDstPulseWidth] * 0.45f;
        osc_.setPulseWidth(p.pulseWidth + pwm);
        osc2_.setPulseWidth(p.osc2PulseWidth + pwm);

        // --- Wavetable setup (per-osc; frequency is constant across the block
        //     because a wavetable oscillator does not support FM). The morph
        //     frame is modulated by the LFO and the filter envelope. ---------
        const float wtFrameMod = clampf(p.wtFrame
                                        + lfo * p.lfoToWTFrame
                                        + env * p.wtFrameEnv
                                        + md[ModDstWTFrame], 0.0f, 1.0f);
        const float wtLivenessMod = clampf(p.wtLiveness + md[ModDstWTLiveness],
                                           0.0f, 1.0f);
        if (p.osc1IsWT && lib_) {
            wtOsc1_.setTable(&lib_->sets[p.wtTable]);
            wtOsc1_.setFrame(wtFrameMod);
            wtOsc1_.setLiveness(wtLivenessMod);
            wtOsc1_.setFrequency(baseF1);
        }
        if (p.osc2IsWT && lib_) {
            wtOsc2_.setTable(&lib_->sets[p.wtTable]);
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

        float oscSig = 0.0f;
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

            float m = o1 * p.osc1Level + o2 * p.osc2Level;
            oscSig = decimator_.process(m);
        }

        // Noise mixed in at the host rate (own level).
        float noiseSig = rng_.nextBipolar();
        float src = oscSig + noiseSig * p.noiseLevel;

        // --- Filter cutoff modulation (envelope + keytrack + LFO + matrix) --
        float envOct  = p.filterEnvAmt * env * 6.0f;                 // +/-6 oct range
        float lfoOct  = lfo * p.lfoToCutoff * 4.0f;                  // +/-4 oct
        float keyOct  = p.filterKeyTrack * (note_ - 60) / 12.0f;     // 1 oct / octave
        float velOct  = p.velToCutoff * velocity_ * 5.0f;            // velocity opens filter
        float matOct  = md[ModDstCutoff] * 4.0f;                     // matrix -> cutoff
        double cutoff = p.cutoff * std::pow(2.0, envOct + lfoOct + keyOct + velOct + matOct);
        cutoff = clampf(static_cast<float>(cutoff), 20.0f,
                        static_cast<float>(sampleRate_ * 0.45));

        float reso = clampf(p.resonance + lfo * p.lfoToResonance * 0.5f
                          + p.velToResonance * velocity_ * 0.7f
                          + md[ModDstResonance] * 0.7f, 0.0f, 0.98f);

        float drive = clampf(p.filterDrive + p.velToDrive * velocity_
                           + md[ModDstDrive], 0.0f, 1.0f);
        filter_.setParams(cutoff, reso, p.filterSlopeMix, p.analogAmount, drive);
        float filtered = filter_.process(src);

        // --- VCA (velocity depth + matrix amplitude modulation / tremolo) ----
        float velGain = (1.0f - p.velToVolume) + p.velToVolume * velocity_;
        float ampMod  = clampf(1.0f + md[ModDstAmp], 0.0f, 2.0f);
        float amp = ampVal * velGain * ampMod;
        return filtered * amp;
    }

private:
    double sampleRate_ = 44100.0;

    Oscillator osc_;
    Oscillator osc2_;
    WavetableOscillator wtOsc1_;
    WavetableOscillator wtOsc2_;
    const WavetableLibrary* lib_ = nullptr;
    Decimator2x decimator_;
    ADSR       ampEnv_;
    ADSR       filtEnv_;
    LadderFilter filter_;
    LFO        lfoLocal_;
    int        lfoElapsed_ = 0;
    int        lfoFadeSamples_ = 240;
    FastRandom rng_{0x9e3779b97f4a7c15ULL};

    int   note_ = 60;
    float random_ = 0.0f;
    float targetPitch_ = 60.0f;
    float glidePitch_ = 60.0f;
    float velocity_ = 1.0f;
    bool  held_ = false;
    uint64_t age_ = 0;

    float drift_ = 0.0f;
    float drift2_ = 0.0f;
    int   driftCounter_ = 32;
    double driftInc_ = 32.0;
};

} // namespace synth
