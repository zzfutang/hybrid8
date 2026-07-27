//
//  AudioUnitViewController.swift
//  Principal class of the R50 AU extension. Implements AUAudioUnitFactory
//  (creates the R50AudioUnit) and hosts the SwiftUI editor view.
//
//  Musical typing lives here (not just in the standalone app) because the AUv3
//  editor runs in a separate view-service process: key events routed to the
//  remote view never reach the host app's event monitor, so the responder that
//  turns the computer keyboard into notes has to be inside the extension.
//

import AppKit
import CoreAudioKit
import SwiftUI

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {

    var audioUnit: AUAudioUnit?

    // Musical typing is only active inside our own standalone host (which
    // announces itself); in a third-party DAW the host owns the keyboard.
    private var standaloneMusicalTyping = false
    private let typing = MusicalTypingStateMachine()
    private var clickMonitor: Any?

    public override func loadView() {
        let keyView = MusicalTypingView(
            frame: NSRect(x: 0, y: 0, width: R50Layout.width, height: R50Layout.height))
        keyView.keyHandler = { [weak self] event, down in
            self?.handleMusicalTyping(event, down: down) ?? false
        }
        self.view = keyView
        self.preferredContentSize = NSSize(width: R50Layout.width,
                                           height: R50Layout.height)
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        DistributedNotificationCenter.default().addObserver(
            forName: Notification.Name("com.johangorsjo.R50.standaloneActive"),
            object: nil, queue: .main) { [weak self] _ in
                self?.standaloneMusicalTyping = true
                // The notification and viewDidAppear race; whichever lands
                // second grabs first responder so typing works from launch.
                self?.grabKeyboardFocus()
            }

        // Anything that takes first responder away — the sample file panel,
        // switching apps, clicking a control — leaves keys falling through to
        // super, where macOS answers every keystroke with the alert sound.
        // Reclaiming it whenever our window becomes key covers all of those.
        NotificationCenter.default.addObserver(
            forName: NSWindow.didBecomeKeyNotification,
            object: nil, queue: .main) { [weak self] notification in
                guard let self,
                      let window = notification.object as? NSWindow,
                      window === self.view.window else { return }
                self.grabKeyboardFocus()
            }

        // A click inside the editor can leave focus on whichever control was
        // hit; take it back once the click finishes.
        clickMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseUp]) {
            [weak self] event in
            if let self, event.window === self.view.window {
                DispatchQueue.main.async { self.grabKeyboardFocus() }
            }
            return event
        }

        if audioUnit != nil { setupUI() }
    }

    deinit {
        if let clickMonitor { NSEvent.removeMonitor(clickMonitor) }
    }

    public override func viewDidAppear() {
        super.viewDidAppear()
        grabKeyboardFocus()
    }

    /// Make the key view first responder so keys reach `handleMusicalTyping`.
    /// Only acts in our standalone host so a third-party DAW keeps control of
    /// the keyboard.
    private func grabKeyboardFocus() {
        guard standaloneMusicalTyping else { return }
        view.window?.makeFirstResponder(view)
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
                                      presetApplier: { r50.applyFactoryPreset($0) },
                                      audioUnit: r50)
        let samples = R50SampleStore(model: model, audioUnit: r50)
        let host = NSHostingView(rootView: R50View(model: model, samples: samples))
        host.frame = view.bounds
        host.autoresizingMask = [.width, .height]
        view.addSubview(host)
        grabKeyboardFocus()
    }

    /// Returns true when the event was consumed (so the view must not fall
    /// through to `super`, which would play the macOS alert sound).
    private func handleMusicalTyping(_ event: NSEvent, down: Bool) -> Bool {
        guard standaloneMusicalTyping,
              event.modifierFlags.intersection([.command, .control, .option]).isEmpty,
              let character = event.charactersIgnoringModifiers?.lowercased().first
        else { return false }

        let (_, output) = typing.handle(
            character: character, isDown: down, isRepeat: event.isARepeat)
        dispatch(output)
        return true
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
