//
//  R50FactoryPresets.swift
//  Production patches for the R50. The bank is generated from
//  compact musical recipes so related sounds share a family resemblance while
//  every entry still has its own oscillator, envelope, structure and FX setup.
//

import AudioToolbox

struct R50FactoryPreset {
    let name: String
    let values: [AUParameterAddress: AUValue]
    let sampleAssets: [Int: String]

    init(name: String, values: [AUParameterAddress: AUValue],
         sampleAssets: [Int: String] = [:]) {
        self.name = name
        self.values = values
        self.sampleAssets = sampleAssets
    }
}

enum R50FactoryPresets {

    static let all: [R50FactoryPreset] = buildBank()

    private static func buildBank() -> [R50FactoryPreset] {
        var bank: [R50FactoryPreset] = []

        let basses = [
            "BASS • Rubber Floor", "BASS • Slap Circuit", "BASS • Night Sub",
            "BASS • Resonant Pick", "BASS • FM Growl", "BASS • Pulse Driver",
            "BASS • Mini Stack", "BASS • Fretless Air", "BASS • Sync Weight",
            "BASS • Acid Memory", "BASS • Digi Thumb", "BASS • Twin Octaves"
        ]
        for (i, name) in basses.enumerated() { bank.append(bass(name, i)) }

        let synths = [
            "SYNTH • Neon Poly", "SYNTH • Brass Stab", "SYNTH • Glass Sync",
            "SYNTH • Pulse Fifth", "SYNTH • Vector Motion", "SYNTH • Rez Chord",
            "SYNTH • Digital Sweep", "SYNTH • PWM Memory", "SYNTH • Ring Leader",
            "SYNTH • Soft Focus", "SYNTH • Hard Wire", "SYNTH • Unison Spark",
            "LEAD • Aftertouch Saw", "LEAD • Mono Reed", "LEAD • Laser Pulse",
            "LEAD • Singing Square", "LEAD • Fusion Wire", "LEAD • Lonely Vox"
        ]
        for (i, name) in synths.enumerated() { bank.append(synth(name, i)) }

        let keys = [
            "KEYS • Mellow Stage", "KEYS • Bright Tines", "KEYS • Dyno Chorus",
            "KEYS • Digital Piano", "KEYS • Glass EP", "KEYS • Soft Grand",
            "KEYS • House Piano", "KEYS • Piano & Strings", "KEYS • Bell Piano",
            "KEYS • LA Stack", "KEYS • Velvet Tines", "KEYS • Wire Clav",
            "KEYS • Chorus Clav", "KEYS • Midnight EP"
        ]
        for (i, name) in keys.enumerated() { bank.append(keysPatch(name, i)) }

        let plucks = [
            "PLUCK • Nylon Dream", "PLUCK • Digital Harp", "PLUCK • Kalimba Sun",
            "PLUCK • Marimba Air", "PLUCK • Vibes Cloud", "PLUCK • Pizzicato Pop",
            "PLUCK • Bottle Wire", "PLUCK • Koto Circuit", "PLUCK • Muted Bell",
            "PLUCK • Ice Mallet", "PLUCK • Picked Pad", "PLUCK • Anvil String"
        ]
        for (i, name) in plucks.enumerated() { bank.append(pluck(name, i)) }

        let pads = [
            "PAD • Warm Horizon", "PAD • Hollow Heaven", "PAD • Choir Glass",
            "PAD • Vector Aurora", "PAD • Analog Sea", "PAD • Digital Mist",
            "PAD • Ooh Machine", "PAD • Spectrum Drift", "PAD • Breath Field",
            "PAD • Ice Palace", "PAD • Slow Brass", "PAD • Solar Choir",
            "PAD • Dark Motion", "PAD • Shimmer Bed", "PAD • Space Memory",
            "PAD • Endless Blue"
        ]
        for (i, name) in pads.enumerated() { bank.append(pad(name, i)) }

        let strings = [
            "STRINGS • Warm Section", "STRINGS • Wide Ensemble",
            "STRINGS • Slow Cinema", "STRINGS • Pizz & Bow",
            "STRINGS • Synth Orchestra", "STRINGS • Chamber Air",
            "STRINGS • Octave Sweep", "STRINGS • Bowed Glass",
            "STRINGS • Marcato Layer", "STRINGS • Silk Machine"
        ]
        for (i, name) in strings.enumerated() { bank.append(stringsPatch(name, i)) }

        let winds = [
            "REED • Warm Clarinet", "REED • Breath Solo", "REED • Digital Oboe",
            "REED • Soft Sax", "WIND • Air Flute", "WIND • Chiff Pipe",
            "WIND • Pan Dream", "WIND • Synthetic Shaku"
        ]
        for (i, name) in winds.enumerated() { bank.append(wind(name, i)) }

        let performance = [
            "BRASS • Power Section", "BRASS • Mute Stack",
            "ORGAN • Drawbar Fast", "ORGAN • Gospel Rotary",
            "BELL • Spectrum Tower", "BELL • Ring Cathedral",
            "WORLD • Taiko Choir", "SPLIT • Bass & Piano",
            "SPLIT • Pad & Lead", "VECTOR • Four Corners"
        ]
        for (i, name) in performance.enumerated() {
            bank.append(performancePatch(name, i))
        }

        bank.append(contentsOf: multisamplePresets())

        precondition(bank.count == 106)
        for index in bank.indices {
            var values = bank[index].values
            addPerformanceModulation(&values, name: bank[index].name,
                                     index: index)
            configureRingOutput(&values, index: index)
            bank[index] = R50FactoryPreset(name: bank[index].name,
                                           values: values,
                                           sampleAssets: bank[index].sampleAssets)
        }
        precondition(bank.allSatisfy { hasModWheelRoute($0.values) })
        return bank
    }

