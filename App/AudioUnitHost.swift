//
//  AudioUnitHost.swift
//  Generic standalone AUv3 host. Product identity is injected so another
//  instrument can reuse the engine, CoreMIDI, and performance UI unchanged.
//

import AVFoundation
import AudioToolbox
import CoreAudioKit
import CoreMIDI
import SwiftUI

final class AudioUnitHost: ObservableObject, PerformanceEventSink {
    let product: InstrumentProduct
    private let engine = AVAudioEngine()
    private var avAudioUnit: AVAudioUnit?
    private(set) var auAudioUnit: AUAudioUnit?

    @Published var status: String = "Loading…"
    @Published var midiInfo: String = "MIDI: connecting…"
    @Published var typingInfo: String = ""
    @Published var viewController: NSViewController?

    private var typingKeyboard: MusicalTypingController?

    // CoreMIDI
    private var midiClient = MIDIClientRef()
    private var inputPort = MIDIPortRef()
    private var connectedSources = Set<MIDIEndpointRef>()
    private var runningStatus: UInt8 = 0

    init(product: InstrumentProduct) {
        self.product = product
        load()
        setupMIDI()
        typingKeyboard = MusicalTypingController(sink: self) { [weak self] state in
            DispatchQueue.main.async { self?.typingInfo = state.description }
        }
    }

    private func load() {
        AVAudioUnit.instantiate(with: product.componentDescription, options: []) {
            [weak self] avAU, error in
            guard let self else { return }
            if let error = error {
                self.status = "Load failed: \(error.localizedDescription)"
                return
            }
            guard let avAU = avAU else { self.status = "Load failed (nil AU)"; return }

            self.avAudioUnit = avAU
            self.auAudioUnit = avAU.auAudioUnit

            // Start the test player on a clean, deterministic patch rather than
            // whatever macOS state-restoration may have left behind.
            if let presetNumber = self.product.initialFactoryPreset {
                let initialPreset = AUAudioUnitPreset()
                initialPreset.number = presetNumber
                initialPreset.name = "Init"
                avAU.auAudioUnit.currentPreset = initialPreset
            }

            self.engine.attach(avAU)
            let format = self.engine.mainMixerNode.outputFormat(forBus: 0)
            self.engine.connect(avAU, to: self.engine.mainMixerNode, format: format)

            do {
                try self.engine.start()
                self.status = "Ready — play the keyboard below."
            } catch {
                self.status = "Engine start failed: \(error.localizedDescription)"
            }

            avAU.auAudioUnit.requestViewController { vc in
                DispatchQueue.main.async {
                    self.viewController = vc
                    self.announceStandaloneHost()
                }
            }
        }
    }

    /// Tell the (out-of-process) AUv3 editor that it is running inside our own
    /// standalone host, so it enables computer-keyboard musical typing. Keys
    /// routed to the remote view never reach this process's event handling, so
    /// the typing responder has to live in the extension.
    private func announceStandaloneHost() {
        DistributedNotificationCenter.default().postNotificationName(
            Notification.Name("com.johangorsjo.Hybrid8.standaloneActive"),
            object: nil, userInfo: nil, options: [.deliverImmediately])
    }

    // MARK: - CoreMIDI input (hardware keyboards / controllers)

    private func setupMIDI() {
        let clientStatus = MIDIClientCreateWithBlock(
            "\(product.name) Host" as CFString, &midiClient) { [weak self] _ in
            // Device plugged in / removed: (re)connect any new sources.
            self?.connectSources()
        }
        guard clientStatus == noErr else {
            DispatchQueue.main.async { self.midiInfo = "MIDI: unavailable" }
            return
        }

        MIDIInputPortCreateWithBlock(
            midiClient, "\(product.name) In" as CFString, &inputPort) {
                [weak self] packetList, _ in
            self?.handle(packetList: packetList)
        }
        connectSources()
    }

    private func connectSources() {
        let count = MIDIGetNumberOfSources()
        for i in 0..<count {
            let src = MIDIGetSource(i)
            if src != 0, !connectedSources.contains(src) {
                if MIDIPortConnectSource(inputPort, src, nil) == noErr {
                    connectedSources.insert(src)
                }
            }
        }
        let n = connectedSources.count
        DispatchQueue.main.async {
            self.midiInfo = n == 0 ? "MIDI: no input device"
                                   : "MIDI: \(n) input\(n == 1 ? "" : "s") connected"
        }
    }

    private func handle(packetList: UnsafePointer<MIDIPacketList>) {
        for packet in packetList.unsafeSequence() {
            let length = Int(packet.pointee.length)
            withUnsafeBytes(of: packet.pointee.data) { raw in
                parseMIDIStream(Array(raw.prefix(length)))
            }
        }
    }

    /// Parse a byte stream (which may contain several messages, and may use
    /// running status) and forward channel-voice messages to the synth.
    private func parseMIDIStream(_ bytes: [UInt8]) {
        var i = 0
        while i < bytes.count {
            var status = bytes[i]
            if status & 0x80 != 0 {
                if status >= 0xF8 { i += 1; continue }             // real-time
                if status >= 0xF0 { runningStatus = 0; i += 1; continue } // sysex/common
                runningStatus = status
                i += 1
            } else {
                guard runningStatus != 0 else { i += 1; continue } // running status
                status = runningStatus
            }

            switch status & 0xF0 {
            case 0x80, 0x90, 0xA0, 0xB0, 0xE0:                    // 2 data bytes
                guard i + 1 < bytes.count else { return }
                sendMIDI([status, bytes[i], bytes[i + 1]])
                i += 2
            case 0xC0, 0xD0:                                      // 1 data byte
                guard i < bytes.count else { return }
                sendMIDI([status, bytes[i]])
                i += 1
            default:
                i += 1
            }
        }
    }

    // MARK: - Play

    func sendPerformanceEvent(_ event: PerformanceMIDIEvent) {
        sendMIDI(event.bytes)
    }

    private func sendMIDI(_ bytes: [UInt8]) {
        guard let block = auAudioUnit?.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { ptr in
            guard let base = ptr.baseAddress else { return }
            block(AUEventSampleTimeImmediate, 0, bytes.count, base)
        }
    }
}
