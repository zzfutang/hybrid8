//
//  SynthTheme.swift
//  Visual language for the panel — a mix of mid-'80s Yamaha (dark charcoal
//  fascia, cream engraved legends, blue-green LCD readout, magenta/blue button
//  groups) and Oberheim (matte black metal, wood end-cheeks, colourful knob
//  caps with bright value arcs). Plus the reusable Knob and Selector controls.
//

import SwiftUI
import AudioToolbox
import Foundation
#if canImport(AppKit)
import AppKit
#endif

extension Color {
    init(hex: UInt32) {
        self.init(.sRGB,
                  red:   Double((hex >> 16) & 0xff) / 255.0,
                  green: Double((hex >> 8)  & 0xff) / 255.0,
                  blue:  Double(hex & 0xff) / 255.0,
                  opacity: 1.0)
    }
    // Perceived luminance — used to pick dark vs. cream text on an accent chip.
    var isLight: Bool {
        // Approximate from sRGB components via NSColor.
        #if canImport(AppKit)
        let ns = NSColor(self).usingColorSpace(.sRGB) ?? .white
        return (0.299 * ns.redComponent + 0.587 * ns.greenComponent + 0.114 * ns.blueComponent) > 0.6
        #else
        return true
        #endif
    }
}

// Warm brushed-brass / charcoal palette from the "Hybrid 8 Redesign".
enum Palette {
    // Chassis + panels
    static let panelTop      = Color(hex: 0x211e17)   // outer fascia top
    static let panelBottom   = Color(hex: 0x17150f)   // outer fascia bottom
    static let sectionTop    = Color(hex: 0x27231a)   // section panel top
    static let sectionBottom = Color(hex: 0x1d1a13)   // section panel bottom
    static let sectionBG     = Color(hex: 0x221f18)
    static let lcdBg         = Color(hex: 0x0c160c)   // dark green-black readout well
    static let comboBg       = Color(hex: 0x161310)   // inset combo / slider well
    static let btnOff        = Color(hex: 0x201d17)   // unlit segment
    static let btnOnTop      = Color(hex: 0x3d372c)   // lit segment gradient
    static let btnOnBottom   = Color(hex: 0x2b261d)

    // Text / legends
    static let engrave       = Color(hex: 0xd8cdb0)   // main cream text
    static let engraveDim    = Color(hex: 0x7a715a)   // dim legend
    static let textLabel     = Color(hex: 0xb3a884)   // control labels
    static let textOff       = Color(hex: 0x8a8168)   // unlit button text
    static let textActive    = Color(hex: 0xf0e6cc)   // lit button text
    static let titleCream    = Color(hex: 0xece2c6)

    // LCD greens
    static let lcd           = Color(hex: 0x8fe07a)   // bright readout
    static let lcdMed        = Color(hex: 0x7db86a)
    static let lcdDim        = Color(hex: 0x5c8a4f)

    static let ledOff        = Color(hex: 0x3a352b)
    static let capTop        = Color(hex: 0x4b4436)
    static let capBottom     = Color(hex: 0x201d16)
    static let track         = Color(hex: 0xd8cdb0).opacity(0.26)
    static let woodLight     = Color(hex: 0x6b4a2c)
    static let woodMid       = Color(hex: 0x5a3d24)
    static let woodDark      = Color(hex: 0x4d3319)

    // Section accents.
    static let oscAccent    = Color(hex: 0xd98a3c)  // amber
    static let osc2Accent   = Color(hex: 0xc85a44)  // coral
    static let filterAccent = Color(hex: 0x3f9aa8)  // cyan
    static let ampAccent    = Color(hex: 0x5fa96b)  // green
    static let filtEnvAccent = Color(hex: 0x5fa96b) // green
    static let lfoAccent    = Color(hex: 0x9a5fa0)  // magenta
    static let globalAccent = Color(hex: 0x4a7bb0)  // blue
    static let velAccent    = Color(hex: 0x4a7bb0)  // blue (Vel lives in Global)
    static let mixerAccent  = Color(hex: 0x8f887a)  // warm gray
    static let wtAccent     = Color(hex: 0xd98a3c)  // amber (WT lives in Osc 1)
    static let arpAccent    = Color(hex: 0xd9a63c)  // amber-gold
    static let chordAccent  = Color(hex: 0x5fa96b)  // green
    static let fxAccent     = Color(hex: 0x4a7bb0)  // blue
}

