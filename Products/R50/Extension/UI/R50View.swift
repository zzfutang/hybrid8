//
//  R50View.swift
//  The R50 editor. A fixed-size fascia laid out at R50Layout dimensions and
//  scaled uniformly to fit whatever size the host gives us, so the panel keeps
//  its proportions in Logic, in the standalone app and at any zoom.
//

import SwiftUI
import UniformTypeIdentifiers

enum R50Page: String, CaseIterable {
    case partial = "Partial"
    case tone    = "Tone"
    case samples = "Samples"
}

struct R50View: View {
    @ObservedObject var model: R50ParameterModel
    @ObservedObject var samples: R50SampleStore
    @State private var page: R50Page = .partial
    @State private var showingImporter = false
    /// Which Partial the Synth page panels are editing.
    @State private var partial = 0

    private func addr(_ field: R50PartialField) -> R50Param {
        r50PartialParam(Int32(partial), field)
    }

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / R50Layout.width,
                            geo.size.height / R50Layout.height)
            fascia
                .frame(width: R50Layout.width, height: R50Layout.height)
                .scaleEffect(scale, anchor: .center)
                .frame(width: geo.size.width, height: geo.size.height)
        }
        .background(R50Palette.chassisLow)
        // Keep the sample browser pointed at the Partial the Synth page edits.
        .onChange(of: partial) { samples.partial = $0 }
    }

    private var fascia: some View {
        VStack(spacing: 12) {
            header
            switch page {
            case .partial: partialPage
            case .tone:    tonePage
            case .samples: samplePage
            }
            footer
        }
        .padding(16)
        .background(
            LinearGradient(colors: [R50Palette.chassisTop, R50Palette.chassisLow],
                           startPoint: .top, endPoint: .bottom))
    }

    // MARK: - Pages

    private var partialPage: some View {
        VStack(spacing: 8) {
            partialStrip
            HStack(alignment: .top, spacing: 10) {
                oscillator.frame(width: 200)
                noise.frame(width: 215)
                filter.frame(width: 285)
                ampEnvelope.frame(width: 175)
                filterEnvelope.frame(width: 175)
            }
            .frame(height: 292)
        }
    }

    /// Which Partial the panels below edit, plus that Partial's own mix
    /// controls — they belong next to the selector rather than buried in a
    /// panel, because they are what make two Partials a Tone.
    private var partialStrip: some View {
        HStack(spacing: 10) {
            HStack(spacing: 2) {
                ForEach(0..<2, id: \.self) { index in
                    let selected = index == partial
                    let on = model.value(r50PartialParam(Int32(index),
                                                         R50FieldEnabled)) >= 0.5
                    Text("PARTIAL \(index + 1)")
                        .font(.system(size: 9, weight: .semibold, design: .monospaced))
                        .tracking(1.0)
                        .foregroundColor(selected ? Color.black
                                                  : (on ? R50Palette.legend
                                                        : Color(white: 0.42)))
                        .padding(.horizontal, 12)
                        .padding(.vertical, 5)
                        .background(selected ? R50Palette.glow : Color(white: 0.14))
                        .contentShape(Rectangle())
                        .onTapGesture { partial = index }
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 2))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))

            R50Selector(title: "", address: addr(R50FieldEnabled),
                        options: R50Parameters.onOffNames, model: model)
                .frame(width: 96)

            Text("EDITING PARTIAL \(partial + 1) — MIX AND STRUCTURE ON THE TONE PAGE")
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(1.0)
                .foregroundColor(R50Palette.glowDim)
            Spacer()
        }
    }

    /// How the two Partials relate: the structure that combines them and the
    /// mix that balances them. Moving these off the Partial page is what let
    /// that page stop overflowing its fascia.
    private var tonePage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "Structure") {
                VStack(alignment: .leading, spacing: 14) {
                    R50WaveGrid(title: "Tone Structure",
                                address: R50ParamToneStructure,
                                options: R50Parameters.toneStructureNames,
                                columns: 3, model: model)
                    Text(structureHelp)
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(4)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 300)

            R50Panel(title: "Structure Controls") {
                HStack(spacing: 4) {
                    R50Knob(title: "Ring", address: R50ParamToneRingLevel, model: model)
                    R50Knob(title: "Blend", address: R50ParamToneBlendTime, model: model)
                    R50Knob(title: "XF Low", address: R50ParamToneCrossfadeLow, model: model)
                    R50Knob(title: "XF High", address: R50ParamToneCrossfadeHigh, model: model)
                }
            }
            .frame(width: 290)

            R50Panel(title: "Partial Mix") {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(0..<2, id: \.self) { index in
                        HStack(spacing: 4) {
                            Text("P\(index + 1)")
                                .font(.system(size: 10, weight: .bold,
                                              design: .monospaced))
                                .foregroundColor(R50Palette.legend)
                                .frame(width: 24)
                            R50Knob(title: "Level",
                                    address: r50PartialParam(Int32(index), R50FieldLevel),
                                    model: model)
                            R50Knob(title: "Pan",
                                    address: r50PartialParam(Int32(index), R50FieldPan),
                                    model: model)
                            R50Knob(title: "Semi",
                                    address: r50PartialParam(Int32(index), R50FieldSemitone),
                                    model: model)
                            R50Knob(title: "Fine",
                                    address: r50PartialParam(Int32(index), R50FieldFine),
                                    model: model)
                        }
                    }
                }
            }
            .frame(width: 330)
        }
        .frame(height: 292)
    }

    private var structureHelp: String {
        switch Int(model.value(R50ParamToneStructure).rounded()) {
        case 1:  return "Ring: the product of both Partials. Level is each dry amount, Ring the product — it needs gain well past 1."
        case 2:  return "Atk/Sus: Partial 1 hands over to Partial 2 across Blend seconds."
        case 3:  return "Vel XF: soft notes favour Partial 1, hard notes Partial 2."
        case 4:  return "Key XF: fades from Partial 1 to Partial 2 between XF Low and XF High."
        default: return "Mix: both Partials sum, balanced by their Levels."
        }
    }

    private var samplePage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "Sample Browser") {
                VStack(alignment: .leading, spacing: 8) {
                    sampleTable
                    HStack(spacing: 8) {
                        actionButton("IMPORT…") { showingImporter = true }
                        actionButton("DELETE") {
                            if let entry = samples.entries
                                .first(where: { $0.index == samples.selectedIndex }) {
                                samples.delete(entry)
                            }
                        }
                        if samples.isImporting {
                            Text("LOADING…")
                                .font(.system(size: 8, weight: .medium,
                                              design: .monospaced))
                                .foregroundColor(R50Palette.glow)
                        }
                    }
                }
            }
            .frame(width: 800)

            R50Panel(title: "Playback") {
                VStack(alignment: .leading, spacing: 14) {
                    HStack(spacing: 4) {
                        R50Knob(title: "Start", address: addr(R50FieldSampleStart),
                                model: model)
                        R50Knob(title: "Octave", address: addr(R50FieldOctave),
                                model: model)
                    }
                    Text(statusText)
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(4)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 330)
        }
        .frame(height: 300)
        .fileImporter(isPresented: $showingImporter,
                      allowedContentTypes: [.wav, .aiff, .audio],
                      allowsMultipleSelection: false) { result in
            if case let .success(urls) = result, let url = urls.first {
                samples.importAudioFile(at: url)
            }
        }
    }

    /// A table, not a grid of buttons: what matters when picking a sample is
    /// its key span, whether it loops and how long it is, none of which fit on
    /// a button.
    private var sampleTable: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                tableCell("NAME", width: 190, header: true)
                tableCell("SRC", width: 55, header: true)
                tableCell("ZONES", width: 60, header: true)
                tableCell("KEY RANGE", width: 110, header: true)
                tableCell("LOOP", width: 70, header: true)
                tableCell("LENGTH", width: 80, header: true)
                tableCell("SIZE", width: 70, header: true)
            }
            .background(Color(white: 0.17))

            Rectangle().fill(R50Palette.panelEdge).frame(height: 1)

            ScrollView {
                VStack(spacing: 0) {
                    ForEach(samples.entries) { entry in
                        let selected = entry.index == samples.selectedIndex
                        HStack(spacing: 0) {
                            tableCell(entry.name, width: 190, selected: selected,
                                      align: .leading)
                            tableCell(entry.source, width: 55, selected: selected)
                            tableCell("\(entry.zones)", width: 60, selected: selected)
                            tableCell(entry.keyRange, width: 110, selected: selected)
                            tableCell(entry.loopLabel, width: 70, selected: selected)
                            tableCell(entry.lengthLabel, width: 80, selected: selected)
                            tableCell(entry.sizeLabel, width: 70, selected: selected)
                        }
                        .background(selected ? R50Palette.glow : Color(white: 0.12))
                        .contentShape(Rectangle())
                        .onTapGesture { samples.select(entry) }
                    }
                }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 2))
        .overlay(RoundedRectangle(cornerRadius: 2)
            .stroke(Color(white: 0.32), lineWidth: 1))
    }

    private func tableCell(_ text: String, width: CGFloat,
                           header: Bool = false, selected: Bool = false,
                           align: Alignment = .center) -> some View {
        Text(header ? text : text)
            .font(.system(size: header ? 8 : 9,
                          weight: header ? .medium : .semibold,
                          design: .monospaced))
            .tracking(header ? 0.8 : 0)
            .foregroundColor(header ? R50Palette.engrave
                                    : (selected ? Color.black : R50Palette.legend))
            .lineLimit(1)
            .minimumScaleFactor(0.65)
            .padding(.horizontal, 6)
            .padding(.vertical, header ? 5 : 4)
            .frame(width: width, alignment: align)
    }

    private func actionButton(_ title: String,
                              action: @escaping () -> Void) -> some View {
        Text(title)
            .font(.system(size: 9, weight: .semibold, design: .monospaced))
            .foregroundColor(R50Palette.legend)
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            .background(Color(white: 0.15))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))
            .contentShape(Rectangle())
            .onTapGesture(perform: action)
    }

    private var statusText: String {
        if let message = samples.errorMessage { return message }
        let sourceIsSample = model.value(addr(R50FieldSourceType)) >= 0.5
        return sourceIsSample
            ? "Sample source active. Factory instruments are generated at startup."
            : "Set Source to Sample on the Synth page to hear these."
    }

    // MARK: - Header

    private var header: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 0) {
                Text("R50")
                    .font(.system(size: 30, weight: .heavy, design: .monospaced))
                    .tracking(4)
                    .foregroundColor(R50Palette.legend)
                Text("Rytell · monotimbral 8-voice")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .tracking(1.2)
                    .foregroundColor(R50Palette.engrave)
            }

            pageTabs
            Spacer()
            presetBrowser
            Spacer()
            R50Meter(level: model.outputLevel)
        }
        .padding(.horizontal, 4)
    }

    private var pageTabs: some View {
        HStack(spacing: 2) {
            ForEach(R50Page.allCases, id: \.self) { candidate in
                let selected = candidate == page
                Text(candidate.rawValue.uppercased())
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .tracking(1.2)
                    .foregroundColor(selected ? Color.black : R50Palette.legend)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(selected ? R50Palette.glow : Color(white: 0.14))
                    .contentShape(Rectangle())
                    .onTapGesture { page = candidate }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 2))
        .overlay(RoundedRectangle(cornerRadius: 2)
            .stroke(Color(white: 0.32), lineWidth: 1))
    }

    private var presetBrowser: some View {
        HStack(spacing: 8) {
            stepButton("◀") { step(-1) }
            Text(R50FactoryPresets.all[safe: model.presetIndex]?.name ?? "Init")
                .font(.system(size: 11, weight: .semibold, design: .monospaced))
                .foregroundColor(R50Palette.glow)
                .frame(width: 170, height: 24)
                .background(R50Palette.track)
                .overlay(RoundedRectangle(cornerRadius: 2)
                    .stroke(Color(white: 0.30), lineWidth: 1))
            stepButton("▶") { step(1) }
        }
    }

    private func stepButton(_ glyph: String, action: @escaping () -> Void) -> some View {
        Text(glyph)
            .font(.system(size: 10, weight: .bold))
            .foregroundColor(R50Palette.legend)
            .frame(width: 22, height: 24)
            .background(Color(white: 0.15))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.30), lineWidth: 1))
            .contentShape(Rectangle())
            .onTapGesture(perform: action)
    }

    private func step(_ delta: Int) {
        let count = R50FactoryPresets.all.count
        guard count > 0 else { return }
        model.applyPreset((model.presetIndex + delta + count) % count)
    }

    // MARK: - Panels

    /// The Partial's sound source. Whichever kind is selected, its *name* is on
    /// screen here — a grid with one button per entry cannot do that once the
    /// list is a dozen waves plus however many samples have been imported.
    private var oscillator: some View {
        let isSample = model.value(addr(R50FieldSourceType)) >= 0.5
        return R50Panel(title: "Source") {
            VStack(alignment: .leading, spacing: 12) {
                R50Selector(title: "Type", address: addr(R50FieldSourceType),
                            options: R50Parameters.sourceTypeNames, model: model)

                R50NameSelector(
                    title: isSample ? "Instrument" : "Wave",
                    address: isSample ? addr(R50FieldSampleInstrument)
                                      : addr(R50FieldOscWave),
                    names: isSample ? samples.entries.map(\.name)
                                    : R50Parameters.waveformNames,
                    model: model)

                HStack(spacing: 4) {
                    if isSample {
                        R50Knob(title: "Start", address: addr(R50FieldSampleStart),
                                model: model)
                    } else {
                        R50Knob(title: "Width", address: addr(R50FieldPulseWidth),
                                model: model)
                    }
                    R50Knob(title: "Octave", address: addr(R50FieldOctave), model: model)
                }
            }
        }
    }

    private var noise: some View {
        R50Panel(title: "Noise") {
            VStack(alignment: .leading, spacing: 12) {
                R50WaveGrid(title: "Spectrum", address: addr(R50FieldNoiseSpectrum),
                            options: R50Parameters.noiseSpectrumNames,
                            columns: 3, model: model)
                HStack(spacing: 4) {
                    R50Knob(title: "Mix", address: addr(R50FieldNoiseMix), model: model)
                    R50Knob(title: "Tone", address: addr(R50FieldNoiseTone), model: model)
                    R50Knob(title: "Rate", address: addr(R50FieldNoiseRate), model: model)
                }
                R50Selector(title: "Tone / Rate Source",
                            address: addr(R50FieldNoisePitchTrack),
                            options: R50Parameters.trackNames, model: model)
            }
        }
    }

    private var filter: some View {
        R50Panel(title: "Filter") {
            VStack(alignment: .leading, spacing: 14) {
                R50Selector(title: "Slope", address: addr(R50FieldSlope),
                            options: R50Parameters.slopeNames, model: model)
                HStack(spacing: 4) {
                    R50Knob(title: "Cutoff", address: addr(R50FieldCutoff), model: model)
                    R50Knob(title: "Reso", address: addr(R50FieldResonance), model: model)
                    R50Knob(title: "Drive", address: addr(R50FieldDrive), model: model)
                    R50Knob(title: "Key Trk", address: addr(R50FieldKeyTrack), model: model)
                }
                HStack(spacing: 4) {
                    R50Knob(title: "Env Amt", address: addr(R50FieldFilterEnvAmount), model: model)
                }
            }
        }
    }

    private var ampEnvelope: some View {
        R50Panel(title: "Amp Envelope") {
            envelopeGrid(attack: R50FieldAmpAttack, decay: R50FieldAmpDecay,
                         sustain: R50FieldAmpSustain, release: R50FieldAmpRelease)
        }
    }

    private var filterEnvelope: some View {
        R50Panel(title: "Filter Envelope") {
            envelopeGrid(attack: R50FieldFilterAttack, decay: R50FieldFilterDecay,
                         sustain: R50FieldFilterSustain, release: R50FieldFilterRelease)
        }
    }

    private func envelopeGrid(attack: R50PartialField, decay: R50PartialField,
                              sustain: R50PartialField,
                              release: R50PartialField) -> some View {
        VStack(spacing: 12) {
            HStack(spacing: 4) {
                R50Knob(title: "Attack", address: addr(attack), model: model)
                R50Knob(title: "Decay", address: addr(decay), model: model)
            }
            HStack(spacing: 4) {
                R50Knob(title: "Sustain", address: addr(sustain), model: model)
                R50Knob(title: "Release", address: addr(release), model: model)
            }
        }
    }

    // MARK: - Footer

    private var footer: some View {
        HStack(spacing: 18) {
            R50Knob(title: "Master", address: R50ParamMasterGain, model: model)
            R50Knob(title: "Bend", address: R50ParamPitchBendRange, model: model)
            Spacer()
            Text("8-VOICE · BAND-LIMITED PCM + NOISE · ZDF LADDER")
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(1.4)
                .foregroundColor(R50Palette.glowDim)
        }
        .padding(.horizontal, 8)
    }
}

private extension Array {
    subscript(safe index: Int) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}
