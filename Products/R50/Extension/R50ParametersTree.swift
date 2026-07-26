//
//  R50ParametersTree.swift
//  Declarative definition of the AUParameterTree. Addresses come from the
//  shared C header (R50Parameters.h) so Swift and C++ agree on every index.
//

import AudioToolbox

enum R50Parameters {

    /// Order must match waveDescriptors() in R50Wave.hpp.
    static let waveformNames = [
        "Saw", "Triangle", "Square", "Pulse 10%", "Pulse",
        "Organ", "Tine", "Clarinet", "Strings", "Vocal Ah", "Bell"
    ]
    static let slopeNames    = ["12 dB", "24 dB"]

    private static let readWrite: AudioUnitParameterOptions =
        [.flag_IsReadable, .flag_IsWritable]
    private static let readWriteLog: AudioUnitParameterOptions =
        [.flag_IsReadable, .flag_IsWritable, .flag_DisplayLogarithmic]

    private static func param(_ addr: R50Param,
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

        let osc = AUParameterTree.createGroup(
            withIdentifier: "osc", name: "Oscillator", children: [
                param(R50ParamOscWave, "oscWave", "Waveform",
                      min: 0, max: AUValue(waveformNames.count - 1), value: 0,
                      unit: .indexed, strings: waveformNames),
                param(R50ParamPulseWidth, "pulseWidth", "Pulse Width",
                      min: 0.02, max: 0.98, value: 0.5),
                param(R50ParamOctave, "octave", "Octave",
                      min: -2, max: 2, value: 0, unit: .indexed),
            ])

        let filter = AUParameterTree.createGroup(
            withIdentifier: "filter", name: "Filter", children: [
                param(R50ParamCutoff, "cutoff", "Cutoff",
                      min: 20, max: 18000, value: 3200,
                      unit: .hertz, log: true),
                param(R50ParamResonance, "resonance", "Resonance",
                      min: 0, max: 1, value: 0.15),
                param(R50ParamDrive, "drive", "Drive",
                      min: 0, max: 1, value: 0),
                param(R50ParamSlope, "slope", "Slope",
                      min: 0, max: 1, value: 1, unit: .indexed,
                      strings: slopeNames),
                param(R50ParamKeyTrack, "keyTrack", "Key Track",
                      min: 0, max: 1, value: 0.5),
                param(R50ParamFilterEnvAmount, "filterEnvAmount", "Env Amount",
                      min: -1, max: 1, value: 0.45),
            ])

        let ampEnv = AUParameterTree.createGroup(
            withIdentifier: "ampEnv", name: "Amp Envelope", children: [
                param(R50ParamAmpAttack, "ampAttack", "Attack",
                      min: 0.001, max: 8, value: 0.004,
                      unit: .seconds, log: true),
                param(R50ParamAmpDecay, "ampDecay", "Decay",
                      min: 0.001, max: 8, value: 0.25,
                      unit: .seconds, log: true),
                param(R50ParamAmpSustain, "ampSustain", "Sustain",
                      min: 0, max: 1, value: 0.75),
                param(R50ParamAmpRelease, "ampRelease", "Release",
                      min: 0.001, max: 8, value: 0.30,
                      unit: .seconds, log: true),
            ])

        let filterEnv = AUParameterTree.createGroup(
            withIdentifier: "filterEnv", name: "Filter Envelope", children: [
                param(R50ParamFilterAttack, "filterAttack", "Attack",
                      min: 0.001, max: 8, value: 0.004,
                      unit: .seconds, log: true),
                param(R50ParamFilterDecay, "filterDecay", "Decay",
                      min: 0.001, max: 8, value: 0.45,
                      unit: .seconds, log: true),
                param(R50ParamFilterSustain, "filterSustain", "Sustain",
                      min: 0, max: 1, value: 0.30),
                param(R50ParamFilterRelease, "filterRelease", "Release",
                      min: 0.001, max: 8, value: 0.30,
                      unit: .seconds, log: true),
            ])

        let global = AUParameterTree.createGroup(
            withIdentifier: "global", name: "Global", children: [
                param(R50ParamMasterGain, "masterGain", "Master",
                      min: 0, max: 1, value: 0.8),
                param(R50ParamPitchBendRange, "bendRange", "Bend Range",
                      min: 0, max: 24, value: 2, unit: .indexed),
            ])

        return AUParameterTree.createTree(
            withChildren: [osc, filter, ampEnv, filterEnv, global])
    }
}