// MARK: - Audio meters

struct MeterFill: View {
    let fraction: CGFloat
    let accent: Color

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color.black.opacity(0.45))
                RoundedRectangle(cornerRadius: 2)
                    .fill(LinearGradient(
                        colors: [accent.opacity(0.65), accent],
                        startPoint: .leading, endPoint: .trailing))
                    .frame(width: geometry.size.width
                                 * min(1, max(0, fraction)))
                    .shadow(color: accent.opacity(0.55), radius: 2)
            }
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color.white.opacity(0.10), lineWidth: 1))
        }
    }
}

struct GainReductionMeter: View {
    let decibels: Float

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 4) {
                Text("GAIN REDUCTION")
                Spacer(minLength: 2)
                Text(String(format: "%.1f dB", max(0, decibels)))
                    .foregroundColor(Palette.lcd)
            }
            .font(.system(size: 8, weight: .semibold, design: .monospaced))
            .foregroundColor(Palette.engrave)
            MeterFill(fraction: CGFloat(min(24, max(0, decibels)) / 24),
                      accent: Palette.fxAccent)
                .frame(height: 10)
        }
        .frame(width: 112)
    }
}

struct StereoOutputMeter: View {
    let left: Float
    let right: Float

    private func fraction(_ linear: Float) -> CGFloat {
        let db = 20 * log10(max(linear, 0.001))
        return CGFloat(min(1, max(0, (db + 60) / 60)))
    }

    var body: some View {
        HStack(spacing: 4) {
            Text("OUT")
                .font(.system(size: 8, weight: .bold, design: .rounded))
                .tracking(0.5)
                .foregroundColor(Palette.engraveDim)
            VStack(spacing: 2) {
                MeterFill(fraction: fraction(left), accent: Palette.lcd)
                    .frame(height: 4)
                MeterFill(fraction: fraction(right), accent: Palette.lcd)
                    .frame(height: 4)
            }
        }
        .frame(width: 108)
    }
}

struct WavetableWaveform: View {
    let samples: [Float]
    let accent: Color

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                // Dark green-black phosphor well.
                RoundedRectangle(cornerRadius: 6)
                    .fill(Palette.lcdBg)
                Path { path in
                    guard samples.count > 1 else {
                        path.move(to: CGPoint(x: 0, y: geometry.size.height / 2))
                        path.addLine(to: CGPoint(x: geometry.size.width,
                                                y: geometry.size.height / 2))
                        return
                    }
                    for index in samples.indices {
                        let x = geometry.size.width
                              * CGFloat(index) / CGFloat(samples.count - 1)
                        let y = geometry.size.height * 0.5
                              - CGFloat(samples[index]) * geometry.size.height * 0.40
                        if index == 0 { path.move(to: CGPoint(x: x, y: y)) }
                        else { path.addLine(to: CGPoint(x: x, y: y)) }
                    }
                }
                .stroke(accent, style: StrokeStyle(lineWidth: 1.6,
                                                   lineJoin: .round))
                .shadow(color: accent.opacity(0.6), radius: 2)   // phosphor glow
            }
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(Color.black.opacity(0.55), lineWidth: 1))
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(Color(hex: 0xd8cdb0).opacity(0.06), lineWidth: 1).padding(1))
        }
    }
}

// MARK: - Rotary knob

struct Knob: View {
    let title: String
    let addr: SynthParam
    @ObservedObject var model: ParameterModel
    let unit: String
    let log: Bool
    let integer: Bool
    let accent: Color
    // Curve skew for log knobs: 1 = pure log; <1 pushes the midpoint toward the
    // top of the range while keeping fine resolution at the bottom.
    let skew: Float
    // When true the (linear 0..1) value is displayed as a mapped time in
    // seconds — the curve lives in the DSP mapping, so the knob stays linear.
    let timeMapped: Bool

