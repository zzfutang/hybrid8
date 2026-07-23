//
//  ContentView.swift
//  Hosts the AU's own editor view and a simple two-octave test keyboard.
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

            Keyboard(host: host)
                .frame(height: 120)
        }
    }
}

// Embed the AU's NSViewController in SwiftUI.
struct AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}

// Minimal click/hold piano covering C3..B4.
struct Keyboard: View {
    @ObservedObject var host: SynthHost
    private let firstNote = 48 // C3

    var body: some View {
        GeometryReader { geo in
            let whiteNotes = whiteKeyNotes()
            let whiteWidth = geo.size.width / CGFloat(whiteNotes.count)
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
        for n in firstNote...(firstNote + 24) where !isBlack(n) { notes.append(n) }
        return notes
    }
    private func blackKeys(whiteWidth: CGFloat) -> [(note: Int, x: CGFloat)] {
        var result: [(Int, CGFloat)] = []
        var whiteIndex = 0
        for n in firstNote...(firstNote + 24) {
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
