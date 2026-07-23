//
//  SynthParametersTree.swift
//  Declarative definition of the AUParameterTree. Addresses come from the
//  shared C header (SynthParameters.h) so Swift and C++ agree on every index.
//

import AudioToolbox

enum SynthParameters {

    private static let readWrite: AudioUnitParameterOptions =
        [.flag_IsReadable, .flag_IsWritable]
    private static let readWriteLog: AudioUnitParameterOptions =
        [.flag_IsReadable, .flag_IsWritable, .flag_DisplayLogarithmic]

    private static func param(_ addr: SynthParam,
                              _ id: String,
                              _ name: String,
                              min: AUValue,
                              max: AUValue,
                              value: AUValue,
                              unit: AudioUnitParameterUnit = .generic,
                              unitName: String? = nil,
                              log: Bool = false,
                              strings: [String]? = nil) -> AUParameter {
        let p = AUParameterTree.createParameter(
            withIdentifier: id,
            name: name,
            address: AUParameterAddress(addr.rawValue),
            min: min, max: max,
            unit: unit,
            unitName: unitName,
            flags: log ? readWriteLog : readWrite,
            valueStrings: strings,
            dependentParameters: nil)
        p.value = value
        return p
    }

    static func buildTree() -> AUParameterTree {

        // MARK: Oscillator
        let osc = AUParameterTree.createGroup(
            withIdentifier: "osc", name: "Oscillator", children: [
            param(SynthParamOscWaveform, "oscWave", "Waveform",
                  min: 0, max: 3, value: 0, unit: .indexed,
                  strings: ["Saw", "Square", "Pulse", "Wavetable"]),
            param(SynthParamOscPulseWidth, "pulseWidth", "Pulse Width",
                  min: 0.02, max: 0.98, value: 0.5),
            param(SynthParamWavetable, "wavetable", "Wavetable",
                  min: 0, max: 3, value: 0, unit: .indexed,
                  strings: ["Harmonic", "FM", "Choir", "Metallic"]),
            param(SynthParamWTFrame, "wtFrame", "WT Frame",
                  min: 0, max: 1, value: 0),
            param(SynthParamWTLiveness, "wtLiveness", "WT Liveness",
                  min: 0, max: 1, value: 0.25),
            param(SynthParamWTFrameEnv, "wtFrameEnv", "WT Frame Env",
                  min: -1, max: 1, value: 0),
            param(SynthParamOctave, "octave", "Octave",
                  min: -2, max: 2, value: 0, unit: .indexed,
                  strings: ["-2", "-1", "0", "+1", "+2"]),
        ])

        // MARK: Oscillator 2
        let osc2 = AUParameterTree.createGroup(
            withIdentifier: "osc2", name: "Oscillator 2", children: [
            param(SynthParamOsc2Waveform, "osc2Wave", "Osc 2 Waveform",
                  min: 0, max: 3, value: 0, unit: .indexed,
                  strings: ["Saw", "Square", "Pulse", "Wavetable"]),
            param(SynthParamOsc2PulseWidth, "osc2PW", "Osc 2 Pulse Width",
                  min: 0.02, max: 0.98, value: 0.5),
            param(SynthParamOsc2Octave, "osc2Octave", "Osc 2 Octave",
                  min: -2, max: 2, value: 0, unit: .indexed,
                  strings: ["-2", "-1", "0", "+1", "+2"]),
            param(SynthParamOsc2Semitone, "osc2Semitone", "Osc 2 Semitone",
                  min: -12, max: 12, value: 0, unit: .relativeSemiTones),
            param(SynthParamOsc2Detune, "osc2Detune", "Osc 2 Detune",
                  min: -100, max: 100, value: 0, unit: .cents),
            param(SynthParamOsc2Sync, "osc2Sync", "Osc 2 Sync",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: ["Off", "On"]),
            param(SynthParamOscCrossMod, "crossMod", "Cross Mod",
                  min: 0, max: 1, value: 0),
            param(SynthParamOscCrossModTZ, "crossModTZ", "Cross Mod Through-Zero",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: ["Exp", "TZ"]),
            param(SynthParamOsc2PitchEnv, "osc2PitchEnv", "Osc 2 Pitch Env",
                  min: -1, max: 1, value: 0),
        ])

        // MARK: Mixer
        let mixer = AUParameterTree.createGroup(
            withIdentifier: "mixer", name: "Mixer", children: [
            param(SynthParamOsc1Level, "osc1Level", "Osc 1 Level",
                  min: 0, max: 1, value: 1),
            param(SynthParamOsc2Level, "osc2Level", "Osc 2 Level",
                  min: 0, max: 1, value: 0),
            param(SynthParamNoiseLevel, "noiseLevel", "Noise Level",
                  min: 0, max: 1, value: 0),
        ])

        // MARK: Amp envelope
        let ampEnv = AUParameterTree.createGroup(
            withIdentifier: "ampEnv", name: "Amp Envelope", children: [
            param(SynthParamAmpAttack, "ampA", "Amp Attack",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.005)),
            param(SynthParamAmpDecay, "ampD", "Amp Decay",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.15)),
            param(SynthParamAmpSustain, "ampS", "Amp Sustain",
                  min: 0, max: 1, value: 0.8),
            param(SynthParamAmpRelease, "ampR", "Amp Release",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.25)),
        ])

        // MARK: Filter
        let filter = AUParameterTree.createGroup(
            withIdentifier: "filter", name: "Filter", children: [
            param(SynthParamFilterCutoff, "cutoff", "Cutoff",
                  min: 20, max: 20000, value: 6000, unit: .hertz, log: true),
            param(SynthParamFilterResonance, "resonance", "Resonance",
                  min: 0, max: 1, value: 0.15),
            param(SynthParamFilterDrive, "filterDrive", "Drive",
                  min: 0, max: 1, value: 0),
            param(SynthParamFilterEnvAmount, "filterEnvAmt", "Env Amount",
                  min: -1, max: 1, value: 0.5),
            param(SynthParamFilterSlope, "slope", "Slope",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: ["12 dB/oct", "24 dB/oct"]),
            param(SynthParamFilterKeyTrack, "keyTrack", "Key Track",
                  min: 0, max: 1, value: 0),
        ])

        // MARK: Filter envelope
        let filtEnv = AUParameterTree.createGroup(
            withIdentifier: "filtEnv", name: "Filter Envelope", children: [
            param(SynthParamFilterAttack, "filtA", "Filter Attack",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.01)),
            param(SynthParamFilterDecay, "filtD", "Filter Decay",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.30)),
            param(SynthParamFilterSustain, "filtS", "Filter Sustain",
                  min: 0, max: 1, value: 0.4),
            param(SynthParamFilterRelease, "filtR", "Filter Release",
                  min: 0, max: 1, value: SynthTime.norm(fromSeconds: 0.4)),
        ])

        // MARK: LFO
        let lfo = AUParameterTree.createGroup(
            withIdentifier: "lfo", name: "LFO", children: [
            param(SynthParamLFOWaveform, "lfoWave", "LFO Waveform",
                  min: 0, max: 2, value: 0, unit: .indexed,
                  strings: ["Sine", "Square", "Saw"]),
            param(SynthParamLFOKeyTrigger, "lfoKeyTrig", "LFO Key Trigger",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: ["Free", "Key"]),
            param(SynthParamLFORate, "lfoRate", "LFO Rate",
                  min: 0.05, max: 30, value: 5, unit: .hertz, log: true),
            param(SynthParamLFODelay, "lfoDelay", "LFO Delay",
                  min: 0, max: 2, value: 0, unit: .seconds),
            param(SynthParamLFOToOscFreq, "lfoOsc", "LFO \u{2192} Osc Freq",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFOToPulseWidth, "lfoPW", "LFO \u{2192} Pulse Width",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFOToCutoff, "lfoCut", "LFO \u{2192} Cutoff",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFOToResonance, "lfoRes", "LFO \u{2192} Resonance",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFOToCrossMod, "lfoXMod", "LFO \u{2192} Cross Mod",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFOToWTFrame, "lfoWTFrame", "LFO \u{2192} WT Frame",
                  min: 0, max: 1, value: 0),
            param(SynthParamLFO2Waveform, "lfo2Wave", "LFO 2 Waveform",
                  min: 0, max: 2, value: 0, unit: .indexed,
                  strings: ["Sine", "Square", "Saw"]),
            param(SynthParamLFO2Rate, "lfo2Rate", "LFO 2 Rate",
                  min: 0.05, max: 30, value: 2, unit: .hertz, log: true),
        ])

        // MARK: Modulation matrix
        let modSources = ["None", "LFO 1", "LFO 2", "Filt Env", "Amp Env",
                          "Velocity", "Key Trk", "Mod Whl", "Aftertch", "Random"]
        let modDests   = ["None", "Osc Pitch", "Osc2 Pitch", "Pulse W", "Cutoff",
                          "Reso", "Drive", "WT Frame", "WT Live", "X-Mod", "Amp"]
        func modSlot(_ n: Int, _ srcA: SynthParam, _ dstA: SynthParam, _ amtA: SynthParam) -> [AUParameter] {
            [param(srcA, "mod\(n)Src", "Mod \(n) Source",
                   min: 0, max: AUValue(modSources.count - 1), value: 0,
                   unit: .indexed, strings: modSources),
             param(dstA, "mod\(n)Dst", "Mod \(n) Dest",
                   min: 0, max: AUValue(modDests.count - 1), value: 0,
                   unit: .indexed, strings: modDests),
             param(amtA, "mod\(n)Amt", "Mod \(n) Amount",
                   min: -1, max: 1, value: 0)]
        }
        var modChildren: [AUParameter] = []
        modChildren += modSlot(1, SynthParamMod1Source, SynthParamMod1Dest, SynthParamMod1Amount)
        modChildren += modSlot(2, SynthParamMod2Source, SynthParamMod2Dest, SynthParamMod2Amount)
        modChildren += modSlot(3, SynthParamMod3Source, SynthParamMod3Dest, SynthParamMod3Amount)
        modChildren += modSlot(4, SynthParamMod4Source, SynthParamMod4Dest, SynthParamMod4Amount)
        modChildren += modSlot(5, SynthParamMod5Source, SynthParamMod5Dest, SynthParamMod5Amount)
        modChildren += modSlot(6, SynthParamMod6Source, SynthParamMod6Dest, SynthParamMod6Amount)
        let matrix = AUParameterTree.createGroup(
            withIdentifier: "matrix", name: "Mod Matrix", children: modChildren)

        // MARK: Velocity
        let velocity = AUParameterTree.createGroup(
            withIdentifier: "velocity", name: "Velocity", children: [
            param(SynthParamVelToVolume, "velVolume", "Vel \u{2192} Volume",
                  min: 0, max: 1, value: 1),
            param(SynthParamVelToCutoff, "velCutoff", "Vel \u{2192} Cutoff",
                  min: 0, max: 1, value: 0),
            param(SynthParamVelToResonance, "velReso", "Vel \u{2192} Resonance",
                  min: 0, max: 1, value: 0),
            param(SynthParamVelToDrive, "velDrive", "Vel \u{2192} Drive",
                  min: 0, max: 1, value: 0),
        ])

        // MARK: Global
        let global = AUParameterTree.createGroup(
            withIdentifier: "global", name: "Global", children: [
            param(SynthParamVoiceCount, "voices", "Voices",
                  min: 1, max: 8, value: 8, unit: .indexed),
            param(SynthParamLegato, "legato", "Legato",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: ["Off", "On"]),
            param(SynthParamGlideTime, "glideTime", "Glide Time",
                  min: 0, max: 1.5, value: 0, unit: .seconds),
            param(SynthParamGlideStart, "glideStart", "Glide Start",
                  min: -12, max: 12, value: 0, unit: .relativeSemiTones),
            param(SynthParamOscPhaseSpread, "phaseSpread", "Phase Spread",
                  min: 0, max: 1, value: 0.5),
            param(SynthParamAnalogAmount, "analog", "Analog",
                  min: 0, max: 1, value: 0.3),
            param(SynthParamMasterGain, "gain", "Master Gain",
                  min: 0, max: 1, value: 0.7),
            param(SynthParamPitchBendRange, "bendRange", "Pitch Bend Range",
                  min: 0, max: 24, value: 2, unit: .relativeSemiTones),
        ])

        return AUParameterTree.createTree(withChildren:
            [osc, osc2, mixer, filter, ampEnv, filtEnv, lfo, matrix, velocity, global])
    }
}
