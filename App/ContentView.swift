//
//  ContentView.swift
//  Hosts the AU's own editor view and a three-octave performance keyboard.
//

import SwiftUI
import AppKit

struct ContentView: View {
    @ObservedObject var host: SynthHost

    var body: some View {
        VStack(spacing: 0) {
            if let vc = host.viewController {
                AUViewRepresentable(viewController: vc)
                    .frame(minHeight: 420)
            } else {
                Text("Hybrid 8")
                    .font(.largeTitle.bold())
                    .frame(maxWidth: .infinity, minHeight: 420)
                    .background(Color(red: 0.11, green: 0.11, blue: 0.13))
            }

            HStack(spacing: 10) {
                Text(host.status)
                Text("·").foregroundColor(.secondary.opacity(0.5))
                Text(host.midiInfo)
                Text("·").foregroundColor(.secondary.opacity(0.5))
                Text(host.typingInfo)
            }
            .font(.caption)
            .foregroundColor(.secondary)
            .padding(.vertical, 4)

            HStack(spacing: 0) {
                PerformanceRibbons(host: host)
                    .frame(width: 116)
                Keyboard(host: host)
            }
                .frame(height: 120)
                .background(Color(red: 0.08, green: 0.08, blue: 0.10))
        }
    }
}

// Embed the AU's NSViewController in SwiftUI.
struct AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}

// Click/hold piano covering three complete octaves, C3..B5.
struct Keyboard: View {
    @ObservedObject var host: SynthHost
    private let firstNote = 48 // C3
    private let noteCount = 36

    var body: some View {
        GeometryReader { geo in
            let whiteNotes = whiteKeyNotes()
            let gaps = CGFloat(max(0, whiteNotes.count - 1))
            let whiteWidth = (geo.size.width - gaps) / CGFloat(whiteNotes.count)
            ZStack(alignment: .topLeading) {
                HStack(spacing: 1) {
                    ForEach(whiteNotes, id: \.self) { note in
                        Key(host: host, note: UInt8(note), isBlack: false)
                    }
                }
                ForEach(blackKeys(whiteWidth: whiteWidth), id: \.note) { bk in
                    Key(host: host, note: UInt8(bk.note), isBlack: true)
                        .frame(width: whiteWidth * 0.6, height: 70)
                        .offset(x: bk.x, y: 0)
                }
            }
        }
        .padding(6)
        .background(Color(red: 0.08, green: 0.08, blue: 0.10))
    }

    private func whiteKeyNotes() -> [Int] {
        var notes: [Int] = []
        for n in firstNote..<(firstNote + noteCount) where !isBlack(n) { notes.append(n) }
        return notes
    }
    private func blackKeys(whiteWidth: CGFloat) -> [(note: Int, x: CGFloat)] {
        var result: [(Int, CGFloat)] = []
        var whiteIndex = 0
        for n in firstNote..<(firstNote + noteCount) {
            if isBlack(n) {
                let x = CGFloat(whiteIndex) * (whiteWidth + 1) - whiteWidth * 0.3
                result.append((n, x))
            } else {
                whiteIndex += 1
            }
        }
        return result.map { (note: $0.0, x: $0.1) }
    }
    private func isBlack(_ note: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(((note % 12) + 12) % 12)
    }
}

// Conventional left-of-keybed performance controls. Pitch is bipolar and
// springs back to centre; modulation is unipolar and holds its last position.
private struct PerformanceRibbons: View {
    @ObservedObject var host: SynthHost

    var body: some View {
        HStack(spacing: 7) {
            MIDIRibbon(title: "PITCH", bipolar: true,
                       accent: Color(red: 0.31, green: 0.61, blue: 0.91)) {
                host.pitchBend($0)
            }
            MIDIRibbon(title: "MOD", bipolar: false,
                       accent: Color(red: 0.56, green: 0.84, blue: 0.42)) {
                host.modWheel($0)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .overlay(alignment: .trailing) {
            Rectangle()
                .fill(Color.white.opacity(0.12))
                .frame(width: 1)
        }
    }
}

private struct MIDIRibbon: View {
    let title: String
    let bipolar: Bool
    let accent: Color
    let onChange: (Double) -> Void
    @State private var value: Double

    init(title: String, bipolar: Bool, accent: Color,
         onChange: @escaping (Double) -> Void) {
        self.title = title
        self.bipolar = bipolar
        self.accent = accent
        self.onChange = onChange
        _value = State(initialValue: 0)
    }

    var body: some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.system(size: 8, weight: .bold, design: .rounded))
                .tracking(0.8)
                .foregroundColor(Color.white.opacity(0.65))
            GeometryReader { geometry in
                let normalized = CGFloat(bipolar ? (value + 1) * 0.5 : value)
                ZStack {
                    RoundedRectangle(cornerRadius: 5)
                        .fill(Color.black.opacity(0.58))
                    Rectangle()
                        .fill(Color.white.opacity(0.09))
                        .frame(width: 1)
                    if bipolar {
                        Rectangle()
                            .fill(Color.white.opacity(0.18))
                            .frame(height: 1)
                    }
                    RoundedRectangle(cornerRadius: 2)
                        .fill(accent)
                        .frame(height: 4)
                        .shadow(color: accent.opacity(0.8), radius: 3)
                        .offset(y: (0.5 - normalized) * max(0, geometry.size.height - 10))
                }
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(Color.white.opacity(0.15), lineWidth: 1))
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let position = Double(min(1, max(
                            0, 1 - gesture.location.y / geometry.size.height)))
                        value = bipolar ? position * 2 - 1 : position
                        onChange(value)
                    }
                    .onEnded { _ in
                        if bipolar {
                            value = 0
                            onChange(0)
                        }
                    })
            }
        }
        .frame(maxWidth: .infinity)
        .accessibilityLabel("\(title) ribbon")
        .help(bipolar ? "Pitch bend · returns to centre"
                       : "Modulation wheel · holds position")
    }
}

private struct Key: View {
    @ObservedObject var host: SynthHost
    let note: UInt8
    let isBlack: Bool
    @State private var down = false

    var body: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(fill)
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.3)))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in if !down { down = true; host.noteOn(note) } }
                .onEnded { _ in down = false; host.noteOff(note) })
    }

    private var fill: Color {
        if down { return Color(red: 0.98, green: 0.62, blue: 0.20) }
        return isBlack ? Color.black : Color.white
    }
}
