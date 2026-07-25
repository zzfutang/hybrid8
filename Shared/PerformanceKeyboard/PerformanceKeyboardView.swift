import SwiftUI

struct PerformanceKeyboardView: View {
    let sink: PerformanceEventSink
    var firstNote = 48
    var noteCount = 36

    var body: some View {
        HStack(spacing: 0) {
            PerformanceRibbons(sink: sink)
                .frame(width: 116)
            PianoKeyboardView(sink: sink, firstNote: firstNote, noteCount: noteCount)
        }
        .frame(height: 120)
        .background(Color(red: 0.08, green: 0.08, blue: 0.10))
    }
}

private struct PianoKeyboardView: View {
    let sink: PerformanceEventSink
    let firstNote: Int
    let noteCount: Int

    var body: some View {
        GeometryReader { geometry in
            let whiteNotes = notes.filter { !Self.isBlack($0) }
            let gaps = CGFloat(max(0, whiteNotes.count - 1))
            let whiteWidth = (geometry.size.width - gaps) / CGFloat(whiteNotes.count)
            ZStack(alignment: .topLeading) {
                HStack(spacing: 1) {
                    ForEach(whiteNotes, id: \.self) {
                        PianoKey(sink: sink, note: UInt8($0), isBlack: false)
                    }
                }
                ForEach(blackKeys(whiteWidth: whiteWidth), id: \.note) { key in
                    PianoKey(sink: sink, note: UInt8(key.note), isBlack: true)
                        .frame(width: whiteWidth * 0.6, height: 70)
                        .offset(x: key.x)
                }
            }
        }
        .padding(6)
    }

    private var notes: Range<Int> { firstNote..<(firstNote + noteCount) }

    private func blackKeys(whiteWidth: CGFloat) -> [(note: Int, x: CGFloat)] {
        var result: [(Int, CGFloat)] = []
        var whiteIndex = 0
        for note in notes {
            if Self.isBlack(note) {
                result.append((note, CGFloat(whiteIndex) * (whiteWidth + 1) - whiteWidth * 0.3))
            } else {
                whiteIndex += 1
            }
        }
        return result
    }

    private static func isBlack(_ note: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(((note % 12) + 12) % 12)
    }
}

private struct PianoKey: View {
    let sink: PerformanceEventSink
    let note: UInt8
    let isBlack: Bool
    @State private var isDown = false

    var body: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(isDown ? Color(red: 0.98, green: 0.62, blue: 0.20)
                         : (isBlack ? .black : .white))
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(.black.opacity(0.3)))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in
                    guard !isDown else { return }
                    isDown = true
                    sink.sendPerformanceEvent(.noteOn(note: note, velocity: 100))
                }
                .onEnded { _ in
                    isDown = false
                    sink.sendPerformanceEvent(.noteOff(note: note))
                })
    }
}

private struct PerformanceRibbons: View {
    let sink: PerformanceEventSink

    var body: some View {
        HStack(spacing: 7) {
            MIDIRibbon(title: "PITCH", bipolar: true,
                       accent: Color(red: 0.31, green: 0.61, blue: 0.91)) {
                sink.sendPerformanceEvent(.pitchBend(normalized: $0))
            }
            MIDIRibbon(title: "MOD", bipolar: false,
                       accent: Color(red: 0.56, green: 0.84, blue: 0.42)) {
                sink.sendPerformanceEvent(.controlChange(
                    controller: 1, value: UInt8(($0 * 127).rounded())))
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .overlay(alignment: .trailing) {
            Rectangle().fill(.white.opacity(0.12)).frame(width: 1)
        }
    }
}

private struct MIDIRibbon: View {
    let title: String
    let bipolar: Bool
    let accent: Color
    let onChange: (Double) -> Void
    @State private var value = 0.0

    var body: some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.system(size: 8, weight: .bold, design: .rounded))
                .tracking(0.8)
                .foregroundColor(.white.opacity(0.65))
            GeometryReader { geometry in
                let normalized = CGFloat(bipolar ? (value + 1) * 0.5 : value)
                ZStack {
                    RoundedRectangle(cornerRadius: 5).fill(.black.opacity(0.58))
                    Rectangle().fill(.white.opacity(0.09)).frame(width: 1)
                    if bipolar {
                        Rectangle().fill(.white.opacity(0.18)).frame(height: 1)
                    }
                    RoundedRectangle(cornerRadius: 2)
                        .fill(accent)
                        .frame(height: 4)
                        .shadow(color: accent.opacity(0.8), radius: 3)
                        .offset(y: (0.5 - normalized) * max(0, geometry.size.height - 10))
                }
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(.white.opacity(0.15), lineWidth: 1))
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
    }
}
