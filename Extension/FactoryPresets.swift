//
//  FactoryPresets.swift
//  Built-in presets, defined as overrides on top of the parameter defaults.
//  Shared by the AUAudioUnit (for Logic's factory-preset menu) and the
//  in-plug-in preset browser. Keyed by AUParameterAddress so both agree.
//

import AudioToolbox

struct FactoryPreset {
    let name: String
    let values: [AUParameterAddress: Float]
}

enum FactoryPresets {

    // Legacy fixed routings that are now folded into the mod matrix. Each maps
    // to a (source, destination, scale-ratio); the ratio converts the old
    // authored depth so the migrated slot sounds identical to the old knob.
    private static let foldRoutes: [AUParameterAddress: (SynthModSource, SynthModDest, Float)] = [
        AUParameterAddress(SynthParamLFOToPulseWidth.rawValue): (ModSrcLFO1,      ModDstPulseWidth, 1.0),
        AUParameterAddress(SynthParamLFOToCutoff.rawValue):     (ModSrcLFO1,      ModDstCutoff,     1.0),
        AUParameterAddress(SynthParamLFOToResonance.rawValue):  (ModSrcLFO1,      ModDstResonance,  0.5 / 0.7),
        AUParameterAddress(SynthParamLFOToCrossMod.rawValue):   (ModSrcLFO1,      ModDstCrossMod,   1.0),
        AUParameterAddress(SynthParamLFOToWTFrame.rawValue):    (ModSrcLFO1,      ModDstWTFrame,    1.0),
        AUParameterAddress(SynthParamVelToCutoff.rawValue):     (ModSrcVelocity,  ModDstCutoff,     5.0 / 4.0),
        AUParameterAddress(SynthParamVelToResonance.rawValue):  (ModSrcVelocity,  ModDstResonance,  1.0),
        AUParameterAddress(SynthParamVelToDrive.rawValue):      (ModSrcVelocity,  ModDstDrive,      1.0),
        AUParameterAddress(SynthParamOsc2PitchEnv.rawValue):    (ModSrcFilterEnv, ModDstOsc2Pitch,  1.0),
        AUParameterAddress(SynthParamWTFrameEnv.rawValue):      (ModSrcFilterEnv, ModDstWTFrame,    1.0),
    ]

    private static func make(_ pairs: [(SynthParam, Float)],
                             mods: [(SynthModSource, SynthModDest, Float)] = [])
        -> [AUParameterAddress: Float] {
        var d = [AUParameterAddress: Float]()
        var slot = 0                              // next free mod-matrix slot
        let modBase = AUParameterAddress(SynthParamMod1Source.rawValue)
        for (k, v) in pairs {
            // Presets still author the old Osc Mix (balance) / Noise Mix (blend);
            // translate them into the new independent mixer levels.
            if k.rawValue == SynthParamOscMix.rawValue {
                d[AUParameterAddress(SynthParamOsc1Level.rawValue)] = 1 - v
                d[AUParameterAddress(SynthParamOsc2Level.rawValue)] = v
                continue
            }
            if k.rawValue == SynthParamNoiseMix.rawValue {
                d[AUParameterAddress(SynthParamNoiseLevel.rawValue)] = v
                continue
            }
            // Fold legacy fixed routings into mod-matrix slots.
            let addr = AUParameterAddress(k.rawValue)
            if let route = foldRoutes[addr], slot < 6 {
                let base = modBase + AUParameterAddress(slot * 3)
                d[base]     = Float(route.0.rawValue)   // source
                d[base + 1] = Float(route.1.rawValue)   // destination
                d[base + 2] = v * route.2               // scaled amount
                slot += 1
                continue
            }
            // Envelope times are authored in seconds for readability but stored
            // as the normalised 0..1 the parameters now use.
            d[addr] = SynthTime.isTime(addr) ? SynthTime.norm(fromSeconds: v) : v
        }
        // Explicit expressive mod-matrix routes fill any remaining free slots.
        for m in mods where slot < 6 {
            let base = modBase + AUParameterAddress(slot * 3)
            d[base]     = Float(m.0.rawValue)
            d[base + 1] = Float(m.1.rawValue)
            d[base + 2] = m.2
            slot += 1
        }
        return d
    }

