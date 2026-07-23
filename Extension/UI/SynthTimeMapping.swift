//
//  SynthTimeMapping.swift
//  Swift mirror of the C++ envelope-time mapping (Utils.hpp / SynthParameters.h).
//  Used to display seconds for the normalised 0..1 time parameters, to seed the
//  parameter-tree defaults, and to store presets in readable seconds.
//

import AudioToolbox
import Foundation

enum SynthTime {
    private static let tMin  = Float(SYNTH_TIME_MIN)
    private static let tMax  = Float(SYNTH_TIME_MAX)
    private static let skew  = Float(SYNTH_TIME_SKEW)
    private static let ratio = Float(SYNTH_TIME_MAX / SYNTH_TIME_MIN)

    static func seconds(fromNorm n: Float) -> Float {
        let nn = min(max(n, 0), 1)
        let e = powf(nn, skew)
        return tMin * powf(ratio, e)
    }

    static func norm(fromSeconds s: Float) -> Float {
        let ss = min(max(s, tMin), tMax)
        let e = logf(ss / tMin) / logf(ratio)
        return powf(max(0, e), 1.0 / skew)
    }

    /// The six envelope-time parameter addresses (normalised 0..1 in the tree).
    static let addresses: Set<AUParameterAddress> = [
        AUParameterAddress(SynthParamAmpAttack.rawValue),
        AUParameterAddress(SynthParamAmpDecay.rawValue),
        AUParameterAddress(SynthParamAmpRelease.rawValue),
        AUParameterAddress(SynthParamFilterAttack.rawValue),
        AUParameterAddress(SynthParamFilterDecay.rawValue),
        AUParameterAddress(SynthParamFilterRelease.rawValue),
    ]

    static func isTime(_ a: AUParameterAddress) -> Bool { addresses.contains(a) }

    /// Human-readable seconds string for a normalised value.
    static func displayString(fromNorm n: Float) -> String {
        let s = seconds(fromNorm: n)
        if s >= 10 { return String(format: "%.1fs", s) }
        if s >= 1  { return String(format: "%.2fs", s) }
        return String(format: "%.3fs", s)
    }
}
