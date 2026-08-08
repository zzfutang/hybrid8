//
//  R50PitchDetect.hpp
//  Estimates the pitch of an imported sample so its root key does not have to
//  be assumed. Runs on the loader thread, never on the render thread.
//
//  YIN rather than plain autocorrelation. Autocorrelation peaks at every
//  multiple of the period, so on anything harmonically rich it reports an
//  octave — or two — below the truth; YIN's cumulative mean normalisation is
//  what suppresses those, and it is the difference between a detector that
//  works on a sawtooth and one that only works on a sine.
//
//  It lives in C++ rather than in the importer because it is signal processing
//  and because that is where it can be tested. The Swift side calls through the
//  adapter.
//

#pragma once

#include <cmath>
#include <vector>

namespace r50 {

struct DetectedPitch {
    double hertz      = 0.0;
    int    rootKey    = 60;
    /// How sharp the *recording* is against the nearest semitone. It is not a
    /// tuning correction and must be negated to become one — a sample 30 cents
    /// sharp needs playback pulled 30 cents down. Named for what it measures so
    /// that the sign cannot be quietly ignored at the call site.
    float  centsSharp = 0.0f;
    float  confidence = 0.0f;   // 0..1; low means the material is not pitched
    bool   valid      = false;
};

/// `confidence` above about 0.5 means the material had a period worth trusting.
/// Noise and unpitched percussion come back invalid or barely confident, which
/// is the point: guessing a root for a hi-hat is worse than declining to.
inline DetectedPitch detectPitch(const float *samples, int count,
                                 double sampleRate) {
    DetectedPitch result;
    if (samples == nullptr || count <= 0 || sampleRate <= 0.0) return result;

    // Analyse past the attack. A transient is not periodic, and starting at
    // sample zero measures the strike rather than the note behind it.
    const int start = std::min(count / 5, static_cast<int>(0.05 * sampleRate));
    const int window = std::min(4096, count - start);
    if (window <= 512) return result;

    // Headroom above the 2 kHz a sample is realistically recorded at, so a
    // high root is not sitting on the edge of the search.
    const int minLag = std::max(2, static_cast<int>(sampleRate / 2500.0));
    const int maxLag = std::min(window / 2, static_cast<int>(sampleRate / 40.0));
    if (maxLag <= minLag + 1) return result;

    // Accumulated from lag 1, searched from minLag. Starting the cumulative
    // mean at minLag makes normalised[minLag] identically 1.0 — the value is
    // its own running mean — so a period landing near the top of the range can
    // never dip below the threshold and the octave below it gets picked
    // instead. A 1975 Hz sample, inside the advertised range, detected an
    // octave low for exactly that reason.
    std::vector<double> normalised(maxLag + 1, 1.0);
    double running = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (int n = 0; n < window - lag; ++n) {
            const double d = static_cast<double>(samples[start + n])
                           - static_cast<double>(samples[start + n + lag]);
            sum += d * d;
        }
        running += sum;
        normalised[lag] = running > 0.0 ? sum * lag / running : 1.0;
    }

    // The *first* dip below the threshold, not the global minimum: the global
    // one frequently sits at a multiple of the true period, which is exactly
    // the octave error this algorithm exists to avoid.
    const double threshold = 0.15;
    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        if (normalised[lag] >= threshold) continue;
        int candidate = lag;
        while (candidate + 1 <= maxLag
            && normalised[candidate + 1] < normalised[candidate]) {
            ++candidate;
        }
        best = candidate;
        break;
    }
    if (best < 0) {
        double lowest = 1e30;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            if (normalised[lag] < lowest) { lowest = normalised[lag]; best = lag; }
        }
    }
    // best - 1 is a valid index for any best >= 2, now that the cumulative
    // mean is accumulated from lag 1 rather than from minLag.
    if (best < 2 || best >= maxLag) return result;

    // Parabolic interpolation. A period is rarely a whole number of samples,
    // and rounding it costs tens of cents on a high note.
    const double a = normalised[best - 1];
    const double b = normalised[best];
    const double c = normalised[best + 1];
    const double denominator = 2.0 * (2.0 * b - a - c);
    const double offset = denominator != 0.0 ? (c - a) / denominator : 0.0;
    const double period = best + offset;
    if (period <= 0.0) return result;

    const double hertz = sampleRate / period;
    if (!std::isfinite(hertz) || hertz <= 20.0 || hertz >= 5000.0) return result;

    const double midi = 69.0 + 12.0 * std::log2(hertz / 440.0);
    const int rootKey = static_cast<int>(std::lround(midi));
    if (rootKey < 0 || rootKey > 127) return result;

    result.hertz      = hertz;
    result.rootKey    = rootKey;
    result.centsSharp = static_cast<float>((midi - rootKey) * 100.0);
    result.confidence = static_cast<float>(std::max(0.0, 1.0 - normalised[best]));
    result.valid      = true;
    return result;
}

} // namespace r50