    @EnvironmentObject private var help: HelpModel
    @State private var value: Float
    @State private var startNorm: CGFloat?
    @State private var hovering = false

    private let lo: Float
    private let hi: Float

    init(_ title: String, _ addr: SynthParam, _ model: ParameterModel,
         accent: Color, unit: String = "", log: Bool = false,
         integer: Bool = false, skew: Float = 1.0, timeMapped: Bool = false) {
        self.title = title; self.addr = addr; self.model = model
        self.unit = unit; self.log = log; self.integer = integer
        self.accent = accent; self.skew = max(0.1, skew); self.timeMapped = timeMapped
        let p = model.param(addr)
        let rawLo = p?.minValue ?? 0
        self.lo = log ? max(rawLo, 0.0001) : rawLo
        self.hi = p?.maxValue ?? 1
        _value = State(initialValue: p?.value ?? 0)
    }

    var body: some View {
        VStack(spacing: 2) {
            ZStack {
                // 270-degree track + value arc.
                KnobArc(fraction: 1)
                    .stroke(Palette.track, style: StrokeStyle(lineWidth: 2.5, lineCap: .butt))
                    .frame(width: 40, height: 40)
                KnobArc(fraction: normFor(value))
                    .stroke(accent, style: StrokeStyle(lineWidth: 2.5, lineCap: .butt))
                    .frame(width: 40, height: 40)
                    .shadow(color: accent.opacity(0.55), radius: 2)
                let effectiveNorm = normFor(model.effectiveValue(addr))
                if startNorm == nil
                    && model.supportsModulationIndicator(addr)
                    && abs(effectiveNorm - normFor(value)) > 0.002 {
                    KnobArc(fraction: effectiveNorm)
                        .trim(from: max(0, effectiveNorm - 0.02), to: effectiveNorm)
                        .stroke(Color.white, style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
                        .frame(width: 40, height: 40)
                        .shadow(color: .black.opacity(0.9), radius: 1)
                }

                // Machined metal cap: brushed conic ring + domed radial centre.
                Circle()
                    .fill(AngularGradient(colors: [
                        Color(hex: 0x2c281f), Color(hex: 0x443d30), Color(hex: 0x201d16),
                        Color(hex: 0x3c352a), Color(hex: 0x2c281f)], center: .center))
                    .frame(width: 32, height: 32)
                    .shadow(color: .black.opacity(0.6), radius: 2, y: 1)
                Circle()
                    .fill(RadialGradient(colors: [Palette.capTop, Palette.capBottom],
                                         center: UnitPoint(x: 0.5, y: 0.30),
                                         startRadius: 1, endRadius: 17))
                    .frame(width: 28, height: 28)
                    .overlay(Circle().stroke(Color(hex: 0xd8cdb0).opacity(0.14), lineWidth: 0.5))

                KnobPointer(norm: normFor(value))
                    .stroke(accent, style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
                    .frame(width: 26, height: 26)
            }
            .frame(width: 46, height: 46)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if startNorm == nil { startNorm = normFor(value) }
                        let delta = -g.translation.height / 180.0
                        var v = valueFor((startNorm ?? 0) + delta)
                        if integer { v = v.rounded() }
                        value = v
                        model.set(addr, value)
                    }
                    .onEnded { _ in startNorm = nil }
            )

            // Label by default; on hover it flips to the green LCD value.
            ZStack {
                Text(title.uppercased())
                    .font(.system(size: 8.5, weight: .semibold, design: .rounded))
                    .tracking(1.0)
                    .foregroundColor(Palette.textLabel)
                    .opacity(hovering ? 0 : 1)
                Text(displayString)
                    .font(.system(size: 11, weight: .regular, design: .monospaced))
                    .foregroundColor(Palette.lcd)
                    .shadow(color: Palette.lcd.opacity(0.5), radius: 3)
                    .opacity(hovering ? 1 : 0)
            }
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .frame(height: 12)
        }
        .frame(width: 52)
        .onHover { h in
            hovering = h
            let t = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if h { help.set(t) } else { help.clear(ifMatches: t) }
        }
        .onChange(of: model.version) { _ in
            // Local gesture state avoids host-observer feedback interrupting a
            // drag, but must follow presets and host automation once idle.
            if startNorm == nil, let parameter = model.param(addr) {
                value = parameter.value
            }
        }
    }

