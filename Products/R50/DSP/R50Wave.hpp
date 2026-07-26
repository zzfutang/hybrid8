//
//  R50Wave.hpp
//  Band-limited single-cycle wave tables and the analytic spectra that generate
//  them.
//
//  R50 has no PolyBLEP oscillator, so nothing else is band-limiting the output.
//  A single-cycle table played back at an arbitrary rate is not band-limited on
//  its own: a 1024-point table holds harmonics up to the 511th, and transposing
//  it two octaves up folds everything above the 250th back down as inharmonic
//  aliasing. The fix is a mip pyramid — one table per octave, each holding only
//  the harmonics that still fit below 20 kHz at that octave — with the two
//  adjacent levels crossfaded so pitch movement across an octave boundary does
//  not step audibly.
//
//  The mip machinery follows Hybrid8Wavetable.hpp, which has this working
//  already; the frame/variant/liveness/import machinery it also carries is not
//  needed here and is deliberately left behind. Two deliberate deviations:
//  every level length is a power of two so reads mask the index instead of
//  relying on a guard sample, and interpolation is 4-point cubic rather than
//  linear — at the top levels a table is only 64 points long and linear
//  interpolation there is audible.
//
//  Everything in this file is offline: pyramids are built once, before any
//  audio runs, and are immutable afterwards. Nothing here may be called from
//  the render thread.
//

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "R50Levels.hpp"
#include "Utils.hpp"

namespace r50 {

// ---- Geometry -------------------------------------------------------------

static constexpr int    kWaveBaseLen    = 1024;    // richest level (<=511 harmonics)
static constexpr int    kWaveMinLen     = 64;
static constexpr int    kWaveNumLevels  = 10;      // one per octave from 20 Hz
static constexpr double kWaveFMin       = 20.0;
static constexpr double kWaveAudibleMax = 20000.0;
static constexpr int    kWaveMaxHarm    = kWaveBaseLen / 2 - 1;

struct WaveMip {
    std::vector<float> samples;
    int length = 0;
    int mask   = 0;      // length - 1; length is always a power of two
};

struct WavePyramid {
    std::array<WaveMip, kWaveNumLevels> levels;
};

/// Highest harmonic that still lands below 20 kHz when a fundamental at the
/// top of this level's octave is played.
inline int waveMaxHarmonic(int level) {
    const double fTop = kWaveFMin * std::pow(2.0, level + 1);
    const int k = static_cast<int>(std::floor(kWaveAudibleMax / fTop));
    return std::max(1, std::min(k, kWaveMaxHarm));
}

/// Fractional mip level for a fundamental. Each level spans one octave; the
/// fraction drives the crossfade between adjacent levels.
inline float waveLevelForFreq(double f0) {
    if (f0 <= kWaveFMin) return 0.0f;
    float level = static_cast<float>(std::log2(f0 / kWaveFMin));
    if (level < 0.0f) level = 0.0f;
    if (level > static_cast<float>(kWaveNumLevels - 1))
        level = static_cast<float>(kWaveNumLevels - 1);
    return level;
}

inline int waveNextPow2(int x) {
    int p = kWaveMinLen;
    while (p < x) p <<= 1;
    return p;
}

// ---- Spectra --------------------------------------------------------------

/// Magnitude and phase per harmonic; index k is the harmonic number, so [0] is
/// unused (a single-cycle wave carries no DC).
struct WaveSpectrum {
    std::vector<float> mag;
    std::vector<float> phase;

