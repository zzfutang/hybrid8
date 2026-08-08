import AppKit

/// One state machine for standalone musical typing. Text entry keeps normal key
/// handling while it is active; Escape or Return explicitly returns ownership
/// to the instrument.
///
/// The controller is a process-local event monitor: it observes key events no
/// matter which view currently has focus and turns them into notes. It does
/// NOT silence the alert beep — on current macOS, returning nil from a local
/// monitor no longer suppresses dispatch (verified with a breakpoint on
/// NSBeep: the event continues into -[NSWindow keyDown:] regardless), so the
/// swallowing lives in the responder chain instead: MusicalTypingKeyCatcher
/// in the host window, and the AU editor's container view in the appex.
/// `isActive` is where the appex declines the keyboard while a DAW is
/// frontmost.
final class MusicalTypingController {
    private weak var sink: PerformanceEventSink?
    private let isActive: () -> Bool
    private let stateChanged: (MusicalTypingState) -> Void
    private var monitors: [Any] = []
    private let machine = MusicalTypingStateMachine()

    init(sink: PerformanceEventSink,
         isActive: @escaping () -> Bool = { true },
         stateChanged: @escaping (MusicalTypingState) -> Void) {
        self.sink = sink
        self.isActive = isActive
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
        guard isActive() else { return event }
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
            // Returning nil is best-effort suppression; the responder-level
            // catchers are what actually keep unhandled keys from beeping.
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
