//
//  R50ParametersTree.swift
//  Declarative definition of the AUParameterTree. Addresses come from the
//  shared C header (R50Parameters.h) so Swift and C++ agree on every index.
//
//  Both Partials are generated from one field table through r50PartialParam(),
//  the same accessor the engine uses. Writing sixty entries by hand would let
//  the two Partials drift apart, and would duplicate the address mapping in a
//  second place.
//

import AudioToolbox

enum R50Parameters {

    /// Order must match waveDescriptors() in R50Wave.hpp.
    static let waveformNames = [
        "Saw", "Triangle", "Square", "Pulse 10%", "Pulse",
        "Organ", "Tine", "Clarinet", "Strings", "Vocal Ah", "Bell"
    ]
    static let slopeNames    = ["12 dB", "24 dB"]

    /// Order must match NoiseSpectrum in R50Noise.hpp.
    static let noiseSpectrumNames = [
        "White", "Pink", "Brown", "Blue", "Violet", "Band", "S&H"
    ]
    static let trackNames      = ["Fixed", "Track"]
    static let sourceTypeNames = ["Wave", "Sample"]
    static let onOffNames      = ["Off", "On"]

    /// Order must match ToneStructure in R50Voice.hpp.
    static let toneStructureNames = [
        "Mix", "Ring", "Atk/Sus", "Vel XF", "Key XF"
    ]

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

    // MARK: - Partial fields

    private struct Field {
        let field: R50PartialField
        let id: String
        let name: String
        let min: AUValue
        let max: AUValue
        let value: AUValue
        var unit: AudioUnitParameterUnit = .generic
        var log: Bool = false
        var strings: [String]? = nil
    }

    private static var partialFields: [Field] {
        [
            Field(field: R50FieldEnabled, id: "Enabled", name: "Enabled",
                  min: 0, max: 1, value: 1, unit: .indexed, strings: onOffNames),
            Field(field: R50FieldSourceType, id: "SourceType", name: "Source",
                  min: 0, max: 1, value: 0, unit: .indexed,
                  strings: sourceTypeNames),
            Field(field: R50FieldSampleInstrument, id: "SampleInstrument",
                  name: "Instrument", min: 0, max: 63, value: 0, unit: .indexed),
            Field(field: R50FieldSampleStart, id: "SampleStart",
                  name: "Sample Start", min: 0, max: 1, value: 0),

            Field(field: R50FieldOscWave, id: "Wave", name: "Waveform",
                  min: 0, max: AUValue(waveformNames.count - 1), value: 0,
                  unit: .indexed, strings: waveformNames),
            Field(field: R50FieldPulseWidth, id: "PulseWidth",
                  name: "Pulse Width", min: 0.02, max: 0.98, value: 0.5),
            Field(field: R50FieldOctave, id: "Octave", name: "Octave",
                  min: -2, max: 2, value: 0, unit: .indexed),
            Field(field: R50FieldSemitone, id: "Semitone", name: "Semitone",
                  min: -24, max: 24, value: 0, unit: .indexed),
            Field(field: R50FieldFine, id: "Fine", name: "Fine",
                  min: -100, max: 100, value: 0, unit: .cents),

            Field(field: R50FieldNoiseMix, id: "NoiseMix", name: "Noise Mix",
                  min: 0, max: 1, value: 0),
            Field(field: R50FieldNoiseSpectrum, id: "NoiseSpectrum",
                  name: "Noise Spectrum", min: 0,
                  max: AUValue(noiseSpectrumNames.count - 1), value: 0,
                  unit: .indexed, strings: noiseSpectrumNames),
            Field(field: R50FieldNoiseTone, id: "NoiseTone", name: "Noise Tone",
                  min: 0, max: 1, value: 0.5),
            Field(field: R50FieldNoiseRate, id: "NoiseRate", name: "Noise Rate",
                  min: 20, max: 16000, value: 4000, unit: .hertz, log: true),
            Field(field: R50FieldNoisePitchTrack, id: "NoiseTrack",
                  name: "Noise Track", min: 0, max: 1, value: 0,
                  unit: .indexed, strings: trackNames),

            Field(field: R50FieldCutoff, id: "Cutoff", name: "Cutoff",
                  min: 20, max: 18000, value: 3200, unit: .hertz, log: true),
            Field(field: R50FieldResonance, id: "Resonance", name: "Resonance",
                  min: 0, max: 1, value: 0.15),
            Field(field: R50FieldDrive, id: "Drive", name: "Drive",
                  min: 0, max: 1, value: 0),
            Field(field: R50FieldSlope, id: "Slope", name: "Slope",
                  min: 0, max: 1, value: 1, unit: .indexed, strings: slopeNames),
            Field(field: R50FieldKeyTrack, id: "KeyTrack", name: "Key Track",
                  min: 0, max: 1, value: 0.5),
            Field(field: R50FieldFilterEnvAmount, id: "FilterEnvAmount",
                  name: "Env Amount", min: -1, max: 1, value: 0.45),

            Field(field: R50FieldAmpAttack, id: "AmpAttack", name: "Amp Attack",
                  min: 0.001, max: 8, value: 0.004, unit: .seconds, log: true),
            Field(field: R50FieldAmpDecay, id: "AmpDecay", name: "Amp Decay",
                  min: 0.001, max: 8, value: 0.25, unit: .seconds, log: true),
            Field(field: R50FieldAmpSustain, id: "AmpSustain",
                  name: "Amp Sustain", min: 0, max: 1, value: 0.75),
            Field(field: R50FieldAmpRelease, id: "AmpRelease",
                  name: "Amp Release", min: 0.001, max: 8, value: 0.30,
                  unit: .seconds, log: true),

            Field(field: R50FieldFilterAttack, id: "FilterAttack",
                  name: "Filter Attack", min: 0.001, max: 8, value: 0.004,
                  unit: .seconds, log: true),
            Field(field: R50FieldFilterDecay, id: "FilterDecay",
                  name: "Filter Decay", min: 0.001, max: 8, value: 0.45,
                  unit: .seconds, log: true),
            Field(field: R50FieldFilterSustain, id: "FilterSustain",
                  name: "Filter Sustain", min: 0, max: 1, value: 0.30),
            Field(field: R50FieldFilterRelease, id: "FilterRelease",
                  name: "Filter Release", min: 0.001, max: 8, value: 0.30,
                  unit: .seconds, log: true),

            Field(field: R50FieldLevel, id: "Level", name: "Level",
                  min: 0, max: 1, value: 1),
            Field(field: R50FieldPan, id: "Pan", name: "Pan",
                  min: -1, max: 1, value: 0),
        ]
    }