    static let all: [FactoryPreset] = [
        FactoryPreset(name: "Init", values: [:]),

        FactoryPreset(name: "Fat Saw Bass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOctave, -1),
            (SynthParamOsc2Waveform, 0), (SynthParamOsc2Detune, 12),
            (SynthParamOscMix, 0.5), (SynthParamFilterDrive, 0.3),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 1100),
            (SynthParamFilterResonance, 0.35), (SynthParamFilterEnvAmount, 0.55),
            (SynthParamFilterDecay, 0.35), (SynthParamFilterSustain, 0.1),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.75), (SynthParamAmpRelease, 0.15),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.3),
            (SynthParamMasterGain, 0.78),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.22),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -14),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 3),
        ], mods: [(ModSrcAftertouch, ModDstDrive, 0.3)])),

        FactoryPreset(name: "Warm Pad", values: make([
            (SynthParamOscWaveform, 0), (SynthParamFilterCutoff, 2600),
            (SynthParamFilterResonance, 0.18), (SynthParamFilterEnvAmount, 0.3),
            (SynthParamAmpAttack, 0.6), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 1.3),
            (SynthParamFilterAttack, 0.8), (SynthParamFilterDecay, 1.0),
            (SynthParamFilterSustain, 0.5), (SynthParamFilterRelease, 1.3),
            (SynthParamLFOToOscFreq, 0.06), (SynthParamLFORate, 4.5),
            (SynthParamNoiseMix, 0.04), (SynthParamAnalogAmount, 0.5),
            (SynthParamOscPhaseSpread, 0.7), (SynthParamLFO2Rate, 0.3),
            (SynthParamStereoSpread, 0.7),
            (SynthParamChorusMix, 0.3), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.28), (SynthParamReverbSize, 0.72),
            (SynthParamReverbDecay, 3.6), (SynthParamReverbTone, 0.5),
        ], mods: [(ModSrcLFO2, ModDstCutoff, 0.22), (ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Bright Pluck", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 7), (SynthParamOscMix, 0.35),
            (SynthParamFilterCutoff, 6500),
            (SynthParamFilterResonance, 0.3), (SynthParamFilterEnvAmount, 0.7),
            (SynthParamFilterDecay, 0.28), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.25),
            (SynthParamVelToCutoff, 0.4), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.74), (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.24), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.32), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.18), (SynthParamReverbDecay, 1.8),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "PWM Strings", values: make([
            (SynthParamOscWaveform, 2), (SynthParamOscPulseWidth, 0.5),
            (SynthParamLFOToPulseWidth, 0.45), (SynthParamLFORate, 0.7),
            (SynthParamFilterCutoff, 3500), (SynthParamFilterResonance, 0.12),
            (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.35), (SynthParamAmpSustain, 0.85),
            (SynthParamAmpRelease, 0.7), (SynthParamAnalogAmount, 0.5),
            (SynthParamOscPhaseSpread, 0.8), (SynthParamStereoSpread, 0.75),
            (SynthParamChorusMix, 0.35), (SynthParamChorusDepth, 0.45),
            (SynthParamReverbMix, 0.24), (SynthParamReverbSize, 0.68),
            (SynthParamReverbDecay, 3.0),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Acid Lead", values: make([
            (SynthParamOscWaveform, 0), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 750), (SynthParamFilterResonance, 0.78),
            (SynthParamFilterDrive, 0.45),
            (SynthParamFilterEnvAmount, 0.6), (SynthParamFilterDecay, 0.25),
            (SynthParamFilterSustain, 0.1),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpSustain, 0.9),
            (SynthParamAmpRelease, 0.1), (SynthParamAnalogAmount, 0.3),
            (SynthParamVoiceCount, 1), (SynthParamGlideTime, 0.05),
            (SynthParamMasterGain, 0.72),
            (SynthParamDelayMix, 0.22), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.4), (SynthParamDelayTone, 0.5),
            (SynthParamReverbMix, 0.14), (SynthParamReverbDecay, 1.5),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Detuned Unison", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, -16), (SynthParamOscMix, 0.5),
            (SynthParamFilterCutoff, 4200), (SynthParamFilterResonance, 0.2),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.6),
            (SynthParamAmpAttack, 0.02), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.5),
            (SynthParamAnalogAmount, 0.55), (SynthParamOscPhaseSpread, 0.7),
            (SynthParamLFO2Rate, 0.5),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.3),
            (SynthParamStereoSpread, 0.8),
            (SynthParamChorusMix, 0.28), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.2), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 2.4),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.35), (ModSrcLFO2, ModDstCutoff, 0.1)])),

        FactoryPreset(name: "Wobble Bass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOctave, -1),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 500),
            (SynthParamFilterResonance, 0.5), (SynthParamLFOToCutoff, 0.7),
            (SynthParamLFORate, 3.0), (SynthParamLFOWaveform, 0),
            (SynthParamAmpSustain, 0.9), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAnalogAmount, 0.3), (SynthParamVoiceCount, 1),
            (SynthParamMasterGain, 0.76),
            // Trig-sync the wobble LFO so it restarts in phase on every note —
            // the wobble stays locked to the groove instead of drifting.
            (SynthParamLFO1Mode, 1),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -12),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 2),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Sync Sweep Lead", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Sync, 1), (SynthParamOsc2Semitone, 5),
            (SynthParamOsc2PitchEnv, 0.6), (SynthParamOscMix, 0.8),
            (SynthParamFilterCutoff, 10000), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.0), (SynthParamFilterDecay, 0.6),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.5),
            (SynthParamAmpSustain, 0.7), (SynthParamAmpRelease, 0.3),
            (SynthParamAnalogAmount, 0.3), (SynthParamMasterGain, 0.7),
            (SynthParamDelayMix, 0.24), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.35), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.18), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcModWheel, ModDstOsc2Pitch, 0.3), (ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "TZ-FM Bells", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOscCrossMod, 0.4), (SynthParamOscCrossModTZ, 1),
            (SynthParamOsc2Semitone, 7), (SynthParamLFOToCrossMod, 0.25),
            (SynthParamLFORate, 3.0), (SynthParamOscMix, 0.3),
            (SynthParamFilterCutoff, 8000), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.35), (SynthParamFilterDecay, 0.7),
            (SynthParamFilterSustain, 0.15),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.9),
            (SynthParamAmpSustain, 0.15), (SynthParamAmpRelease, 0.7),
            (SynthParamAnalogAmount, 0.3), (SynthParamStereoSpread, 0.5),
            (SynthParamReverbMix, 0.3), (SynthParamReverbSize, 0.78),
            (SynthParamReverbDecay, 4.0), (SynthParamReverbTone, 0.6),
            (SynthParamDelayMix, 0.18), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.28),
        ], mods: [(ModSrcModWheel, ModDstCrossMod, 0.25)])),

        FactoryPreset(name: "Vintage Brass", values: make([
            // Two detuned saws, 12 dB filter, a FAST filter-env attack + drive
            // for the brassy "bite" (the brightness snaps in, then settles).
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 11), (SynthParamOscMix, 0.5),
            (SynthParamFilterSlope, 0), (SynthParamFilterCutoff, 1700),
            (SynthParamFilterResonance, 0.32), (SynthParamFilterEnvAmount, 0.62),
            (SynthParamFilterAttack, 0.010), (SynthParamFilterDecay, 0.28),
            (SynthParamFilterSustain, 0.35), (SynthParamFilterDrive, 0.32),
            (SynthParamAmpAttack, 0.018), (SynthParamAmpDecay, 0.30),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.28),
            (SynthParamVelToCutoff, 0.45), (SynthParamVelToDrive, 0.2),
            (SynthParamAnalogAmount, 0.5), (SynthParamOscPhaseSpread, 0.6),
            (SynthParamNoiseMix, 0.02), (SynthParamMasterGain, 0.72),
            (SynthParamStereoSpread, 0.5),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -16),
            (SynthParamCompressorRatio, 3), (SynthParamCompressorMakeup, 3),
            (SynthParamReverbMix, 0.16), (SynthParamReverbSize, 0.55),
            (SynthParamReverbDecay, 1.4),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        // ---------------------------------------------------------------
        // Sample recreations (matched to keys.wav / juno_pad.wav by the
        // Tools/ spectral matcher: two saws an octave apart through a resonant
        // 24 dB low-pass — the shared oscillator/filter recipe both samples
        // reduced to — differentiated by their envelopes).
        // ---------------------------------------------------------------

        FactoryPreset(name: "Warm Keys", values: make([
            // keys.wav: warm, decaying electric-piano key. Instant attack, long
            // amp decay to silence; resonant low-pass ~560 Hz emphasises the
            // low harmonics, filter env brightens the attack then closes.
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, 1),
            (SynthParamOsc1Level, 0.65), (SynthParamOsc2Level, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 560),
            (SynthParamFilterResonance, 0.45), (SynthParamFilterEnvAmount, 0.35),
            (SynthParamFilterAttack, 0.003), (SynthParamFilterDecay, 0.55),
            (SynthParamFilterSustain, 0.10),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.30),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.3),
            (SynthParamAnalogAmount, 0.2), (SynthParamMasterGain, 0.85),
            (SynthParamStereoSpread, 0.4),
            (SynthParamChorusMix, 0.18), (SynthParamChorusDepth, 0.3),
            (SynthParamReverbMix, 0.18), (SynthParamReverbSize, 0.55),
            (SynthParamReverbDecay, 1.8),
        ])),

        FactoryPreset(name: "Juno Poly Pad", values: make([
            // juno_pad.wav: octave-heavy poly pad. Two equal saws an octave
            // apart (h2 as strong as the fundamental), resonant 24 dB filter,
            // ~40 ms attack, sustains when held; analog drift + light chorus.
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, 1),
            (SynthParamOsc1Level, 0.5), (SynthParamOsc2Level, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 560),
            (SynthParamFilterResonance, 0.38), (SynthParamFilterEnvAmount, 0.12),
            (SynthParamFilterAttack, 0.05), (SynthParamFilterDecay, 0.8),
            (SynthParamFilterSustain, 0.6),
            (SynthParamAmpAttack, 0.045), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.55), (SynthParamAmpRelease, 0.9),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.7),
            (SynthParamChorusMix, 0.3), (SynthParamChorusDepth, 0.4),
            (SynthParamStereoSpread, 0.75), (SynthParamMasterGain, 0.72),
            (SynthParamReverbMix, 0.22), (SynthParamReverbSize, 0.68),
            (SynthParamReverbDecay, 2.6),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        // ---------------------------------------------------------------
        // 80s synth-pop collection
        // ---------------------------------------------------------------

        FactoryPreset(name: "DX Electric Piano", values: make([
            (SynthParamOscMix, 0.15), (SynthParamOscCrossMod, 0.22),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 0),
            (SynthParamFilterCutoff, 9000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.5),
            (SynthParamAmpSustain, 0.25), (SynthParamAmpRelease, 0.4),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.4),
            (SynthParamAnalogAmount, 0.2), (SynthParamMasterGain, 0.75),
            (SynthParamStereoSpread, 0.45),
            (SynthParamChorusMix, 0.22), (SynthParamChorusDepth, 0.35),
            (SynthParamReverbMix, 0.2), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Poly Brass", values: make([
            // Fuller 24 dB brass section: darker base, quick filter-env attack,
            // drive and velocity for punch and dynamic bite.
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 9), (SynthParamOscMix, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 1900),
            (SynthParamFilterResonance, 0.34), (SynthParamFilterEnvAmount, 0.6),
            (SynthParamFilterAttack, 0.012), (SynthParamFilterDecay, 0.30),
            (SynthParamFilterSustain, 0.45), (SynthParamFilterDrive, 0.30),
            (SynthParamAmpAttack, 0.020), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.30),
            (SynthParamVelToCutoff, 0.5), (SynthParamVelToDrive, 0.2),
            (SynthParamAnalogAmount, 0.45), (SynthParamOscPhaseSpread, 0.6),
            (SynthParamMasterGain, 0.80), (SynthParamStereoSpread, 0.55),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -16),
            (SynthParamCompressorRatio, 3), (SynthParamCompressorMakeup, 3),
            (SynthParamReverbMix, 0.16), (SynthParamReverbSize, 0.58),
            (SynthParamReverbDecay, 1.5),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Brass Stab", values: make([
            // Aggressive, short synth-brass hit — near-instant filter attack,
            // high env + resonance + heavy drive for maximum bite.
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 12), (SynthParamOscMix, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 1300),
            (SynthParamFilterResonance, 0.40), (SynthParamFilterEnvAmount, 0.75),
            (SynthParamFilterAttack, 0.004), (SynthParamFilterDecay, 0.22),
            (SynthParamFilterSustain, 0.25), (SynthParamFilterDrive, 0.45),
            (SynthParamAmpAttack, 0.006), (SynthParamAmpDecay, 0.30),
            (SynthParamAmpSustain, 0.70), (SynthParamAmpRelease, 0.18),
            (SynthParamVelToCutoff, 0.55), (SynthParamVelToDrive, 0.3),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.5),
            (SynthParamMasterGain, 0.76), (SynthParamStereoSpread, 0.45),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -14),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 2),
            (SynthParamReverbMix, 0.14), (SynthParamReverbSize, 0.5),
            (SynthParamReverbDecay, 1.2),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Juno Pad", values: make([
            (SynthParamOscWaveform, 2), (SynthParamOscPulseWidth, 0.5),
            (SynthParamLFOToPulseWidth, 0.5), (SynthParamLFORate, 0.4),
            (SynthParamOsc2Waveform, 0), (SynthParamOsc2Detune, 6),
            (SynthParamOscMix, 0.4), (SynthParamFilterCutoff, 3000),
            (SynthParamFilterResonance, 0.1), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.6), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 1.2),
            (SynthParamFilterAttack, 0.8), (SynthParamFilterSustain, 0.6),
            (SynthParamAnalogAmount, 0.45), (SynthParamOscPhaseSpread, 0.8),
            (SynthParamNoiseMix, 0.03), (SynthParamMasterGain, 0.68),
            (SynthParamLFO2Rate, 0.25), (SynthParamStereoSpread, 0.7),
            (SynthParamChorusMix, 0.32), (SynthParamChorusDepth, 0.45),
            (SynthParamReverbMix, 0.26), (SynthParamReverbSize, 0.7),
            (SynthParamReverbDecay, 3.2),
        ], mods: [(ModSrcLFO2, ModDstCutoff, 0.18), (ModSrcAftertouch, ModDstAmp, 0.3)])),

        FactoryPreset(name: "DX Bass", values: make([
            (SynthParamOctave, -1), (SynthParamOsc2Octave, -1),
            (SynthParamOscMix, 0.2), (SynthParamOscCrossMod, 0.3),
            (SynthParamOscCrossModTZ, 1), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 1200), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.4), (SynthParamFilterDecay, 0.25),
            (SynthParamFilterSustain, 0.2),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.6), (SynthParamAmpRelease, 0.15),
            (SynthParamVelToCutoff, 0.3), (SynthParamAnalogAmount, 0.25),
            (SynthParamMasterGain, 0.78),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -14),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 3),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Synthwave Bass", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOsc2Waveform, 0),
            (SynthParamOscMix, 0.35), (SynthParamOctave, -1),
            (SynthParamOsc2Octave, -1), (SynthParamOsc2Detune, 5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 900),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterDecay, 0.3), (SynthParamFilterSustain, 0.15),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.7), (SynthParamAmpRelease, 0.12),
            (SynthParamVelToCutoff, 0.35), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.78),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -14),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 3),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        // The bell character comes from an INHARMONIC cross-mod ratio: osc 2
        // (the modulator) sits a tritone above osc 1 (the carrier), a ratio of
        // 2^(6/12)=√2≈1.414 — non-integer, so the FM sidebands land on
        // inharmonic partials (×0.42, ×1.41, ×2.41, ×3.42) that ring like a
        // struck bell rather than the harmonic organ tone an octave (×2.0)
        // gives. Through-zero linear FM keeps the pitch stable while the filter
        // envelope drives the cross-mod depth so the clangy strike decays into a
        // purer hum — the classic Roland/DX bell evolution.
        FactoryPreset(name: "Glass Bells", values: make([
            (SynthParamOscMix, 0.15), (SynthParamOscCrossMod, 0.25),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 6),
            (SynthParamFilterCutoff, 9000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.28), (SynthParamFilterDecay, 0.5),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 1.6),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 1.0),
            (SynthParamVelToCutoff, 0.5), (SynthParamVelToVolume, 1.0),
            (SynthParamAnalogAmount, 0.15), (SynthParamMasterGain, 0.78),
            (SynthParamStereoSpread, 0.55),
            (SynthParamReverbMix, 0.32), (SynthParamReverbSize, 0.8),
            (SynthParamReverbDecay, 4.5), (SynthParamReverbTone, 0.6),
        ], mods: [(ModSrcFilterEnv, ModDstCrossMod, 0.35),
                  (ModSrcModWheel, ModDstCrossMod, 0.3)])),

        FactoryPreset(name: "Analog Strings", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 10), (SynthParamOscMix, 0.5),
            (SynthParamFilterCutoff, 4000), (SynthParamFilterResonance, 0.08),
            (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.3), (SynthParamAmpDecay, 0.8),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.8),
            (SynthParamFilterAttack, 0.4), (SynthParamFilterSustain, 0.7),
            (SynthParamLFOToOscFreq, 0.05), (SynthParamLFORate, 5.0),
            (SynthParamAnalogAmount, 0.55), (SynthParamOscPhaseSpread, 0.9),
            (SynthParamNoiseMix, 0.02), (SynthParamMasterGain, 0.66),
            (SynthParamLFO2Rate, 0.3), (SynthParamStereoSpread, 0.85),
            (SynthParamChorusMix, 0.35), (SynthParamChorusDepth, 0.45),
            (SynthParamReverbMix, 0.26), (SynthParamReverbSize, 0.75),
            (SynthParamReverbDecay, 3.5),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4), (ModSrcLFO2, ModDstCutoff, 0.12)])),

        FactoryPreset(name: "Pluck Arp", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 4), (SynthParamOscMix, 0.4),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 2500),
            (SynthParamFilterResonance, 0.25), (SynthParamFilterEnvAmount, 0.6),
            (SynthParamFilterDecay, 0.2), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.2),
            (SynthParamVelToCutoff, 0.4), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.72), (SynthParamStereoSpread, 0.55),
            (SynthParamDelayMix, 0.26), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.36), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.18), (SynthParamReverbDecay, 1.8),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Fifth Stab", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Semitone, 7), (SynthParamOscMix, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 3500),
            (SynthParamFilterResonance, 0.15), (SynthParamFilterEnvAmount, 0.4),
            (SynthParamFilterDecay, 0.25), (SynthParamFilterSustain, 0.3),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.5),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.25),
            (SynthParamVelToCutoff, 0.4), (SynthParamVelToVolume, 1.0),
            (SynthParamOscPhaseSpread, 0.5), (SynthParamAnalogAmount, 0.35),
            (SynthParamMasterGain, 0.72), (SynthParamStereoSpread, 0.55),
            (SynthParamDelayMix, 0.2), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.3),
            (SynthParamReverbMix, 0.2), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 1.8),
        ])),

        FactoryPreset(name: "Saw Lead", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 7), (SynthParamOscMix, 0.4),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 6000),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.01), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.25),
            (SynthParamLFOToOscFreq, 0.15), (SynthParamLFORate, 5.5),
            (SynthParamVelToCutoff, 0.3), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.72),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.3),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.2), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.32),
            (SynthParamReverbMix, 0.15), (SynthParamReverbDecay, 1.6),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Square Lead", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOscMix, 0.0),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 4500),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.25),
            (SynthParamAmpAttack, 0.01), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.2),
            (SynthParamLFOToOscFreq, 0.2), (SynthParamLFORate, 6.0),
            (SynthParamAnalogAmount, 0.3), (SynthParamMasterGain, 0.72),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.25),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.2), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.34),
            (SynthParamReverbMix, 0.15), (SynthParamReverbDecay, 1.6),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Cinematic Pad", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 12), (SynthParamOscMix, 0.5),
            (SynthParamFilterCutoff, 2500), (SynthParamFilterResonance, 0.12),
            (SynthParamFilterEnvAmount, 0.25),
            (SynthParamAmpAttack, 0.8), (SynthParamAmpDecay, 1.5),
            (SynthParamAmpSustain, 0.9), (SynthParamAmpRelease, 2.0),
            (SynthParamFilterAttack, 1.2), (SynthParamFilterSustain, 0.6),
            (SynthParamLFOToOscFreq, 0.08), (SynthParamLFORate, 4.5),
            (SynthParamAnalogAmount, 0.7), (SynthParamOscPhaseSpread, 0.9),
            (SynthParamMasterGain, 0.62), (SynthParamLFO2Rate, 0.15),
            (SynthParamStereoSpread, 0.9),
            (SynthParamChorusMix, 0.3), (SynthParamChorusDepth, 0.5),
            (SynthParamReverbMix, 0.4), (SynthParamReverbSize, 0.9),
            (SynthParamReverbDecay, 6.0), (SynthParamReverbTone, 0.45),
            (SynthParamReverbPreDelay, 0.03),
        ], mods: [(ModSrcLFO2, ModDstOscPitch, 0.03), (ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Sync Stab", values: make([
            (SynthParamOscMix, 0.8), (SynthParamOsc2Sync, 1),
            (SynthParamOsc2Semitone, 3), (SynthParamOsc2PitchEnv, 0.5),
            (SynthParamFilterCutoff, 9000), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.0), (SynthParamFilterDecay, 0.3),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.25),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.3),
            (SynthParamAnalogAmount, 0.3), (SynthParamMasterGain, 0.72),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.22), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.34), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.18), (SynthParamReverbDecay, 1.6),
        ], mods: [(ModSrcModWheel, ModDstOsc2Pitch, 0.3)])),

        FactoryPreset(name: "Mallet", values: make([
            (SynthParamOscMix, 0.25), (SynthParamOscCrossMod, 0.3),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Octave, 1),
            (SynthParamOsc2Semitone, 7),
            (SynthParamFilterCutoff, 8000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.3),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.6),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.3),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.5),
            (SynthParamMasterGain, 0.72), (SynthParamStereoSpread, 0.5),
            (SynthParamReverbMix, 0.28), (SynthParamReverbSize, 0.72),
            (SynthParamReverbDecay, 2.8),
        ])),

        FactoryPreset(name: "Bell Pad", values: make([
            (SynthParamOscMix, 0.35), (SynthParamOscCrossMod, 0.25),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 12),
            (SynthParamFilterCutoff, 6000), (SynthParamFilterResonance, 0.08),
            (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.4), (SynthParamAmpDecay, 1.5),
            (SynthParamAmpSustain, 0.6), (SynthParamAmpRelease, 1.5),
            (SynthParamLFOToCrossMod, 0.15), (SynthParamLFORate, 1.5),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.8),
            (SynthParamMasterGain, 0.65), (SynthParamStereoSpread, 0.85),
            (SynthParamChorusMix, 0.25), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.35), (SynthParamReverbSize, 0.85),
            (SynthParamReverbDecay, 5.0), (SynthParamReverbTone, 0.55),
        ], mods: [(ModSrcAftertouch, ModDstAmp, 0.3)])),

        FactoryPreset(name: "Orchestra Hit", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 1),
            (SynthParamOsc2Detune, 12), (SynthParamOscMix, 0.5),
            (SynthParamNoiseMix, 0.15), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 5000), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.2),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.2),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.4),
            (SynthParamOscPhaseSpread, 0.7), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.72), (SynthParamStereoSpread, 0.7),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -18),
            (SynthParamCompressorRatio, 6), (SynthParamCompressorMakeup, 5),
            (SynthParamReverbMix, 0.26), (SynthParamReverbSize, 0.65),
            (SynthParamReverbDecay, 1.6),
        ])),

        FactoryPreset(name: "Digital Piano", values: make([
            (SynthParamOscMix, 0.2), (SynthParamOscCrossMod, 0.18),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 0),
            (SynthParamFilterCutoff, 10000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.8),
            (SynthParamAmpSustain, 0.15), (SynthParamAmpRelease, 0.5),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.45),
            (SynthParamMasterGain, 0.74), (SynthParamStereoSpread, 0.45),
            (SynthParamChorusMix, 0.16), (SynthParamChorusDepth, 0.3),
            (SynthParamReverbMix, 0.2), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 2.2),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Bright Chime", values: make([
            (SynthParamOscMix, 0.3), (SynthParamOscCrossMod, 0.35),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Octave, 2),
            (SynthParamFilterCutoff, 14000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.2), (SynthParamFilterDecay, 0.7),
            (SynthParamFilterSustain, 0.05),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 1.2),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 1.0),
            (SynthParamVelToCutoff, 0.5), (SynthParamMasterGain, 0.68),
            (SynthParamStereoSpread, 0.6),
            (SynthParamDelayMix, 0.24), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.34), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.32), (SynthParamReverbSize, 0.82),
            (SynthParamReverbDecay, 4.5),
        ], mods: [(ModSrcModWheel, ModDstCrossMod, 0.3)])),

        FactoryPreset(name: "Fat Unison Bass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 14), (SynthParamOscMix, 0.5),
            (SynthParamOctave, -1), (SynthParamOsc2Octave, -1),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 1000),
            (SynthParamFilterResonance, 0.18), (SynthParamFilterEnvAmount, 0.4),
            (SynthParamFilterDecay, 0.3), (SynthParamFilterSustain, 0.2),
            (SynthParamFilterDrive, 0.25),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.75), (SynthParamAmpRelease, 0.15),
            (SynthParamVelToCutoff, 0.3), (SynthParamAnalogAmount, 0.4),
            (SynthParamOscPhaseSpread, 0.4), (SynthParamMasterGain, 0.78),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.28),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -14),
            (SynthParamCompressorRatio, 5), (SynthParamCompressorMakeup, 3),
        ], mods: [(ModSrcAftertouch, ModDstDrive, 0.3)])),

        FactoryPreset(name: "Dream Lead", values: make([
            (SynthParamOscWaveform, 2), (SynthParamOscPulseWidth, 0.35),
            (SynthParamOscMix, 0.3), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 6), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 3500), (SynthParamFilterResonance, 0.15),
            (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.05), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.4),
            (SynthParamLFOToOscFreq, 0.12), (SynthParamLFORate, 5.0),
            (SynthParamLFOToPulseWidth, 0.2), (SynthParamAnalogAmount, 0.4),
            (SynthParamMasterGain, 0.7),
            (SynthParamUnison, 1), (SynthParamUnisonDetune, 0.3),
            (SynthParamStereoSpread, 0.6),
            (SynthParamChorusMix, 0.25), (SynthParamChorusDepth, 0.4),
            (SynthParamDelayMix, 0.22), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.34),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.2),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.35)])),

        // ---------------------------------------------------------------
        // Drums / percussion  (play low notes for kick/tom; the filter
        // envelope drives osc-2 pitch for the classic pitch-drop)
        // ---------------------------------------------------------------

        FactoryPreset(name: "Synth Kick", values: make([
            (SynthParamOscMix, 1.0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, -2), (SynthParamOsc2PitchEnv, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 200),
            (SynthParamFilterResonance, 0.25), (SynthParamFilterEnvAmount, 0.15),
            (SynthParamFilterAttack, 0.001), (SynthParamFilterDecay, 0.06),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.1),
            (SynthParamAnalogAmount, 0.1), (SynthParamMasterGain, 0.85),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -18),
            (SynthParamCompressorRatio, 6), (SynthParamCompressorAttack, 0.005),
            (SynthParamCompressorRelease, 0.12), (SynthParamCompressorMakeup, 4),
        ])),

        FactoryPreset(name: "Synth Snare", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOscMix, 0.0),
            (SynthParamNoiseMix, 0.65), (SynthParamFilterSlope, 0),
            (SynthParamFilterCutoff, 2500), (SynthParamFilterResonance, 0.2),
            (SynthParamFilterEnvAmount, 0.25), (SynthParamFilterAttack, 0.001),
            (SynthParamFilterDecay, 0.1), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.18),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.1),
            (SynthParamAnalogAmount, 0.2), (SynthParamMasterGain, 0.72),
            (SynthParamCompressorOn, 1), (SynthParamCompressorThreshold, -18),
            (SynthParamCompressorRatio, 4), (SynthParamCompressorMakeup, 3),
            (SynthParamStereoSpread, 0.4),
            (SynthParamReverbMix, 0.16), (SynthParamReverbSize, 0.4),
            (SynthParamReverbDecay, 0.8),
        ])),

        FactoryPreset(name: "Closed Hat", values: make([
            (SynthParamOscMix, 0.0), (SynthParamNoiseMix, 1.0),
            // High-pass the noise for a crisp metallic hat (was a dull low-pass).
            (SynthParamFilterMode, 2), (SynthParamFilterSlope, 0),
            (SynthParamFilterCutoff, 8000),
            (SynthParamFilterResonance, 0.15), (SynthParamFilterEnvAmount, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.04),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.02),
            (SynthParamMasterGain, 0.6),
        ])),

        FactoryPreset(name: "Open Hat", values: make([
            (SynthParamOscMix, 0.0), (SynthParamNoiseMix, 1.0),
            // High-pass the noise for a crisp metallic hat (was a dull low-pass).
            (SynthParamFilterMode, 2), (SynthParamFilterSlope, 0),
            (SynthParamFilterCutoff, 8000),
            (SynthParamFilterResonance, 0.15),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.2),
            (SynthParamMasterGain, 0.58),
        ])),

        FactoryPreset(name: "Synth Tom", values: make([
            (SynthParamOscMix, 1.0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, -1), (SynthParamOsc2PitchEnv, 0.35),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 700),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.1),
            (SynthParamFilterAttack, 0.001), (SynthParamFilterDecay, 0.12),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.15),
            (SynthParamAnalogAmount, 0.15), (SynthParamMasterGain, 0.8),
        ])),

        FactoryPreset(name: "Zap", values: make([
            (SynthParamOscMix, 1.0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, 1), (SynthParamOsc2PitchEnv, 0.6),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 2500),
            (SynthParamFilterResonance, 0.45), (SynthParamFilterEnvAmount, 0.3),
            (SynthParamFilterAttack, 0.001), (SynthParamFilterDecay, 0.18),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.2),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.08),
            (SynthParamMasterGain, 0.7),
        ])),

        FactoryPreset(name: "Laser", values: make([
            (SynthParamOscMix, 0.3), (SynthParamOscCrossMod, 0.4),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Octave, 1),
            (SynthParamOsc2PitchEnv, 0.5), (SynthParamFilterSlope, 0),
            (SynthParamFilterCutoff, 5000), (SynthParamFilterResonance, 0.3),
            (SynthParamFilterEnvAmount, 0.2), (SynthParamFilterAttack, 0.001),
            (SynthParamFilterDecay, 0.15), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.18),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.08),
            (SynthParamMasterGain, 0.68),
        ])),

        FactoryPreset(name: "Noise Clap", values: make([
            (SynthParamOscMix, 0.0), (SynthParamNoiseMix, 1.0),
            (SynthParamFilterSlope, 0), (SynthParamFilterCutoff, 3000),
            (SynthParamFilterResonance, 0.25), (SynthParamFilterEnvAmount, 0.1),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.12),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.06),
            (SynthParamMasterGain, 0.65), (SynthParamStereoSpread, 0.6),
            (SynthParamReverbMix, 0.24), (SynthParamReverbSize, 0.5),
            (SynthParamReverbDecay, 1.0),
        ])),

        // ---------------------------------------------------------------
        // Arps  (tight plucks with fast filter decay — great for
        // arpeggiator / step-sequenced lines)
        // ---------------------------------------------------------------

        FactoryPreset(name: "Arp Saw", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 6), (SynthParamOscMix, 0.35),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 3000),
            (SynthParamFilterResonance, 0.25), (SynthParamFilterEnvAmount, 0.55),
            (SynthParamFilterDecay, 0.15), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.25),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.15),
            (SynthParamVelToCutoff, 0.4), (SynthParamAnalogAmount, 0.25),
            (SynthParamMasterGain, 0.72),
            (SynthParamArpOn, 1), (SynthParamArpMode, 0), (SynthParamArpOctaves, 2),
            (SynthParamArpRate, 11), (SynthParamArpGate, 0.5),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.28), (SynthParamDelayTime, 11),
            (SynthParamDelayFeedback, 0.38), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Arp Square", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOscMix, 0.0),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 3500),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterDecay, 0.15), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.2),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.12),
            (SynthParamVelToCutoff, 0.4), (SynthParamMasterGain, 0.72),
            (SynthParamArpOn, 1), (SynthParamArpMode, 0), (SynthParamArpOctaves, 2),
            (SynthParamArpRate, 11), (SynthParamArpGate, 0.5),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.28), (SynthParamDelayTime, 11),
            (SynthParamDelayFeedback, 0.38), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Arp Bell", values: make([
            (SynthParamOscMix, 0.3), (SynthParamOscCrossMod, 0.25),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 12),
            (SynthParamFilterCutoff, 9000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.2), (SynthParamFilterDecay, 0.2),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.35),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.25),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.4),
            (SynthParamMasterGain, 0.7),
            (SynthParamArpOn, 1), (SynthParamArpMode, 2), (SynthParamArpOctaves, 2),
            (SynthParamArpRate, 11), (SynthParamArpGate, 0.4),
            (SynthParamStereoSpread, 0.55),
            (SynthParamDelayMix, 0.3), (SynthParamDelayTime, 11),
            (SynthParamDelayFeedback, 0.4), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.28), (SynthParamReverbSize, 0.75),
            (SynthParamReverbDecay, 3.0),
        ])),

        FactoryPreset(name: "Arp Sync", values: make([
            (SynthParamOscMix, 0.8), (SynthParamOsc2Sync, 1),
            (SynthParamOsc2Semitone, 5), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 6000), (SynthParamFilterResonance, 0.15),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.18),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.25),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.12),
            (SynthParamVelToCutoff, 0.3), (SynthParamMasterGain, 0.72),
            (SynthParamArpOn, 1), (SynthParamArpMode, 0), (SynthParamArpOctaves, 2),
            (SynthParamArpRate, 12), (SynthParamArpGate, 0.45),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.26), (SynthParamDelayTime, 11),
            (SynthParamDelayFeedback, 0.36), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.0),
        ])),

        FactoryPreset(name: "Arp Octave", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Octave, 1), (SynthParamOscMix, 0.4),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 4000),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterDecay, 0.15), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.22),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.12),
            (SynthParamVelToCutoff, 0.35), (SynthParamAnalogAmount, 0.25),
            (SynthParamMasterGain, 0.72),
            (SynthParamArpOn, 1), (SynthParamArpMode, 0), (SynthParamArpOctaves, 3),
            (SynthParamArpRate, 12), (SynthParamArpGate, 0.4),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.28), (SynthParamDelayTime, 12),
            (SynthParamDelayFeedback, 0.38), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Arp Digital", values: make([
            (SynthParamOscMix, 0.2), (SynthParamOscCrossMod, 0.3),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 7),
            (SynthParamFilterCutoff, 8000), (SynthParamFilterResonance, 0.1),
            (SynthParamFilterEnvAmount, 0.25), (SynthParamFilterDecay, 0.15),
            (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.2),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.1),
            (SynthParamVelToCutoff, 0.4), (SynthParamVelToVolume, 1.0),
            (SynthParamMasterGain, 0.72),
            (SynthParamArpOn, 1), (SynthParamArpMode, 3), (SynthParamArpOctaves, 2),
            (SynthParamArpRate, 12), (SynthParamArpGate, 0.4),
            (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.3), (SynthParamDelayTime, 12),
            (SynthParamDelayFeedback, 0.4), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.22), (SynthParamReverbDecay, 2.2),
        ])),

        // ---------------------------------------------------------------
        // Wavetable collection  (Osc waveform = WT). Choir presets add
        // vibrato, detune and a touch of breath to sound less static.
        // ---------------------------------------------------------------

        FactoryPreset(name: "WT Choir", values: make([
            (SynthParamOscWaveform, 3), (SynthParamOsc2Waveform, 3),
            (SynthParamWavetable, 2), (SynthParamWTFrame, 0.45),
            (SynthParamWTLiveness, 0.45), (SynthParamOsc2Detune, 11),
            (SynthParamOscMix, 0.5), (SynthParamFilterCutoff, 8000),
            (SynthParamFilterResonance, 0.08), (SynthParamFilterEnvAmount, 0.1),
            (SynthParamAmpAttack, 0.35), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 1.4),
            (SynthParamFilterAttack, 0.5), (SynthParamFilterSustain, 0.7),
            (SynthParamLFOToOscFreq, 0.12), (SynthParamLFORate, 5.2),
            (SynthParamLFODelay, 0.4), (SynthParamNoiseMix, 0.03),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.85),
            (SynthParamMasterGain, 0.66), (SynthParamStereoSpread, 0.85),
            (SynthParamChorusMix, 0.3), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.32), (SynthParamReverbSize, 0.82),
            (SynthParamReverbDecay, 4.5),
        ], mods: [(ModSrcModWheel, ModDstWTFrame, 0.3), (ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "WT Angels", values: make([
            (SynthParamOscWaveform, 3), (SynthParamOsc2Waveform, 3),
            (SynthParamWavetable, 2), (SynthParamWTFrame, 0.85),
            (SynthParamWTLiveness, 0.6), (SynthParamOsc2Detune, 13),
            (SynthParamOscMix, 0.5), (SynthParamFilterCutoff, 10000),
            (SynthParamFilterResonance, 0.07), (SynthParamFilterEnvAmount, 0.1),
            (SynthParamAmpAttack, 0.6), (SynthParamAmpDecay, 1.2),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 2.0),
            (SynthParamFilterAttack, 0.7), (SynthParamFilterSustain, 0.7),
            (SynthParamLFOToOscFreq, 0.1), (SynthParamLFORate, 4.8),
            (SynthParamLFODelay, 0.5), (SynthParamNoiseMix, 0.04),
            (SynthParamAnalogAmount, 0.5), (SynthParamOscPhaseSpread, 0.95),
            (SynthParamMasterGain, 0.6), (SynthParamStereoSpread, 0.95),
            (SynthParamChorusMix, 0.32), (SynthParamChorusDepth, 0.45),
            (SynthParamReverbMix, 0.42), (SynthParamReverbSize, 0.92),
            (SynthParamReverbDecay, 6.5), (SynthParamReverbTone, 0.5),
            (SynthParamReverbPreDelay, 0.04),
        ], mods: [(ModSrcModWheel, ModDstWTFrame, 0.3)])),

        FactoryPreset(name: "WT FM Bell", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 1),
            (SynthParamWTFrame, 0.6), (SynthParamWTLiveness, 0.3),
            (SynthParamOscMix, 0.0), (SynthParamFilterCutoff, 12000),
            (SynthParamFilterResonance, 0.05), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamFilterDecay, 0.8), (SynthParamFilterSustain, 0.1),
            (SynthParamWTFrameEnv, 0.3),   // bright clang on attack -> mellows
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 1.5),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 1.2),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.4),
            (SynthParamMasterGain, 0.7), (SynthParamStereoSpread, 0.55),
            (SynthParamDelayMix, 0.2), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.3), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.3), (SynthParamReverbSize, 0.8),
            (SynthParamReverbDecay, 4.0),
        ], mods: [(ModSrcModWheel, ModDstWTFrame, 0.4)])),

        FactoryPreset(name: "WT FM Piano", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 1),
            (SynthParamWTFrame, 0.32), (SynthParamWTLiveness, 0.15),
            (SynthParamOscMix, 0.0), (SynthParamFilterCutoff, 9000),
            (SynthParamFilterResonance, 0.05), (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.6),
            (SynthParamAmpSustain, 0.25), (SynthParamAmpRelease, 0.4),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.45),
            (SynthParamMasterGain, 0.72), (SynthParamStereoSpread, 0.4),
            (SynthParamChorusMix, 0.18), (SynthParamChorusDepth, 0.3),
            (SynthParamReverbMix, 0.2), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 2.0),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "WT Glass Pad", values: make([
            (SynthParamOscWaveform, 3), (SynthParamOsc2Waveform, 3),
            (SynthParamWavetable, 3), (SynthParamWTFrame, 0.4),
            (SynthParamWTLiveness, 0.5), (SynthParamOsc2Detune, 8),
            (SynthParamOscMix, 0.5), (SynthParamFilterCutoff, 9000),
            (SynthParamFilterResonance, 0.1), (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.5), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 1.8),
            (SynthParamFilterAttack, 0.6), (SynthParamFilterSustain, 0.7),
            (SynthParamLFOToOscFreq, 0.06), (SynthParamLFORate, 4.0),
            (SynthParamAnalogAmount, 0.4), (SynthParamOscPhaseSpread, 0.9),
            (SynthParamMasterGain, 0.64), (SynthParamLFO2Rate, 0.3),
            (SynthParamStereoSpread, 0.88),
            (SynthParamChorusMix, 0.28), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.34), (SynthParamReverbSize, 0.85),
            (SynthParamReverbDecay, 5.0),
        ], mods: [(ModSrcLFO2, ModDstWTFrame, 0.2), (ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "WT Metal Lead", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 3),
            (SynthParamWTFrame, 0.7), (SynthParamWTLiveness, 0.3),
            (SynthParamOscMix, 0.0), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 6000), (SynthParamFilterResonance, 0.2),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.4),
            (SynthParamFilterSustain, 0.5),
            (SynthParamAmpAttack, 0.01), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.3),
            (SynthParamLFOToOscFreq, 0.15), (SynthParamLFORate, 5.5),
            (SynthParamVelToCutoff, 0.3), (SynthParamAnalogAmount, 0.3),
            (SynthParamMasterGain, 0.7), (SynthParamStereoSpread, 0.5),
            (SynthParamDelayMix, 0.24), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.36), (SynthParamDelayPingPong, 1),
            (SynthParamReverbMix, 0.2), (SynthParamReverbDecay, 2.2),
        ], mods: [(ModSrcModWheel, ModDstWTFrame, 0.4), (ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "WT Harmonic Pad", values: make([
            (SynthParamOscWaveform, 3), (SynthParamOsc2Waveform, 3),
            (SynthParamWavetable, 0), (SynthParamWTFrame, 0.42),
            (SynthParamWTLiveness, 0.4), (SynthParamOsc2Detune, 10),
            (SynthParamOscMix, 0.5), (SynthParamFilterCutoff, 4000),
            (SynthParamFilterResonance, 0.12), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.4), (SynthParamAmpDecay, 1.0),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 1.4),
            (SynthParamFilterAttack, 0.6), (SynthParamFilterSustain, 0.6),
            (SynthParamAnalogAmount, 0.5), (SynthParamOscPhaseSpread, 0.9),
            (SynthParamMasterGain, 0.68), (SynthParamLFO2Rate, 0.3),
            (SynthParamStereoSpread, 0.85),
            (SynthParamChorusMix, 0.28), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.28), (SynthParamReverbSize, 0.78),
            (SynthParamReverbDecay, 3.8),
        ], mods: [(ModSrcLFO2, ModDstWTFrame, 0.15), (ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "WT Guitar Pluck", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 0),
            (SynthParamWTFrame, 0.85), (SynthParamWTLiveness, 0.1),
            (SynthParamOscMix, 0.0), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 5000), (SynthParamFilterResonance, 0.15),
            (SynthParamFilterEnvAmount, 0.4), (SynthParamFilterDecay, 0.3),
            (SynthParamFilterSustain, 0.1), (SynthParamWTFrameEnv, 0.12),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 0.6),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.3),
            (SynthParamVelToCutoff, 0.4), (SynthParamVelToVolume, 1.0),
            (SynthParamAnalogAmount, 0.25), (SynthParamMasterGain, 0.74),
            (SynthParamStereoSpread, 0.45),
            (SynthParamDelayMix, 0.2), (SynthParamDelayTime, 7),
            (SynthParamDelayFeedback, 0.3),
            (SynthParamReverbMix, 0.22), (SynthParamReverbSize, 0.6),
            (SynthParamReverbDecay, 1.8),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.25)])),

        FactoryPreset(name: "WT Nylon", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 0),
            (SynthParamWTFrame, 0.95), (SynthParamWTLiveness, 0.1),
            (SynthParamOscMix, 0.0), (SynthParamFilterCutoff, 4000),
            (SynthParamFilterResonance, 0.1), (SynthParamFilterEnvAmount, 0.3),
            (SynthParamFilterDecay, 0.4), (SynthParamFilterSustain, 0.15),
            (SynthParamAmpAttack, 0.003), (SynthParamAmpDecay, 0.8),
            (SynthParamAmpSustain, 0.1), (SynthParamAmpRelease, 0.4),
            (SynthParamVelToCutoff, 0.35), (SynthParamVelToVolume, 1.0),
            (SynthParamAnalogAmount, 0.2), (SynthParamMasterGain, 0.74),
            (SynthParamStereoSpread, 0.4),
            (SynthParamReverbMix, 0.24), (SynthParamReverbSize, 0.62),
            (SynthParamReverbDecay, 2.0),
        ])),

        FactoryPreset(name: "WT Sweep", values: make([
            (SynthParamOscWaveform, 3), (SynthParamOsc2Waveform, 3),
            (SynthParamWavetable, 0), (SynthParamWTFrame, 0.5),
            (SynthParamWTLiveness, 0.5), (SynthParamOsc2Detune, 7),
            (SynthParamOscMix, 0.5), (SynthParamFilterSlope, 1),
            (SynthParamFilterCutoff, 2000), (SynthParamFilterResonance, 0.3),
            (SynthParamFilterEnvAmount, 0.6), (SynthParamFilterDecay, 0.8),
            (SynthParamFilterSustain, 0.3),
            (SynthParamAmpAttack, 0.05), (SynthParamAmpDecay, 0.5),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.5),
            (SynthParamLFOToCutoff, 0.3), (SynthParamLFOToWTFrame, 0.4),
            (SynthParamLFORate, 0.4), (SynthParamAnalogAmount, 0.35),
            (SynthParamMasterGain, 0.7), (SynthParamStereoSpread, 0.8),
            (SynthParamChorusMix, 0.26), (SynthParamChorusDepth, 0.4),
            (SynthParamReverbMix, 0.3), (SynthParamReverbSize, 0.8),
            (SynthParamReverbDecay, 4.0),
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.3)])),
    ]

    // Display order of the preset-browser category submenus.
    static let categoryOrder = [
        "Basic", "Bass", "Keys", "Pad", "Lead", "Bells", "Brass", "Stab", "Wave", "Drums", "Arp"
    ]

    static func category(for name: String) -> String {
        switch name {
        case "Init":
            return "Basic"
        case "Fat Saw Bass", "Wobble Bass", "DX Bass", "Synthwave Bass", "Fat Unison Bass":
            return "Bass"
        case "DX Electric Piano", "Digital Piano", "Warm Keys":
            return "Keys"
        case "Warm Pad", "PWM Strings", "Juno Pad", "Juno Poly Pad", "Analog Strings", "Cinematic Pad", "Bell Pad":
            return "Pad"
        case "Bright Pluck", "Acid Lead", "Detuned Unison", "Sync Sweep Lead",
             "Saw Lead", "Square Lead", "Dream Lead":
            return "Lead"
        case "TZ-FM Bells", "Glass Bells", "Mallet", "Bright Chime":
            return "Bells"
        case "Vintage Brass", "Poly Brass", "Brass Stab":
            return "Brass"
        case "Fifth Stab", "Sync Stab", "Orchestra Hit":
            return "Stab"
        case "WT Choir", "WT Angels", "WT FM Bell", "WT FM Piano", "WT Glass Pad",
             "WT Metal Lead", "WT Harmonic Pad", "WT Guitar Pluck", "WT Nylon", "WT Sweep":
            return "Wave"
        case "Synth Kick", "Synth Snare", "Closed Hat", "Open Hat",
             "Synth Tom", "Zap", "Laser", "Noise Clap":
            return "Drums"
        case "Pluck Arp", "Arp Saw", "Arp Square", "Arp Bell",
             "Arp Sync", "Arp Octave", "Arp Digital":
            return "Arp"
        default:
            return "Basic"
        }
    }
}
