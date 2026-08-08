//
//  MusicalTypingKeyCatcher.swift
//  A responder-chain key sink for the standalone host window.
//
//  The host's MusicalTypingController watches key events through a local
//  monitor and turns them into notes — but on current macOS, returning nil
//  from the monitor no longer suppresses dispatch: the event continues to the
//  window, and with nothing focused the window's own keyDown answers it with
//  the alert beep (verified with a breakpoint on NSBeep). Swallowing has to
//  happen in the responder chain. This view parks itself as first responder
//  whenever focus rests on the window itself and consumes every plain key;
//  the monitor remains the note dispatcher, so nothing double-triggers.
//

import AppKit
import SwiftUI

struct MusicalTypingKeyCatcher: NSViewRepresentable {
    func makeNSView(context: Context) -> KeyCatcherView { KeyCatcherView() }
    func updateNSView(_ view: KeyCatcherView, context: Context) {}
}

final class KeyCatcherView: NSView {
    private var observers: [Any] = []

    override var acceptsFirstResponder: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        observers.forEach(NotificationCenter.default.removeObserver)
        observers = []
        guard let window else { return }
        park("joined window")
        for name in [NSWindow.didBecomeKeyNotification,
                     NSWindow.didUpdateNotification] {
            observers.append(NotificationCenter.default.addObserver(
                forName: name, object: window, queue: .main) { [weak self] _ in
                    self?.park("window update")
                })
        }
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    /// Claim first responder only from the window itself (or nothing): that
    /// is the configuration that beeps. Focus held by any real view — the
    /// embedded AU editor, a text field — is left alone.
    private func park(_ reason: String) {
        guard let window,
              window.firstResponder == nil || window.firstResponder === window
        else { return }
        window.makeFirstResponder(self)
    }

    override func keyDown(with event: NSEvent) {
        // Plain keys are the musical keyboard's; the monitor already made
        // the note. Consuming here is what silences the window's beep.
        if event.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            return
        }
        super.keyDown(with: event)
    }

    override func keyUp(with event: NSEvent) {
        if event.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            return
        }
        super.keyUp(with: event)
    }
}
