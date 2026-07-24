//
//  SynthView.swift
//  The instrument fascia: wood end-cheeks, a brushed charcoal panel, a branded
//  header with a preset browser, and six accent-coloured sections of knobs and
//  lit selector buttons. Aesthetic: mid-'80s Yamaha × Oberheim.
//

import SwiftUI
import AudioToolbox

struct SynthView: View {
    // A wide hardware-panel aspect ratio gives the three synthesis columns and
    // the arp/FX row enough horizontal room. The whole fascia still scales
    // uniformly when a host supplies a smaller editor window.
    private static let designSize = CGSize(width: 1360, height: 890)

    @ObservedObject var model: ParameterModel
    @StateObject private var presets: PresetStore
    @StateObject private var help = HelpModel()
    @State private var showingSave = false
    @State private var saveName = ""
    @State private var lowerTab = 0
    @State private var effectsTab = 0

    init(model: ParameterModel) {
        _model = ObservedObject(wrappedValue: model)
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
        LinearGradient(colors: [Palette.woodLight, Palette.woodDark, Palette.woodLight],
                       startPoint: .leading, endPoint: .trailing)
            .frame(width: 18)
            .overlay(Rectangle().stroke(Color.black.opacity(0.5), lineWidth: 1))
    }

    // MARK: Main panel

    private var panel: some View {
        VStack(spacing: 10) {
            header
            helpBar               // kept near the top so it stays on-screen as the panel grows
            VStack(spacing: 12) {
                HStack(alignment: .top, spacing: 12) {
                    // Column 1 — Oscillators + Mixer
                    VStack(spacing: 10) {
                        oscillatorPanel.frame(height: 186)
                        osc2Panel.frame(height: 192)
                        mixerPanel.frame(height: 118)
                        Spacer(minLength: 0)
                    }
                    // Column 2 — Envelopes + LFO
                    VStack(spacing: 10) {
                        ampEnvPanel.frame(height: 118)
                        filterEnvPanel.frame(height: 118)
                        lfoPanel.frame(height: 260)
                        Spacer(minLength: 0)
                    }
                    // Column 3 — Filter + Global (Vel Vol lives in Global)
                    VStack(spacing: 10) {
                        filterPanel.frame(height: 226)
                        globalPanel.frame(height: 280)
                        Spacer(minLength: 0)
                    }
                }
                .frame(height: 516)
                HStack(alignment: .top, spacing: 12) {
                    xModPanel.frame(width: 420)
                    arpEffectsPanel.frame(maxWidth: .infinity)
                }
                .frame(height: 112)
                modMatrixPanel.frame(height: 104)
            }
            .id(model.version) // rebuild only the controls on value changes
        }
        .padding(EdgeInsets(top: 22, leading: 16, bottom: 14, trailing: 16))
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(
            LinearGradient(colors: [Palette.panelTop, Palette.panelBottom],
                           startPoint: .top, endPoint: .bottom)
        )
    }

