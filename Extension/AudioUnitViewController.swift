//
//  AudioUnitViewController.swift
//  Principal class of the AU extension. Implements AUAudioUnitFactory (creates
//  the SynthAudioUnit) and hosts the SwiftUI editor view.
//

import AppKit
import CoreAudioKit
import SwiftUI

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {

    var audioUnit: AUAudioUnit?
    private var standaloneMusicalTyping = false
    private var downNotes: [Character: UInt8] = [:]
    private var octaveShift = 0
    private var typingVelocity: UInt8 = 100

    private static let noteMap: [Character: Int] = [
        "a": 0, "w": 1, "s": 2, "e": 3, "d": 4, "f": 5, "t": 6, "g": 7,
        "y": 8, "h": 9, "u": 10, "j": 11, "k": 12, "o": 13, "l": 14,
        "p": 15, ";": 16
    ]

    public override func loadView() {
        let keyView = MusicalTypingView(
            frame: NSRect(x: 0, y: 0, width: 1700, height: 824))
        keyView.keyHandler = { [weak self] event, down in
            self?.handleMusicalTyping(event, down: down) ?? false
        }
        self.view = keyView
        self.preferredContentSize = NSSize(width: 1700, height: 824)
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        DistributedNotificationCenter.default().addObserver(
            forName: Notification.Name("com.johangorsjo.Hybrid8.standaloneActive"),
            object: nil, queue: .main) { [weak self] _ in
                self?.standaloneMusicalTyping = true
            }
        NotificationCenter.default.addObserver(
            forName: Notification.Name("Hybrid8SearchDidResign"),
            object: nil, queue: .main) { [weak self] _ in
                guard let self, self.standaloneMusicalTyping else { return }
                self.view.window?.makeFirstResponder(self.view)
            }
        if audioUnit != nil { setupUI() }
    }

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try SynthAudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        DispatchQueue.main.async { [weak self] in
            if self?.isViewLoaded == true { self?.setupUI() }
        }
        return au
    }

    private func setupUI() {
        guard let synthAudioUnit = audioUnit as? SynthAudioUnit,
              let tree = synthAudioUnit.parameterTree else { return }
        view.subviews.forEach { $0.removeFromSuperview() }
        let model = ParameterModel(
            tree: tree,
            effectiveProvider: { synthAudioUnit.effectiveValue(for: $0) },
            meterProvider: { synthAudioUnit.meterValues() })
        let wavetables = WavetableStore(model: model, audioUnit: synthAudioUnit)
        let host = NSHostingView(rootView:
            SynthView(model: model, wavetables: wavetables))
        host.frame = view.bounds
        host.autoresizingMask = [.width, .height]
        view.addSubview(host)
    }

    private func handleMusicalTyping(_ event: NSEvent, down: Bool) -> Bool {
        guard standaloneMusicalTyping,
              event.modifierFlags.intersection([.command, .control, .option]).isEmpty,
              let character = event.charactersIgnoringModifiers?.lowercased().first
        else { return false }
        if down && !event.isARepeat {
            switch character {
            case "z": octaveShift = max(-4, octaveShift - 1); return true
            case "x": octaveShift = min(4, octaveShift + 1); return true
            case "c": typingVelocity = UInt8(max(1, Int(typingVelocity) - 16)); return true
            case "v": typingVelocity = UInt8(min(127, Int(typingVelocity) + 16)); return true
            default: break
            }
        }
        guard let semitone = Self.noteMap[character] else { return false }
        let midiNote = 48 + octaveShift * 12 + semitone
        guard (0...127).contains(midiNote) else { return true }
        let note = UInt8(midiNote)
        if down {
            guard !event.isARepeat, downNotes[character] == nil else { return true }
            downNotes[character] = note
            sendMIDI([0x90, note, typingVelocity])
        } else if let sounding = downNotes.removeValue(forKey: character) {
            sendMIDI([0x80, sounding, 0])
        }
        return true
    }

    private func sendMIDI(_ bytes: [UInt8]) {
        guard let block = audioUnit?.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return }
            block(AUEventSampleTimeImmediate, 0, bytes.count, base)
        }
    }
}

private final class MusicalTypingView: NSView {
    var keyHandler: ((NSEvent, Bool) -> Bool)?
    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        if keyHandler?(event, true) != true { super.keyDown(with: event) }
    }

    override func keyUp(with event: NSEvent) {
        if keyHandler?(event, false) != true { super.keyUp(with: event) }
    }
}
