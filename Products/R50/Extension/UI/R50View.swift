//
//  R50View.swift
//  The R50 editor. A fixed-size fascia laid out at R50Layout dimensions and
//  scaled uniformly to fit whatever size the host gives us, so the panel keeps
//  its proportions in Logic, in the standalone app and at any zoom.
//
//  The visual language is a digital workstation rather than an analogue
//  emulation: values are read out as numbers over bars, lists are lists, and
//  the envelopes are plotted. Nothing pretends to be a panel-mounted pot.
//

import SwiftUI
import UniformTypeIdentifiers

/// Which button opened the browser's file panel.
enum BrowserFileAction {
    case importAudio, exportFolder
}

enum R50Page: String, CaseIterable {
    case partial   = "Partial"
    case envelopes = "Envelopes"
    case tone      = "Tone"
    case modulation = "Mod"
    case effects   = "FX"
    case samples   = "Samples"
}

struct R50View: View {
    @ObservedObject var model: R50ParameterModel
    @ObservedObject var samples: R50SampleStore
    @State private var page: R50Page = .partial
    /// One file panel, not two. SwiftUI honours a single presentation modifier
    /// of each kind per view, so a second .fileImporter silently shadows the
    /// first — which is exactly what stopped IMPORT opening anything once
    /// EXPORT was added beside it.
    @State private var showingFilePanel = false
    @State private var fileAction: BrowserFileAction = .importAudio
    @State private var showingPresets = false
    /// Which Partial the Partial and Envelopes pages are editing.
    @State private var partial = 0
    @State private var auditionKey = 60

