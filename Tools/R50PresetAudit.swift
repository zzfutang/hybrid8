#!/usr/bin/env swift
//
// Render every installed R50 factory preset through the real AUv3 and write a
// compact CSV of objective health/spectral measurements beside the WAV files.
//

import Accelerate
import AVFoundation
import AudioToolbox
import Foundation

private let sampleRate = 48_000.0
private let noteSeconds = 1.4
private let tailSeconds = 2.0
private let frameCount = AVAudioFrameCount(512)

private func fourCC(_ text: String) -> OSType {
    text.utf8.reduce(0) { ($0 << 8) | OSType($1) }
}

private func midi(_ unit: AUAudioUnit, _ bytes: [UInt8]) {
    guard let block = unit.scheduleMIDIEventBlock else { return }
    bytes.withUnsafeBufferPointer {
        block(AUEventSampleTimeImmediate, 0, $0.count, $0.baseAddress!)
    }
}

private func safeName(_ name: String, _ index: Int) -> String {
    let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_"))
    let body = name.unicodeScalars.map { allowed.contains($0) ? Character(String($0)) : "_" }
    return String(format: "%03d_", index + 1) + String(body)
}

private struct Metrics {
    var peak = 0.0
    var sumSquares = 0.0
    var sum = 0.0
    var maxStep = 0.0
    var clipped = 0
    var samples = 0
    var previous = 0.0
    var analysis: [Double] = []

    mutating func add(_ value: Double, capture: Bool) {
        peak = max(peak, abs(value))
        sumSquares += value * value
        sum += value
        if samples > 0 { maxStep = max(maxStep, abs(value - previous)) }
        if abs(value) >= 0.999 { clipped += 1 }
        previous = value
        samples += 1
        if capture && analysis.count < 8192 { analysis.append(value) }
    }
}

private func spectralMetrics(_ source: [Double]) -> (Double, Double, Double) {
    let n = 8192
    guard source.count >= n else { return (0, 0, 0) }
    var real = Array(source.prefix(n))
    for i in 0..<n {
        real[i] *= 0.5 - 0.5 * cos(2.0 * .pi * Double(i) / Double(n - 1))
    }
    var imag = [Double](repeating: 0, count: n)
    let log2n = vDSP_Length(log2(Double(n)))
    guard let setup = vDSP_create_fftsetupD(log2n, FFTRadix(kFFTRadix2)) else {
        return (0, 0, 0)
    }
    defer { vDSP_destroy_fftsetupD(setup) }
    real.withUnsafeMutableBufferPointer { realBuffer in
        imag.withUnsafeMutableBufferPointer { imagBuffer in
            var split = DSPDoubleSplitComplex(
                realp: realBuffer.baseAddress!, imagp: imagBuffer.baseAddress!)
            vDSP_fft_zipD(setup, &split, 1, log2n, FFTDirection(FFT_FORWARD))
        }
    }

    var total = 0.0, high = 0.0, ultra = 0.0, logSum = 0.0
    let nyquistBin = n / 2
    for bin in 1..<nyquistBin {
        let power = real[bin] * real[bin] + imag[bin] * imag[bin] + 1e-24
        let frequency = Double(bin) * sampleRate / Double(n)
        total += power
        if frequency >= 10_000 { high += power }
        if frequency >= 16_000 { ultra += power }
        logSum += log(power)
    }
    let arithmetic = total / Double(nyquistBin - 1)
    let flatness = exp(logSum / Double(nyquistBin - 1)) / max(arithmetic, 1e-24)
    return (high / max(total, 1e-24), ultra / max(total, 1e-24), flatness)
}

private func instantiate(_ description: AudioComponentDescription) throws -> AVAudioUnit {
    var result: AVAudioUnit?
    var failure: Error?
    AVAudioUnit.instantiate(with: description, options: [.loadOutOfProcess]) {
        unit, error in
        result = unit
        failure = error
    }
    let deadline = Date().addingTimeInterval(20)
    while result == nil && failure == nil && Date() < deadline {
        RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
    }
    if let failure { throw failure }
    guard let result else {
        throw NSError(domain: "R50PresetAudit", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "R50 AUv3 was not found"])
    }
    return result
}

let outputURL = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first
    ?? "build/preset-audit", isDirectory: true)
try FileManager.default.createDirectory(at: outputURL,
                                        withIntermediateDirectories: true)

let description = AudioComponentDescription(
    componentType: kAudioUnitType_MusicDevice,
    componentSubType: fourCC("R50v"),
    componentManufacturer: fourCC("Jhgn"),
    componentFlags: 0,
    componentFlagsMask: 0)
