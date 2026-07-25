import Foundation

struct MusicalTypingState: Equatable {
    var octaveShift = 0
    var velocity: UInt8 = 100

    var description: String {
        let octave = octaveShift >= 0 ? "+\(octaveShift)" : "\(octaveShift)"
        return "⌨ keys A–;  ·  octave \(octave) (Z/X)  ·  vel \(velocity) (C/V)"
    }
}

/// Platform-independent musical typing behavior shared by the standalone event
/// monitor and the AU remote view responder.
final class MusicalTypingStateMachine {
    enum Output: Equatable {
        case noteOn(note: UInt8, velocity: UInt8)
        case noteOff(note: UInt8)
    }

    private static let noteMap: [Character: Int] = [
        "a": 0, "w": 1, "s": 2, "e": 3, "d": 4, "f": 5, "t": 6, "g": 7,
        "y": 8, "h": 9, "u": 10, "j": 11, "k": 12, "o": 13, "l": 14,
        "p": 15, ";": 16
    ]

    private(set) var state = MusicalTypingState()
    private var soundingNotes: [Character: UInt8] = [:]

    func handle(character: Character, isDown: Bool, isRepeat: Bool) -> (Bool, Output?) {
        if isDown && !isRepeat {
            switch character {
            case "z":
                state.octaveShift = max(-4, state.octaveShift - 1)
                return (true, nil)
            case "x":
                state.octaveShift = min(4, state.octaveShift + 1)
                return (true, nil)
            case "c":
                state.velocity = UInt8(max(1, Int(state.velocity) - 16))
                return (true, nil)
            case "v":
                state.velocity = UInt8(min(127, Int(state.velocity) + 16))
                return (true, nil)
            default:
                break
            }
        }

        guard let semitone = Self.noteMap[character] else { return (false, nil) }
        let value = 48 + state.octaveShift * 12 + semitone
        guard (0...127).contains(value) else { return (true, nil) }
        let note = UInt8(value)

        if isDown {
            guard !isRepeat, soundingNotes[character] == nil else { return (true, nil) }
            soundingNotes[character] = note
            return (true, .noteOn(note: note, velocity: state.velocity))
        }
        guard let sounding = soundingNotes.removeValue(forKey: character) else {
            return (true, nil)
        }
        return (true, .noteOff(note: sounding))
    }

    func releaseAll() -> [Output] {
        defer { soundingNotes.removeAll() }
        return soundingNotes.values.map { .noteOff(note: $0) }
    }
}
