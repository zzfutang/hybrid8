import AppKit

/// One state machine for standalone musical typing. Text entry keeps normal key
/// handling while it is active; Escape or Return explicitly returns ownership
/// to the instrument.
final class MusicalTypingController {
    private weak var sink: PerformanceEventSink?
    private let stateChanged: (MusicalTypingState) -> Void
    private var monitors: [Any] = []
    private let machine = MusicalTypingStateMachine()

    init(sink: PerformanceEventSink,
         stateChanged: @escaping (MusicalTypingState) -> Void) {
        self.sink = sink
        self.stateChanged = stateChanged
        stateChanged(machine.state)

        monitors = [
            NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
                self?.handle(event, isDown: true) ?? event
            },
            NSEvent.addLocalMonitorForEvents(matching: .keyUp) { [weak self] event in
                self?.handle(event, isDown: false) ?? event
            },
            NSEvent.addLocalMonitorForEvents(matching: .leftMouseDown) { event in
                Self.endTextEditingIfClickLeavesEditor(event)
                return event
            }
        ].compactMap { $0 }
    }

    deinit {
        monitors.forEach(NSEvent.removeMonitor)
        releaseAllNotes()
    }

    private func handle(_ event: NSEvent, isDown: Bool) -> NSEvent? {
        if !event.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            return event
        }

        if let window = event.window, window.firstResponder is NSTextView {
            if isDown && [36, 53, 76].contains(event.keyCode) {
                window.endEditing(for: nil)
                window.makeFirstResponder(window.contentView)
                return nil
            }
            return event
        }

        guard let character = event.charactersIgnoringModifiers?.lowercased().first else {
            return event
        }

        let result = machine.handle(
            character: character, isDown: isDown, isRepeat: event.isARepeat)
        guard result.0 else {
            // Swallow unhandled key-downs so the responder chain does not play
            // the macOS alert sound ("click") for keys the instrument doesn't
            // use. Modifier combos and text-field keys were already returned
            // above, so app/menu shortcuts are unaffected. Key-ups never beep,
            // so pass them through to keep normal event flow.
            return isDown ? nil : event
        }
        dispatch(result.1)
        stateChanged(machine.state)
        return nil
    }

    private func releaseAllNotes() {
        machine.releaseAll().forEach(dispatch)
    }

    private func dispatch(_ output: MusicalTypingStateMachine.Output?) {
        switch output {
        case let .noteOn(note, velocity):
            sink?.sendPerformanceEvent(.noteOn(note: note, velocity: velocity))
        case let .noteOff(note):
            sink?.sendPerformanceEvent(.noteOff(note: note))
        case nil:
            break
        }
    }

    private static func endTextEditingIfClickLeavesEditor(_ event: NSEvent) {
        guard let window = event.window,
              let fieldEditor = window.firstResponder as? NSTextView,
              fieldEditor.isFieldEditor,
              let contentView = window.contentView else { return }
        let point = contentView.convert(event.locationInWindow, from: nil)
        let hit = contentView.hitTest(point)
        if hit !== fieldEditor && hit?.isDescendant(of: fieldEditor) != true {
            window.endEditing(for: nil)
            window.makeFirstResponder(contentView)
        }
    }
}