let instrument = try instantiate(description)
let audioUnit = instrument.auAudioUnit
let presets = audioUnit.factoryPresets ?? []
guard presets.count == 100 else {
    fatalError("Expected 100 R50 factory presets, found \(presets.count)")
}

let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
let engine = AVAudioEngine()
engine.attach(instrument)
engine.connect(instrument, to: engine.mainMixerNode, format: format)
try engine.enableManualRenderingMode(.offline, format: format,
                                     maximumFrameCount: frameCount)
try engine.start()

var csv = "index,name,peak,rms,dc,max_step,clip_percent,hf10k_percent,hf16k_percent,flatness,status\n"
var failures = 0

for (index, preset) in presets.enumerated() {
    audioUnit.currentPreset = preset
    engine.reset()
    if !engine.isRunning { try engine.start() }

    let path = outputURL.appendingPathComponent(safeName(preset.name, index) + ".wav")
    let file = try AVAudioFile(forWriting: path, settings: [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVSampleRateKey: sampleRate,
        AVNumberOfChannelsKey: 2,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsFloatKey: false,
        AVLinearPCMIsBigEndianKey: false
    ])

    for note in [48, 60, 64, 67] { midi(audioUnit, [0x90, UInt8(note), 104]) }
    var metrics = Metrics()
    let noteFrames = Int(noteSeconds * sampleRate)
    let totalFrames = Int((noteSeconds + tailSeconds) * sampleRate)
    var rendered = 0

    while rendered < totalFrames {
        if rendered >= noteFrames && rendered - Int(frameCount) < noteFrames {
            for note in [48, 60, 64, 67] { midi(audioUnit, [0x80, UInt8(note), 0]) }
        }
        let wanted = min(Int(frameCount), totalFrames - rendered)
        let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                      frameCapacity: AVAudioFrameCount(wanted))!
        let status = try engine.renderOffline(AVAudioFrameCount(wanted), to: buffer)
        guard status == .success else { continue }
        try file.write(from: buffer)
        let capture = rendered >= Int(0.55 * sampleRate)
            && rendered < Int(0.55 * sampleRate) + 8192
        if let channels = buffer.floatChannelData {
            for frame in 0..<Int(buffer.frameLength) {
                let mono = 0.5 * (Double(channels[0][frame]) + Double(channels[1][frame]))
                metrics.add(mono, capture: capture)
            }
        }
        rendered += Int(buffer.frameLength)
    }

    midi(audioUnit, [0xB0, 123, 0])
    let rms = sqrt(metrics.sumSquares / Double(max(metrics.samples, 1)))
    let dc = abs(metrics.sum / Double(max(metrics.samples, 1)))
    let clipPercent = 100 * Double(metrics.clipped) / Double(max(metrics.samples, 1))
    let (hf, ultra, flatness) = spectralMetrics(metrics.analysis)
    var problems: [String] = []
    if metrics.peak < 0.003 { problems.append("silent") }
    if metrics.peak > 1.02 || clipPercent > 0.02 { problems.append("clipping") }
    if dc > 0.01 { problems.append("dc") }
    if metrics.maxStep > max(0.35, rms * 12) { problems.append("discontinuity") }
    if ultra > 0.10 { problems.append("ultrasonic") }
    if preset.name.hasPrefix("REED") || preset.name.hasPrefix("WIND") {
        if hf > 0.18 { problems.append("reed_hf") }
        if flatness > 0.45 { problems.append("reed_noisy") }
    }
    let status = problems.isEmpty ? "PASS" : problems.joined(separator: "+")
    if !problems.isEmpty { failures += 1 }
    let escapedName = "\"\(preset.name.replacingOccurrences(of: "\"", with: "\"\""))\""
    csv += String(format: "%d,%@,%.6f,%.6f,%.6f,%.6f,%.5f,%.4f,%.4f,%.4f,%@\n",
                  index, escapedName, metrics.peak, rms, dc, metrics.maxStep,
                  clipPercent, hf * 100, ultra * 100, flatness, status)
    print(String(format: "[%3d/100] %-30@ peak %.3f  HF %.1f%%  %@",
                 index + 1, preset.name as NSString, metrics.peak, hf * 100, status))
}

try csv.write(to: outputURL.appendingPathComponent("report.csv"),
              atomically: true, encoding: .utf8)
print("\nRendered \(presets.count) presets to \(outputURL.path)")
print("Flagged \(failures) presets; measurements are in report.csv")
