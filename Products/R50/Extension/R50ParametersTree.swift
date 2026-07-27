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

    /// Order must match ShaperType in R50Waveshaper.hpp.
    static let shaperTypeNames = ["Off", "Soft", "Hard", "Fold", "Rect"]
    static let shaperPositionNames = ["Pre", "Post"]

    /// Order must match ModSource / ModDestination / ModTarget in
    /// R50Modulation.hpp, and LFOWave in Shared/DSPCore/LFO.hpp.
    static let lfoWaveNames = ["Sine", "Square", "Saw Up", "Saw Down", "S&H"]
    static let modSourceNames = [
        "—", "LFO 1", "LFO 2", "Amp Env", "Filter Env", "Pitch Env",
        "Velocity", "Key Track", "Mod Wheel", "Aftertouch", "Random",
        "Macro 1", "Macro 2", "Macro 3", "Macro 4"
    ]
    static let modDestinationNames = [
        "—", "Pitch", "Cutoff", "Resonance", "Level", "Pan",
        "Wave", "Pulse Width", "Noise Mix", "Sample Start", "Shaper Drive",
        "Ring Level"
    ]
    static let modTargetNames = ["Both", "P1", "P2"]

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

            // Workstation EG: attack to a level, decay to a break point, slope
            // to sustain. Slope at 0 skips the break stage, which is what makes
            // the defaults identical to the ADSR this replaced.
            Field(field: R50FieldAmpAttackLevel, id: "AmpAttackLevel",
                  name: "Amp Attack Level", min: 0, max: 1, value: 1),
            Field(field: R50FieldAmpBreak, id: "AmpBreak",
                  name: "Amp Break", min: 0, max: 1, value: 1),
            Field(field: R50FieldAmpSlope, id: "AmpSlope",
                  name: "Amp Slope", min: 0, max: 8, value: 0, unit: .seconds),
            Field(field: R50FieldFilterAttackLevel, id: "FilterAttackLevel",
                  name: "Filter Attack Level", min: 0, max: 1, value: 1),
            Field(field: R50FieldFilterBreak, id: "FilterBreak",
                  name: "Filter Break", min: 0, max: 1, value: 1),
            Field(field: R50FieldFilterSlope, id: "FilterSlope",
                  name: "Filter Slope", min: 0, max: 8, value: 0, unit: .seconds),

            Field(field: R50FieldPitchKeyFollow, id: "PitchKeyFollow",
                  name: "Key Follow", min: 0, max: 2, value: 1),

            Field(field: R50FieldPitchAmount, id: "PitchAmount",
                  name: "Pitch Env Amount", min: -24, max: 24, value: 0),
            Field(field: R50FieldPitchAttack, id: "PitchAttack",
                  name: "Pitch Env Attack", min: 0.001, max: 4, value: 0.001,
                  unit: .seconds, log: true),
            Field(field: R50FieldPitchDecay, id: "PitchDecay",
                  name: "Pitch Env Decay", min: 0.001, max: 8, value: 0.2,
                  unit: .seconds, log: true),

            Field(field: R50FieldShaperType, id: "ShaperType",
                  name: "Shaper", min: 0,
                  max: AUValue(shaperTypeNames.count - 1), value: 0,
                  unit: .indexed, strings: shaperTypeNames),
            Field(field: R50FieldShaperDrive, id: "ShaperDrive",
                  name: "Shaper Drive", min: 0, max: 1, value: 0),
            Field(field: R50FieldShaperPosition, id: "ShaperPosition",
                  name: "Shaper Position", min: 0, max: 1, value: 0,
                  unit: .indexed, strings: shaperPositionNames),
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
                // Ring modulation multiplies two signals that are each well
                // below unity once envelopes and filters have acted, so the
                // product is roughly the product of their amplitudes — an
                // order of magnitude down. The control needs gain well past
                // 1 to make ring the character of a patch rather than a hint.
                param(R50ParamToneRingLevel, "toneRingLevel", "Ring Level",
                      min: 0, max: 8, value: 1),
                param(R50ParamToneBlendTime, "toneBlendTime", "Blend Time",
                      min: 0.001, max: 4, value: 0.25,
                      unit: .seconds, log: true),
                param(R50ParamToneCrossfadeLow, "toneCrossfadeLow", "XF Low",
                      min: 0, max: 127, value: 48, unit: .indexed),
                param(R50ParamToneCrossfadeHigh, "toneCrossfadeHigh", "XF High",
                      min: 0, max: 127, value: 72, unit: .indexed),
            ])

        let effects = AUParameterTree.createGroup(
            withIdentifier: "fx", name: "Effects", children: [
                param(R50ParamFxCompressor, "fxCompressor", "Compressor",
                      min: 0, max: 1, value: 0),

                param(R50ParamFxChorusMix, "fxChorusMix", "Chorus Mix",
                      min: 0, max: 1, value: 0),
                param(R50ParamFxChorusRate, "fxChorusRate", "Chorus Rate",
                      min: 0.05, max: 8, value: 0.6, unit: .hertz, log: true),
                param(R50ParamFxChorusDepth, "fxChorusDepth", "Chorus Depth",
                      min: 0, max: 1, value: 0.4),

                param(R50ParamFxDelayMix, "fxDelayMix", "Delay Mix",
                      min: 0, max: 1, value: 0),
                param(R50ParamFxDelayTime, "fxDelayTime", "Delay Time",
                      min: 0.02, max: 2, value: 0.32, unit: .seconds, log: true),
                param(R50ParamFxDelayFeedback, "fxDelayFeedback", "Delay Feedback",
                      min: 0, max: 0.95, value: 0.35),
                param(R50ParamFxDelayTone, "fxDelayTone", "Delay Tone",
                      min: 0, max: 1, value: 0.5),
                param(R50ParamFxDelayPingPong, "fxDelayPingPong", "Ping Pong",
                      min: 0, max: 1, value: 1),

                param(R50ParamFxReverbMix, "fxReverbMix", "Reverb Mix",
                      min: 0, max: 1, value: 0),
                param(R50ParamFxReverbSize, "fxReverbSize", "Reverb Size",
                      min: 0, max: 1, value: 0.55),
                param(R50ParamFxReverbDecay, "fxReverbDecay", "Reverb Decay",
                      min: 0.2, max: 12, value: 2.4, unit: .seconds, log: true),
                param(R50ParamFxReverbTone, "fxReverbTone", "Reverb Tone",
                      min: 0, max: 1, value: 0.55),
            ])

        var lfoChildren: [AUParameter] = []
        for index in 0..<2 {
            let base = index == 0 ? R50ParamLfo1Wave : R50ParamLfo2Wave
            func at(_ offset: UInt64) -> R50Param {
                R50Param(base.rawValue + offset)
            }
            lfoChildren += [
                param(at(0), "lfo\(index + 1)Wave", "LFO \(index + 1) Wave",
                      min: 0, max: AUValue(lfoWaveNames.count - 1), value: 0,
                      unit: .indexed, strings: lfoWaveNames),
                param(at(1), "lfo\(index + 1)Rate", "LFO \(index + 1) Rate",
                      min: 0.02, max: 24, value: index == 0 ? 5 : 0.6,
                      unit: .hertz, log: true),
                param(at(2), "lfo\(index + 1)Delay", "LFO \(index + 1) Delay",
                      min: 0, max: 5, value: 0, unit: .seconds),
                param(at(3), "lfo\(index + 1)Fade", "LFO \(index + 1) Fade",
                      min: 0, max: 5, value: 0, unit: .seconds),
                param(at(4), "lfo\(index + 1)Retrigger", "LFO \(index + 1) Retrigger",
                      min: 0, max: 1, value: 1, unit: .indexed,
                      strings: ["Free", "Note"]),
                param(at(5), "lfo\(index + 1)Phase", "LFO \(index + 1) Phase",
                      min: 0, max: 1, value: 0),
            ]
        }

        var slotChildren: [AUParameter] = []
        for slot in 0..<6 {
            slotChildren += [
                param(r50ModSlotParam(Int32(slot), R50ModFieldSource),
                      "mod\(slot + 1)Source", "Mod \(slot + 1) Source",
                      min: 0, max: AUValue(modSourceNames.count - 1), value: 0,
                      unit: .indexed, strings: modSourceNames),
                param(r50ModSlotParam(Int32(slot), R50ModFieldDestination),
                      "mod\(slot + 1)Dest", "Mod \(slot + 1) Destination",
                      min: 0, max: AUValue(modDestinationNames.count - 1), value: 0,
                      unit: .indexed, strings: modDestinationNames),
                param(r50ModSlotParam(Int32(slot), R50ModFieldTarget),
                      "mod\(slot + 1)Target", "Mod \(slot + 1) Target",
                      min: 0, max: 2, value: 0, unit: .indexed,
                      strings: modTargetNames),
                param(r50ModSlotParam(Int32(slot), R50ModFieldAmount),
                      "mod\(slot + 1)Amount", "Mod \(slot + 1) Amount",
                      min: -1, max: 1, value: 0),
            ]
        }

        let modulation = AUParameterTree.createGroup(
            withIdentifier: "mod", name: "Modulation",
            children: lfoChildren + slotChildren + [
                param(R50ParamMacro1, "macro1", "Macro 1", min: 0, max: 1, value: 0),
                param(R50ParamMacro2, "macro2", "Macro 2", min: 0, max: 1, value: 0),
                param(R50ParamMacro3, "macro3", "Macro 3", min: 0, max: 1, value: 0),
                param(R50ParamMacro4, "macro4", "Macro 4", min: 0, max: 1, value: 0),
            ])

        let global = AUParameterTree.createGroup(
            withIdentifier: "global", name: "Global", children: [
                param(R50ParamMasterGain, "masterGain", "Master",
                      min: 0, max: 1, value: 0.8),
                param(R50ParamPitchBendRange, "bendRange", "Bend Range",
                      min: 0, max: 24, value: 2, unit: .indexed),
            ])

        return AUParameterTree.createTree(
            withChildren: [partialGroup(0), partialGroup(1), tone,
                           modulation, effects, global])
    }
}