    // MARK: - Musical recipes

    private static func multisamplePresets() -> [R50FactoryPreset] {
        [
            multisamplePreset("KEYS • Multisample Grand",
                              "factory.piano_multisample",
                              0.002, 3.0, 0.14, 0.55, 8_500, 0.08, 0.14),
            multisamplePreset("PLUCK • Acoustic Guitar",
                              "factory.acoustic_guitar",
                              0.002, 2.2, 0.12, 0.35, 7_500, 0.03, 0.12),
            multisamplePreset("PLUCK • Nylon Guitar",
                              "factory.nylon_guitar_multisample",
                              0.002, 2.5, 0.16, 0.42, 6_500, 0.04, 0.16),
            multisamplePreset("PAD • Ooh Ensemble",
                              "factory.ooh_choir",
                              0.18, 0.8, 0.88, 1.4, 4_200, 0.22, 0.28),
            multisamplePreset("STRINGS • Cello Section",
                              "factory.cello",
                              0.12, 0.7, 0.92, 0.9, 3_200, 0.12, 0.18),
            multisamplePreset("PLUCK • Pizzicato Celli",
                              "factory.pizzicato_celli",
                              0.001, 0.75, 0.0, 0.3, 6_000, 0.03, 0.14),
        ]
    }

    private static func multisamplePreset(
        _ name: String, _ assetID: String,
        _ attack: AUValue, _ decay: AUValue, _ sustain: AUValue,
        _ release: AUValue, _ cutoff: AUValue,
        _ chorusMix: AUValue, _ reverbMix: AUValue
    ) -> R50FactoryPreset {
        var assets: [Int: String] = [:]
        var v = sampleBase(assetID, &assets, cutoff: cutoff,
                           attack: attack, release: release)
        put(&v, p1(R50FieldAmpDecay), decay)
        put(&v, p1(R50FieldAmpSustain), sustain)
        addChorus(&v, mix: chorusMix)
        addReverb(&v, decay: release + 1.2, mix: reverbMix)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func bass(_ name: String, _ i: Int) -> R50FactoryPreset {
        // A bass whose name promises an instrument is built on its
        // multisample; the rest stay virtual-analog waves. Sampled basses get
        // a much higher cutoff — the recording already has the right
        // spectrum, and the slap transient is the sound.
        let sampled: [Int: String] = [
            1: Instrument.slapBass,        // Slap Circuit
            3: Instrument.pickedBass,      // Resonant Pick
            7: Instrument.acousticBass,    // Fretless Air
            10: Instrument.slapBass,       // Digi Thumb
        ]
        let wave = [0, 0, 1, 4, 2, 4, 0, 7, 3, 0, 2, 0][i]
        var assets: [Int: String] = [:]
        var v: [AUParameterAddress: AUValue]
        if let sample = sampled[i] {
            v = sampleBase(sample, &assets,
                           cutoff: 2400 + AUValue(i) * 120,
                           attack: 0.002,
                           release: 0.08 + AUValue(i % 4) * 0.04)
        } else {
            v = base(wave: wave, cutoff: 260 + AUValue(i) * 75,
                     attack: 0.002, release: 0.08 + AUValue(i % 4) * 0.04)
        }
        put(&v, p1(R50FieldOctave), -1)
        put(&v, p1(R50FieldSlope), 1)
        put(&v, p1(R50FieldResonance), 0.25 + AUValue(i % 4) * 0.11)
        put(&v, p1(R50FieldFilterEnvAmount), 0.45 + AUValue(i % 3) * 0.12)
        put(&v, p1(R50FieldFilterDecay), 0.12 + AUValue(i % 5) * 0.07)
        put(&v, p1(R50FieldFilterSustain), i == 7 ? 0.45 : 0.05)
        put(&v, p1(R50FieldAmpDecay), 0.35 + AUValue(i % 4) * 0.18)
        put(&v, p1(R50FieldAmpSustain), 0.45 + AUValue(i % 3) * 0.12)
        if i == 2 || i == 6 || i == 11 {
            enable(&v, partial: 1, wave: 1, octave: -2, level: 0.32,
                   pan: 0, fine: 0)
        }
        if i == 4 || i == 8 {
            put(&v, addr(R50ParamToneStructure), AUValue(Structure.ring))
            put(&v, addr(R50ParamToneRingLevel), 1.7)
            enable(&v, partial: 1, wave: 2, octave: -1, level: 0.18,
                   pan: 0, fine: 3)
        }
        addDrive(&v, amount: 0.18 + AUValue(i % 3) * 0.09)
        put(&v, addr(R50ParamVoiceMode), 1)   // a bass is monophonic
        if i == 7 { put(&v, addr(R50ParamGlideTime), 0.09) }  // fretless slides
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func synth(_ name: String, _ i: Int) -> R50FactoryPreset {
        let wave = [0, 8, 10, 4, 0, 2, 11, 4, 10, 6, 3, 0,
                    0, 7, 3, 1, 2, 9][i]
        var v = base(wave: wave, cutoff: 1800 + AUValue(i % 6) * 850,
                     attack: i == 9 ? 0.18 : 0.008,
                     release: 0.25 + AUValue(i % 4) * 0.16)
        put(&v, p1(R50FieldResonance), 0.12 + AUValue(i % 5) * 0.08)
        put(&v, p1(R50FieldFilterEnvAmount), 0.18 + AUValue(i % 4) * 0.14)
        put(&v, p1(R50FieldFilterDecay), 0.35 + AUValue(i % 5) * 0.22)
        put(&v, p1(R50FieldAmpSustain), i == 1 || i == 5 ? 0.55 : 0.88)
        enable(&v, partial: 1, wave: (wave + 1) % 12,
               octave: i == 3 ? 0 : (i % 6 == 0 ? -1 : 0),
               level: 0.35, pan: 0.35, fine: AUValue(5 + i % 5))
        put(&v, p1(R50FieldPan), -0.35)
        var assets: [Int: String] = [:]
        if i == 4 || i == 6 {
            vectorTone(&v, &assets,
                       sample: i == 4 ? Instrument.spectrum4 : Instrument.spectrum7,
                       mix: 0.42, depth: 0.25)
        }
        if i == 8 {
            put(&v, addr(R50ParamToneStructure), AUValue(Structure.ring))
            put(&v, addr(R50ParamToneRingLevel), 2.0)
        }
        if i >= 12 {
            put(&v, p1(R50FieldAmpAttack), 0.025)
            put(&v, p1(R50FieldAmpRelease), 0.16)
            put(&v, p1(R50FieldPitchAmount), i == 14 ? 12 : 2)
            put(&v, p1(R50FieldPitchDecay), 0.08)
            put(&v, addr(R50ParamVoiceMode), 1)   // a lead is monophonic
            put(&v, addr(R50ParamGlideTime), 0.05) // a touch of portamento
        }
        addChorus(&v, mix: i % 3 == 0 ? 0.28 : 0.16)
        if i % 4 == 2 { addDelay(&v, time: 0.24, feedback: 0.28, mix: 0.22) }
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func keysPatch(_ name: String, _ i: Int) -> R50FactoryPreset {
        let sustain = [Instrument.piano, Instrument.piano, Instrument.piano,
                       Instrument.piano, Instrument.glassPad, Instrument.piano,
                       Instrument.piano, Instrument.strings, Instrument.glassPad,
                       Instrument.warmPad, Instrument.piano, Instrument.nasty,
                       Instrument.fatBlock, Instrument.piano][i]
        let attack = i == 11 || i == 12 ? Instrument.pick
            : (i == 1 || i == 2 || i == 4 || i == 10
                ? Instrument.tineStrike : Instrument.pianoHammer)
        var assets: [Int: String] = [:]
        var v = sampleBase(sustain, &assets,
                           cutoff: 3600 + AUValue(i % 5) * 1000,
                           attack: 0.003, release: 0.35 + AUValue(i % 4) * 0.16)
        attackSustain(&v, &assets, attack: attack, sustainSample: sustain,
                      blend: 0.045 + AUValue(i % 3) * 0.02)
        put(&v, p2(R50FieldAmpDecay), 1.2 + AUValue(i % 4) * 0.5)
        put(&v, p2(R50FieldAmpSustain), 0.2 + AUValue(i % 3) * 0.12)
        put(&v, p2(R50FieldKeyTrack), 0.55)
        if i == 7 {
            put(&v, addr(R50ParamToneStructure), AUValue(Structure.mix))
            put(&v, p1(R50FieldLevel), 0.65)
            assets[1] = Instrument.strings
            put(&v, p2(R50FieldAmpAttack), 0.22)
            put(&v, p2(R50FieldAmpSustain), 0.9)
        }
        if i == 8 || i == 9 {
            assets[1] = i == 8 ? Instrument.glassPad : Instrument.warmPad
        }
        addChorus(&v, mix: [1, 2, 4, 10, 12, 13].contains(i) ? 0.34 : 0.12)
        addReverb(&v, decay: 1.8 + AUValue(i % 4) * 0.55, mix: 0.18)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func pluck(_ name: String, _ i: Int) -> R50FactoryPreset {
        let attacks = [Instrument.pluck, Instrument.pluck, Instrument.kalimba,
                       Instrument.marimba, Instrument.vibraphone,
                       Instrument.pizzicato, Instrument.chiff, Instrument.pick,
                       Instrument.mallet, Instrument.xylophone, Instrument.pick,
                       Instrument.anvil]
        let sustains = [Instrument.nylonGuitar, Instrument.glassPad,
                        Instrument.warmPad, Instrument.warmPad,
                        Instrument.glassPad, Instrument.strings,
                        Instrument.spectrum3, Instrument.spectrum6,
                        Instrument.glassPad, Instrument.spectrum8,
                        Instrument.warmPad, Instrument.strings]
        var assets: [Int: String] = [:]
        var v = sampleBase(sustains[i], &assets,
                           cutoff: 4300 + AUValue(i % 5) * 1300,
                           attack: 0.001, release: 0.35 + AUValue(i % 4) * 0.22)
        attackSustain(&v, &assets, attack: attacks[i], sustainSample: sustains[i],
                      blend: 0.035 + AUValue(i % 4) * 0.025)
        put(&v, p2(R50FieldAmpDecay), 0.65 + AUValue(i % 5) * 0.45)
        put(&v, p2(R50FieldAmpSustain), i % 3 == 0 ? 0.12 : 0)
        put(&v, p2(R50FieldFilterEnvAmount), 0.25)
        put(&v, p2(R50FieldFilterDecay), 0.45)
        if i == 5 { put(&v, p2(R50FieldOctave), 1) }
        addDelay(&v, time: 0.16 + AUValue(i % 4) * 0.07,
                 feedback: 0.20 + AUValue(i % 3) * 0.09, mix: 0.18)
        addReverb(&v, decay: 2.3 + AUValue(i % 4) * 0.7, mix: 0.24)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func pad(_ name: String, _ i: Int) -> R50FactoryPreset {
        let samples = [Instrument.warmPad, Instrument.warmPad, Instrument.choir,
                       Instrument.glassPad, Instrument.strings, Instrument.spectrum2,
                       Instrument.voiceOoh, Instrument.spectrum5, Instrument.breath,
                       Instrument.glassPad, Instrument.trumpet, Instrument.choir,
                       Instrument.spectrum9, Instrument.glassPad,
                       Instrument.spectrum6, Instrument.warmPad]
        var assets: [Int: String] = [:]
        var v = sampleBase(samples[i], &assets,
                           cutoff: 1700 + AUValue(i % 6) * 650,
                           attack: 0.45 + AUValue(i % 5) * 0.22,
                           release: 1.4 + AUValue(i % 4) * 0.65)
        put(&v, p1(R50FieldAmpSustain), 1)
        put(&v, p1(R50FieldFilterAttack), 0.7 + AUValue(i % 4) * 0.5)
        put(&v, p1(R50FieldFilterSustain), 0.55)
        put(&v, p1(R50FieldPan), -0.32)
        enable(&v, partial: 1, wave: [4, 8, 9, 10][i % 4],
               octave: i % 5 == 0 ? -1 : 0, level: 0.42,
               pan: 0.32, fine: AUValue(6 + i % 4))
        put(&v, p2(R50FieldAmpAttack), 0.65 + AUValue(i % 4) * 0.28)
        put(&v, p2(R50FieldAmpRelease), 1.8)
        put(&v, p2(R50FieldCutoff), 2200 + AUValue(i % 5) * 700)
        if i % 4 == 3 || i == 7 || i == 12 || i == 15 {
            vectorTone(&v, &assets,
                       sample: [Instrument.voiceOoh, Instrument.spectrum7,
                                Instrument.choir, Instrument.spectrum9][i % 4],
                       mix: 0.5, depth: 0.22 + AUValue(i % 3) * 0.08)
        }
        if i == 8 {
            put(&v, p2(R50FieldNoiseMix), 0.7)
            put(&v, p2(R50FieldNoiseSpectrum), 5)
            put(&v, p2(R50FieldNoisePitchTrack), 1)
        }
        addChorus(&v, mix: 0.30)
        addReverb(&v, decay: 4.0 + AUValue(i % 5), mix: 0.38)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func stringsPatch(_ name: String, _ i: Int) -> R50FactoryPreset {
        var assets: [Int: String] = [:]
        var v = sampleBase(Instrument.strings, &assets,
                           cutoff: 2600 + AUValue(i % 5) * 700,
                           attack: 0.18 + AUValue(i % 4) * 0.13,
                           release: 0.7 + AUValue(i % 4) * 0.3)
        put(&v, p1(R50FieldPan), -0.48)
        enable(&v, partial: 1, wave: 8, octave: i == 6 ? 1 : 0,
               level: 0.48, pan: 0.48, fine: 7)
        put(&v, p2(R50FieldAmpAttack), 0.24 + AUValue(i % 3) * 0.16)
        put(&v, p2(R50FieldAmpRelease), 0.9)
        put(&v, p2(R50FieldCutoff), 3000)
        if i == 3 || i == 8 {
            put(&v, addr(R50ParamToneStructure), AUValue(Structure.attackSustain))
            put(&v, addr(R50ParamToneBlendTime), 0.09)
            put(&v, p1(R50FieldSourceType), 1)
            assets[0] = i == 3 ? Instrument.pizzicato : Instrument.bowScrape
            put(&v, p1(R50FieldAmpDecay), 0.35)
            put(&v, p1(R50FieldAmpSustain), 0)
            put(&v, p2(R50FieldSourceType), 1)
            assets[1] = Instrument.strings
        }
        if i == 7 { assets[1] = Instrument.glassPad }
        addChorus(&v, mix: 0.36)
        addReverb(&v, decay: 2.8 + AUValue(i % 4) * 0.8, mix: 0.28)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func wind(_ name: String, _ i: Int) -> R50FactoryPreset {
        let wave = [7, 7, 9, 7, 7, 5, 7, 9][i]
        var v = base(wave: wave, cutoff: 3300 + AUValue(i % 4) * 850,
                     attack: 0.045 + AUValue(i % 3) * 0.035,
                     release: 0.18 + AUValue(i % 3) * 0.08)
        put(&v, p1(R50FieldAmpSustain), 0.92)
        put(&v, p1(R50FieldKeyTrack), 0.55)
        put(&v, p1(R50FieldNoiseMix), 0.12 + AUValue(i % 4) * 0.07)
        put(&v, p1(R50FieldNoiseSpectrum), 5)
        put(&v, p1(R50FieldNoiseTone), 0.3)
        put(&v, p1(R50FieldNoisePitchTrack), 1)
        var assets: [Int: String] = [:]
        if i == 4 || i == 5 || i == 7 {
            attackSustain(&v, &assets,
                          attack: i == 5 ? Instrument.chiff : Instrument.breath,
                          sustainSample: Instrument.flute, blend: 0.09)
        }
        addReverb(&v, decay: 1.9 + AUValue(i % 4) * 0.5, mix: 0.19)
        return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
    }

    private static func performancePatch(
        _ name: String, _ i: Int
    ) -> R50FactoryPreset {
        var assets: [Int: String] = [:]
        switch i {
        case 0, 1:
            var v = sampleBase(Instrument.trumpet, &assets,
                               cutoff: i == 0 ? 5200 : 3200,
                               attack: 0.035, release: 0.28)
            attackSustain(&v, &assets, attack: Instrument.lipBuzz,
                          sustainSample: Instrument.trumpet, blend: 0.055)
            enable(&v, partial: 2, wave: 8, octave: 0, level: 0.38,
                   pan: 0.25, fine: 5)
            addReverb(&v, decay: 2.1, mix: 0.2)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        case 2, 3:
            var v = sampleBase(Instrument.organ, &assets, cutoff: 7200,
                               attack: 0.002, release: 0.06)
            enable(&v, partial: 1, wave: 5, octave: i == 2 ? 1 : 0,
                   level: 0.42, pan: 0, fine: 0)
            addRotary(&v, fast: i == 2)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        case 4, 5:
            var v = sampleBase(i == 4 ? Instrument.spectrum8 : Instrument.gong,
                               &assets,
                               cutoff: 8200, attack: 0.002, release: 2.2)
            enable(&v, partial: 1, wave: 10, octave: 1, level: 0.25,
                   pan: 0.3, fine: 7)
            if i == 5 {
                put(&v, addr(R50ParamToneStructure), AUValue(Structure.ring))
                put(&v, addr(R50ParamToneRingLevel), 2.1)
            }
            addDelay(&v, time: 0.31, feedback: 0.42, mix: 0.28)
            addReverb(&v, decay: 6.5, mix: 0.42)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        case 6:
            var v = sampleBase(Instrument.choir, &assets, cutoff: 3600,
                               attack: 0.12, release: 1.1)
            attackSustain(&v, &assets, attack: Instrument.taikoDrum,
                          sustainSample: Instrument.choir, blend: 0.13)
            addReverb(&v, decay: 5.2, mix: 0.44)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        case 7:
            var v = base(wave: 0, cutoff: 650, attack: 0.002, release: 0.1)
            put(&v, p1(R50FieldOctave), -1)
            splitTone(&v, &assets, sample: Instrument.piano, point: 55)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        case 8:
            var v = sampleBase(Instrument.warmPad, &assets, cutoff: 2400,
                               attack: 0.4, release: 1.2)
            splitTone(&v, &assets, sample: Instrument.spectrum3, point: 67)
            put(&v, p3(R50FieldAmpAttack), 0.03)
            put(&v, p3(R50FieldAmpRelease), 0.25)
            addDelay(&v, time: 0.24, feedback: 0.3, mix: 0.2)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        default:
            var v = sampleBase(Instrument.strings, &assets, cutoff: 3400,
                               attack: 0.35, release: 1.5)
            enable(&v, partial: 1, wave: 9, octave: 0, level: 0.42,
                   pan: 0.3, fine: 6)
            vectorTone(&v, &assets, sample: Instrument.spectrum9,
                       mix: 0.5, depth: 0.34)
            enable(&v, partial: 3, wave: 10, octave: 1, level: 0.32,
                   pan: 0.5, fine: -5)
            addReverb(&v, decay: 5.8, mix: 0.4)
            return R50FactoryPreset(name: name, values: v, sampleAssets: assets)
        }
    }

    // MARK: - Recipe building blocks

    private static func base(
        wave: Int, cutoff: AUValue, attack: AUValue, release: AUValue
    ) -> [AUParameterAddress: AUValue] {
        [
            p1(R50FieldOscWave): AUValue(wave),
            p1(R50FieldCutoff): cutoff,
            p1(R50FieldKeyTrack): 0.5,
            p1(R50FieldAmpAttack): attack,
            p1(R50FieldAmpDecay): 0.8,
            p1(R50FieldAmpSustain): 0.8,
            p1(R50FieldAmpRelease): release,
            p1(R50FieldFilterAttack): 0.004,
            p1(R50FieldFilterDecay): 0.6,
            p1(R50FieldFilterSustain): 0.35
        ]
    }

    /// Sample-sourced partial 0. The numeric selector parameter is written as
    /// a safe bootstrap zero; the persistent ID goes into the preset's
    /// sampleAssets map, which preset application resolves against the loaded
    /// library after the parameter reset. Neither directory ordering nor
    /// library growth can repoint a recipe.
    private static func sampleBase(
        _ instrument: String, _ assets: inout [Int: String],
        cutoff: AUValue, attack: AUValue, release: AUValue
    ) -> [AUParameterAddress: AUValue] {
        var v = base(wave: 0, cutoff: cutoff, attack: attack, release: release)
        put(&v, p1(R50FieldSourceType), 1)
        put(&v, p1(R50FieldSampleInstrument), 0)
        assets[0] = instrument
        return v
    }

    private static func attackSustain(
        _ v: inout [AUParameterAddress: AUValue],
        _ assets: inout [Int: String], attack: String,
        sustainWave: Int? = nil, sustainSample: String? = nil, blend: AUValue
    ) {
        put(&v, addr(R50ParamToneStructure), AUValue(Structure.attackSustain))
        put(&v, addr(R50ParamToneBlendTime), blend)
        put(&v, p1(R50FieldSourceType), 1)
        put(&v, p1(R50FieldSampleInstrument), 0)
        assets[0] = attack
        put(&v, p1(R50FieldFilterEnvAmount), 0)
        put(&v, p1(R50FieldAmpDecay), 0.25)
        put(&v, p1(R50FieldAmpSustain), 0)
        put(&v, p2(R50FieldEnabled), 1)
        if let sustainSample {
            put(&v, p2(R50FieldSourceType), 1)
            put(&v, p2(R50FieldSampleInstrument), 0)
            assets[1] = sustainSample
        } else {
            put(&v, p2(R50FieldOscWave), AUValue(sustainWave ?? 0))
        }
        put(&v, p2(R50FieldCutoff), v[p1(R50FieldCutoff)] ?? 4000)
        put(&v, p2(R50FieldAmpAttack), 0.02)
        put(&v, p2(R50FieldAmpSustain), 0.75)
        put(&v, p2(R50FieldAmpRelease), v[p1(R50FieldAmpRelease)] ?? 0.4)
    }

    private static func enable(
        _ v: inout [AUParameterAddress: AUValue], partial: Int, wave: Int,
        octave: Int, level: AUValue, pan: AUValue, fine: AUValue
    ) {
        let a = partialAddress(partial)
        put(&v, a(R50FieldEnabled), 1)
        put(&v, a(R50FieldOscWave), AUValue(wave))
        put(&v, a(R50FieldOctave), AUValue(octave))
        put(&v, a(R50FieldLevel), level)
        put(&v, a(R50FieldPan), pan)
        put(&v, a(R50FieldFine), fine)
        put(&v, a(R50FieldCutoff), 4200)
        put(&v, a(R50FieldAmpAttack), 0.01)
        put(&v, a(R50FieldAmpSustain), 0.85)
        put(&v, a(R50FieldAmpRelease), 0.5)
    }

    private static func vectorTone(
        _ v: inout [AUParameterAddress: AUValue],
        _ assets: inout [Int: String], sample: String,
        mix: AUValue, depth: AUValue
    ) {
        put(&v, addr(R50ParamPatchStructure), 4)
        put(&v, addr(R50ParamPatchVectorMix), mix)
        put(&v, addr(R50ParamToneBLevel), 0.82)
        put(&v, p3(R50FieldEnabled), 1)
        put(&v, p3(R50FieldSourceType), 1)
        put(&v, p3(R50FieldSampleInstrument), 0)
        assets[2] = sample
        put(&v, p3(R50FieldCutoff), 4800)
        put(&v, p3(R50FieldAmpAttack), 0.45)
        put(&v, p3(R50FieldAmpSustain), 1)
        put(&v, p3(R50FieldAmpRelease), 1.5)
        put(&v, p3(R50FieldPan), 0.25)
        put(&v, addr(R50ParamVectorLfoWave), 0)
        put(&v, addr(R50ParamVectorLfoRate), 0.08)
        put(&v, addr(R50ParamVectorLfoDepth), depth)
        put(&v, addr(R50ParamVectorLfoRetrigger), 0)
    }

    private static func splitTone(
        _ v: inout [AUParameterAddress: AUValue],
        _ assets: inout [Int: String], sample: String, point: AUValue
    ) {
        put(&v, addr(R50ParamPatchStructure), 1)
        put(&v, addr(R50ParamPatchSplitPoint), point)
        put(&v, p3(R50FieldEnabled), 1)
        put(&v, p3(R50FieldSourceType), 1)
        put(&v, p3(R50FieldSampleInstrument), 0)
        assets[2] = sample
        put(&v, p3(R50FieldCutoff), 5200)
        put(&v, p3(R50FieldAmpAttack), 0.01)
        put(&v, p3(R50FieldAmpSustain), 0.8)
        put(&v, p3(R50FieldAmpRelease), 0.45)
    }

    private static func addChorus(
        _ v: inout [AUParameterAddress: AUValue], mix: AUValue
    ) {
        put(&v, slot(0, R50FxFieldAlgorithm), 7)
        put(&v, slot(0, R50FxFieldMix), mix)
        put(&v, slot(0, R50FxFieldControl1), chorusRate(0.35))
    }

    private static func addDelay(
        _ v: inout [AUParameterAddress: AUValue], time: AUValue,
        feedback: AUValue, mix: AUValue
    ) {
        put(&v, slot(1, R50FxFieldAlgorithm), 5)
        put(&v, slot(1, R50FxFieldMix), mix)
        put(&v, slot(1, R50FxFieldControl1), delayTime(time))
        put(&v, slot(1, R50FxFieldControl3), delayFeedback(feedback))
    }

    private static func addReverb(
        _ v: inout [AUParameterAddress: AUValue], decay: AUValue, mix: AUValue
    ) {
        put(&v, slot(2, R50FxFieldAlgorithm), 1)
        put(&v, slot(2, R50FxFieldMix), mix)
        put(&v, slot(2, R50FxFieldControl2), reverbDecay(decay))
    }

    private static func addDrive(
        _ v: inout [AUParameterAddress: AUValue], amount: AUValue
    ) {
        put(&v, slot(0, R50FxFieldAlgorithm), 14)
        put(&v, slot(0, R50FxFieldMix), 0.42)
        put(&v, slot(0, R50FxFieldControl1), amount)
    }

    private static func addRotary(
        _ v: inout [AUParameterAddress: AUValue], fast: Bool
    ) {
        put(&v, slot(0, R50FxFieldAlgorithm), 12)
        put(&v, slot(0, R50FxFieldMix), 1)
        put(&v, slot(0, R50FxFieldMode1), fast ? 1 : 0)
        put(&v, slot(2, R50FxFieldAlgorithm), 2)
        put(&v, slot(2, R50FxFieldMix), 0.18)
    }

    /// Every factory sound has a useful wheel gesture plus category-specific
    /// movement. Slot 1 is always the wheel so performance behaviour is
    /// predictable while browsing; the remaining routes give the bank life
    /// without forcing the same vibrato onto a bass, piano and pad.
    private static func addPerformanceModulation(
        _ v: inout [AUParameterAddress: AUValue], name: String, index: Int
    ) {
        let wheelAmount: AUValue
        if name.hasPrefix("BASS") {
            wheelAmount = 0.14
        } else if name.hasPrefix("PAD") || name.hasPrefix("STRINGS") {
            wheelAmount = 0.20
        } else if name.hasPrefix("REED") || name.hasPrefix("WIND")
                    || name.hasPrefix("BRASS") {
            wheelAmount = 0.17
        } else {
            wheelAmount = 0.16
        }
        mod(&v, slot: 0, source: ModSource.wheel,
            destination: ModDestination.cutoff, amount: wheelAmount)

        if name.hasPrefix("PAD") {
            put(&v, addr(R50ParamLfo1Rate), 0.10 + AUValue(index % 4) * 0.035)
            put(&v, addr(R50ParamLfo1Retrigger), 0)
            mod(&v, slot: 1, source: ModSource.lfo1,
                destination: ModDestination.cutoff, amount: 0.045)
            put(&v, addr(R50ParamLfo2Rate), 0.055 + AUValue(index % 3) * 0.025)
            put(&v, addr(R50ParamLfo2Retrigger), 0)
            mod(&v, slot: 2, source: ModSource.lfo2,
                destination: ModDestination.pan, amount: 0.12)
        } else if name.hasPrefix("STRINGS") {
            delayedVibrato(&v, amount: 0.007, rate: 4.7)
            put(&v, addr(R50ParamLfo2Rate), 0.13)
            put(&v, addr(R50ParamLfo2Retrigger), 0)
            mod(&v, slot: 2, source: ModSource.lfo2,
                destination: ModDestination.pan, amount: 0.07)
        } else if name.hasPrefix("SYNTH") || name.hasPrefix("LEAD")
                    || name.hasPrefix("REED") || name.hasPrefix("WIND")
                    || name.hasPrefix("BRASS") {
            delayedVibrato(&v, amount: name.hasPrefix("LEAD") ? 0.014 : 0.009,
                           rate: 4.8 + AUValue(index % 4) * 0.35)
        } else if name.hasPrefix("BASS") || name.hasPrefix("KEYS")
                    || name.hasPrefix("PLUCK") {
            mod(&v, slot: 1, source: ModSource.velocity,
                destination: ModDestination.cutoff,
                amount: name.hasPrefix("BASS") ? 0.08 : 0.11)
        } else {
            put(&v, addr(R50ParamLfo1Rate), 0.18)
            put(&v, addr(R50ParamLfo1Retrigger), 0)
            mod(&v, slot: 1, source: ModSource.lfo1,
                destination: ModDestination.pan, amount: 0.06)
        }

        if Int((v[addr(R50ParamToneStructure)] ?? 0).rounded())
            == Structure.ring {
            // The wheel both opens the sound and brings the product forward.
            mod(&v, slot: 3, source: ModSource.wheel,
                destination: ModDestination.ringLevel, amount: 0.12)
        }
        if Int((v[addr(R50ParamPatchStructure)] ?? 0).rounded()) == 4 {
            // Vector patches deliberately leave room above their base position.
            mod(&v, slot: 3, source: ModSource.wheel,
                destination: ModDestination.vectorMix, amount: 0.28)
        }
    }

    private static func delayedVibrato(
        _ v: inout [AUParameterAddress: AUValue],
        amount: AUValue, rate: AUValue
    ) {
        put(&v, addr(R50ParamLfo1Rate), rate)
        put(&v, addr(R50ParamLfo1Delay), 0.28)
        put(&v, addr(R50ParamLfo1Fade), 0.65)
        put(&v, addr(R50ParamLfo1Retrigger), 1)
        mod(&v, slot: 1, source: ModSource.lfo1,
            destination: ModDestination.pitch, amount: amount)
    }

    /// Ring is a Tone output now, not two hidden halves of the Partial buses.
    /// The ring product rides the main path at full level — through whatever
    /// insert slots the preset carries — with a modest alternating stereo
    /// position. (The old mixer split it 0.24 direct / 0.88 into the serial
    /// chain; with insert routing the whole path traverses the rack, and each
    /// insert's own Mix keeps the unprocessed component alive.)
    private static func configureRingOutput(
        _ v: inout [AUParameterAddress: AUValue], index: Int
    ) {
        if Int((v[addr(R50ParamToneStructure)] ?? 0).rounded())
            == Structure.ring {
            put(&v, addr(R50ParamToneRingPan), index.isMultiple(of: 2) ? -0.14 : 0.14)
            put(&v, addr(R50ParamToneRingDry), 1.0)
        }
        if Int((v[addr(R50ParamToneBStructure)] ?? 0).rounded())
            == Structure.ring {
            put(&v, addr(R50ParamToneBRingPan), index.isMultiple(of: 2) ? 0.14 : -0.14)
            put(&v, addr(R50ParamToneBRingDry), 1.0)
        }
    }

    private static func mod(
        _ v: inout [AUParameterAddress: AUValue], slot index: Int,
        source: Int, destination: Int, target: Int = 0, amount: AUValue
    ) {
        put(&v, modSlot(index, R50ModFieldSource), AUValue(source))
        put(&v, modSlot(index, R50ModFieldDestination), AUValue(destination))
        put(&v, modSlot(index, R50ModFieldTarget), AUValue(target))
        put(&v, modSlot(index, R50ModFieldAmount), amount)
    }

    private static func hasModWheelRoute(
        _ values: [AUParameterAddress: AUValue]
    ) -> Bool {
        for index in 0..<6
        where Int((values[modSlot(index, R50ModFieldSource)] ?? 0).rounded())
                == ModSource.wheel
           && Int((values[modSlot(index, R50ModFieldDestination)] ?? 0).rounded()) != 0
           && abs(values[modSlot(index, R50ModFieldAmount)] ?? 0) > 0.0001 {
            return true
        }
        return false
    }

    private static func put(
        _ values: inout [AUParameterAddress: AUValue],
        _ address: AUParameterAddress, _ value: AUValue
    ) {
        values[address] = value
    }

    private static func chorusRate(_ hertz: AUValue) -> AUValue {
        AUValue(log(max(0.05, hertz) / 0.05) / log(160.0))
    }
    private static func delayTime(_ seconds: AUValue) -> AUValue {
        AUValue(log(max(0.001, seconds) / 0.001) / log(2000.0))
    }
    private static func delayFeedback(_ amount: AUValue) -> AUValue {
        (amount + 0.95) / 1.9
    }
    private static func reverbDecay(_ seconds: AUValue) -> AUValue {
        AUValue(log(max(0.4, seconds) / 0.4) / log(30.0))
    }

    private static func addr(_ p: R50Param) -> AUParameterAddress {
        AUParameterAddress(p.rawValue)
    }
    private static func slot(
        _ index: Int, _ field: R50FxSlotField
    ) -> AUParameterAddress {
        AUParameterAddress(r50FxSlotParam(Int32(index), field).rawValue)
    }
    private static func modSlot(
        _ index: Int, _ field: R50ModSlotField
    ) -> AUParameterAddress {
        AUParameterAddress(r50ModSlotParam(Int32(index), field).rawValue)
    }
    private static func p1(_ field: R50PartialField) -> AUParameterAddress {
        AUParameterAddress(r50PartialParam(0, field).rawValue)
    }
    private static func p2(_ field: R50PartialField) -> AUParameterAddress {
        AUParameterAddress(r50PartialParam(1, field).rawValue)
    }
    private static func p3(_ field: R50PartialField) -> AUParameterAddress {
        AUParameterAddress(r50PartialParam(2, field).rawValue)
    }

    private static func partialAddress(
        _ index: Int
    ) -> (R50PartialField) -> AUParameterAddress {
        { field in AUParameterAddress(r50PartialParam(Int32(index), field).rawValue) }
    }

    /// Persistent factory asset IDs. Presets reference samples only through
    /// these; library indices are never written into a recipe, because an
    /// index is an accident of discovery order. The vocabulary is the old
    /// generated bank's; each name maps to the closest instrument in the
    /// sampled factory set that replaced it, so several names deliberately
    /// share a target (every struck bar is the vibraphone now).
    private enum Instrument {
        static let choir = "factory.ahh_choir"
        static let strings = "factory.strings"
        static let warmPad = "factory.ooh_choir"
        static let glassPad = "factory.glass_pad"
        static let voiceOoh = "factory.ooh_choir"
        static let flute = "factory.pan_flute"
        static let trumpet = "factory.brass_section"
        static let organ = "factory.percussive_organ"
        static let nylonGuitar = "factory.nylon_guitar_multisample"
        static let piano = "factory.piano_multisample"
        static let gong = "factory.spectrum_4"
        static let nasty = "factory.spectrum_9"
        static let fatBlock = "factory.spectrum_1"
        static let mallet = "factory.vibraphone"
        static let pluck = "factory.harp"
        static let chiff = "factory.pan_flute"
        static let noiseBurst = "factory.anvil"
        static let tineStrike = "factory.vibraphone"
        static let marimba = "factory.vibraphone"
        static let vibraphone = "factory.vibraphone"
        static let xylophone = "factory.vibraphone"
        static let kalimba = "factory.harp"
        static let slapBass = "factory.slap_bass_multisample"
        static let pullBass = "factory.picked_bass"
        static let pickedBass = "factory.picked_bass"
        static let acousticBass = "factory.acoustic_bass"
        static let pick = "factory.acoustic_guitar"
        static let pianoHammer = "factory.piano_multisample"
        static let anvil = "factory.anvil"
        static let taikoDrum = "factory.drum_kit"
        static let lipBuzz = "factory.brass_section"
        static let breath = "factory.pan_flute"
        static let bowScrape = "factory.cello"
        static let spectrum1 = "factory.spectrum_1"
        static let spectrum2 = "factory.spectrum_2"
        static let spectrum3 = "factory.spectrum_3"
        static let spectrum4 = "factory.spectrum_4"
        static let spectrum5 = "factory.spectrum_5"
        static let spectrum6 = "factory.spectrum_6"
        static let spectrum7 = "factory.spectrum_7"
        static let spectrum8 = "factory.spectrum_8"
        static let spectrum9 = "factory.spectrum_9"
        static let pizzicato = "factory.pizzicato_celli"
    }

    private enum Structure {
        static let mix = 0, ring = 1, attackSustain = 2
    }
    private enum ModSource {
        static let lfo1 = 1, lfo2 = 2, velocity = 6, wheel = 8
    }
    private enum ModDestination {
        static let pitch = 1, cutoff = 2, pan = 5
        static let ringLevel = 11, vectorMix = 12
    }
}