    private func normFor(_ v: Float) -> CGFloat {
        if log {
            let e = (log2f(max(v, lo)) - log2f(lo)) / (log2f(hi) - log2f(lo))
            return CGFloat(powf(max(0, e), 1.0 / skew))   // inverse of the skew
        }
        return CGFloat((v - lo) / (hi - lo))
    }
    private func valueFor(_ n: CGFloat) -> Float {
        let nn = Float(min(max(n, 0), 1))
        if log {
            let e = powf(nn, skew)                          // skew the log position
            return powf(2, log2f(lo) + e * (log2f(hi) - log2f(lo)))
        }
        return lo + nn * (hi - lo)
    }
    private var displayString: String {
        let v = value
        if timeMapped {
            return SynthTime.displayString(fromNorm: v)
        }
        if integer {
            // Show a signed value only for bipolar controls (e.g. semitones).
            let s = lo < 0 ? String(format: "%+.0f", v) : String(format: "%.0f", v)
            return unit.isEmpty ? s : "\(s)\(unit)"
        }
        let num: String
        if abs(v) >= 1000 { num = String(format: "%.0f", v) }
        else if abs(v) >= 100 { num = String(format: "%.0f", v) }
        else if abs(v) >= 10 { num = String(format: "%.1f", v) }
        else if abs(v) >= 1 { num = String(format: "%.2f", v) }
        else { num = String(format: "%.3f", v) }
        return unit.isEmpty ? num : "\(num)\(unit)"
    }
}

// MARK: - Lit segment chip (shared by selectors / toggles)

struct SegChip: View {
    let text: String
    let on: Bool
    let accent: Color
    var showLED: Bool = true
    private let radius: CGFloat = 5
    var body: some View {
        HStack(spacing: 6) {
            if showLED {
                Circle()
                    .fill(on ? accent : Palette.ledOff)
                    .frame(width: 6, height: 6)
                    .overlay(   // tiny specular pip on the lit dot
                        Circle().fill(Color.white.opacity(on ? 0.35 : 0))
                            .frame(width: 2, height: 2).offset(x: -0.6, y: -0.6))
                    .shadow(color: on ? accent.opacity(0.9) : .clear, radius: on ? 3.5 : 0)
            }
            Text(text.uppercased())
                .font(.system(size: 9.5, weight: .semibold, design: .rounded))
                .tracking(0.5)
                .foregroundColor(on ? Palette.textActive : Palette.textOff)
                .lineLimit(1)
                .fixedSize()
                .frame(minWidth: 26)   // uniform button width; never stretches to fill
        }
        .padding(.horizontal, 10).padding(.vertical, 6)
        // Every segment reads as a raised button; the lit one is warmer.
        .background(
            RoundedRectangle(cornerRadius: radius).fill(
                on ? AnyShapeStyle(LinearGradient(colors: [Palette.btnOnTop, Palette.btnOnBottom],
                                                  startPoint: .top, endPoint: .bottom))
                   : AnyShapeStyle(LinearGradient(colors: [Color(hex: 0x2a261e), Palette.btnOff],
                                                  startPoint: .top, endPoint: .bottom)))
        )
        // Rounded bevel: a soft top highlight and a dark base edge, warmer when lit.
        .overlay(
            RoundedRectangle(cornerRadius: radius)
                .stroke(on ? accent.opacity(0.35) : Color.black.opacity(0.55), lineWidth: 1)
        )
        .overlay(alignment: .top) {
            RoundedRectangle(cornerRadius: radius)
                .stroke(Color(hex: 0xd8cdb0).opacity(on ? 0.16 : 0.06), lineWidth: 1)
                .padding(0.5)
                .mask(LinearGradient(colors: [.black, .clear],
                                     startPoint: .top, endPoint: .center))
        }
    }
}

