//
//  SynthView.swift
//  The instrument fascia: wood end-cheeks, a brushed charcoal panel, a branded
//  header with a preset browser, and six accent-coloured sections of knobs and
//  lit selector buttons. Aesthetic: mid-'80s Yamaha × Oberheim.
//

import SwiftUI
import AudioToolbox
import UniformTypeIdentifiers
#if canImport(AppKit)
import AppKit
#endif

struct SynthView: View {
    // A wide hardware-panel aspect ratio gives the three synthesis columns and
    // the arp/FX row enough horizontal room. The whole fascia still scales
    // uniformly when a host supplies a smaller editor window.
    private static let designSize = CGSize(width: 1700, height: 824)

    // Width of one grid column. The 3-column row sizes its columns to equal
    // thirds automatically; reusing this for the X-Mod panel makes the
    // Arpeggiator beside it start exactly on the middle column's left edge.
    private static var columnWidth: CGFloat {
        let content = designSize.width - 36 - 24   // wood cheeks (2×18) + padding (2×12)
        return (content - 24) / 3                  // 3 columns, 2 gaps of 12
    }

    @ObservedObject var model: ParameterModel
    @StateObject private var wavetables: WavetableStore
    @StateObject private var presets: PresetStore
    @StateObject private var help = HelpModel()
    @State private var showingSave = false
    @State private var saveName = ""
    @State private var effectsTab = 0
    @State private var showingWavetableBrowser = false
    @State private var wavetableBrowserOscillator = 1
    @State private var mainTab = 0        // 0 = Synth, 1 = Performance

    init(model: ParameterModel, wavetables: WavetableStore) {
        _model = ObservedObject(wrappedValue: model)
        _wavetables = StateObject(wrappedValue: wavetables)
        _presets = StateObject(wrappedValue: PresetStore(model: model))
    }

