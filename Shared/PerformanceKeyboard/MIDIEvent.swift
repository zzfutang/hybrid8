import AudioToolbox

enum PerformanceMIDIEvent: Equatable {
    case noteOn(note: UInt8, velocity: UInt8)
    case noteOff(note: UInt8)
    case pitchBend(normalized: Double)
    case controlChange(controller: UInt8, value: UInt8)

    var bytes: [UInt8] {
        switch self {
        case let .noteOn(note, velocity):
            return [0x90, note, velocity]
        case let .noteOff(note):
            return [0x80, note, 0]
        case let .pitchBend(normalized):
            let clamped = min(1.0, max(-1.0, normalized))
            let value = Int(((clamped + 1.0) * 0.5 * 16_383.0).rounded())
            return [0xE0, UInt8(value & 0x7f), UInt8((value >> 7) & 0x7f)]
        case let .controlChange(controller, value):
            return [0xB0, controller, value]
        }
    }
}

protocol PerformanceEventSink: AnyObject {
    func sendPerformanceEvent(_ event: PerformanceMIDIEvent)
}