    private func addr(_ field: R50PartialField) -> R50Param {
        r50PartialParam(Int32(partial), field)
    }

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / R50Layout.width,
                            geo.size.height / R50Layout.height)
            ZStack(alignment: .top) {
                fascia
                    .frame(width: R50Layout.width, height: R50Layout.height,
                           alignment: .top)
                    .scaleEffect(scale, anchor: .top)
            }
            .frame(width: geo.size.width, height: geo.size.height,
                   alignment: .top)
        }
        .background(R50Palette.chassisLow)
        // Keep the sample browser pointed at the Partial being edited.
        .onChange(of: partial) { samples.partial = $0 }
    }

    private var fascia: some View {
        VStack(spacing: 12) {
            header
            pageContent
                .frame(maxWidth: .infinity,
                       minHeight: page == .effects
                           ? R50Layout.effectsPageHeight : R50Layout.pageHeight,
                       maxHeight: page == .effects
                           ? R50Layout.effectsPageHeight : R50Layout.pageHeight,
                       alignment: .top)
            if page != .effects {
                footer
            }
        }
        .padding(16)
        .background(
            LinearGradient(colors: [R50Palette.chassisTop, R50Palette.chassisLow],
                           startPoint: .top, endPoint: .bottom))
    }

    @ViewBuilder
    private var pageContent: some View {
        switch page {
        case .partial:   partialPage
        case .envelopes: envelopePage
        case .tone:      tonePage
        case .modulation: modPage
        case .effects:   effectsPage
        case .samples:   samplePage
        }
    }

    // MARK: - Header

    private var header: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 0) {
                Text("R50")
                    .font(R50Type.wordmark(size: 34))
                    .tracking(3)
                    .foregroundColor(R50Palette.legend)
                Text("DIGITAL SYNTHESIZER")
                    .font(R50Type.strapline(size: 9))
                    .tracking(1.4)
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
                    .tracking(1.0)
                    .foregroundColor(selected ? Color.black : R50Palette.legend)
                    .padding(.horizontal, 10)
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

    /// Steppers for walking neighbours, and the name itself opens the full list
    /// — stepping through twenty-one patches to reach one is not a way to
    /// choose a sound.
    private var presetBrowser: some View {
        HStack(spacing: 8) {
            stepButton("◀") { step(-1) }

            Button { showingPresets.toggle() } label: {
                HStack(spacing: 4) {
                    Text(R50FactoryPresets.all[safe: model.presetIndex]?.name ?? "Init")
                        .font(.system(size: 11, weight: .semibold, design: .monospaced))
                        .foregroundColor(R50Palette.glow)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                    Spacer(minLength: 2)
                    Text("▾")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundColor(R50Palette.engrave)
                }
                .padding(.horizontal, 8)
                .frame(width: 190, height: 24)
                .background(R50Palette.track)
                .overlay(RoundedRectangle(cornerRadius: 2)
                    .stroke(Color(white: 0.30), lineWidth: 1))
            }
            .buttonStyle(.plain)
            .popover(isPresented: $showingPresets, arrowEdge: .bottom) {
                presetList
            }

            stepButton("▶") { step(1) }
        }
    }

    private var presetList: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 1) {
                ForEach(Array(R50FactoryPresets.all.enumerated()), id: \.offset) {
                    index, preset in
                    let selected = index == model.presetIndex
                    Text(preset.name)
                        .font(.system(size: 11, weight: .medium, design: .monospaced))
                        .foregroundColor(selected ? Color.black : R50Palette.legend)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 5)
                        .background(selected ? R50Palette.glow : Color.clear)
                        .contentShape(Rectangle())
                        .onTapGesture {
                            model.applyPreset(index)
                            showingPresets = false
                        }
                }
            }
            .padding(4)
        }
        .frame(width: 230,
               height: min(CGFloat(R50FactoryPresets.all.count) * 26 + 12, 420))
        .background(Color(white: 0.13))
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

    /// Which Partial the page below edits, and whether it sounds at all.
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
            Spacer()
        }
    }

    // MARK: - Partial page

    private var partialPage: some View {
        VStack(spacing: 8) {
            partialStrip
            HStack(alignment: .top, spacing: 10) {
                source.frame(width: 262)
                noise.frame(width: 262)
                filter.frame(width: 300)
                shaper.frame(width: 236)
            }
            .frame(maxHeight: .infinity)
        }
    }

    /// The Partial's sound source. Whichever kind is selected, its name is on
    /// screen — a grid with one button per entry cannot do that once the list
    /// is a dozen waves plus however many samples have been imported.
    private var source: some View {
        let isSample = model.value(addr(R50FieldSourceType)) >= 0.5
        return R50Panel(title: "Source") {
            VStack(alignment: .leading, spacing: 8) {
                R50Selector(title: "Type", address: addr(R50FieldSourceType),
                            options: R50Parameters.sourceTypeNames, model: model)
                R50NameSelector(
                    title: isSample ? "Instrument" : "Wave",
                    address: isSample ? addr(R50FieldSampleInstrument)
                                      : addr(R50FieldOscWave),
                    names: isSample ? samples.entries.map(\.name)
                                    : R50Parameters.waveformNames,
                    model: model)
                if isSample {
                    R50Value(title: "Sample Start",
                             address: addr(R50FieldSampleStart), model: model)
                } else {
                    R50Value(title: "Pulse Width",
                             address: addr(R50FieldPulseWidth), model: model)
                }
                R50Value(title: "Octave", address: addr(R50FieldOctave), model: model)
                R50Value(title: "Semitone", address: addr(R50FieldSemitone), model: model)
                R50Value(title: "Fine", address: addr(R50FieldFine), model: model)
                R50Value(title: "Key Follow",
                         address: addr(R50FieldPitchKeyFollow), model: model)
            }
        }
    }

    private var noise: some View {
        R50Panel(title: "Noise") {
            VStack(alignment: .leading, spacing: 8) {
                R50WaveGrid(title: "Spectrum", address: addr(R50FieldNoiseSpectrum),
                            options: R50Parameters.noiseSpectrumNames,
                            columns: 4, model: model)
                R50Value(title: "Mix", address: addr(R50FieldNoiseMix), model: model)
                R50Value(title: "Tone", address: addr(R50FieldNoiseTone), model: model)
                R50Value(title: "Rate", address: addr(R50FieldNoiseRate), model: model)
                R50Selector(title: "Tone / Rate Source",
                            address: addr(R50FieldNoisePitchTrack),
                            options: R50Parameters.trackNames, model: model)
            }
        }
    }

    private var filter: some View {
        R50Panel(title: "Filter") {
            VStack(alignment: .leading, spacing: 8) {
                R50Selector(title: "Slope", address: addr(R50FieldSlope),
                            options: R50Parameters.slopeNames, model: model)
                R50Value(title: "Cutoff", address: addr(R50FieldCutoff), model: model)
                R50Value(title: "Resonance", address: addr(R50FieldResonance), model: model)
                R50Value(title: "Key Track", address: addr(R50FieldKeyTrack), model: model)
                R50Value(title: "Env Amount", address: addr(R50FieldFilterEnvAmount),
                         model: model)
            }
        }
    }

    private var shaper: some View {
        R50Panel(title: "Waveshaper") {
            VStack(alignment: .leading, spacing: 8) {
                R50Selector(title: "Type", address: addr(R50FieldShaperType),
                            options: R50Parameters.shaperTypeNames, model: model)
                R50Value(title: "Drive", address: addr(R50FieldShaperDrive), model: model)
                R50Selector(title: "Position", address: addr(R50FieldShaperPosition),
                            options: R50Parameters.shaperPositionNames, model: model)
                Text("PRE lets the filter tame what shaping adds. POST puts it on top of the filtered signal.")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .foregroundColor(R50Palette.engrave)
                    .lineLimit(4)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: - Envelopes page

    private var envelopePage: some View {
        VStack(spacing: 8) {
            partialStrip
            HStack(alignment: .top, spacing: 10) {
                envelopePanel(title: "Amp Envelope",
                              attack: R50FieldAmpAttack,
                              attackLevel: R50FieldAmpAttackLevel,
                              decay: R50FieldAmpDecay,
                              breakPoint: R50FieldAmpBreak,
                              slope: R50FieldAmpSlope,
                              sustain: R50FieldAmpSustain,
                              release: R50FieldAmpRelease)
                    .frame(width: 372)
                envelopePanel(title: "Filter Envelope",
                              attack: R50FieldFilterAttack,
                              attackLevel: R50FieldFilterAttackLevel,
                              decay: R50FieldFilterDecay,
                              breakPoint: R50FieldFilterBreak,
                              slope: R50FieldFilterSlope,
                              sustain: R50FieldFilterSustain,
                              release: R50FieldFilterRelease)
                    .frame(width: 372)
                pitchEnvelope.frame(width: 316)
            }
            .frame(maxHeight: .infinity)
        }
    }

    private func envelopePanel(title: String,
                               attack: R50PartialField,
                               attackLevel: R50PartialField,
                               decay: R50PartialField,
                               breakPoint: R50PartialField,
                               slope: R50PartialField,
                               sustain: R50PartialField,
                               release: R50PartialField) -> some View {
        R50Panel(title: title) {
            VStack(alignment: .leading, spacing: 6) {
                R50EnvelopeGraph(attack: addr(attack),
                                 attackLevel: addr(attackLevel),
                                 decay: addr(decay),
                                 breakPoint: addr(breakPoint),
                                 slope: addr(slope),
                                 sustain: addr(sustain),
                                 release: addr(release),
                                 model: model)
                    .frame(height: 58)

                HStack(alignment: .top, spacing: 10) {
                    VStack(spacing: 1) {
                        R50Value(title: "Attack", address: addr(attack), model: model)
                        R50Value(title: "Decay", address: addr(decay), model: model)
                        R50Value(title: "Slope", address: addr(slope), model: model)
                        R50Value(title: "Release", address: addr(release), model: model)
                    }
                    VStack(spacing: 1) {
                        R50Value(title: "Atk Level", address: addr(attackLevel), model: model)
                        R50Value(title: "Break", address: addr(breakPoint), model: model)
                        R50Value(title: "Sustain", address: addr(sustain), model: model)
                        Text("SLOPE 0 SKIPS BREAK")
                            .font(.system(size: 7, weight: .medium, design: .monospaced))
                            .foregroundColor(R50Palette.glowDim)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
            }
        }
    }

    private var pitchEnvelope: some View {
        R50Panel(title: "Pitch Envelope") {
            VStack(alignment: .leading, spacing: 8) {
                R50Value(title: "Amount", address: addr(R50FieldPitchAmount), model: model)
                R50Value(title: "Attack", address: addr(R50FieldPitchAttack), model: model)
                R50Value(title: "Decay", address: addr(R50FieldPitchDecay), model: model)
                Text("Bends this Partial at note-on and settles back to pitch. A few semitones with a short decay is what makes a sampled attack read as struck.")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .foregroundColor(R50Palette.engrave)
                    .lineLimit(6)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: - Tone page

    private var tonePage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "Structure") {
                VStack(alignment: .leading, spacing: 12) {
                    R50WaveGrid(title: "Tone Structure",
                                address: R50ParamToneStructure,
                                options: R50Parameters.toneStructureNames,
                                columns: 3, model: model)
                    Text(structureHelp)
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(5)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 320)

            R50Panel(title: "Structure Controls") {
                VStack(alignment: .leading, spacing: 8) {
                    R50Value(title: "Ring Level", address: R50ParamToneRingLevel,
                             model: model)
                    R50Value(title: "Blend Time", address: R50ParamToneBlendTime,
                             model: model)
                    R50Value(title: "XF Low", address: R50ParamToneCrossfadeLow,
                             model: model)
                    R50Value(title: "XF High", address: R50ParamToneCrossfadeHigh,
                             model: model)
                }
            }
            .frame(width: 300)

            R50Panel(title: "Partial Mix") {
                VStack(alignment: .leading, spacing: 12) {
                    ForEach(0..<2, id: \.self) { index in
                        VStack(alignment: .leading, spacing: 2) {
                            Text("PARTIAL \(index + 1)")
                                .font(.system(size: 8, weight: .bold,
                                              design: .monospaced))
                                .tracking(1.0)
                                .foregroundColor(R50Palette.glowDim)
                            R50Value(title: "Level",
                                     address: r50PartialParam(Int32(index), R50FieldLevel),
                                     model: model)
                            R50Value(title: "Pan",
                                     address: r50PartialParam(Int32(index), R50FieldPan),
                                     model: model)
                            R50Value(title: "Dry",
                                     address: r50PartialParam(Int32(index),
                                                               R50FieldDryLevel),
                                     model: model)
                            HStack(spacing: 5) {
                                R50Value(title: "Send 1",
                                    address: r50PartialParam(Int32(index),
                                                              R50FieldSend1),
                                    model: model)
                                R50Value(title: "Send 2",
                                    address: r50PartialParam(Int32(index),
                                                              R50FieldSend2),
                                    model: model)
                                R50Value(title: "Send 3",
                                    address: r50PartialParam(Int32(index),
                                                              R50FieldSend3),
                                    model: model)
                            }
                        }
                    }
                }
            }
            .frame(width: 300)
        }
        .frame(maxHeight: .infinity)
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

    // MARK: - Mod page

    /// Two LFOs, a six-slot matrix and four macros. The matrix is a table for
    /// the same reason the sample browser is: source, target, destination and
    /// amount are four facts about one route, and they read across.
    private var modPage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "LFO 1") { lfoControls(0) }.frame(width: 215)
            R50Panel(title: "LFO 2") { lfoControls(1) }.frame(width: 215)

            R50Panel(title: "Modulation Matrix") {
                modMatrixTable
            }
            .frame(width: 470)

            R50Panel(title: "Macros") {
                VStack(alignment: .leading, spacing: 8) {
                    R50Value(title: "Macro 1", address: R50ParamMacro1, model: model)
                    R50Value(title: "Macro 2", address: R50ParamMacro2, model: model)
                    R50Value(title: "Macro 3", address: R50ParamMacro3, model: model)
                    R50Value(title: "Macro 4", address: R50ParamMacro4, model: model)
                    Text("Macros are matrix sources. Route one to several destinations to sweep them together.")
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(4)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 218)
        }
        .frame(maxHeight: .infinity)
    }

    private func lfoControls(_ index: Int) -> some View {
        let base = index == 0 ? R50ParamLfo1Wave.rawValue : R50ParamLfo2Wave.rawValue
        func at(_ offset: UInt64) -> R50Param { R50Param(base + offset) }
        return VStack(alignment: .leading, spacing: 8) {
            R50NameSelector(title: "Wave", address: at(0),
                            names: R50Parameters.lfoWaveNames, model: model)
            R50Value(title: "Rate", address: at(1), model: model)
            R50Value(title: "Delay", address: at(2), model: model)
            R50Value(title: "Fade", address: at(3), model: model)
            R50Selector(title: "Phase Source", address: at(4),
                        options: ["Free", "Note"], model: model)
            R50Value(title: "Phase", address: at(5), model: model)
        }
    }

    private var modMatrixTable: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                tableCell("SOURCE", width: 130, header: true)
                tableCell("TARGET", width: 70, header: true)
                tableCell("DESTINATION", width: 130, header: true)
                tableCell("AMOUNT", width: 110, header: true)
            }
            .background(Color(white: 0.17))
            Rectangle().fill(R50Palette.panelEdge).frame(height: 1)

            ForEach(0..<6, id: \.self) { slot in
                HStack(spacing: 4) {
                    R50NameSelector(
                        title: "",
                        address: r50ModSlotParam(Int32(slot), R50ModFieldSource),
                        names: R50Parameters.modSourceNames, model: model)
                        .frame(width: 126)
                    R50NameSelector(
                        title: "",
                        address: r50ModSlotParam(Int32(slot), R50ModFieldTarget),
                        names: R50Parameters.modTargetNames, model: model)
                        .frame(width: 66)
                    R50NameSelector(
                        title: "",
                        address: r50ModSlotParam(Int32(slot), R50ModFieldDestination),
                        names: R50Parameters.modDestinationNames, model: model)
                        .frame(width: 126)
                    R50Value(title: "",
                             address: r50ModSlotParam(Int32(slot), R50ModFieldAmount),
                             model: model)
                        .frame(width: 106)
                }
                .padding(.vertical, 1)
            }
        }
    }

    // MARK: - Effects page

    private var effectsPage: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 10) {
                R50Panel(title: "Routing") {
                    VStack(alignment: .leading, spacing: 8) {
                        R50Selector(title: "Topology", address: R50ParamFxTopology,
                                    options: R50Parameters.effectTopologyNames,
                                    model: model)
                        topologyDiagram
                    }
                }
                .frame(width: 650)
                R50Panel(title: "Post-Rack Compressor") {
                    VStack(alignment: .leading, spacing: 7) {
                        R50Value(title: "Amount", address: R50ParamFxCompressor,
                                 model: model)
                        Text("STEREO LINKED • AFTER SLOT 3")
                            .font(.system(size: 8, weight: .medium,
                                          design: .monospaced))
                            .tracking(0.7)
                            .foregroundColor(R50Palette.engrave)
                    }
                }
                .frame(maxWidth: .infinity)
            }
            // Routing needs the selector plus its diagram below the common
            // panel header. At 118 points it drew outside the proposed row,
            // below the compressor, and swallowed the inter-row margin.
            .frame(height: 140)
            HStack(alignment: .top, spacing: 10) {
                ForEach(0..<3, id: \.self) { slot in effectSlot(slot) }
            }
            .frame(maxHeight: .infinity)
        }
        .frame(maxHeight: .infinity)
    }

    private func fxAddress(_ slot: Int, _ field: R50FxSlotField) -> R50Param {
        r50FxSlotParam(Int32(slot), field)
    }

    private func effectSlot(_ slot: Int) -> some View {
        let algorithm = Int(model.value(
            fxAddress(slot, R50FxFieldAlgorithm)).rounded())
        let labels = effectControlLabels(algorithm)
        return R50Panel(title: "Slot \(slot + 1)") {
            VStack(alignment: .leading, spacing: 7) {
                HStack(alignment: .top, spacing: 8) {
                    R50NameSelector(title: "Algorithm",
                        address: fxAddress(slot, R50FxFieldAlgorithm),
                        names: R50Parameters.effectAlgorithmNames, model: model)
                        .frame(maxWidth: .infinity)
                    R50Selector(title: "Bypass",
                        address: fxAddress(slot, R50FxFieldBypass),
                        options: R50Parameters.onOffNames, model: model)
                        .frame(width: 132)
                }
                HStack(alignment: .top, spacing: 8) {
                    VStack(alignment: .leading, spacing: 6) {
                        R50Value(title: "Input",
                            address: fxAddress(slot, R50FxFieldInputGain), model: model)
                        R50Value(title: "Output",
                            address: fxAddress(slot, R50FxFieldOutputGain), model: model)
                        R50MappedValue(title: "Mix / Return",
                            address: fxAddress(slot, R50FxFieldMix), model: model,
                            display: percent)
                        R50Value(title: "Width",
                            address: fxAddress(slot, R50FxFieldWidth), model: model,
                            displayOverride: { String(format: "%.0f%%", $0 * 100) })
                    }
                    VStack(alignment: .leading, spacing: 5) {
                        HStack(alignment: .top, spacing: 8) {
                            ForEach(0..<2, id: \.self) { column in
                                VStack(alignment: .leading, spacing: 5) {
                                    ForEach(Array(labels.enumerated()).filter {
                                        $0.offset % 2 == column && !$0.element.isEmpty
                                    }, id: \.offset) { index, label in
                                        R50MappedValue(title: label,
                                            address: fxAddress(slot, R50FxSlotField(
                                                rawValue: R50FxFieldControl1.rawValue
                                                    + UInt32(index))),
                                            model: model,
                                            display: { effectControlDisplay(
                                                algorithm, index: index, value: $0) })
                                    }
                                }
                                .frame(maxWidth: .infinity)
                            }
                        }
                        effectModeControls(slot, algorithm: algorithm)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity)
        .help(effectHelp(algorithm))
    }

    private func effectHelp(_ algorithm: Int) -> String {
        switch algorithm {
        case 0: return "Off passes audio unchanged in a serial path and returns silence in a parallel path."
        case 1: return "Hall: a broad, smooth global reverb for long spaces and pads."
        case 2: return "Room: a shorter global reverb with a compact, audible room boundary."
        case 3: return "Plate/Stage: bright plate density or a darker, tighter stage character."
        case 4: return "Early Reflections: discrete ambience patterns without a long late-reverb tail."
        case 5: return "Stereo Delay: independent left and right delay times with filtered feedback."
        case 6: return "Cross Delay: stereo delay whose feedback can cross between channels."
        case 7: return "Chorus: a single modulated delay pair for pitch motion and stereo width."
        case 8: return "Ensemble: multiple drifting voices for a denser string-machine modulation."
        case 9: return "Flanger: a very short modulated delay with bipolar feedback."
        case 10: return "Phaser: six or twelve swept all-pass stages with stereo phase offset."
        case 11: return "Tremolo/Pan: amplitude modulation, stereo tremolo, or automatic panning."
        case 12: return "Rotary: accelerating horn and drum rotors with Doppler and amplitude motion."
        case 13: return "Equalizer: global low shelf, mid bell, and high shelf tone shaping."
        case 14: return "Overdrive: oversampled soft saturation with selectable digital character."
        case 15: return "Distortion: oversampled hard, cubic, or fuzz transfer functions."
        case 16: return "Exciter: filtered high-frequency harmonics blended with the direct signal."
        default: return "Global effect slot."
        }
    }

    private func effectControlLabels(_ algorithm: Int) -> [String] {
        switch algorithm {
        case 8: return ["Rate", "Depth", "Spread", "Drift", "Tone", "Low Cut"]
        case 9: return ["Rate", "Depth", "Manual", "Feedback",
                        "Stereo Phase", "Tone"]
        case 10: return ["Rate", "Depth", "Center", "Spread",
                         "Feedback", "", "", "Stereo Phase"]
        case 11: return ["Rate", "Depth", "Shape", "Stereo Phase", "Bias"]
        case 12: return ["Slow", "Fast", "Rotor Ratio", "Accel",
                         "Doppler", "AM Depth", "Xover"]
        case 13: return ["Low Gain", "Low Freq", "Mid Gain", "", "",
                         "High Gain", "High Freq"]
        case 14: return ["Drive", "", "Asymmetry", "", "", "Level"]
        case 15: return ["Drive", "", "Bias", "", "", "Level"]
        case 16: return ["Frequency", "Drive", "Harmonics", "Amount"]
        case 7: return ["Rate", "Depth"]
        case 5: return ["Left Time", "Right Time", "Feedback", "",
                        "Low Cut", "High Cut", "Saturation"]
        case 6: return ["Left Time", "Right Time", "Feedback", "Cross",
                        "Low Cut", "High Cut", "Saturation"]
        case 1, 2, 3:
            return ["Pre-delay", "Decay", "Size", "Damping"]
        case 4:
            return ["Pre-delay", "Length", "Density", "Decay Shape",
                    "Tone", "Spread"]
        case 0: return []
        default: return ["Control 1", "Control 2", "Control 3", "Control 4"]
        }
    }

    private func percent(_ value: Float) -> String {
        String(format: "%.0f%%", value * 100)
    }

    private func signedPercent(_ value: Float) -> String {
        String(format: "%+.0f%%", (value * 2 - 1) * 95)
    }

    private func frequency(_ hz: Float) -> String {
        hz >= 1000 ? String(format: "%.1f kHz", hz / 1000)
                   : String(format: "%.0f Hz", hz)
    }

    private func duration(_ seconds: Float) -> String {
        seconds >= 1 ? String(format: "%.2f s", seconds)
                     : String(format: "%.0f ms", seconds * 1000)
    }

    /// Must mirror EffectSlot::setDescriptor. Storage remains normalized so
    /// automation addresses and saved states stay stable.
    private func effectControlDisplay(_ algorithm: Int, index: Int,
                                      value: Float) -> String {
        switch (algorithm, index) {
        case (1, 0), (3, 0): return duration(value * 0.2)
        case (2, 0): return duration(value * 0.08)
        case (1, 1): return duration(0.4 * pow(30, value))
        case (2, 1): return duration(0.15 * pow(26.6667, value))
        case (3, 1): return duration(0.3 * pow(40, value))
        case (1...3, 2): return percent(value)
        case (1...3, 3): return frequency(1000 * pow(20, value))
        case (1...3, 4): return String(format: "%.2f×", 0.25 + 1.75 * value)
        case (1...3, 5...7): return percent(value)
        case (4, 0): return duration(value * 0.2)
        case (4, 1): return duration(0.03 * pow(26.6667, value))
        case (4, 2): return percent(value)
        case (4, 3): return String(format: "%+.0f%%", (value * 2 - 1) * 100)
        case (4, 4): return frequency(500 * pow(40, value))
        case (4, 5): return String(format: "%.0f%%", value * 200)
        case (5...6, 0), (5...6, 1):
            return duration(0.001 * pow(2000, value))
        case (5...6, 2): return signedPercent(value)
        case (6, 3): return percent(value)
        case (5...6, 4): return frequency(20 * pow(100, value))
        case (5...6, 5): return frequency(500 * pow(40, value))
        case (5...6, 6): return percent(value)
        case (7, 0): return String(format: "%.2f Hz", 0.05 * pow(160, value))
        case (7, 1): return percent(value)
        case (8, 0): return String(format: "%.2f Hz", 0.03 * pow(100, value))
        case (8, 1), (8, 2), (8, 3): return percent(value)
        case (8, 4): return frequency(1000 * pow(20, value))
        case (8, 5): return frequency(20 * pow(25, value))
        case (9, 0), (10, 0):
            return String(format: "%.2f Hz", 0.03 * pow(333.333, value))
        case (9, 1), (10, 1): return percent(value)
        case (9, 2): return duration(0.0001 * pow(150, value))
        case (9, 3): return signedPercent(value)
        case (9, 4), (10, 7): return String(format: "%.0f°", value * 180)
        case (9, 5): return frequency(1000 * pow(20, value))
        case (10, 2): return frequency(80 * pow(50, value))
        case (10, 3): return String(format: "%.1f oct", 0.5 + 5.5 * value)
        case (10, 4): return signedPercent(value)
        case (11, 0): return String(format: "%.2f Hz", 0.03 * pow(666.667, value))
        case (11, 1): return percent(value)
        case (11, 2), (11, 4):
            return String(format: "%+.0f%%", (value * 2 - 1) * 100)
        case (11, 3): return String(format: "%.0f°", value * 180)
        case (12, 0): return String(format: "%.2f Hz", 0.2 * pow(7.5, value))
        case (12, 1): return String(format: "%.2f Hz", 3 * pow(3.333333, value))
        case (12, 2): return String(format: "%.2f×", 0.5 * pow(4, value))
        case (12, 3): return duration(0.2 * pow(40, value))
        case (12, 4), (12, 5): return percent(value)
        case (12, 6): return frequency(400 * pow(5, value))
        case (13, 0), (13, 2), (13, 5):
            return String(format: "%+.1f dB", value * 36 - 18)
        case (13, 1): return frequency(40 * pow(25, value))
        case (13, 3): return frequency(100 * pow(100, value))
        case (13, 4): return String(format: "%.2f", 0.2 * pow(40, value))
        case (13, 6): return frequency(1000 * pow(16, value))
        case (14, 0): return String(format: "%.1f dB", value * 36)
        case (15, 0): return String(format: "%.1f dB", value * 48)
        case (14...15, 1): return frequency(500 * pow(32, value))
        case (14, 2): return percent(value)
        case (15, 2): return String(format: "%+.0f%%", (value * 2 - 1) * 100)
        case (14, 3): return String(format: "%+.1f dB", value * 24 - 12)
        case (15, 3): return frequency(20 * pow(50, value))
        case (14, 5): return String(format: "%+.1f dB", value * 36 - 24)
        case (15, 5): return String(format: "%+.1f dB", value * 42 - 30)
        case (16, 0): return frequency(1000 * pow(10, value))
        case (16, 1): return String(format: "%.1f dB", value * 36)
        case (16, 2), (16, 3): return percent(value)
        case (16, 4): return frequency(4000 * pow(5, value))
        case (16, 5): return String(format: "%+.1f dB", value * 24 - 12)
        default: return String(format: "%.2f", value)
        }
    }

    @ViewBuilder
    private func effectModeControls(_ slot: Int, algorithm: Int) -> some View {
        switch algorithm {
        case 3:
            R50Selector(title: "Character",
                        address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Plate", "Stage"], model: model)
        case 4:
            R50Selector(title: "Pattern",
                        address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Room", "Hall", "Gated", "Reverse"], model: model)
        case 9:
            HStack(spacing: 8) {
                R50Selector(title: "Feedback", address: fxAddress(slot, R50FxFieldMode1),
                            options: ["Same", "Cross"], model: model)
                R50Selector(title: "Wave", address: fxAddress(slot, R50FxFieldMode2),
                            options: ["Sine", "Triangle"], model: model)
            }
        case 10:
            HStack(spacing: 8) {
                R50Selector(title: "Stages", address: fxAddress(slot, R50FxFieldMode1),
                            options: ["6", "12"], model: model)
                R50Selector(title: "Wave", address: fxAddress(slot, R50FxFieldMode2),
                            options: ["Sine", "Triangle"], model: model)
            }
        case 11:
            R50Selector(title: "Mode", address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Tremolo", "Stereo", "Auto-Pan"], model: model)
        case 12:
            R50Selector(title: "Speed", address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Stop", "Slow", "Fast"], model: model)
        case 14:
            R50Selector(title: "Character",
                        address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Soft", "Warm", "Bright"], model: model)
        case 15:
            R50Selector(title: "Mode",
                        address: fxAddress(slot, R50FxFieldMode1),
                        options: ["Hard", "Cubic", "Fuzz"], model: model)
        default:
            EmptyView()
        }
    }

    private var topologyDiagram: some View {
        let topology = Int(model.value(R50ParamFxTopology).rounded())
        let text: String
        switch topology {
        case 1: text = "DRY ─────────► OUT    ① + ② + ③ ─────────► OUT"
        case 2: text = "DRY ─► OUT    ① ─► ② ─┐    ③ ─────────┴► OUT"
        case 3: text = "DRY ─► OUT    ① ─┐    ② ─┴─► ③ ───────► OUT"
        default: text = "DRY ─► OUT    SEND ─► ① ─► ② ─► ③ ───► OUT"
        }
        return Text(text)
            .font(.system(size: 9, weight: .semibold, design: .monospaced))
            .foregroundColor(R50Palette.glow)
            .padding(.horizontal, 8)
            .padding(.vertical, 7)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(R50Palette.track)
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))
            .help("Dry is always summed directly. Sends feed the three global slots; topology determines whether those slots run in series or parallel.")
    }

    // MARK: - Samples page

    private var samplePage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "Sample Browser") {
                VStack(alignment: .leading, spacing: 8) {
                    sampleTable
                    HStack(spacing: 8) {
                        actionButton("IMPORT…") {
                            fileAction = .importAudio
                            showingFilePanel = true
                        }
                        actionButton("EXPORT…") {
                            fileAction = .exportFolder
                            showingFilePanel = true
                        }
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
                        } else {
                            Text("EXPORT WRITES THE SELECTED SAMPLE'S ZONES AS WAV — + MARKS AN IMPORT")
                                .font(.system(size: 8, weight: .medium,
                                              design: .monospaced))
                                .tracking(0.8)
                                .foregroundColor(R50Palette.glowDim)
                        }
                    }
                }
            }
            .frame(width: 800)

            R50Panel(title: "Playback") {
                VStack(alignment: .leading, spacing: 8) {
                    R50Value(title: "Sample Start", address: addr(R50FieldSampleStart),
                             model: model)
                    R50Value(title: "Octave", address: addr(R50FieldOctave), model: model)

                    rootKeyEditor
                    factoryFolderRow

                    // Multisamples exist because they do not sound the same
                    // across the keyboard, so a preview fixed at middle C would
                    // hide the very thing the zones are there to fix.
                    HStack(spacing: 6) {
                        Text("AUDITION KEY")
                            .font(.system(size: 8, weight: .medium, design: .monospaced))
                            .tracking(0.8)
                            .foregroundColor(R50Palette.engrave)
                        Spacer()
                        actionButton("<") { auditionKey = max(12, auditionKey - 12) }
                        Text(SampleEntry.noteName(auditionKey))
                            .font(.system(size: 9, weight: .semibold, design: .monospaced))
                            .foregroundColor(R50Palette.legend)
                            .frame(width: 34)
                        actionButton(">") { auditionKey = min(120, auditionKey + 12) }
                    }
                    Text(statusText)
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(5)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 330)
        }
        .frame(maxHeight: .infinity)
        // Export picks a folder, not a file: a generated multisample is five
        // zones and they are only useful together. `fileAction` is deliberately
        // not cleared by the binding — the completion handler needs to still
        // know which button opened the panel.
        .fileImporter(isPresented: $showingFilePanel,
                      allowedContentTypes: fileAction == .exportFolder
                          ? [.folder] : [.wav, .aiff, .audio],
                      allowsMultipleSelection: false) { result in
            guard case let .success(urls) = result, let url = urls.first else { return }
            switch fileAction {
            case .importAudio:
                samples.importAudioFile(at: url)
            case .exportFolder:
                if let entry = samples.entries
                    .first(where: { $0.index == samples.selectedIndex }) {
                    samples.exportInstrument(entry, to: url)
                }
            }
        }
    }

    /// A table, not a grid of buttons: what matters when picking a sample is
    /// its key span, whether it loops and how long it is, and none of that fits
    /// on a button.
    private var sampleTable: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                tableCell("", width: 34, header: true)
                tableCell("NAME", width: 235, header: true)
                tableCell("ZONES", width: 60, header: true)
                tableCell("KEY RANGE", width: 120, header: true)
                tableCell("ROOT", width: 74, header: true)
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
                            playCell(entry, selected: selected)
                            tableCell(entry.isFactory ? entry.name : "+ " + entry.name,
                                      width: 235, selected: selected,
                                      align: .leading, accent: !entry.isFactory)
                            tableCell("\(entry.zones)", width: 60, selected: selected)
                            tableCell(entry.keyRange, width: 120, selected: selected)
                            tableCell(entry.rootLabel, width: 74, selected: selected,
                                      accent: entry.retunable)
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

    /// The factory samples are real files now, so the only thing standing
    /// between someone and editing one is knowing where they are — and they are
    /// inside the sandbox container, which is not a guessable path.
    private var factoryFolderRow: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Text("FACTORY FILES")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .tracking(0.8)
                    .foregroundColor(R50Palette.engrave)
                Spacer()
                actionButton("REVEAL") { samples.revealFactoryDirectory() }
            }
            Text("The shipped factory WAVs. Edit them in Products/R50/factory_samples and rebuild; factory_samples.json maps each file to its instrument.")
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .foregroundColor(R50Palette.engrave)
                .lineLimit(3)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// Root key of the selected import. Detection is a guess — it is right far
    /// more often than a hardcoded middle C, but a one-shot or a noisy sample
    /// can defeat it, so it has to be correctable by hand.
    private var rootKeyEditor: some View {
        let entry = samples.entries.first { $0.index == samples.selectedIndex }
        return VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Text("ROOT KEY")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .tracking(0.8)
                    .foregroundColor(R50Palette.engrave)
                Spacer()
                if let entry, entry.retunable {
                    actionButton("<") { samples.setRoot(entry, key: entry.rootKey - 1,
                                                        cents: entry.tuneCents) }
                    Text(entry.rootLabel)
                        .font(.system(size: 9, weight: .semibold, design: .monospaced))
                        .foregroundColor(R50Palette.legend)
                        .frame(width: 56)
                    actionButton(">") { samples.setRoot(entry, key: entry.rootKey + 1,
                                                        cents: entry.tuneCents) }
                } else {
                    Text(entry?.rootLabel ?? "—")
                        .font(.system(size: 9, weight: .semibold, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                }
            }
            if let entry, entry.retunable {
                HStack(spacing: 8) {
                    actionButton("DETECT") { samples.redetect(entry) }
                    actionButton("ZERO CENTS") { samples.setRoot(entry, key: entry.rootKey,
                                                                 cents: 0) }
                }
            }
        }
    }

    /// Its own hit area rather than the row's: tapping a row assigns the sample
    /// to the Partial, and you have to be able to hear one without doing that.
    private func playCell(_ entry: SampleEntry, selected: Bool) -> some View {
        Text("\u{25B6}")
            .font(.system(size: 9, weight: .bold))
            .foregroundColor(selected ? Color.black : R50Palette.glow)
            .frame(width: 34, height: 17)
            .contentShape(Rectangle())
            .onTapGesture { samples.audition(entry, note: auditionKey) }
    }

    private func tableCell(_ text: String, width: CGFloat,
                           header: Bool = false, selected: Bool = false,
                           align: Alignment = .center,
                           accent: Bool = false) -> some View {
        Text(text)
            .font(.system(size: header ? 8 : 9,
                          weight: header ? .medium : .semibold,
                          design: .monospaced))
            .tracking(header ? 0.8 : 0)
            .foregroundColor(header ? R50Palette.engrave
                                    : (selected ? Color.black
                                                : (accent ? R50Palette.accent
                                                          : R50Palette.legend)))
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
            ? "Sample source active on Partial \(partial + 1). Factory samples load from WAV files on disk."
            : "Set Source to Sample on the Partial page to hear these."
    }

    // MARK: - Footer

    private var footer: some View {
        HStack(spacing: 18) {
            R50Value(title: "Master", address: R50ParamMasterGain, model: model)
                .frame(width: 190)
            R50Value(title: "Bend Range", address: R50ParamPitchBendRange, model: model)
                .frame(width: 190)
            Spacer()
            Text("8-VOICE · 2 PARTIALS · BAND-LIMITED PCM + NOISE · ZDF LADDER")
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
