//
//  AudioUnitViewController.swift
//  Principal class of the R50 AU extension. Implements AUAudioUnitFactory
//  (creates the R50AudioUnit) and hosts the SwiftUI editor view.
//
//  Musical typing lives here (not just in the standalone app) because the
//  AUv3 editor runs in a separate view-service process. Two facts shape the
//  implementation, both established empirically:
//
//  - NSEvent local monitors never fire for key events in the view service:
//    viewbridge hands keys straight to the responder chain, bypassing
//    NSApplication's sendEvent. So interception happens in the RESPONDER
//    CHAIN: the container view under the SwiftUI hierarchy sees every key
//    that no control consumed, regardless of which control has focus, and
//    swallowing there is what prevents the macOS alert beep at the window.
//
//  - Key events reach the host app's process only while focus is on the
//    host's own chrome; the host's monitor covers that side.
//

import AppKit
import CoreAudioKit
import SwiftUI

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {

    var audioUnit: AUAudioUnit?

    private let typing = MusicalTypingStateMachine()

    // Musical typing is only for our own standalone container; in a DAW the
    // host owns the computer keyboard. Frontmost-app identity is the primary
    // signal — it needs no handshake — and the standalone's distributed
    // announcement remains as a fallback.
    private var standaloneAnnounced = false

    /// The container's identifier is the extension's own with the trailing
    /// component dropped (com.johangorsjo.R50.AUv3 -> com.johangorsjo.R50).
    private static let containerBundleID: String? = {
        guard let id = Bundle.main.bundleIdentifier,
              let dot = id.lastIndex(of: ".") else { return nil }
        return String(id[..<dot])
    }()

    private var hostedInOwnStandalone: Bool {
        guard let container = Self.containerBundleID else { return false }
        return NSWorkspace.shared.frontmostApplication?.bundleIdentifier
            == container || standaloneAnnounced
    }

    public override func loadView() {
        let view = KeyInterceptView(
            frame: NSRect(x: 0, y: 0, width: R50Layout.width,
                          height: R50Layout.height))
        view.keyHandler = { [weak self] event, down in
            self?.handleKey(event, down: down) ?? false
        }
        self.view = view
        self.preferredContentSize = NSSize(width: R50Layout.width,
                                           height: R50Layout.height)
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        DistributedNotificationCenter.default().addObserver(
            forName: Notification.Name("com.johangorsjo.R50.standaloneActive"),
            object: nil, queue: .main) { [weak self] _ in
                self?.standaloneAnnounced = true
            }

        // AppKit resets the remote window's responder to its own container
        // view at will (window updates, panel dismissals), which takes our
        // interceptor out of the key path again. didUpdate fires on every
        // window pass; the park is a cheap no-op while focus is ours.
        NotificationCenter.default.addObserver(
            forName: NSWindow.didUpdateNotification,
            object: nil, queue: .main) { [weak self] notification in
                guard let self,
                      let window = notification.object as? NSWindow,
                      window === self.view.window else { return }
                self.parkKeyboardFocus("windowDidUpdate")
            }

        if audioUnit != nil { setupUI() }
    }

    public override func viewDidAppear() {
        super.viewDidAppear()
        parkKeyboardFocus("viewDidAppear")
    }

    /// The remote window's forwarded key events start at its first responder
    /// and bubble UP. On arrival that responder is Apple's
    /// FlippedAUContainerView — our superview — so the chain skips our views
    /// entirely and the window answers every key with the alert beep. The
    /// interceptor is only in the path while first responder is our container
    /// or something inside it, so park it there whenever focus rests
    /// anywhere else that is not actively editing text.
    private func parkKeyboardFocus(_ reason: String) {
        guard let window = view.window else { return }
        let responder = window.firstResponder
        let ours = (responder as? NSView).map {
            $0 === view || $0.isDescendant(of: view)
        } ?? false
        if ours || responder is NSTextView { return }
        window.makeFirstResponder(view)
    }

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try R50AudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        DispatchQueue.main.async { [weak self] in
            if self?.isViewLoaded == true { self?.setupUI() }
        }
        return au
    }

    private func setupUI() {
        guard let r50 = audioUnit as? R50AudioUnit,
              let tree = r50.parameterTree else { return }
        view.subviews.forEach { $0.removeFromSuperview() }
        let model = R50ParameterModel(tree: tree,
                                      meterProvider: { r50.outputMeter() },
                                      headroomProvider: { r50.headroomPeak() },
                                      presetApplier: { r50.applyFactoryPreset($0) },
                                      audioUnit: r50)
        let samples = R50SampleStore(model: model, audioUnit: r50)
        let host = NSHostingView(rootView: R50View(model: model, samples: samples))
        host.frame = view.bounds
        host.autoresizingMask = [.width, .height]
        view.addSubview(host)
        parkKeyboardFocus("setupUI")
    }

    /// Returns true when the event was consumed. Consuming is what prevents
    /// the beep, so in our own standalone every plain key-down is consumed
    /// whether or not it maps to a note; a DAW keeps its keyboard untouched.
    private func handleKey(_ event: NSEvent, down: Bool) -> Bool {
        guard hostedInOwnStandalone else { return false }
        if !event.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            return false
        }
        // A text field's editor consumes its keys before they can bubble
        // here; anything that still arrives while editing is not ours.
        if event.window?.firstResponder is NSTextView { return false }

        guard let character = event.charactersIgnoringModifiers?
            .lowercased().first else { return down }
        let (handled, output) = typing.handle(
            character: character, isDown: down, isRepeat: event.isARepeat)
        if handled { dispatch(output) }
        return down ? true : handled
    }

    private func dispatch(_ output: MusicalTypingStateMachine.Output?) {
        switch output {
        case let .noteOn(note, velocity): sendMIDI([0x90, note, velocity])
        case let .noteOff(note): sendMIDI([0x80, note, 0])
        case nil: break
        }
    }

    private func sendMIDI(_ bytes: [UInt8]) {
        guard let block = audioUnit?.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return }
            block(AUEventSampleTimeImmediate, 0, bytes.count, base)
        }
    }
}

/// The container under the SwiftUI hierarchy. Keys bubble up the responder
/// chain into keyDown/keyUp here whenever no control consumed them; the
/// handler decides whether they become notes, are swallowed, or fall through
/// to super (which is where the system beep lives).
private final class KeyInterceptView: NSView {
    var keyHandler: ((NSEvent, Bool) -> Bool)?
    override var acceptsFirstResponder: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window else { return }
        let responder = window.firstResponder
        let ours = (responder as? NSView).map {
            $0 === self || $0.isDescendant(of: self)
        } ?? false
        if !ours && !(responder is NSTextView) {
            window.makeFirstResponder(self)
        }
    }

    override func keyDown(with event: NSEvent) {
        if keyHandler?(event, true) != true { super.keyDown(with: event) }
    }

    override func keyUp(with event: NSEvent) {
        if keyHandler?(event, false) != true { super.keyUp(with: event) }
    }
}