// Dark inset well that groups a row of segment chips.
struct SegGroup<Content: View>: View {
    @ViewBuilder let content: () -> Content
    var body: some View {
        HStack(spacing: 4) { content() }
            .padding(4)
            .background(
                RoundedRectangle(cornerRadius: 7)
                    .fill(Palette.comboBg)
                    .shadow(color: .black.opacity(0.4), radius: 1.5, y: 1)   // inset feel
            )
            .overlay(RoundedRectangle(cornerRadius: 7)
                .stroke(Color.black.opacity(0.6), lineWidth: 1))
            .overlay(RoundedRectangle(cornerRadius: 7)
                .stroke(Color(hex: 0xd8cdb0).opacity(0.05), lineWidth: 1).padding(1))
    }
}

// MARK: - Segmented selector

struct Selector: View {
    let title: String
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    private let minV: Int
    @EnvironmentObject private var help: HelpModel
    @State private var index: Int

    init(_ title: String, _ addr: SynthParam, _ options: [String],
         _ model: ParameterModel, accent: Color) {
        self.title = title; self.addr = addr; self.options = options
        self.model = model; self.accent = accent
        let lo = Int(model.param(addr)?.minValue ?? 0)
        self.minV = lo
        _index = State(initialValue: Int((model.param(addr)?.value ?? 0).rounded()) - lo)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title.uppercased())
                .font(.system(size: 8.5, weight: .semibold, design: .rounded))
                .tracking(1.3)
                .foregroundColor(Palette.textLabel)
            SegGroup {
                ForEach(options.indices, id: \.self) { i in
                    Button {
                        index = i
                        model.set(addr, Float(i + minV))
                    } label: {
                        SegChip(text: options[i], on: i == index, accent: accent)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }
}

// MARK: - Compact on/off toggle (single lit button)

struct ToggleButton: View {
    let title: String
    let addr: SynthParam
    let model: ParameterModel
    let accent: Color
    @EnvironmentObject private var help: HelpModel
    @State private var on: Bool

    init(_ title: String, _ addr: SynthParam, _ model: ParameterModel, accent: Color) {
        self.title = title; self.addr = addr; self.model = model; self.accent = accent
        _on = State(initialValue: (model.param(addr)?.value ?? 0) >= 0.5)
    }

    var body: some View {
        // Same label + single-chip structure as Dropdown, so a toggle and a
        // dropdown sitting side by side (e.g. Chord: On / Type / Inversion) line
        // up exactly.
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            Button { on.toggle(); model.set(addr, on ? 1 : 0) } label: {
                SegChip(text: on ? "On" : "Off", on: on, accent: accent)
                    .frame(minWidth: 52)
            }
            .buttonStyle(.plain)
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }
}

// MARK: - Compact popup menu (for many-option selectors, e.g. mod matrix)

struct Dropdown: View {
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    let helpText: String
    // When true, index 0 is a "None" state and renders dim (used by the mod
    // matrix so empty slots read as inactive). Otherwise every value is lit.
    let dimsNone: Bool
    @EnvironmentObject private var help: HelpModel
    @State private var index: Int
    @State private var showing = false

    init(_ addr: SynthParam, _ options: [String], _ model: ParameterModel,
         accent: Color, helpText: String = "", dimsNone: Bool = false) {
        self.addr = addr; self.options = options; self.model = model
        self.accent = accent; self.helpText = helpText; self.dimsNone = dimsNone
        _index = State(initialValue: Int((model.param(addr)?.value ?? 0).rounded()))
    }

    // The closed control is a raised, beveled chip identical to the SegChip
    // button groups; a custom Button + popover (rather than the system Menu,
    // whose label styling macOS overrides) guarantees the hardware look.
    var body: some View {
        let lit = !dimsNone || index > 0
        Button { showing.toggle() } label: {
            HStack(spacing: 4) {
                Circle()
                    .fill(lit ? accent : Palette.ledOff)
                    .frame(width: 6, height: 6)
                    .overlay(Circle().fill(Color.white.opacity(lit ? 0.35 : 0))
                        .frame(width: 2, height: 2).offset(x: -0.6, y: -0.6))
                    .shadow(color: lit ? accent.opacity(0.9) : .clear, radius: lit ? 3.5 : 0)
                Text(index < options.count ? options[index] : "?")
                    .font(.system(size: 10, weight: .semibold, design: .rounded))
                    .foregroundColor(lit ? Palette.textActive : Palette.textOff)
                    .lineLimit(1)
                    .fixedSize()
                Spacer(minLength: 2)
                Image(systemName: "arrowtriangle.down.fill")
                    .font(.system(size: 6, weight: .bold))
                    .foregroundColor(Palette.engrave.opacity(0.7))
            }
            .padding(.leading, 6).padding(.trailing, 5).padding(.vertical, 6)
            .frame(maxWidth: .infinity)
            .background(
                RoundedRectangle(cornerRadius: 5)
                    .fill(LinearGradient(colors: [Palette.btnOnTop, Palette.btnOnBottom],
                                         startPoint: .top, endPoint: .bottom)))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color.black.opacity(0.55), lineWidth: 1))
            .overlay(alignment: .top) {
                RoundedRectangle(cornerRadius: 5)
                    .stroke(Color(hex: 0xd8cdb0).opacity(0.14), lineWidth: 1)
                    .padding(0.5)
                    .mask(LinearGradient(colors: [.black, .clear],
                                         startPoint: .top, endPoint: .center))
            }
        }
        .buttonStyle(.plain)
        .popover(isPresented: $showing, arrowEdge: .bottom) {
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(options.indices, id: \.self) { i in
                        Button {
                            index = i; model.set(addr, Float(i)); showing = false
                        } label: {
                            HStack(spacing: 7) {
                                Image(systemName: "checkmark")
                                    .font(.system(size: 8, weight: .bold))
                                    .foregroundColor(accent)
                                    .opacity(i == index ? 1 : 0)
                                Text(options[i])
                                    .font(.system(size: 11, weight: .medium, design: .rounded))
                                    .foregroundColor(i == index ? Palette.textActive : Palette.engrave)
                                Spacer(minLength: 8)
                            }
                            .padding(.horizontal, 9).padding(.vertical, 5)
                            .frame(minWidth: 120, alignment: .leading)
                            .contentShape(Rectangle())
                            .background(RoundedRectangle(cornerRadius: 4)
                                .fill(i == index ? accent.opacity(0.16) : Color.clear))
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(6)
            }
            .frame(maxHeight: 320)
            .background(Palette.sectionBottom)
        }
        .onHover { hovering in
            if hovering { help.set(helpText) } else { help.clear(ifMatches: helpText) }
        }
    }
}

// MARK: - Compact bipolar amount slider (centre = 0)

struct AmountSlider: View {
    let addr: SynthParam
    let model: ParameterModel
    let accent: Color
    let helpText: String
    @EnvironmentObject private var help: HelpModel
    @State private var value: Float
    private let lo: Float
    private let hi: Float