    private static func partialGroup(_ index: Int) -> AUParameterGroup {
        let children = partialFields.map { field -> AUParameter in
            // Partial 2 is off by default, so an existing one-Partial patch
            // sounds exactly as it did before this parameter block existed.
            let value = (field.field == R50FieldEnabled && index > 0)
                ? AUValue(0) : field.value
            return param(r50PartialParam(Int32(index), field.field),
                         "p\(index + 1)\(field.id)",
                         "P\(index + 1) \(field.name)",
                         min: field.min, max: field.max, value: value,
                         unit: field.unit, log: field.log,
                         strings: field.strings)
        }
        return AUParameterTree.createGroup(
            withIdentifier: "partial\(index + 1)",
            name: "Partial \(index + 1)", children: children)
    }

    // MARK: - Tree

    static func buildTree() -> AUParameterTree {
        let tone = AUParameterTree.createGroup(
            withIdentifier: "tone", name: "Tone", children: [
                param(R50ParamToneStructure, "toneStructure", "Structure",
                      min: 0, max: AUValue(toneStructureNames.count - 1),
                      value: 0, unit: .indexed, strings: toneStructureNames),
                param(R50ParamToneRingLevel, "toneRingLevel", "Ring Level",
                      min: 0, max: 1, value: 1),
                param(R50ParamToneBlendTime, "toneBlendTime", "Blend Time",
                      min: 0.001, max: 4, value: 0.25,
                      unit: .seconds, log: true),
                param(R50ParamToneCrossfadeLow, "toneCrossfadeLow", "XF Low",
                      min: 0, max: 127, value: 48, unit: .indexed),
                param(R50ParamToneCrossfadeHigh, "toneCrossfadeHigh", "XF High",
                      min: 0, max: 127, value: 72, unit: .indexed),
            ])

        let global = AUParameterTree.createGroup(
            withIdentifier: "global", name: "Global", children: [
                param(R50ParamMasterGain, "masterGain", "Master",
                      min: 0, max: 1, value: 0.8),
                param(R50ParamPitchBendRange, "bendRange", "Bend Range",
                      min: 0, max: 24, value: 2, unit: .indexed),
            ])

        return AUParameterTree.createTree(
            withChildren: [partialGroup(0), partialGroup(1), tone, global])
    }
}