    var body: some View {
        GeometryReader { geometry in
            let widthScale = geometry.size.width / Self.designSize.width
            let heightScale = geometry.size.height / Self.designSize.height
            let scale = max(0.35, min(widthScale, heightScale))

            ZStack {
                fascia
                    .frame(width: Self.designSize.width,
                           height: Self.designSize.height)
                    .scaleEffect(scale)
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipped()
            .background(Palette.panelBottom)
        }
    }

    private var fascia: some View {
        HStack(spacing: 0) {
            woodCheek
            panel
            woodCheek
        }
        .background(Palette.panelBottom)
        .environmentObject(help)
    }

    // MARK: Wood side panels

    private var woodCheek: some View {
        LinearGradient(colors: [Palette.woodMid, Palette.woodLight, Palette.woodDark, Palette.woodMid],
                       startPoint: .leading, endPoint: .trailing)
            .frame(width: 18)
            .overlay(Rectangle().stroke(Color.black.opacity(0.55), lineWidth: 1))
    }

    // MARK: Main panel

    private var panel: some View {
        VStack(spacing: 10) {
            header                // the help display + tab pills live in the header
            Group {
                if mainTab == 0 { synthTabContent } else { performanceTabContent }
            }
            .id(model.version)    // rebuild only the controls on value changes
            Spacer(minLength: 0)
        }
        .padding(EdgeInsets(top: 14, leading: 12, bottom: 12, trailing: 12))
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(
            LinearGradient(colors: [Palette.panelTop, Palette.panelBottom],
                           startPoint: .top, endPoint: .bottom)
        )
    }

    // MARK: Tab layouts

    // Synth: the sound-design engine — oscillators, mixer, envelopes, filter,
    // LFO, cross-mod and the modulation matrix.
    private var synthTabContent: some View {
        VStack(spacing: 20) {
            HStack(alignment: .top, spacing: 12) {
                VStack(spacing: 12) {
                    oscillatorsPanel.frame(height: 392)
                    mixerPanel.frame(height: 120)
                }
                VStack(spacing: 12) {
                    envelopePanel.frame(height: 204)
                    lfoPanel.frame(height: 256)
                }
                VStack(spacing: 12) {
                    filterPanel.frame(height: 240)
                    xModPanel.frame(height: 220)
                }
            }
            .frame(height: 524)
            modMatrixPanel.frame(height: 104)
        }
    }

    // Performance: how the instrument plays — global voicing/expression, the
    // effects chain, arpeggiator and chord trigger.
    private var performanceTabContent: some View {
        HStack(alignment: .top, spacing: 12) {
            // Left 2/3: effects chain on top, then the rest of the panels.
            VStack(spacing: 20) {
                fxPanel.frame(height: 180)
                mainPanel.frame(height: 206)
                HStack(alignment: .top, spacing: 12) {
                    glidePanel.frame(maxWidth: .infinity)
                    arpPanel.frame(maxWidth: .infinity)
                    chordPanel.frame(maxWidth: .infinity)
                }
                .frame(height: 150)
            }
            .frame(maxWidth: .infinity)
            // Right 1/3: searchable, category-filtered patch navigator.
            presetsPanel.frame(width: Self.columnWidth)
        }
        .frame(height: 576)
    }

    private var presetsPanel: some View {
        Panel(title: "Patch Navigator", accent: Palette.filterAccent) {
            PatchNavigator(presets: presets)
        }
    }

    // MARK: Header tab pills

    private var navPills: some View {
        HStack(spacing: 6) {
            navPill("Performance", 1)
            navPill("Synth", 0)
        }
    }

    private func navPill(_ title: String, _ index: Int) -> some View {
        let selected = mainTab == index
        return Button { mainTab = index } label: {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .heavy, design: .rounded))
                .tracking(1.2)
                .foregroundColor(selected ? Palette.textActive : Palette.textOff)
                .padding(.horizontal, 16).padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 7)
                        .fill(selected
                              ? AnyShapeStyle(LinearGradient(
                                    colors: [Palette.btnOnTop, Palette.btnOnBottom],
                                    startPoint: .top, endPoint: .bottom))
                              : AnyShapeStyle(Palette.btnOff)))
                .overlay(RoundedRectangle(cornerRadius: 7)
                    .stroke(selected ? Palette.globalAccent.opacity(0.55)
                                     : Color.black.opacity(0.5), lineWidth: 1))
                .overlay(alignment: .top) {
                    if selected {
                        RoundedRectangle(cornerRadius: 7)
                            .stroke(Color(hex: 0xd8cdb0).opacity(0.14), lineWidth: 1)
                            .padding(0.5)
                            .mask(LinearGradient(colors: [.black, .clear],
                                                 startPoint: .top, endPoint: .center))
                    }
                }
        }
        .buttonStyle(.plain)
    }

    // Hover-help readout, bottom-left.
    private var helpBar: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(Palette.lcd)
                .frame(width: 6, height: 6)
                .shadow(color: Palette.lcd.opacity(0.7), radius: 3)
            Text(help.text.isEmpty ? "HOVER ANY CONTROL TO SEE ITS VALUE AND DESCRIPTION."
                                   : help.text.uppercased())
                .font(.system(size: 12, weight: .regular, design: .monospaced))
                .tracking(0.4)
                .foregroundColor(Palette.lcdMed)
                .lineLimit(1)
                .truncationMode(.tail)   // long text truncates rather than shrinking the font
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 12)
        // Match the middle synthesis column so the LED reads as one grid unit.
        .frame(width: Self.columnWidth, height: 52) // fixed size so text length never shifts the layout
        .background(
            RoundedRectangle(cornerRadius: 5)
                .fill(Palette.lcdBg)
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.black, lineWidth: 1))
        )
    }

    // MARK: Header + preset browser

    // Header mirrors the three-column synthesis grid below: identity on the
    // left column, the hover-help LED on the middle column (so it lines up
    // exactly with the middle synthesis column), patch + meter on the right.
    private var header: some View {
        HStack(alignment: .center, spacing: 12) {
            HStack(spacing: 14) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("HYBRID 8")
                        .font(.system(size: 24, weight: .black, design: .rounded))
                        .tracking(3)
                        .foregroundColor(Palette.titleCream)
                        .fixedSize()
                    Text("VA · WAVETABLE POLYSYNTH")
                        .font(.system(size: 9, weight: .semibold, design: .rounded))
                        .tracking(3)
                        .foregroundColor(Palette.textLabel)
                        .fixedSize()
                }
                navPills          // Synth / Performance tab selector
                Spacer(minLength: 0)
            }
            .frame(width: Self.columnWidth)

            helpBar               // middle column: hover-help readout

            HStack(spacing: 14) {
                Spacer(minLength: 0)
                presetBar
                StereoOutputMeter(left: model.outputLevelL,
                                  right: model.outputLevelR)
            }
            .frame(width: Self.columnWidth)
        }
        .frame(maxWidth: .infinity)
        .padding(.bottom, 8)
    }

    private var presetBar: some View {
        HStack(spacing: 8) {
            Text("PATCH")
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(1.5)
                .foregroundColor(Palette.engraveDim)

            arrow("chevron.left") { presets.prev() }

            Menu {
                ForEach(presets.categoryGroups) { group in
                    Menu(group.name) {
                        ForEach(group.indices, id: \.self) { i in
                            Button(presets.factory[i].name) { presets.applyFactory(i) }
                        }
                    }
                }
                if !presets.userPresets.isEmpty {
                    Menu("User") {
                        ForEach(presets.userPresets.indices, id: \.self) { i in
                            Button(presets.userPresets[i].name) { presets.applyUser(i) }
                        }
                    }
                }
                Divider()
                Button("Init Patch") { presets.initPatch() }
                if presets.currentIsUser {
                    Button("Delete \u{201C}\(presets.currentName)\u{201D}", role: .destructive) {
                        presets.deleteCurrentUser()
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Text(presets.currentName)
                        .font(.system(size: 12, weight: .medium, design: .monospaced))
                        .foregroundColor(Palette.lcd)
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    Image(systemName: "chevron.down")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundColor(Palette.lcd.opacity(0.7))
                }
                .padding(.horizontal, 10).padding(.vertical, 7)
                .frame(width: 180)
                .background(lcdBackground)
            }
            .menuStyle(.borderlessButton)
            .fixedSize()

            arrow("chevron.right") { presets.next() }

            pill("SAVE") { saveName = presets.currentName; showingSave = true }
                .popover(isPresented: $showingSave, arrowEdge: .bottom) { savePanel }
        }
    }

    private var savePanel: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("SAVE PRESET")
                .font(.system(size: 11, weight: .heavy, design: .rounded)).tracking(1)
                .foregroundColor(Palette.engrave)
            TextField("Preset name", text: $saveName)
                .textFieldStyle(.roundedBorder)
                .frame(width: 220)
                .onSubmit { commitSave() }
            HStack {
                Spacer()
                Button("Cancel") { showingSave = false }
                Button("Save") { commitSave() }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(16)
        .frame(width: 260)
    }

    private func commitSave() {
        presets.saveUser(name: saveName)
        showingSave = false
    }

    private var lcdBackground: some View {
        RoundedRectangle(cornerRadius: 5)
            .fill(Color(red: 0.05, green: 0.10, blue: 0.08))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color.black.opacity(0.7), lineWidth: 1))
    }

    private func arrow(_ symbol: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Palette.engrave)
                .frame(width: 26, height: 30)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.white.opacity(0.12), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    private func pill(_ title: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 10, weight: .bold, design: .rounded)).tracking(0.5)
                .foregroundColor(Palette.engrave)
                .padding(.horizontal, 12).frame(height: 30)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.oscAccent.opacity(0.5), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    // MARK: Sections

    // Osc 1 and Osc 2 share one panel, each an accent-labelled section with its
    // own waveform selector.
    private var oscillatorsPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let isPulse1 = w1 == 2              // only Pulse uses the width knob
        let isPulse2 = w2 == 2
        let isWT1 = w1 == 3
        let isWT2 = w2 == 3
        let anyWT = isWT1 || isWT2
        let frame2Linked = (model.param(SynthParamWTFrame2Link)?.value ?? 1) >= 0.5
        let table2Linked = (model.param(SynthParamWTTable2Link)?.value ?? 1) >= 0.5
        let table1Slot = wavetables.selectedSlot(for: SynthParamWavetable)
        let storedTable2Slot = wavetables.selectedSlot(for: SynthParamWTTable2)
        let table2Slot = table2Linked ? table1Slot : storedTable2Slot
        let table1Used = isWT1 || (isWT2 && table2Linked)
        return Panel(title: "Oscillators", accent: Palette.oscAccent) {
            VStack(alignment: .leading, spacing: 10) {
                // --- Osc 1 ---
                oscSectionHeader("Osc 1", accent: Palette.oscAccent,
                                 wave: SynthParamOscWaveform)
                HStack(spacing: 8) {
                    wavetablePickerButton(slot: table1Slot, oscillator: 1)
                        .dimmed(!table1Used)
                        .frame(maxWidth: .infinity)
                    LiveWavetablePreview(store: wavetables,
                                         entry: wavetables.entry(slot: table1Slot),
                                         initialFrame: model.param(SynthParamWTFrame)?.value ?? 0,
                                         frameNotification: .hybrid8WavetableFrameChanged)
                        .dimmed(!table1Used)
                        .frame(maxWidth: .infinity, maxHeight: 42)
                }
                .frame(height: 42)
                HStack(spacing: 0) {
                    Knob("Octave", SynthParamOctave, model,
                         accent: Palette.oscAccent, unit: "", integer: true)
                        .frame(maxWidth: .infinity)
                    Knob("P.Width", SynthParamOscPulseWidth, model, accent: Palette.oscAccent)
                        .dimmed(!isPulse1).frame(maxWidth: .infinity)
                    Knob("Frame", SynthParamWTFrame, model, accent: Palette.wtAccent)
                        .dimmed(!(isWT1 || (isWT2 && frame2Linked)))
                        .frame(maxWidth: .infinity)
                    Knob("Live", SynthParamWTLiveness, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                    Knob("Smooth", SynthParamWTSmooth, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                }
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                    .padding(.vertical, 2)
                // --- Osc 2 ---
                oscSectionHeader("Osc 2", accent: Palette.osc2Accent,
                                 wave: SynthParamOsc2Waveform)
                HStack(spacing: 8) {
                    wavetablePickerButton(slot: table2Slot, oscillator: 2)
                        .dimmed(!isWT2 || table2Linked)
                        .frame(maxWidth: .infinity)
                    LiveWavetablePreview(store: wavetables,
                                         entry: wavetables.entry(slot: table2Slot),
                                         initialFrame: model.param(frame2Linked
                                            ? SynthParamWTFrame
                                            : SynthParamWTFrame2)?.value ?? 0,
                                         frameNotification: frame2Linked
                                            ? .hybrid8WavetableFrameChanged
                                            : .hybrid8WavetableFrame2Changed)
                        .id(frame2Linked)
                        .dimmed(!isWT2)
                        .frame(maxWidth: .infinity, maxHeight: 42)
                    ToggleButton("Table Link", SynthParamWTTable2Link, model,
                                 accent: Palette.wtAccent)
                        .dimmed(!isWT2)
                }
                .frame(height: 42)
                HStack(spacing: 0) {
                    Knob("Octave", SynthParamOsc2Octave, model,
                         accent: Palette.osc2Accent, unit: "", integer: true)
                        .frame(maxWidth: .infinity)
                    Knob("Semi", SynthParamOsc2Semitone, model,
                         accent: Palette.osc2Accent, unit: "", integer: true).frame(maxWidth: .infinity)
                    Knob("Detune", SynthParamOsc2Detune, model, accent: Palette.osc2Accent, unit: "c").frame(maxWidth: .infinity)
                    Knob("P.Width", SynthParamOsc2PulseWidth, model, accent: Palette.osc2Accent)
                        .dimmed(!isPulse2).frame(maxWidth: .infinity)
                    Knob("Frame", SynthParamWTFrame2, model, accent: Palette.wtAccent)
                        .dimmed(!isWT2 || frame2Linked).frame(maxWidth: .infinity)
                    ToggleButton("Frame Link", SynthParamWTFrame2Link, model,
                                 accent: Palette.wtAccent)
                        .dimmed(!isWT2).frame(maxWidth: .infinity)
                }
            }
        }
        .sheet(isPresented: $showingWavetableBrowser) {
            WavetableBrowser(
                store: wavetables, model: model,
                tableParameter: wavetableBrowserOscillator == 2
                    ? SynthParamWTTable2 : SynthParamWavetable,
                frameParameter: wavetableBrowserOscillator == 2
                    ? SynthParamWTFrame2 : SynthParamWTFrame,
                oscillatorName: "OSC \(wavetableBrowserOscillator)",
                isPresented: $showingWavetableBrowser)
                .frame(width: 620, height: 500)
        }
    }

    // Accent chip label + inline waveform selector for one oscillator section.
    private func oscSectionHeader(_ name: String, accent: Color,
                                  wave: SynthParam) -> some View {
        HStack(spacing: 8) {
            // Plain accent-coloured text label (matches the Envelope AMP/FILTER
            // labels) rather than a filled chip.
            Text(name.uppercased())
                .font(.system(size: 10, weight: .heavy, design: .rounded))
                .tracking(1.0)
                .foregroundColor(accent)
            Spacer(minLength: 8)
            InlineWaveSelect(wave, ["Saw", "Squ", "Pls", "WT", "Sin", "Tri"],
                             model, accent: accent)
        }
    }

    private func wavetablePickerButton(slot: Int, oscillator: Int) -> some View {
        let entry = wavetables.entry(slot: slot)
        return Button {
            wavetableBrowserOscillator = oscillator
            showingWavetableBrowser = true
        } label: {
            HStack(spacing: 6) {
                Text(entry.name)
                    .font(.system(size: 12, weight: .semibold, design: .rounded))
                    .foregroundColor(Palette.engrave)
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                Spacer(minLength: 4)
                // Raised drop-arrow well on the right edge.
                Image(systemName: "arrowtriangle.down.fill")
                    .font(.system(size: 9, weight: .bold))
                    .foregroundColor(Palette.engrave.opacity(0.85))
                    .frame(width: 26)
                    .frame(maxHeight: .infinity)
                    .background(
                        Rectangle().fill(Color.white.opacity(0.04))
                            .overlay(alignment: .leading) {
                                Rectangle().fill(Color.black.opacity(0.4)).frame(width: 1)
                            })
            }
            .padding(.leading, 12)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 6).fill(Palette.comboBg))
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(Color.black.opacity(0.55), lineWidth: 1))
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(Color(hex: 0xd8cdb0).opacity(0.06), lineWidth: 1).padding(1))
            .clipShape(RoundedRectangle(cornerRadius: 6))
        }
        .buttonStyle(.plain)
    }

    private var mixerPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        return Panel(title: "Mixer", accent: Palette.mixerAccent) {
            HStack(spacing: 0) {
                Knob("Osc 1", SynthParamOsc1Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Osc 2", SynthParamOsc2Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Sub", SynthParamSubOscLevel, model, accent: Palette.mixerAccent)
                    .dimmed(w1 == 3).frame(maxWidth: .infinity)
                Knob("Noise", SynthParamNoiseLevel, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Stereo", SynthParamStereoSpread, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
            }
        }
    }

    private var filterPanel: some View {
        Panel(title: "Filter", accent: Palette.filterAccent) {
            VStack(alignment: .leading, spacing: 8) {
                HStack(alignment: .top, spacing: 12) {
                    Selector("Mode", SynthParamFilterMode, ["LP", "BP", "HP"],
                             model, accent: Palette.filterAccent)
                    Selector("Slope", SynthParamFilterSlope, ["12dB", "24dB"],
                             model, accent: Palette.filterAccent)
                }
                HStack(spacing: 0) {
                    Knob("Cutoff", SynthParamFilterCutoff, model,
                         accent: Palette.filterAccent, unit: "", log: true).frame(maxWidth: .infinity)
                    Knob("Reso", SynthParamFilterResonance, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("Drive", SynthParamFilterDrive, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("Env", SynthParamFilterEnvAmount, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("KeyTrk", SynthParamFilterKeyTrack, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var lfoPanel: some View {
        Panel(title: "LFO", accent: Palette.lfoAccent) {
            VStack(spacing: 5) {
                lfoRow("LFO 1", wave: SynthParamLFOWaveform,
                       rate: SynthParamLFORate, delay: SynthParamLFODelay,
                       polarity: SynthParamLFO1Polarity,
                       phase: SynthParamLFO1Phase, mode: SynthParamLFO1Mode)
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                lfoRow("LFO 2", wave: SynthParamLFO2Waveform,
                       rate: SynthParamLFO2Rate, delay: SynthParamLFO2Delay,
                       polarity: SynthParamLFO2Polarity,
                       phase: SynthParamLFO2Phase, mode: SynthParamLFO2Mode)
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                lfoRow("LFO 3", wave: SynthParamLFO3Waveform,
                       rate: SynthParamLFO3Rate, delay: SynthParamLFO3Delay,
                       polarity: SynthParamLFO3Polarity,
                       phase: SynthParamLFO3Phase, mode: SynthParamLFO3Mode)
            }
        }
    }

    private func lfoRow(_ name: String, wave: SynthParam, rate: SynthParam,
                        delay: SynthParam, polarity: SynthParam,
                        phase: SynthParam, mode: SynthParam) -> some View {
        HStack(alignment: .top, spacing: 0) {
            Text(name)
                .font(.system(size: 9, weight: .heavy, design: .rounded))
                .foregroundColor(Palette.engrave)
                .frame(width: 34, height: 46, alignment: .leading)
            Spacer(minLength: 6)
            VStack(alignment: .leading, spacing: 3) {
                Text("WAVE")
                    .font(.system(size: 8, weight: .semibold, design: .rounded))
                    .foregroundColor(Palette.engrave)
                Dropdown(wave, ["Sine", "Square", "SawUp", "SawDown", "S&H" ],
                         model, accent: Palette.lfoAccent,
                         helpText: SynthHelp.text(
                            for: AUParameterAddress(wave.rawValue)))
                    .frame(width: 70)
            }
            Spacer(minLength: 6)
            Knob("Rate", rate, model, accent: Palette.lfoAccent,
                 unit: "", log: true)
            Spacer(minLength: 6)
            Knob("Delay", delay, model, accent: Palette.lfoAccent, unit: "s")
            Spacer(minLength: 6)
            VStack(alignment: .leading, spacing: 3) {
                Text("POLARITY")
                    .font(.system(size: 8, weight: .semibold, design: .rounded))
                    .foregroundColor(Palette.engrave)
                Dropdown(polarity, ["Bi", "Uni"], model,
                         accent: Palette.lfoAccent,
                         helpText: SynthHelp.text(
                            for: AUParameterAddress(polarity.rawValue)))
                    .frame(width: 56)
            }
            Spacer(minLength: 6)
            Knob("Phase", phase, model, accent: Palette.lfoAccent)
            Spacer(minLength: 6)
            VStack(alignment: .leading, spacing: 3) {
                Text("MODE")
                    .font(.system(size: 8, weight: .semibold, design: .rounded))
                    .foregroundColor(Palette.engrave)
                Dropdown(mode, ["Loop", "Trig", "1-Shot"], model,
                         accent: Palette.lfoAccent,
                         helpText: SynthHelp.text(
                            for: AUParameterAddress(mode.rawValue)))
                    .frame(width: 76)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    // Amp and Filter envelopes share one panel: two accent-labelled ADSR rows.
    private var envelopePanel: some View {
        Panel(title: "Envelopes", accent: Palette.ampAccent) {
            VStack(spacing: 8) {
                envRow("Amp", accent: Palette.ampAccent,
                       a: SynthParamAmpAttack, d: SynthParamAmpDecay,
                       s: SynthParamAmpSustain, r: SynthParamAmpRelease)
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                envRow("Filter", accent: Palette.filtEnvAccent,
                       a: SynthParamFilterAttack, d: SynthParamFilterDecay,
                       s: SynthParamFilterSustain, r: SynthParamFilterRelease)
            }
        }
    }

    private func envRow(_ name: String, accent: Color, a: SynthParam,
                        d: SynthParam, s: SynthParam, r: SynthParam) -> some View {
        HStack(alignment: .center, spacing: 4) {
            Text(name.uppercased())
                .font(.system(size: 9, weight: .heavy, design: .rounded))
                .tracking(0.5)
                .foregroundColor(accent)
                .frame(width: 38, alignment: .leading)
            Knob("Attack", a, model, accent: accent, timeMapped: true).frame(maxWidth: .infinity)
            Knob("Decay", d, model, accent: accent, timeMapped: true).frame(maxWidth: .infinity)
            Knob("Sustain", s, model, accent: accent).frame(maxWidth: .infinity)
            Knob("Release", r, model, accent: accent, timeMapped: true).frame(maxWidth: .infinity)
        }
    }

    private var mainPanel: some View {
        Panel(title: "Main", accent: Palette.globalAccent) {
            // Uniform 5-column grid so every knob lines up in rows and columns.
            VStack(spacing: 10) {
                HStack(alignment: .top, spacing: 0) {
                    Knob("Voices", SynthParamVoiceCount, model, accent: Palette.globalAccent, integer: true).frame(maxWidth: .infinity)
                    ToggleButton("Unison", SynthParamUnison, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Uni Det", SynthParamUnisonDetune, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Phase", SynthParamOscPhaseSpread, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Analog", SynthParamAnalogAmount, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                }
                HStack(alignment: .top, spacing: 0) {
                    Knob("Master", SynthParamMasterGain, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    // Vel -> Volume stays hardwired; Vel -> Cutoff/Reso/Drive are in the Mod Matrix.
                    Knob("Vel Vol", SynthParamVelToVolume, model, accent: Palette.velAccent).frame(maxWidth: .infinity)
                    Knob("Vibrato", SynthParamLFOToOscFreq, model,
                         accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    VStack(alignment: .center, spacing: 3) {
                        Text("VIB SOURCE")
                            .font(.system(size: 8, weight: .semibold,
                                         design: .rounded))
                            .foregroundColor(Palette.engrave)
                        Dropdown(SynthParamVibratoLFO,
                                 ["LFO 1", "LFO 2", "LFO 3"], model,
                                 accent: Palette.globalAccent,
                                 helpText: SynthHelp.text(for:
                                    AUParameterAddress(
                                        SynthParamVibratoLFO.rawValue)))
                            .frame(width: 70)
                    }
                        .frame(maxWidth: .infinity)
                    VStack(alignment: .center, spacing: 3) {
                        Text("WT RESOLUTION")
                            .font(.system(size: 8, weight: .semibold,
                                         design: .rounded))
                            .foregroundColor(Palette.engrave)
                        Dropdown(SynthParamWTResolution,
                                 ["Clean", "12 Bit", "8 Bit", "Vintage"], model,
                                 accent: Palette.wtAccent,
                                 helpText: SynthHelp.text(for:
                                    AUParameterAddress(
                                        SynthParamWTResolution.rawValue)))
                            .frame(width: 78)
                    }
                        .frame(maxWidth: .infinity)
                }
            }
        }
    }

    // Note-connection & pitch: legato, portamento (glide), glide start and
    // pitch-bend range — grouped apart from the mixer/voicing globals.
    private var glidePanel: some View {
        Panel(title: "Glide", accent: Palette.globalAccent) {
            HStack(alignment: .top, spacing: 0) {
                ToggleButton("Legato", SynthParamLegato, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                Knob("Glide", SynthParamGlideTime, model, accent: Palette.globalAccent, unit: "s").frame(maxWidth: .infinity)
                Knob("Start", SynthParamGlideStart, model, accent: Palette.globalAccent, unit: "", integer: true).frame(maxWidth: .infinity)
                Knob("Bend", SynthParamPitchBendRange, model, accent: Palette.globalAccent, unit: "st").frame(maxWidth: .infinity)
            }
        }
    }

    private var xModPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let anyWT = w1 == 3 || w2 == 3
        return Panel(title: "X-MOD", accent: Palette.osc2Accent) {
            VStack(spacing: 12) {
                HStack(alignment: .top, spacing: 10) {
                    ToggleButton("Sync", SynthParamOsc2Sync, model,
                                 accent: Palette.osc2Accent)
                        .dimmed(anyWT)
                    ToggleButton("TZ", SynthParamOscCrossModTZ, model,
                                 accent: Palette.osc2Accent)
                        .dimmed(anyWT)
                    VStack(alignment: .leading, spacing: 3) {
                        Text("CHARACTER")
                            .font(.system(size: 9, weight: .semibold,
                                         design: .rounded))
                            .tracking(0.6)
                            .foregroundColor(Palette.engrave)
                        Dropdown(SynthParamRingModCharacter,
                                 ["Clean", "Diode"], model,
                                 accent: Palette.osc2Accent,
                                 helpText: SynthHelp.text(for:
                                    AUParameterAddress(
                                        SynthParamRingModCharacter.rawValue)))
                            .frame(width: 78)
                    }
                    Spacer(minLength: 0)
                }
                HStack(alignment: .top, spacing: 0) {
                    Knob("Amount", SynthParamOscCrossMod, model,
                         accent: Palette.osc2Accent)
                        .dimmed(anyWT).frame(maxWidth: .infinity)
                    Knob("Ring", SynthParamRingModLevel, model,
                         accent: Palette.osc2Accent).frame(maxWidth: .infinity)
                    Knob("Env Pitch", SynthParamOsc2PitchEnv, model,
                         accent: Palette.filtEnvAccent)
                        .dimmed(anyWT).frame(maxWidth: .infinity)
                    Knob("LFO Amt", SynthParamLFOToCrossMod, model,
                         accent: Palette.lfoAccent)
                        .dimmed(anyWT).frame(maxWidth: .infinity)
                }
            }
        }
    }

    // Separate Performance-tab panels (previously one tabbed panel).
    private var arpPanel: some View {
        Panel(title: "Arpeggiator", accent: Palette.arpAccent) { arpControls }
    }

    private var chordPanel: some View {
        Panel(title: "Chord Trigger", accent: Palette.chordAccent) { chordControls }
    }

    private var fxPanel: some View {
        Panel(title: "Effects Chain", accent: Palette.fxAccent) { effectsControls }
    }

    private var arpControls: some View {
            HStack(alignment: .top, spacing: 8) {
                ToggleButton("Arp", SynthParamArpOn, model, accent: Palette.arpAccent)
                ToggleButton("Hold", SynthParamArpHold, model, accent: Palette.arpAccent)
                arpDropdown("Mode", SynthParamArpMode,
                            ["Up", "Down", "Up-Dn", "Rnd"], width: 72)
                arpDropdown("Octaves", SynthParamArpOctaves,
                            ["1", "2", "3", "4"], width: 64)
                arpDropdown("Division", SynthParamArpRate,
                            SynthParameters.syncDivisionStrings, width: 74)
                Knob("Gate", SynthParamArpGate, model, accent: Palette.arpAccent)
                Spacer(minLength: 0)
            }
    }

    private var chordControls: some View {
        HStack(alignment: .top, spacing: 14) {
            ToggleButton("Chord", SynthParamChordOn, model,
                         accent: Palette.chordAccent)
            chordDropdown("Type", SynthParamChordType,
                          ["Major", "Minor", "Maj 7", "Min 7", "Dom 7",
                           "Sus 2", "Sus 4", "Dim", "Aug"], width: 104)
            chordDropdown("Inversion", SynthParamChordInversion,
                          ["Root", "First", "Second", "Third"], width: 88)
            Spacer(minLength: 0)
        }
    }

    private func chordDropdown(_ title: String, _ address: SynthParam,
                               _ options: [String], width: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            Dropdown(address, options, model, accent: Palette.chordAccent,
                     helpText: SynthHelp.text(for: AUParameterAddress(address.rawValue)))
                .frame(width: width)
        }
    }

    private func arpDropdown(_ title: String, _ address: SynthParam,
                             _ options: [String], width: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            Dropdown(address, options, model, accent: Palette.arpAccent,
                     helpText: SynthHelp.text(for: AUParameterAddress(address.rawValue)))
                .frame(width: width)
        }
    }

    private var effectsControls: some View {
        HStack(alignment: .top, spacing: 8) {
            VStack(spacing: 2) {
                effectTabButton("Compressor", index: 0)
                effectTabButton("Chorus", index: 1)
                effectTabButton("Delay", index: 2)
                effectTabButton("Reverb", index: 3)
            }
            .frame(width: 62)
            Rectangle().fill(Color.white.opacity(0.08)).frame(width: 1, height: 70)
                .padding(.horizontal, 3)
            Group {
                if effectsTab == 0 {
                    compressorControls
                } else if effectsTab == 1 {
                    chorusControls
                } else if effectsTab == 2 {
                    delayControls
                } else {
                    reverbControls
                }
            }
            Spacer(minLength: 0)
        }
    }

    private func effectTabButton(_ title: String, index: Int) -> some View {
        let selected = effectsTab == index
        return Button {
            effectsTab = index
        } label: {
            Text(title)
                .font(.system(size: 8, weight: .bold, design: .rounded))
                .tracking(0.4)
                .foregroundColor(selected ? Palette.engrave : Palette.engraveDim)
                .frame(width: 58, height: 16)
                .background(
                    RoundedRectangle(cornerRadius: 4)
                        .fill(selected ? Palette.fxAccent.opacity(0.18)
                                       : Color.black.opacity(0.22))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(selected ? Palette.fxAccent.opacity(0.75)
                                         : Color.white.opacity(0.08), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
    }

    private var compressorControls: some View {
        HStack(alignment: .top, spacing: 0) {
            ToggleButton("Comp", SynthParamCompressorOn, model,
                         accent: Palette.fxAccent)
                .frame(maxWidth: .infinity)
            Knob("Threshold", SynthParamCompressorThreshold, model,
                 accent: Palette.fxAccent, unit: "dB").frame(maxWidth: .infinity)
            Knob("Ratio", SynthParamCompressorRatio, model,
                 accent: Palette.fxAccent, unit: ":1").frame(maxWidth: .infinity)
            Knob("Attack", SynthParamCompressorAttack, model,
                 accent: Palette.fxAccent, unit: "s").frame(maxWidth: .infinity)
            Knob("Release", SynthParamCompressorRelease, model,
                 accent: Palette.fxAccent, unit: "s").frame(maxWidth: .infinity)
            Knob("Makeup", SynthParamCompressorMakeup, model,
                 accent: Palette.fxAccent, unit: "dB").frame(maxWidth: .infinity)
            GainReductionMeter(decibels: model.compressorGainReductionDB)
                .padding(.top, 12)
                .frame(maxWidth: .infinity)
        }
    }

    private var chorusControls: some View {
        HStack(alignment: .top, spacing: 0) {
                Knob("Mix", SynthParamChorusMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Cho Sync", SynthParamChorusRate)
                    .frame(maxWidth: .infinity)
                Knob("Depth", SynthParamChorusDepth, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
        }
    }

    private var delayControls: some View {
        HStack(alignment: .top, spacing: 0) {
                Knob("Mix", SynthParamDelayMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Sync", SynthParamDelayTime)
                    .frame(maxWidth: .infinity)
                Knob("Feedback", SynthParamDelayFeedback, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                Knob("Tone", SynthParamDelayTone, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                Knob("PingPong", SynthParamDelayPingPong, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
        }
    }

    private var reverbControls: some View {
        HStack(alignment: .top, spacing: 0) {
            Knob("Mix", SynthParamReverbMix, model,
                 accent: Palette.fxAccent).frame(maxWidth: .infinity)
            Knob("Size", SynthParamReverbSize, model,
                 accent: Palette.fxAccent).frame(maxWidth: .infinity)
            Knob("Decay", SynthParamReverbDecay, model,
                 accent: Palette.fxAccent, unit: "s").frame(maxWidth: .infinity)
            Knob("Tone", SynthParamReverbTone, model,
                 accent: Palette.fxAccent).frame(maxWidth: .infinity)
            Knob("Pre Delay", SynthParamReverbPreDelay, model,
                 accent: Palette.fxAccent, unit: "s").frame(maxWidth: .infinity)
        }
    }

    private func fxDivision(_ title: String, _ address: SynthParam) -> some View {
        VStack(alignment: .center, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.5)
                .foregroundColor(Palette.engrave)
                .lineLimit(1)
            Dropdown(address, SynthParameters.syncDivisionStrings, model,
                     accent: Palette.fxAccent,
                     helpText: SynthHelp.text(for: AUParameterAddress(address.rawValue)))
                .frame(width: 58)
        }
    }

    // MARK: Mod matrix

    static let modSources = ["Src", "LFO 1", "LFO 2", "Filt Env", "Amp Env",
                             "Velocity", "Key Trk", "Mod Whl", "Aftertch",
                             "Random", "LFO 3"]
    static let modDests   = ["Dest", "Osc Pitch", "Osc2 Pitch", "Pulse W", "Cutoff",
                             "Reso", "Drive", "WT Frame", "WT Live", "X-Mod", "Amp",
                             "Osc1 Pitch", "Osc1 Level", "Osc2 Level", "Noise",
                             "Voice Pan", "Filt Slope", "Filt Mode", "Osc1 PW",
                             "Osc2 PW", "WT Smooth", "WT Frame2", "Sub Level",
                             "Ring Level"]

    private func modSlot(_ n: Int, _ s: SynthParam, _ d: SynthParam, _ a: SynthParam) -> some View {
        HStack(spacing: 5) {
            Dropdown(s, Self.modSources, model, accent: Palette.lfoAccent,
                     helpText: "Modulation source feeding slot \(n).", dimsNone: true)
                .frame(width: 95)
            Image(systemName: "arrow.right")
                .font(.system(size: 7, weight: .bold))
                .foregroundColor(Palette.engraveDim)
            Dropdown(d, Self.modDests, model, accent: Palette.filterAccent,
                     helpText: "Destination that slot \(n) modulates.", dimsNone: true)
                .frame(width: 93)
            AmountSlider(a, model, accent: Palette.lfoAccent,
                         helpText: "Bipolar modulation amount for slot \(n).")
                .frame(width: 170)
        }
    }

    private var modMatrixPanel: some View {
        Panel(title: "Mod Matrix", accent: Palette.lfoAccent) {
            VStack(spacing: 9) {
                HStack(spacing: 18) {
                    modSlot(1, SynthParamMod1Source, SynthParamMod1Dest, SynthParamMod1Amount)
                    modSlot(2, SynthParamMod2Source, SynthParamMod2Dest, SynthParamMod2Amount)
                    modSlot(3, SynthParamMod3Source, SynthParamMod3Dest, SynthParamMod3Amount)
                }
                HStack(spacing: 18) {
                    modSlot(4, SynthParamMod4Source, SynthParamMod4Dest, SynthParamMod4Amount)
                    modSlot(5, SynthParamMod5Source, SynthParamMod5Dest, SynthParamMod5Amount)
                    modSlot(6, SynthParamMod6Source, SynthParamMod6Dest, SynthParamMod6Amount)
                }
            }
        }
    }
}

private struct LiveWavetablePreview: View {
    @ObservedObject var store: WavetableStore
    let entry: WavetableEntry
    let frameNotification: Notification.Name
    @State private var frame: Float

    init(store: WavetableStore, entry: WavetableEntry, initialFrame: Float,
         frameNotification: Notification.Name) {
        self.store = store
        self.entry = entry
        self.frameNotification = frameNotification
        _frame = State(initialValue: initialFrame)
    }

    var body: some View {
        let frameIndex = min(entry.frameCount - 1,
                             max(0, Int((frame
                                 * Float(entry.frameCount - 1)).rounded())))
        WavetableWaveform(
            samples: store.preview(for: entry, normalizedFrame: frame),
            accent: Palette.lcd)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .overlay(alignment: .bottomTrailing) {
                Text("\(frameIndex + 1)/\(entry.frameCount)")
                    .font(.system(size: 9, weight: .medium, design: .monospaced))
                    .foregroundColor(Palette.lcdMed)
                    .padding(.horizontal, 7).padding(.vertical, 5)
            }
        .onReceive(NotificationCenter.default.publisher(
            for: frameNotification)) { notification in
                if let value = notification.object as? Float {
                    frame = value
                }
            }
    }
}

private struct WavetableBrowser: View {
    @ObservedObject var store: WavetableStore
    @ObservedObject var model: ParameterModel
    let tableParameter: SynthParam
    let frameParameter: SynthParam
    let oscillatorName: String
    @Binding var isPresented: Bool
    @State private var search = ""
    @State private var pendingDelete: WavetableEntry?
    @State private var showingFileImporter = false

    private var filtered: [WavetableEntry] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        if query.isEmpty { return store.entries }
        return store.entries.filter {
            $0.name.localizedCaseInsensitiveContains(query)
        }
    }

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                Text("\(oscillatorName) WAVETABLE LIBRARY")
                    .font(.system(size: 14, weight: .heavy, design: .rounded))
                    .tracking(1.2)
                    .foregroundColor(Palette.engrave)
                Spacer()
                Button {
                    showingFileImporter = true
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "plus")
                        Text(store.isImporting ? "IMPORTING…" : "IMPORT WAV…")
                    }
                    .font(.system(size: 10, weight: .bold, design: .rounded))
                    .foregroundColor(Palette.panelBottom)
                    .padding(.horizontal, 11)
                    .frame(height: 28)
                    .background(RoundedRectangle(cornerRadius: 5)
                        .fill(Palette.wtAccent))
                }
                .buttonStyle(.plain)
                    .disabled(store.isImporting)
                Button {
                    isPresented = false
                } label: {
                    Text("DONE")
                        .font(.system(size: 10, weight: .bold, design: .rounded))
                        .foregroundColor(Palette.engrave)
                        .padding(.horizontal, 10)
                        .frame(height: 28)
                        .background(RoundedRectangle(cornerRadius: 5)
                            .fill(Color.white.opacity(0.08)))
                        .overlay(RoundedRectangle(cornerRadius: 5)
                            .stroke(Color.white.opacity(0.12), lineWidth: 1))
                }
                .buttonStyle(.plain)
            }
            HStack {
                Image(systemName: "magnifyingglass")
                    .foregroundColor(Palette.engraveDim)
                TextField("Search tables", text: $search)
                    .textFieldStyle(.plain)
            }
            .padding(.horizontal, 9)
            .frame(height: 30)
            .background(RoundedRectangle(cornerRadius: 5)
                .fill(Color.black.opacity(0.25)))

            ScrollView {
                LazyVStack(spacing: 5) {
                    ForEach(filtered) { entry in
                        browserRow(entry)
                    }
                }
            }
        }
        .padding(16)
        .background(Palette.panelBottom)
        .fileImporter(isPresented: $showingFileImporter,
                      allowedContentTypes: [.wav, .aiff, .audio],
                      allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first {
                    store.importAudioFile(at: url, selectFor: tableParameter)
                }
            case .failure(let error):
                store.errorMessage = error.localizedDescription
            }
        }
        .alert("Delete Wavetable?", isPresented: Binding(
            get: { pendingDelete != nil },
            set: { if !$0 { pendingDelete = nil } })) {
                Button("Cancel", role: .cancel) { pendingDelete = nil }
                Button("Delete", role: .destructive) {
                    if let entry = pendingDelete { store.delete(entry) }
                    pendingDelete = nil
                }
            } message: {
                Text("The imported audio file will be removed from Hybrid 8’s library.")
            }
        .alert("Import Error", isPresented: Binding(
            get: { store.errorMessage != nil },
            set: { if !$0 { store.errorMessage = nil } })) {
                Button("OK") { store.errorMessage = nil }
            } message: {
                Text(store.errorMessage ?? "")
            }
    }

    private func browserRow(_ entry: WavetableEntry) -> some View {
        let selected = entry.slot == store.selectedSlot(for: tableParameter)
        let frame = model.param(frameParameter)?.value ?? 0
        return Button {
            store.select(entry, for: tableParameter)
            isPresented = false
        } label: {
            HStack(spacing: 12) {
                WavetableWaveform(
                    samples: store.preview(for: entry, normalizedFrame: frame),
                    accent: selected ? Palette.wtAccent : Palette.engraveDim)
                    .frame(width: 150, height: 42)
                VStack(alignment: .leading, spacing: 3) {
                    Text(entry.name)
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .foregroundColor(Palette.engrave)
                    Text("\(entry.isFactory ? "FACTORY" : "USER")  ·  \(entry.frameCount) FRAMES  ·  \(entry.frameLength) SAMPLES")
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(Palette.engraveDim)
                }
                Spacer()
                if selected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(Palette.wtAccent)
                }
                if !entry.isFactory {
                    Button {
                        pendingDelete = entry
                    } label: {
                        Image(systemName: "trash")
                            .foregroundColor(Palette.engraveDim)
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 10)
            .frame(height: 58)
            .background(RoundedRectangle(cornerRadius: 6)
                .fill(selected ? Palette.wtAccent.opacity(0.13)
                               : Palette.sectionBG))
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(selected ? Palette.wtAccent.opacity(0.65)
                                 : Color.white.opacity(0.06), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Performance patch navigator

private struct PatchNavigator: View {
    @ObservedObject var presets: PresetStore
    @State private var search = ""
    @State private var selectedCategory = "All"
    @FocusState private var searchFocused: Bool

    private struct Patch: Identifiable {
        enum Source {
            case factory(Int)
            case user(Int)
        }

        let id: String
        let name: String
        let category: String
        let source: Source
    }

    private var categories: [String] {
        ["All"] + presets.categoryGroups.map(\.name)
            + (presets.userPresets.isEmpty ? [] : ["User"])
    }

    private var patches: [Patch] {
        let factory = presets.factory.indices.map { index in
            Patch(id: "factory-\(index)",
                  name: presets.factory[index].name,
                  category: FactoryPresets.category(for: presets.factory[index].name),
                  source: .factory(index))
        }
        let user = presets.userPresets.indices.map { index in
            Patch(id: "user-\(index)",
                  name: presets.userPresets[index].name,
                  category: "User",
                  source: .user(index))
        }
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        return (factory + user).filter { patch in
            let inCategory = selectedCategory == "All"
                || patch.category == selectedCategory
            let matchesSearch = query.isEmpty
                || patch.name.localizedStandardContains(query)
                || patch.category.localizedStandardContains(query)
            return inCategory && matchesSearch
        }
    }

    var body: some View {
        VStack(spacing: 10) {
            searchField
            HStack(alignment: .top, spacing: 10) {
                categoryRail
                    .frame(width: 112)
                patchList
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onChange(of: presets.userPresets.count) { _ in
            if selectedCategory == "User" && presets.userPresets.isEmpty {
                selectedCategory = "All"
            }
        }
        .onDisappear { resignSearchFocus() }
    }

    private var searchField: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(Palette.lcdDim)
            TextField("Search patches", text: $search)
                .textFieldStyle(.plain)
                .font(.system(size: 11, weight: .medium, design: .monospaced))
                .foregroundColor(Palette.lcd)
                .frame(width: 200)
                .layoutPriority(1)
                .focused($searchFocused)
                .onSubmit { resignSearchFocus() }
                .onExitCommand { resignSearchFocus() }
                .help("Search patches · Return or Escape resumes Musical Typing")
            if !search.isEmpty {
                Button { search = "" } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 11))
                        .foregroundColor(Palette.lcdDim)
                }
                .buttonStyle(.plain)
                .help("Clear patch search")
            }
        }
        .padding(.horizontal, 10)
        .frame(height: 34)
        .background(lcdBackground)
    }

    private var categoryRail: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("CATEGORY")
                .font(.system(size: 8, weight: .bold, design: .rounded))
                .tracking(1.2)
                .foregroundColor(Palette.engraveDim)
                .padding(.horizontal, 8)

            ScrollView(showsIndicators: false) {
                LazyVStack(spacing: 3) {
                    ForEach(categories, id: \.self) { category in
                        categoryButton(category)
                    }
                }
            }
        }
    }

    private func categoryButton(_ category: String) -> some View {
        let selected = selectedCategory == category
        return Button {
            resignSearchFocus()
            selectedCategory = category
        } label: {
            HStack(spacing: 6) {
                RoundedRectangle(cornerRadius: 1)
                    .fill(selected ? Palette.filterAccent : Palette.ledOff)
                    .frame(width: 3, height: 16)
                    .shadow(color: selected ? Palette.filterAccent.opacity(0.65) : .clear,
                            radius: 2)
                Text(category.uppercased())
                    .font(.system(size: 9, weight: selected ? .bold : .medium,
                                  design: .rounded))
                    .tracking(0.5)
                    .foregroundColor(selected ? Palette.textActive : Palette.textOff)
                    .lineLimit(1)
                Spacer(minLength: 0)
                Text("\(count(for: category))")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .foregroundColor(selected ? Palette.filterAccent : Palette.engraveDim)
            }
            .padding(.horizontal, 7)
            .frame(height: 27)
            .background(RoundedRectangle(cornerRadius: 4)
                .fill(selected ? Palette.filterAccent.opacity(0.12) : Color.clear))
            .overlay(RoundedRectangle(cornerRadius: 4)
                .stroke(selected ? Palette.filterAccent.opacity(0.35) : Color.clear,
                        lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    private var patchList: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(selectedCategory.uppercased())
                Spacer()
                Text("\(patches.count) PATCH\(patches.count == 1 ? "" : "ES")")
            }
            .font(.system(size: 8, weight: .bold, design: .rounded))
            .tracking(1.0)
            .foregroundColor(Palette.engraveDim)
            .padding(.horizontal, 3)

            if patches.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "waveform.slash")
                        .font(.system(size: 20))
                    Text("NO PATCHES FOUND")
                        .font(.system(size: 9, weight: .bold, design: .rounded))
                        .tracking(1)
                }
                .foregroundColor(Palette.engraveDim)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView(showsIndicators: false) {
                    LazyVStack(spacing: 5) {
                        ForEach(patches) { patch in
                            patchRow(patch)
                        }
                    }
                }
            }
        }
    }

    private func patchRow(_ patch: Patch) -> some View {
        let selected = isSelected(patch)
        return Button {
            resignSearchFocus()
            apply(patch)
        } label: {
            HStack(spacing: 9) {
                Image(systemName: selected ? "checkmark.circle.fill" : "waveform")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(selected ? Palette.filterAccent : Palette.engraveDim)
                    .frame(width: 16)
                VStack(alignment: .leading, spacing: 2) {
                    Text(patch.name)
                        .font(.system(size: 11, weight: selected ? .bold : .medium,
                                      design: .rounded))
                        .foregroundColor(selected ? Palette.textActive : Palette.engrave)
                        .lineLimit(1)
                    Text(patch.category.uppercased())
                        .font(.system(size: 7.5, weight: .semibold, design: .monospaced))
                        .tracking(0.8)
                        .foregroundColor(selected ? Palette.filterAccent : Palette.engraveDim)
                }
                Spacer(minLength: 0)
                Image(systemName: "chevron.right")
                    .font(.system(size: 8, weight: .bold))
                    .foregroundColor(selected ? Palette.filterAccent.opacity(0.8)
                                              : Palette.engraveDim.opacity(0.45))
            }
            .padding(.horizontal, 10)
            .frame(height: 43)
            .background(RoundedRectangle(cornerRadius: 5)
                .fill(selected ? Palette.filterAccent.opacity(0.13)
                               : Color.black.opacity(0.17)))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(selected ? Palette.filterAccent.opacity(0.6)
                                 : Color.white.opacity(0.055), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .help("Load \(patch.name)")
    }

    private var lcdBackground: some View {
        RoundedRectangle(cornerRadius: 5)
            .fill(Palette.lcdBg)
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color.black.opacity(0.7), lineWidth: 1))
    }

    private func count(for category: String) -> Int {
        if category == "All" {
            return presets.factory.count + presets.userPresets.count
        }
        if category == "User" {
            return presets.userPresets.count
        }
        return presets.categoryGroups.first(where: { $0.name == category })?.indices.count ?? 0
    }

    private func isSelected(_ patch: Patch) -> Bool {
        switch patch.source {
        case .factory(let index):
            return !presets.currentIsUser && presets.currentIndex == index
        case .user(let index):
            return presets.currentIsUser
                && presets.currentIndex == presets.factory.count + index
        }
    }

    private func apply(_ patch: Patch) {
        switch patch.source {
        case .factory(let index): presets.applyFactory(index)
        case .user(let index): presets.applyUser(index)
        }
    }

    private func resignSearchFocus() {
        searchFocused = false
        #if canImport(AppKit)
        // AUv3 editors can run in a remote view service. FocusState updates the
        // SwiftUI model, but AppKit's shared field editor can remain first
        // responder there and continue intercepting the host's Musical Typing.
        DispatchQueue.main.async {
            NSApp.keyWindow?.makeFirstResponder(nil)
            NSApp.sendAction(#selector(NSResponder.resignFirstResponder),
                             to: nil, from: nil)
            // Hand the keyboard back to the musical-typing responder.
            NotificationCenter.default.post(
                name: Notification.Name("Hybrid8SearchDidResign"), object: nil)
        }
        #endif
    }
}