    WaveSpectrum() : mag(kWaveMaxHarm + 1, 0.0f), phase(kWaveMaxHarm + 1, 0.0f) {}
};

/// Deterministic pseudo-random phase for harmonic k. Used by the metallic
/// spectra, where aligned phases would produce an unnaturally peaky waveform
/// and cost headroom after normalisation.
inline float waveScatterPhase(int k, int seed) {
    uint32_t h = static_cast<uint32_t>(k * 2654435761u)
               ^ static_cast<uint32_t>(seed * 40503u + 12345u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return static_cast<float>((h & 0xffffffu) / static_cast<double>(0x1000000)
                              * synth::kTwoPi);
}

// Sine phase: buildMip sums cos(phase + k*w*n), so -pi/2 yields sin().
static const float kSinePhase = -static_cast<float>(synth::kPi) * 0.5f;

// ---- Pyramid construction -------------------------------------------------

/// Additive synthesis of one level, using a per-harmonic phasor recurrence so
/// there is no trig call in the inner loop. normFactor of 0 leaves the level
/// unnormalised (used for level 0, whose peak is measured first).
inline WaveMip waveBuildMip(const WaveSpectrum &spectrum, int level,
                            float normFactor) {
    const int maxHarmonic = waveMaxHarmonic(level);
    const int length = std::min(kWaveBaseLen,
                                std::max(kWaveMinLen,
                                         waveNextPow2(2 * maxHarmonic)));

    WaveMip mip;
    mip.length = length;
    mip.mask   = length - 1;
    mip.samples.assign(length, 0.0f);

    const int highest = std::min(maxHarmonic,
                                 static_cast<int>(spectrum.mag.size()) - 1);
    for (int k = 1; k <= highest; ++k) {
        const float magnitude = spectrum.mag[k];
        if (magnitude == 0.0f) continue;

        const double w  = synth::kTwoPi * k / length;
        double real = std::cos(spectrum.phase[k]);
        double imag = std::sin(spectrum.phase[k]);
        const double cw = std::cos(w), sw = std::sin(w);

        for (int n = 0; n < length; ++n) {
            mip.samples[n] += static_cast<float>(magnitude * real);
            const double nextReal = real * cw - imag * sw;
            const double nextImag = real * sw + imag * cw;
            real = nextReal;
            imag = nextImag;
        }
    }

    if (normFactor > 0.0f) {
        for (float &sample : mip.samples) sample /= normFactor;
    }
    return mip;
}

/// Build every level, normalising all of them by the level-0 peak so loudness
/// does not jump as pitch crosses an octave boundary.
inline WavePyramid waveBuildPyramid(const WaveSpectrum &spectrum) {
    WavePyramid pyramid;
    pyramid.levels[0] = waveBuildMip(spectrum, 0, 0.0f);

    double sumSquares = 0.0;
    float peak = 1e-9f;
    for (float sample : pyramid.levels[0].samples) {
        sumSquares += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::fabs(sample));
    }
    const float rms = static_cast<float>(
        std::sqrt(sumSquares / std::max<size_t>(1, pyramid.levels[0].samples.size())));

    float scale = kSourceTargetRms / std::max(rms, 1e-9f);
    if (peak * scale > kSourcePeakCeiling) scale = kSourcePeakCeiling / peak;
    const float normFactor = 1.0f / scale;

    for (float &sample : pyramid.levels[0].samples) sample /= normFactor;
    for (int level = 1; level < kWaveNumLevels; ++level)
        pyramid.levels[level] = waveBuildMip(spectrum, level, normFactor);

    return pyramid;
}

// ---- Reading --------------------------------------------------------------

/// 4-point Catmull-Rom read. Index arithmetic masks rather than wrapping, which
/// is why every level length is a power of two.
inline float waveSampleMip(const WaveMip &mip, double phase) {
    const double position = phase * mip.length;
    int i1 = static_cast<int>(position);
    const float fraction = static_cast<float>(position - i1);

    const int i0 = (i1 - 1) & mip.mask;
    const int i2 = (i1 + 1) & mip.mask;
    const int i3 = (i1 + 2) & mip.mask;
    i1 &= mip.mask;

    const float y0 = mip.samples[i0], y1 = mip.samples[i1];
    const float y2 = mip.samples[i2], y3 = mip.samples[i3];

    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

/// Read with the two adjacent mip levels crossfaded.
inline float waveSample(const WavePyramid &pyramid, float levelF, double phase) {
    int lower = static_cast<int>(levelF);
    if (lower < 0) lower = 0;
    if (lower > kWaveNumLevels - 1) lower = kWaveNumLevels - 1;
    const int upper = std::min(lower + 1, kWaveNumLevels - 1);
    const float blend = levelF - lower;

    const float a = waveSampleMip(pyramid.levels[lower], phase);
    if (upper == lower || blend <= 0.0f) return a;
    const float b = waveSampleMip(pyramid.levels[upper], phase);
    return a + blend * (b - a);
}

// ---- Spectrum generators --------------------------------------------------
//
// All spectra are analytic. An analytic spectrum produces a cleaner pyramid
// than an extracted one, because every level is resynthesised from exact
// harmonic amplitudes rather than resampled from a fixed table.

inline WaveSpectrum waveSpectrumSaw() {
    WaveSpectrum s;
    for (int k = 1; k <= kWaveMaxHarm; ++k) {
        s.mag[k]   = 1.0f / k;
        s.phase[k] = kSinePhase;
    }
    return s;
}

inline WaveSpectrum waveSpectrumTriangle() {
    WaveSpectrum s;
    for (int k = 1; k <= kWaveMaxHarm; k += 2) {
        s.mag[k] = 1.0f / static_cast<float>(k * k);
        // Alternating sign, folded into the phase: cos(x + pi/2) == -sin(x).
        const bool negative = (((k - 1) / 2) & 1) != 0;
        s.phase[k] = negative ? -kSinePhase : kSinePhase;
    }
    return s;
}

/// Drawbar-style: fundamental plus octaves, with a little 3rd and 6th for
/// warmth. No harmonics above the 8th, which is what makes it read as "organ"
/// rather than "filtered saw".
inline WaveSpectrum waveSpectrumOrgan() {
    WaveSpectrum s;
    const struct { int k; float m; } bars[] = {
        {1, 1.00f}, {2, 0.80f}, {3, 0.35f}, {4, 0.50f},
        {6, 0.20f}, {8, 0.30f}
    };
    for (const auto &bar : bars) {
        s.mag[bar.k]   = bar.m;
        s.phase[bar.k] = kSinePhase;
    }
    return s;
}

/// Electric-piano tine: strong fundamental, a hollow gap, then a bright cluster
/// around the 4th-5th that gives the bell-like attack colour.
inline WaveSpectrum waveSpectrumTine() {
    WaveSpectrum s;
    const struct { int k; float m; } partials[] = {
        {1, 1.00f}, {2, 0.22f}, {3, 0.09f}, {4, 0.45f}, {5, 0.30f},
        {6, 0.08f}, {7, 0.06f}, {9, 0.05f}, {12, 0.04f}, {14, 0.03f}
    };
    for (const auto &partial : partials) {
        s.mag[partial.k]   = partial.m;
        s.phase[partial.k] = kSinePhase;
    }
    return s;
}

/// Odd harmonics only — the closed-pipe spectrum. A small even-harmonic
/// residue keeps it from sounding like a mathematically perfect square.
inline WaveSpectrum waveSpectrumClarinet() {
    WaveSpectrum s;
    for (int k = 1; k <= 48; ++k) {
        const float rolloff = std::exp(-k / 11.0f);
        s.mag[k]   = ((k & 1) ? 1.0f / k : 0.06f / k) * rolloff;
        s.phase[k] = kSinePhase;
    }
    return s;
}

/// Saw-like but with the top rolled off, which is what separates a section of
/// bowed strings from a raw sawtooth.
inline WaveSpectrum waveSpectrumStrings() {
    WaveSpectrum s;
    for (int k = 1; k <= kWaveMaxHarm; ++k) {
        const float rolloff = std::exp(-k / 55.0f);
        // Slight alternation gives the spectrum a little bite.
        const float tilt = (k & 1) ? 1.0f : 0.85f;
        s.mag[k]   = (1.0f / k) * rolloff * tilt;
        s.phase[k] = kSinePhase;
    }
    return s;
}

/// Two formant peaks over a 1/k source, evaluated against a nominal C3
/// fundamental. Because a single-cycle table is pitch-independent, the formants
/// transpose with the note rather than staying at fixed frequencies — inherent
/// to this synthesis method, and characteristic of the instruments it evokes.
inline WaveSpectrum waveSpectrumVocalAh() {
    WaveSpectrum s;
    const double nominalF0 = 130.81;   // C3
    const struct { double hz, bw, gain; } formants[] = {
        { 700.0, 130.0, 1.00 },
        {1200.0, 190.0, 0.72 },
        {2600.0, 320.0, 0.22 }
    };
    for (int k = 1; k <= 90; ++k) {
        const double hz = k * nominalF0;
        // A vowel is a glottal source shaped by tract resonances, so the
        // formants sit on top of a 1/k source floor rather than replacing it.
        // Without the floor the fundamental — which is nowhere near a formant —
        // vanishes entirely and the wave reads as a thin whistle.
        double shaped = 0.18;
        for (const auto &formant : formants) {
            const double x = (hz - formant.hz) / formant.bw;
            shaped += formant.gain * std::exp(-x * x);
        }
        s.mag[k]   = static_cast<float>(shaped / k * 2.0);
        s.phase[k] = kSinePhase;
    }
    return s;
}

/// Metallic: sparse, weighted towards partials that are not simple octaves, with
/// scattered phases. A single-cycle table can only hold harmonic partials, so
/// this approximates an inharmonic bell by choosing an uneven harmonic subset
/// rather than by detuning.
inline WaveSpectrum waveSpectrumBell() {
    WaveSpectrum s;
    const struct { int k; float m; } partials[] = {
        {1, 1.00f}, {2, 0.35f}, {3, 0.55f}, {5, 0.45f}, {7, 0.38f},
        {9, 0.28f}, {11, 0.22f}, {13, 0.18f}, {17, 0.12f}, {21, 0.09f},
        {27, 0.06f}
    };
    for (const auto &partial : partials) {
        s.mag[partial.k]   = partial.m;
        s.phase[partial.k] = waveScatterPhase(partial.k, 7);
    }
    return s;
}

// ---- Library --------------------------------------------------------------

enum WavePyramidId {
    kPyramidSaw = 0,
    kPyramidTriangle,
    kPyramidOrgan,
    kPyramidTine,
    kPyramidClarinet,
    kPyramidStrings,
    kPyramidVocalAh,
    kPyramidBell,
    kPyramidCount
};

/// How a selectable wave reads its pyramid. Difference reads the saw pyramid
/// twice, offset by the pulse width: saw(t) - saw(t - w) is a pulse of width w,
/// band-limited exactly as well as the saw it came from. One pyramid therefore
/// serves saw, square, any fixed pulse width, and continuous PWM.
enum class WaveRead { Single, Difference };

struct WaveDescriptor {
    WavePyramidId pyramid;
    WaveRead      read;
    float         fixedWidth;   // < 0 -> follow the width parameter
};

static constexpr int kWaveCount = 11;

inline const WaveDescriptor *waveDescriptors() {
    static const WaveDescriptor descriptors[kWaveCount] = {
        { kPyramidSaw,      WaveRead::Single,     -1.0f },  //  0 Saw
        { kPyramidTriangle, WaveRead::Single,     -1.0f },  //  1 Triangle
        { kPyramidSaw,      WaveRead::Difference,  0.50f }, //  2 Square
        { kPyramidSaw,      WaveRead::Difference,  0.10f }, //  3 Pulse 10%
        { kPyramidSaw,      WaveRead::Difference, -1.0f },  //  4 Pulse (variable)
        { kPyramidOrgan,    WaveRead::Single,     -1.0f },  //  5 Organ
        { kPyramidTine,     WaveRead::Single,     -1.0f },  //  6 Tine
        { kPyramidClarinet, WaveRead::Single,     -1.0f },  //  7 Clarinet
        { kPyramidStrings,  WaveRead::Single,     -1.0f },  //  8 Strings
        { kPyramidVocalAh,  WaveRead::Single,     -1.0f },  //  9 Vocal Ah
        { kPyramidBell,     WaveRead::Single,     -1.0f },  // 10 Bell
    };
    return descriptors;
}

struct WaveLibrary {
    std::array<WavePyramid, kPyramidCount> pyramids;
};

inline WaveLibrary waveBuildLibrary() {
    WaveLibrary library;
    library.pyramids[kPyramidSaw]      = waveBuildPyramid(waveSpectrumSaw());
    library.pyramids[kPyramidTriangle] = waveBuildPyramid(waveSpectrumTriangle());
    library.pyramids[kPyramidOrgan]    = waveBuildPyramid(waveSpectrumOrgan());
    library.pyramids[kPyramidTine]     = waveBuildPyramid(waveSpectrumTine());
    library.pyramids[kPyramidClarinet] = waveBuildPyramid(waveSpectrumClarinet());
    library.pyramids[kPyramidStrings]  = waveBuildPyramid(waveSpectrumStrings());
    library.pyramids[kPyramidVocalAh]  = waveBuildPyramid(waveSpectrumVocalAh());
    library.pyramids[kPyramidBell]     = waveBuildPyramid(waveSpectrumBell());
    return library;
}

/// The shared, immutable table set. Built once on first call — which the engine
/// forces from its constructor, so the work never lands on the render thread —
/// and shared by every voice and every engine instance in the process.
inline const WaveLibrary &waveLibrary() {
    static const WaveLibrary library = waveBuildLibrary();
    return library;
}

} // namespace r50
