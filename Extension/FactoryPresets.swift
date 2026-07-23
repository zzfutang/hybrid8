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
            (SynthParamMasterGain, 0.74),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "PWM Strings", values: make([
            (SynthParamOscWaveform, 2), (SynthParamOscPulseWidth, 0.5),
            (SynthParamLFOToPulseWidth, 0.45), (SynthParamLFORate, 0.7),
            (SynthParamFilterCutoff, 3500), (SynthParamFilterResonance, 0.12),
            (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAmpAttack, 0.35), (SynthParamAmpSustain, 0.85),
            (SynthParamAmpRelease, 0.7), (SynthParamAnalogAmount, 0.5),
            (SynthParamOscPhaseSpread, 0.8),
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
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.35), (ModSrcLFO2, ModDstCutoff, 0.1)])),

        FactoryPreset(name: "Wobble Bass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOctave, -1),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 500),
            (SynthParamFilterResonance, 0.5), (SynthParamLFOToCutoff, 0.7),
            (SynthParamLFORate, 3.0), (SynthParamLFOWaveform, 0),
            (SynthParamAmpSustain, 0.9), (SynthParamFilterEnvAmount, 0.2),
            (SynthParamAnalogAmount, 0.3), (SynthParamVoiceCount, 1),
            (SynthParamMasterGain, 0.76),
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
            (SynthParamAnalogAmount, 0.3),
        ], mods: [(ModSrcModWheel, ModDstCrossMod, 0.25)])),

        FactoryPreset(name: "Vintage Brass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamFilterCutoff, 3000),
            (SynthParamFilterResonance, 0.25), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterAttack, 0.05), (SynthParamFilterDecay, 0.4),
            (SynthParamFilterSustain, 0.4),
            (SynthParamAmpAttack, 0.03), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.3),
            (SynthParamAnalogAmount, 0.6), (SynthParamOscPhaseSpread, 0.6),
            (SynthParamNoiseMix, 0.03),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

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
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Poly Brass", values: make([
            (SynthParamOscWaveform, 0), (SynthParamOsc2Waveform, 0),
            (SynthParamOsc2Detune, 8), (SynthParamOscMix, 0.5),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 1500),
            (SynthParamFilterResonance, 0.15), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterAttack, 0.08), (SynthParamFilterDecay, 0.5),
            (SynthParamFilterSustain, 0.5),
            (SynthParamAmpAttack, 0.04), (SynthParamAmpDecay, 0.4),
            (SynthParamAmpSustain, 0.85), (SynthParamAmpRelease, 0.3),
            (SynthParamVelToCutoff, 0.4), (SynthParamAnalogAmount, 0.4),
            (SynthParamOscPhaseSpread, 0.6), (SynthParamMasterGain, 0.7),
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.35)])),

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
            (SynthParamLFO2Rate, 0.25),
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
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.3)])),

        FactoryPreset(name: "Glass Bells", values: make([
            (SynthParamOscMix, 0.25), (SynthParamOscCrossMod, 0.45),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 12),
            (SynthParamLFOToCrossMod, 0.2), (SynthParamLFORate, 2.5),
            (SynthParamFilterCutoff, 12000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.3), (SynthParamFilterDecay, 0.8),
            (SynthParamFilterSustain, 0.1),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 1.5),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 1.2),
            (SynthParamVelToCutoff, 0.5), (SynthParamVelToVolume, 1.0),
            (SynthParamAnalogAmount, 0.2), (SynthParamMasterGain, 0.7),
        ], mods: [(ModSrcModWheel, ModDstCrossMod, 0.3)])),

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
            (SynthParamLFO2Rate, 0.3),
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
            (SynthParamMasterGain, 0.72),
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
            (SynthParamMasterGain, 0.72),
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
        ], mods: [(ModSrcAftertouch, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Square Lead", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOscMix, 0.0),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 4500),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.25),
            (SynthParamAmpAttack, 0.01), (SynthParamAmpDecay, 0.3),
            (SynthParamAmpSustain, 0.8), (SynthParamAmpRelease, 0.2),
            (SynthParamLFOToOscFreq, 0.2), (SynthParamLFORate, 6.0),
            (SynthParamAnalogAmount, 0.3), (SynthParamMasterGain, 0.72),
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
            (SynthParamMasterGain, 0.64), (SynthParamLFO2Rate, 0.15),
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
            (SynthParamMasterGain, 0.72),
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
            (SynthParamMasterGain, 0.65),
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
            (SynthParamMasterGain, 0.72),
        ])),

        FactoryPreset(name: "Digital Piano", values: make([
            (SynthParamOscMix, 0.2), (SynthParamOscCrossMod, 0.18),
            (SynthParamOscCrossModTZ, 1), (SynthParamOsc2Semitone, 0),
            (SynthParamFilterCutoff, 10000), (SynthParamFilterResonance, 0.05),
            (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.8),
            (SynthParamAmpSustain, 0.15), (SynthParamAmpRelease, 0.5),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.45),
            (SynthParamMasterGain, 0.74),
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
        ])),

        FactoryPreset(name: "Closed Hat", values: make([
            (SynthParamOscMix, 0.0), (SynthParamNoiseMix, 1.0),
            (SynthParamFilterSlope, 0), (SynthParamFilterCutoff, 11000),
            (SynthParamFilterResonance, 0.15), (SynthParamFilterEnvAmount, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.04),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.02),
            (SynthParamMasterGain, 0.6),
        ])),

        FactoryPreset(name: "Open Hat", values: make([
            (SynthParamOscMix, 0.0), (SynthParamNoiseMix, 1.0),
            (SynthParamFilterSlope, 0), (SynthParamFilterCutoff, 11000),
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
            (SynthParamMasterGain, 0.65),
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
        ], mods: [(ModSrcModWheel, ModDstCutoff, 0.4)])),

        FactoryPreset(name: "Arp Square", values: make([
            (SynthParamOscWaveform, 1), (SynthParamOscMix, 0.0),
            (SynthParamFilterSlope, 1), (SynthParamFilterCutoff, 3500),
            (SynthParamFilterResonance, 0.2), (SynthParamFilterEnvAmount, 0.5),
            (SynthParamFilterDecay, 0.15), (SynthParamFilterSustain, 0.0),
            (SynthParamAmpAttack, 0.001), (SynthParamAmpDecay, 0.2),
            (SynthParamAmpSustain, 0.0), (SynthParamAmpRelease, 0.12),
            (SynthParamVelToCutoff, 0.4), (SynthParamMasterGain, 0.72),
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
            (SynthParamMasterGain, 0.66),
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
            (SynthParamMasterGain, 0.62),
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
            (SynthParamMasterGain, 0.7),
        ], mods: [(ModSrcModWheel, ModDstWTFrame, 0.4)])),

        FactoryPreset(name: "WT FM Piano", values: make([
            (SynthParamOscWaveform, 3), (SynthParamWavetable, 1),
            (SynthParamWTFrame, 0.32), (SynthParamWTLiveness, 0.15),
            (SynthParamOscMix, 0.0), (SynthParamFilterCutoff, 9000),
            (SynthParamFilterResonance, 0.05), (SynthParamFilterEnvAmount, 0.15),
            (SynthParamAmpAttack, 0.002), (SynthParamAmpDecay, 1.6),
            (SynthParamAmpSustain, 0.25), (SynthParamAmpRelease, 0.4),
            (SynthParamVelToVolume, 1.0), (SynthParamVelToCutoff, 0.45),
            (SynthParamMasterGain, 0.72),
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
            (SynthParamMasterGain, 0.7),
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
            (SynthParamMasterGain, 0.7),
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
        case "DX Electric Piano", "Digital Piano":
            return "Keys"
        case "Warm Pad", "PWM Strings", "Juno Pad", "Analog Strings", "Cinematic Pad", "Bell Pad":
            return "Pad"
        case "Bright Pluck", "Acid Lead", "Detuned Unison", "Sync Sweep Lead",
             "Saw Lead", "Square Lead", "Dream Lead":
            return "Lead"
        case "TZ-FM Bells", "Glass Bells", "Mallet", "Bright Chime":
            return "Bells"
        case "Vintage Brass", "Poly Brass":
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