    init(_ addr: SynthParam, _ model: ParameterModel, accent: Color, helpText: String = "") {
        self.addr = addr; self.model = model; self.accent = accent; self.helpText = helpText
        let p = model.param(addr)
        lo = p?.minValue ?? -1; hi = p?.maxValue ?? 1
        _value = State(initialValue: p?.value ?? 0)
    }

    var body: some View {
        HStack(spacing: 5) {
            GeometryReader { geo in
                let w = geo.size.width
                let frac = CGFloat((value - lo) / (hi - lo))            // 0..1
                let zero = CGFloat((0 - lo) / (hi - lo))                // centre
                ZStack(alignment: .leading) {
                    Capsule().fill(Palette.comboBg).frame(height: 4)
                        .overlay(Capsule().stroke(Color.black.opacity(0.5), lineWidth: 1))
                    Rectangle().fill(Palette.engrave.opacity(0.2)).frame(width: 1, height: 10)
                        .offset(x: zero * w - 0.5)
                    Capsule().fill(accent)
                        .frame(width: max(1, abs(frac - zero) * w), height: 4)
                        .offset(x: min(frac, zero) * w)
                    Circle()
                        .fill(RadialGradient(colors: [Palette.capTop, Palette.capBottom],
                                             center: UnitPoint(x: 0.5, y: 0.32),
                                             startRadius: 0, endRadius: 8))
                        .frame(width: 14, height: 14)
                        .overlay(Circle().stroke(accent, lineWidth: 1.5))
                        .shadow(color: accent.opacity(0.6), radius: 2)
                        .offset(x: frac * w - 7)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                    let n = Float(min(max(g.location.x / w, 0), 1))
                    value = lo + n * (hi - lo)
                    if abs(value) < 0.03 { value = 0 }                  // centre detent
                    model.set(addr, value)
                })
            }
            .frame(height: 18)
            Text(String(format: "%+.2f", value))
                .font(.system(size: 9, weight: .medium, design: .monospaced))
                .foregroundColor(value == 0 ? Palette.engraveDim : Palette.lcd)
                .frame(width: 34, alignment: .trailing)
        }
        .onHover { hovering in
            if hovering { help.set(helpText) } else { help.clear(ifMatches: helpText) }
        }
    }
}

