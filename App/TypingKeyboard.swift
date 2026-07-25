//
//  TypingKeyboard.swift
//  "Musical Typing": play the synth from the computer keyboard using the same
//  key layout as Logic Pro / GarageBand.
//
//    A W S E D F T G Y H U J K O L P ;   -> C C# D D# E F F# G G# A A# B C ...
//    Z / X  = octave down / up
//    C / V  = velocity down / up
//

import AppKit

final class TypingKeyboard {
    private weak var host: SynthHost?
    private var monitors: [Any] = []
    private var downNotes: [Character: Int] = [:]   // key char -> sounding MIDI note

    private let baseNote = 48       // 'A' key = C3
    private var octaveShift = 0     // Z / X
    private var velocity = 100      // C / V

    // Semitone offset from the base note for each letter (Logic layout).
    private static let noteMap: [Character: Int] = [
        "a": 0, "w": 1, "s": 2, "e": 3, "d": 4, "f": 5, "t": 6, "g": 7,
        "y": 8, "h": 9, "u": 10, "j": 11, "k": 12, "o": 13, "l": 14, "p": 15, ";": 16
    ]

    init(host: SynthHost) {
        self.host = host
        updateInfo()
        // NOTE: return exactly what handle() returns — nil consumes the event
        // (no system beep), e passes it through. Do NOT use `?? e`, which would
        // turn a consumed (nil) result back into a pass-through.
        let down = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] e in
            guard let self = self else { return e }
            return self.handle(e, down: true)
        }
        let up = NSEvent.addLocalMonitorForEvents(matching: .keyUp) { [weak self] e in
            guard let self = self else { return e }
            return self.handle(e, down: false)
        }
        let mouse = NSEvent.addLocalMonitorForEvents(
            matching: [.leftMouseDown, .rightMouseDown]) { e in
                // SwiftUI's FocusState inside the hosted AU does not always
                // retire AppKit's shared field editor. A click anywhere other
                // than that editor should restore Musical Typing immediately.
                if let window = e.window,
                   window.firstResponder is NSTextView,
                   !(window.contentView?.hitTest(e.locationInWindow) is NSTextView) {
                    window.makeFirstResponder(nil)
                }
                return e
            }
        monitors = [down, up, mouse].compactMap { $0 }
    }

    deinit { monitors.forEach { NSEvent.removeMonitor($0) } }

    private func handle(_ e: NSEvent, down: Bool) -> NSEvent? {
        // Let menu shortcuts and text entry (e.g. the Save dialog) work normally.
        if !e.modifierFlags.intersection([.command, .control, .option]).isEmpty { return e }
        if e.window?.firstResponder is NSTextView {
            // AppKit owns the actual field editor even though the search box
            // lives in SwiftUI. Explicitly end editing so Escape/Return always
            // hand the letter keys back to the performance keyboard.
            if down && (e.keyCode == 53 || e.keyCode == 36 || e.keyCode == 76) {
                e.window?.makeFirstResponder(nil)
                return nil
            }
            return e
        }
        guard let ch = e.charactersIgnoringModifiers?.lowercased().first else { return e }

        // Octave / velocity keys (on key-down only).
        if down && !e.isARepeat {
            switch ch {
            case "z": octaveShift = max(-4, octaveShift - 1); updateInfo(); return nil
            case "x": octaveShift = min(4, octaveShift + 1); updateInfo(); return nil
            case "c": velocity = max(1, velocity - 16); updateInfo(); return nil
            case "v": velocity = min(127, velocity + 16); updateInfo(); return nil
            default: break
            }
        }

        guard let semitone = TypingKeyboard.noteMap[ch] else { return e } // not a note key
        let note = baseNote + octaveShift * 12 + semitone
        guard note >= 0, note <= 127 else { return nil }

        if down {
            if e.isARepeat { return nil }              // ignore auto-repeat
            if downNotes[ch] == nil {
                downNotes[ch] = note
                host?.noteOn(UInt8(note), velocity: UInt8(velocity))
            }
        } else if let sounding = downNotes.removeValue(forKey: ch) {
            host?.noteOff(UInt8(sounding))             // release the note this key started
        }
        return nil                                     // consume (no system beep)
    }

    private func updateInfo() {
        let oct = octaveShift >= 0 ? "+\(octaveShift)" : "\(octaveShift)"
        host?.typingInfo = "⌨ keys A–;  ·  octave \(oct) (Z/X)  ·  vel \(velocity) (C/V)"
    }
}
