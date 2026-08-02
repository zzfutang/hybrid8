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
//  It is an event monitor, not a first-responder view. The old design made a
//  key view first responder and fought to keep it — reclaiming focus on window
//  activation, after every click, after the file panel — and any control that
//  still held focus when a key came down let the event fall through to the
//  macOS alert sound. A process-local monitor sees the extension's key events
//  no matter which view has focus, and swallows what the instrument does not
//  use, so there is no beep to lose a fight to.
//

import AppKit
import CoreAudioKit
import SwiftUI

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory,
                                      PerformanceEventSink {

    var audioUnit: AUAudioUnit?

    private var typingKeyboard: MusicalTypingController?

    // Musical typing is only for our own standalone container; in a DAW the
    // host owns the computer keyboard. Frontmost-app identity is the primary
    // signal — it needs no handshake — and the standalone's distributed
    // announcement remains as a fallback in case the sandbox ever hides
    // NSWorkspace's view of the frontmost application.
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
        if NSWorkspace.shared.frontmostApplication?.bundleIdentifier == container {
            return true
        }
        return standaloneAnnounced && NSApp.isActive
    }

    public override func loadView() {
        self.view = NSView(frame: NSRect(x: 0, y: 0, width: R50Layout.width,
                                         height: R50Layout.height))
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
        typingKeyboard = MusicalTypingController(
            sink: self,
            isActive: { [weak self] in self?.hostedInOwnStandalone ?? false },
            stateChanged: { _ in })
        if audioUnit != nil { setupUI() }
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
    }

    func sendPerformanceEvent(_ event: PerformanceMIDIEvent) {
        sendMIDI(event.bytes)
    }

    private func sendMIDI(_ bytes: [UInt8]) {
        guard let block = audioUnit?.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return }
            block(AUEventSampleTimeImmediate, 0, bytes.count, base)
        }
    }
}