// MARK: - Section panel container

struct Panel<Content: View, Trailing: View>: View {
    let title: String
    let accent: Color
    let trailing: Trailing
    let content: Content

    init(title: String, accent: Color,
         @ViewBuilder trailing: () -> Trailing,
         @ViewBuilder content: () -> Content) {
        self.title = title; self.accent = accent
        self.trailing = trailing(); self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 6) {
                Text(title.uppercased())
                    .font(.system(size: 10.5, weight: .heavy, design: .rounded))
                    .tracking(1.6)
                    .foregroundColor(accent.isLight ? Color(hex: 0x1b1813) : Palette.textActive)
                    .padding(.horizontal, 11).padding(.vertical, 4)
                    .background(RoundedRectangle(cornerRadius: 3).fill(accent))
                    .shadow(color: .black.opacity(0.4), radius: 1, y: 1)
                Spacer(minLength: 8)
                trailing
            }
            Spacer().frame(height: 13)   // fixed, consistent header→content gap
            content
            Spacer(minLength: 0)        // absorbs the slack so content stays top-aligned
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(LinearGradient(colors: [Palette.sectionTop, Palette.sectionBottom],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.black.opacity(0.5), lineWidth: 1))
                .overlay(RoundedRectangle(cornerRadius: 6)
                    .stroke(Color(hex: 0xd8cdb0).opacity(0.06), lineWidth: 1)
                    .padding(1))
                .shadow(color: .black.opacity(0.4), radius: 4, y: 2)
        )
    }
}

// Panels that don't need a header accessory keep the simple two-argument form.
extension Panel where Trailing == EmptyView {
    init(title: String, accent: Color, @ViewBuilder content: () -> Content) {
        self.init(title: title, accent: accent, trailing: { EmptyView() }, content: content)
    }
}

// MARK: - Inline waveform selector (lives in a panel header)

struct InlineWaveSelect: View {
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    @EnvironmentObject private var help: HelpModel

    init(_ addr: SynthParam, _ options: [String], _ model: ParameterModel, accent: Color) {
        self.addr = addr; self.options = options; self.model = model; self.accent = accent
    }

    var body: some View {
        // Render from the parameter itself rather than duplicating it in @State.
        // There are two selectors in the oscillator panel; local state can be
        // retained under the wrong structural identity when that panel is
        // rebuilt, making Osc 1 appear to follow an Osc 2 click.
        let selectedIndex = Int((model.param(addr)?.value ?? 0).rounded())
        SegGroup {
            ForEach(options.indices, id: \.self) { i in
                Button {
                    model.set(addr, Float(i))
                    model.forceRefresh()   // re-evaluate which controls are dimmed
                } label: {
                    SegChip(text: options[i], on: i == selectedIndex, accent: accent)
                }
                .buttonStyle(.plain)
            }
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }
}

extension View {
    /// Dim and disable a control that does not apply to the current osc type.
    func dimmed(_ on: Bool) -> some View {
        self.opacity(on ? 0.25 : 1).allowsHitTesting(!on)
    }
}