    // Hover-help readout, bottom-left.
    private var helpBar: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(help.text.isEmpty ? Palette.engraveDim.opacity(0.5) : Palette.lcd)
                .frame(width: 6, height: 6)
            Text(help.text.isEmpty ? "Hover a control for a description." : help.text)
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(help.text.isEmpty ? Palette.engraveDim : Palette.engrave)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 12).padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color.black.opacity(0.28))
                .overlay(RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.white.opacity(0.06), lineWidth: 1))
        )
    }

    // MARK: Header + preset browser

    private var header: some View {
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: 2) {
                Text("HYBRID 8")
                    .font(.system(size: 26, weight: .black, design: .rounded))
                    .tracking(2)
                    .foregroundColor(Palette.engrave)
                    .fixedSize()
                Text("ANALOG · WAVETABLE POLYSYNTH")
                    .font(.system(size: 9, weight: .semibold, design: .rounded))
                    .tracking(3)
                    .foregroundColor(Palette.engraveDim)
                    .fixedSize()
            }
            HStack(spacing: 3) {
                ForEach([Palette.lfoAccent, Palette.globalAccent,
                         Palette.filterAccent, Palette.ampAccent], id: \.self) { c in
                    RoundedRectangle(cornerRadius: 1.5).fill(c).frame(width: 16, height: 6)
                }
            }
            Spacer()
            presetBar
        }
        .padding(.bottom, 6)
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

    private var oscillatorPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let isWT1 = w1 == 3
        let anyWT = w1 == 3 || w2 == 3      // WT table/frame/live are shared
        return Panel(title: "Osc 1", accent: Palette.oscAccent, trailing: {
            InlineWaveSelect(SynthParamOscWaveform, ["Saw", "Squ", "Pls", "WT"],
                             model, accent: Palette.oscAccent)
        }) {
            VStack(alignment: .leading, spacing: 8) {
                Selector("WT Table", SynthParamWavetable, ["Harm", "FM", "Choir", "Metal"],
                         model, accent: Palette.wtAccent)
                    .dimmed(!anyWT)
                HStack(spacing: 0) {
                    Knob("Octave", SynthParamOctave, model,
                         accent: Palette.oscAccent, unit: "", integer: true)
                        .frame(maxWidth: .infinity)
                    Knob("P.Width", SynthParamOscPulseWidth, model, accent: Palette.oscAccent)
                        .dimmed(isWT1).frame(maxWidth: .infinity)
                    Knob("Frame", SynthParamWTFrame, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                    Knob("Live", SynthParamWTLiveness, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var mixerPanel: some View {
        Panel(title: "Mixer", accent: Palette.mixerAccent) {
            HStack(spacing: 0) {
                Knob("Osc 1", SynthParamOsc1Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Osc 2", SynthParamOsc2Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Noise", SynthParamNoiseLevel, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
            }
        }
    }

    private var osc2Panel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let isWT2 = w2 == 3
        let anyWT = w1 == 3 || w2 == 3      // cross-mod & sync need both analog
        return Panel(title: "Osc 2", accent: Palette.osc2Accent, trailing: {
            InlineWaveSelect(SynthParamOsc2Waveform, ["Saw", "Squ", "Pls", "WT"],
                             model, accent: Palette.osc2Accent)
        }) {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 0) {
                    Knob("Octave", SynthParamOsc2Octave, model,
                         accent: Palette.osc2Accent, unit: "", integer: true)
                        .frame(maxWidth: .infinity)
                    Knob("Semi", SynthParamOsc2Semitone, model,
                         accent: Palette.osc2Accent, unit: "", integer: true).frame(maxWidth: .infinity)
                    Knob("Detune", SynthParamOsc2Detune, model, accent: Palette.osc2Accent, unit: "c").frame(maxWidth: .infinity)
                    Knob("P.Width", SynthParamOsc2PulseWidth, model, accent: Palette.osc2Accent)
                        .dimmed(isWT2).frame(maxWidth: .infinity)
                }
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
                }
                HStack(spacing: 0) {
                    Knob("Env", SynthParamFilterEnvAmount, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("KeyTrk", SynthParamFilterKeyTrack, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Color.clear.frame(maxWidth: .infinity)   // keep column alignment
                }
            }
        }
    }

    private var lfoPanel: some View {
        Panel(title: "LFO", accent: Palette.lfoAccent) {
            VStack(alignment: .leading, spacing: 10) {
                // LFO 1 — its Pitch route (vibrato) is hardwired; every other
                // LFO 1 destination is assigned in the Mod Matrix below.
                HStack(alignment: .top, spacing: 10) {
                    Selector("LFO 1 Wave", SynthParamLFOWaveform, ["Sin", "Squ", "Saw"],
                             model, accent: Palette.lfoAccent)
                    ToggleButton("Key Trig", SynthParamLFOKeyTrigger, model, accent: Palette.lfoAccent)
                }
                HStack(spacing: 0) {
                    Knob("Rate", SynthParamLFORate, model,
                         accent: Palette.lfoAccent, unit: "", log: true).frame(maxWidth: .infinity)
                    Knob("Delay", SynthParamLFODelay, model, accent: Palette.lfoAccent, unit: "s").frame(maxWidth: .infinity)
                    Knob("Vibrato", SynthParamLFOToOscFreq, model, accent: Palette.lfoAccent).frame(maxWidth: .infinity)
                }
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                // LFO 2 — a matrix-only source.
                HStack(alignment: .top, spacing: 10) {
                    Selector("LFO 2 Wave", SynthParamLFO2Waveform, ["Sin", "Squ", "Saw"],
                             model, accent: Palette.lfoAccent)
                    Knob("LFO 2 Rate", SynthParamLFO2Rate, model,
                         accent: Palette.lfoAccent, unit: "", log: true)
                }
            }
        }
    }

    private var ampEnvPanel: some View {
        Panel(title: "Amp Envelope", accent: Palette.ampAccent) {
            HStack(spacing: 0) {
                Knob("Attack", SynthParamAmpAttack, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Decay", SynthParamAmpDecay, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Sustain", SynthParamAmpSustain, model, accent: Palette.ampAccent).frame(maxWidth: .infinity)
                Knob("Release", SynthParamAmpRelease, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
            }
        }
    }

    private var filterEnvPanel: some View {
        Panel(title: "Filter Envelope", accent: Palette.filtEnvAccent) {
            HStack(spacing: 0) {
                Knob("Attack", SynthParamFilterAttack, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Decay", SynthParamFilterDecay, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Sustain", SynthParamFilterSustain, model, accent: Palette.filtEnvAccent).frame(maxWidth: .infinity)
                Knob("Release", SynthParamFilterRelease, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
            }
        }
    }

    private var globalPanel: some View {
        Panel(title: "Global", accent: Palette.globalAccent, trailing: {
            StereoOutputMeter(left: model.outputLevelL,
                              right: model.outputLevelR)
        }) {
            VStack(spacing: 8) {
                HStack(alignment: .top, spacing: 0) {
                    Knob("Voices", SynthParamVoiceCount, model, accent: Palette.globalAccent, integer: true).frame(maxWidth: .infinity)
                    ToggleButton("Legato", SynthParamLegato, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Glide", SynthParamGlideTime, model, accent: Palette.globalAccent, unit: "s").frame(maxWidth: .infinity)
                    Knob("Start", SynthParamGlideStart, model, accent: Palette.globalAccent, unit: "", integer: true).frame(maxWidth: .infinity)
                }
                HStack(spacing: 0) {
                    Knob("Stereo", SynthParamStereoSpread, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Analog", SynthParamAnalogAmount, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Master", SynthParamMasterGain, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Bend", SynthParamPitchBendRange, model, accent: Palette.globalAccent, unit: "st").frame(maxWidth: .infinity)
                }
                HStack(spacing: 0) {
                    // Vel -> Volume stays hardwired; Vel -> Cutoff/Reso/Drive
                    // are assigned in the Mod Matrix.
                    Knob("Phase", SynthParamOscPhaseSpread, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Vel Vol", SynthParamVelToVolume, model, accent: Palette.velAccent).frame(maxWidth: .infinity)
                    ToggleButton("Unison", SynthParamUnison, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Uni Det", SynthParamUnisonDetune, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var xModPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let anyWT = w1 == 3 || w2 == 3
        return Panel(title: "X-MOD", accent: Palette.osc2Accent) {
            HStack(alignment: .top, spacing: 8) {
                ToggleButton("Sync", SynthParamOsc2Sync, model,
                             accent: Palette.osc2Accent)
                    .dimmed(anyWT)
                ToggleButton("TZ", SynthParamOscCrossModTZ, model,
                             accent: Palette.osc2Accent)
                    .dimmed(anyWT)
                Knob("Amount", SynthParamOscCrossMod, model,
                     accent: Palette.osc2Accent)
                    .dimmed(anyWT).frame(maxWidth: .infinity)
                Knob("Env Pitch", SynthParamOsc2PitchEnv, model,
                     accent: Palette.filtEnvAccent)
                    .dimmed(anyWT).frame(maxWidth: .infinity)
                Knob("LFO Amt", SynthParamLFOToCrossMod, model,
                     accent: Palette.lfoAccent)
                    .dimmed(anyWT).frame(maxWidth: .infinity)
            }
        }
    }

    private var arpEffectsPanel: some View {
        Panel(title: lowerPanelTitle,
              accent: lowerTab == 0 ? Palette.arpAccent
                                    : (lowerTab == 1 ? Palette.chordAccent
                                                     : Palette.fxAccent),
              trailing: {
            HStack(spacing: 4) {
                lowerTabButton("ARP", index: 0, accent: Palette.arpAccent)
                lowerTabButton("CHORD", index: 1, accent: Palette.chordAccent)
                lowerTabButton("EFFECTS", index: 2, accent: Palette.fxAccent)
            }
        }) {
            if lowerTab == 0 {
                arpControls
            } else if lowerTab == 1 {
                chordControls
            } else {
                effectsControls
            }
        }
    }

    private var lowerPanelTitle: String {
        if lowerTab == 0 { return "Arpeggiator" }
        if lowerTab == 1 { return "Chord Trigger  ·  Before Arpeggiator" }
        return "Effects  ·  Compressor → Chorus → Delay → Reverb"
    }

    private func lowerTabButton(_ title: String, index: Int,
                                accent: Color) -> some View {
        let selected = lowerTab == index
        return Button {
            lowerTab = index
        } label: {
            Text(title)
                .font(.system(size: 9, weight: .bold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(selected ? Palette.engrave : Palette.engraveDim)
                .padding(.horizontal, 9).padding(.vertical, 4)
                .background(
                    RoundedRectangle(cornerRadius: 4)
                        .fill(selected ? accent.opacity(0.18)
                                       : Color.black.opacity(0.22))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(selected ? accent.opacity(0.75)
                                         : Color.white.opacity(0.08), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
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
                            SynthParameters.syncDivisionStrings, width: 64)
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
                effectTabButton("COMP", index: 0)
                effectTabButton("CHORUS", index: 1)
                effectTabButton("DELAY", index: 2)
                effectTabButton("REVERB", index: 3)
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
                Knob("Cho Mix", SynthParamChorusMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Cho Sync", SynthParamChorusRate)
                    .frame(maxWidth: .infinity)
                Knob("Cho Depth", SynthParamChorusDepth, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
        }
    }

    private var delayControls: some View {
        HStack(alignment: .top, spacing: 0) {
                Knob("Dly Mix", SynthParamDelayMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Dly Sync", SynthParamDelayTime)
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
            Knob("Rev Mix", SynthParamReverbMix, model,
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
                             "Velocity", "Key Trk", "Mod Whl", "Aftertch", "Random"]
    static let modDests   = ["Dest", "Osc Pitch", "Osc2 Pitch", "Pulse W", "Cutoff",
                             "Reso", "Drive", "WT Frame", "WT Live", "X-Mod", "Amp",
                             "Osc1 Pitch", "Osc1 Level", "Osc2 Level", "Noise",
                             "Voice Pan", "Filt Slope", "Filt Mode", "Osc1 PW",
                             "Osc2 PW"]

    private func modSlot(_ n: Int, _ s: SynthParam, _ d: SynthParam, _ a: SynthParam) -> some View {
        HStack(spacing: 5) {
            Dropdown(s, Self.modSources, model, accent: Palette.lfoAccent,
                     helpText: "Modulation source feeding slot \(n).")
                .frame(width: 95)
            Image(systemName: "arrow.right")
                .font(.system(size: 7, weight: .bold))
                .foregroundColor(Palette.engraveDim)
            Dropdown(d, Self.modDests, model, accent: Palette.filterAccent,
                     helpText: "Destination that slot \(n) modulates.")
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
