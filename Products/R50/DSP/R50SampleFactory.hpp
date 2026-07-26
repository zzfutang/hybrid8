//
//  R50SampleFactory.hpp
//  Procedurally generated factory content: looped multi-cycle sustains and
//  one-shot attacks. Nothing is bundled or licensed; everything here is
//  synthesised once at startup, off the render thread.
//
//  Making a generated loop seamless:
//
//      pick a loop length L, then place every partial at an integer multiple
//      of sr/L
//
//  Everything is then periodic over exactly L samples, so the loop joins with
//  no discontinuity at all — while partials one or two bins apart still beat
//  against each other with a period of L. That beating is the whole point:
//  it is the thing a single-cycle table cannot do, no matter how it is
//  interpolated.
//
//  Because the partial frequencies are quantised to sr/L, the loop length is
//  chosen to make the wanted fundamental land on a bin almost exactly, rather
//  than the other way round — see chooseLoopLength.
//

#pragma once

#include <cmath>
#include <vector>

#include "Filter.hpp"
#include "R50Sample.hpp"
#include "Utils.hpp"

namespace r50 {

/// Loop length whose bin grid (sr/L) lands closest to `frequency`, subject to a
/// minimum duration. Returns 0 if nothing suitable was found.
inline int chooseLoopLength(double frequency, double sampleRate,
                            double minSeconds, double maxSeconds) {
    const double minLength = minSeconds * sampleRate;
    const double maxLength = maxSeconds * sampleRate;

    // Derive the cycle-count range from the wanted duration rather than
    // scanning from 1. A high root needs thousands of cycles to fill a
    // second, and a fixed scan limit silently produced no length at all —
    // which mapped as a missing region, and a silent top octave.
    const int firstCycle = std::max(1,
        static_cast<int>(std::ceil(minLength * frequency / sampleRate)));
    const int lastCycle =
        static_cast<int>(std::floor(maxLength * frequency / sampleRate));

    int bestLength = 0;
    double bestError = 1e9;
    for (int cycles = firstCycle; cycles <= lastCycle; ++cycles) {
        const double exact = cycles * sampleRate / frequency;
        const int rounded = static_cast<int>(std::lround(exact));
        const double error = std::fabs(exact - rounded);
        if (error < bestError) { bestError = error; bestLength = rounded; }
    }
    return bestLength;
}

/// One partial of a generated sustain, expressed in bins of sr/L so the result
/// is exactly periodic over the loop.
struct GeneratedPartial {
    int   bin;         // frequency = bin * sr / length
    float amplitude;
    float phase;
};

/// Additive synthesis with a phasor recurrence — no trig in the inner loop.
inline std::vector<float> renderPartials(const std::vector<GeneratedPartial> &partials,
                                         int length) {
    std::vector<float> out(length, 0.0f);
    for (const GeneratedPartial &partial : partials) {
        if (partial.amplitude == 0.0f || partial.bin <= 0) continue;
        const double omega = synth::kTwoPi * partial.bin / length;
        double real = std::cos(partial.phase);
        double imag = std::sin(partial.phase);
        const double cosStep = std::cos(omega), sinStep = std::sin(omega);
        for (int n = 0; n < length; ++n) {
            out[n] += static_cast<float>(partial.amplitude * real);
            const double nextReal = real * cosStep - imag * sinStep;
            const double nextImag = real * sinStep + imag * cosStep;
            real = nextReal;
            imag = nextImag;
        }
    }
    return out;
}

inline void normalise(std::vector<float> &buffer, float peak) {
    float maximum = 1e-9f;
    for (float value : buffer) maximum = std::max(maximum, std::fabs(value));
    const float scale = peak / maximum;
    for (float &value : buffer) value *= scale;
}

/// Deterministic phase for a (bin, seed) pair — generated content must render
/// identically every run.
inline float generatedPhase(int bin, int seed) {
    uint32_t hash = static_cast<uint32_t>(bin * 2246822519u)
                  ^ static_cast<uint32_t>(seed * 3266489917u + 374761393u);
    hash ^= hash >> 15; hash *= 2654435761u; hash ^= hash >> 13;
    return static_cast<float>((hash & 0xffffffu) / static_cast<double>(0x1000000)
                              * synth::kTwoPi);
}

// ---- Sustains -------------------------------------------------------------

enum class SustainVoicing { Choir, Strings, WarmPad, GlassPad };

/// A looped sustain at the given pitch. Each harmonic is split into detuned
/// copies one or two bins apart, which is what produces the slow beating.
inline SampleData generateSustain(SustainVoicing voicing, double frequency,
                                  int rootKey, double sampleRate) {
    const int length = chooseLoopLength(frequency, sampleRate, 0.70, 1.15);
    if (length <= 0) return SampleData{};

    const int fundamental = static_cast<int>(
        std::lround(frequency * length / sampleRate));
    const int seed = static_cast<int>(voicing) * 101 + rootKey;

    std::vector<GeneratedPartial> partials;
    const int maxHarmonic = 26;

    for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic) {
        const int bin = fundamental * harmonic;
        if (bin >= length / 2) break;

        float amplitude = 0.0f;
        switch (voicing) {
            case SustainVoicing::Choir: {
                // Formant-shaped, over a source floor so the fundamental
                // survives — the same trap the Vocal Ah wave fell into.
                const double hz = bin * sampleRate / length;
                double shaped = 0.16;
                const double formants[3][3] = {{620, 120, 1.0},
                                               {1080, 170, 0.66},
                                               {2500, 300, 0.2}};
                for (const auto &formant : formants) {
                    const double x = (hz - formant[0]) / formant[1];
                    shaped += formant[2] * std::exp(-x * x);
                }
                amplitude = static_cast<float>(shaped / harmonic);
                break;
            }
            case SustainVoicing::Strings:
                amplitude = (1.0f / harmonic) * std::exp(-harmonic / 22.0f);
                break;
            case SustainVoicing::WarmPad:
                amplitude = (harmonic <= 8)
                    ? (1.0f / (harmonic * harmonic)) * 1.4f : 0.0f;
                break;
            case SustainVoicing::GlassPad:
                amplitude = ((harmonic % 2) == 1 || harmonic == 4 || harmonic == 8)
                    ? (1.0f / std::sqrt(static_cast<float>(harmonic))) * 0.35f
                    : 0.0f;
                break;
        }
        if (amplitude <= 0.0f) continue;

        // Detuned copies, offset by whole bins so periodicity is preserved.
        const int spread = 2;
        partials.push_back({bin, amplitude, generatedPhase(bin, seed)});
        for (int offset = 1; offset <= spread; ++offset) {
            const float sideAmplitude = amplitude * (offset == 1 ? 0.7f : 0.4f);
            if (bin - offset > 0) {
                partials.push_back({bin - offset, sideAmplitude,
                                    generatedPhase(bin * 7 + offset, seed)});
            }
            if (bin + offset < length / 2) {
                partials.push_back({bin + offset, sideAmplitude,
                                    generatedPhase(bin * 13 + offset, seed)});
            }
        }
    }

    SampleData data;
    data.samples = renderPartials(partials, length);
    normalise(data.samples, 0.85f);
    data.sourceSampleRate = sampleRate;
    data.rootKey  = rootKey;
    data.loopStart = 0;
    data.loopEnd   = static_cast<uint32_t>(length);
    data.loopMode  = LoopMode::Forward;
    return data;
}

// ---- Attacks --------------------------------------------------------------

enum class AttackKind { Mallet, Pluck, Chiff, NoiseBurst, TineStrike };

/// A short one-shot transient. These are not loop-critical, so they are built
/// directly in the time domain with explicit decay envelopes.
inline SampleData generateAttack(AttackKind kind, double sampleRate) {
    struct Recipe { double seconds; double decay; };
    const Recipe recipe = [kind] {
        switch (kind) {
            case AttackKind::Mallet:     return Recipe{0.12, 38.0};
            case AttackKind::Pluck:      return Recipe{0.09, 45.0};
            case AttackKind::Chiff:      return Recipe{0.07, 55.0};
            case AttackKind::NoiseBurst: return Recipe{0.05, 90.0};
            case AttackKind::TineStrike: return Recipe{0.11, 40.0};
        }
        return Recipe{0.08, 50.0};
    }();

    const int length = static_cast<int>(recipe.seconds * sampleRate);
    const double fundamental = 261.6255;   // built at C4, transposed on playback
    synth::FastRandom random(0xA1B2C3D4ULL + static_cast<uint64_t>(kind) * 7919ULL);

    std::vector<float> buffer(length, 0.0f);
    synth::SVFStage band;
    band.setSampleRate(sampleRate);
    band.setCoefficients(kind == AttackKind::Chiff ? 2600.0 : 1400.0, 2.0, 0.0f, 0.0f);

    for (int n = 0; n < length; ++n) {
        const double t = n / sampleRate;
        const float envelope = static_cast<float>(std::exp(-recipe.decay * t));
        const float noise = random.nextBipolar();
        float value = 0.0f;

        switch (kind) {
            case AttackKind::Mallet:
                value = static_cast<float>(
                            std::sin(synth::kTwoPi * fundamental * 2.0 * t) * 0.7
                          + std::sin(synth::kTwoPi * fundamental * 5.4 * t) * 0.3)
                      + noise * 0.25f * static_cast<float>(std::exp(-260.0 * t));
                break;
            case AttackKind::Pluck:
                value = band.process(noise).bp * 1.6f
                      + static_cast<float>(
                            std::sin(synth::kTwoPi * fundamental * t)) * 0.35f;
                break;
            case AttackKind::Chiff:
                value = band.process(noise).bp * 2.0f;
                break;
            case AttackKind::NoiseBurst:
                value = noise;
                break;
            case AttackKind::TineStrike:
                value = static_cast<float>(
                            std::sin(synth::kTwoPi * fundamental * 4.0 * t) * 0.6
                          + std::sin(synth::kTwoPi * fundamental * 9.2 * t) * 0.4)
                      + noise * 0.15f * static_cast<float>(std::exp(-300.0 * t));
                break;
        }
        buffer[n] = value * envelope;
    }

    SampleData data;
    data.samples = std::move(buffer);
    normalise(data.samples, 0.9f);
    data.sourceSampleRate = sampleRate;
    data.rootKey  = 60;
    data.loopStart = 0;
    data.loopEnd   = static_cast<uint32_t>(length);
    data.loopMode  = LoopMode::None;
    return data;
}

// ---- Library assembly -----------------------------------------------------

/// Build the factory instruments into `library`. Sustains are generated at
/// three root keys and mapped into three key zones, so region selection is
/// genuinely exercised rather than merely implemented.
inline void buildFactoryContent(SampleLibrary &library) {
    const double sampleRate = 44100.0;

    struct ZoneSpec { int rootKey, lowKey, highKey; };
    // One zone per octave. Transposition inside a zone is at most a few
    // semitones, so the loop keeps very nearly its generated duration — with
    // three zones the top one stretched from root 72 to key 127 and turned a
    // 0.5 s loop into a 21 ms, 48 Hz buzz.
    static const ZoneSpec zones[7] = {
        { 36,   0,  41},
        { 48,  42,  53},
        { 60,  54,  65},
        { 72,  66,  77},
        { 84,  78,  89},
        { 96,  90, 101},
        {108, 102, 127},
    };

    struct SustainSpec { SustainVoicing voicing; const char *name; };
    static const SustainSpec sustains[4] = {
        {SustainVoicing::Choir,    "Choir"},
        {SustainVoicing::Strings,  "Strings"},
        {SustainVoicing::WarmPad,  "Warm Pad"},
        {SustainVoicing::GlassPad, "Glass Pad"},
    };

    for (const SustainSpec &spec : sustains) {
        Multisample instrument;
        instrument.setName(spec.name);
        for (const ZoneSpec &zone : zones) {
            const double frequency =
                440.0 * std::pow(2.0, (zone.rootKey - 69) / 12.0);
            SampleData data = generateSustain(spec.voicing, frequency,
                                              zone.rootKey, sampleRate);
            if (data.samples.empty()) continue;
            const int slot = library.addSample(std::move(data));
            if (slot < 0) continue;

            SampleRegion region;
            region.lowKey  = zone.lowKey;
            region.highKey = zone.highKey;
            region.rootKey = zone.rootKey;
            region.slot    = slot;
            instrument.regions[instrument.regionCount++] = region;
        }
        library.addInstrument(instrument);
    }

    struct AttackSpec { AttackKind kind; const char *name; };
    static const AttackSpec attacks[5] = {
        {AttackKind::Mallet,     "Mallet"},
        {AttackKind::Pluck,      "Pluck"},
        {AttackKind::Chiff,      "Chiff"},
        {AttackKind::NoiseBurst, "Noise Burst"},
        {AttackKind::TineStrike, "Tine Strike"},
    };

    for (const AttackSpec &spec : attacks) {
        SampleData data = generateAttack(spec.kind, sampleRate);
        const int slot = library.addSample(std::move(data));
        if (slot < 0) continue;

        Multisample instrument;
        instrument.setName(spec.name);
        SampleRegion region;
        region.rootKey = 60;
        region.slot    = slot;
        instrument.regions[0] = region;
        instrument.regionCount = 1;
        library.addInstrument(instrument);
    }
}

inline SampleLibrary::SampleLibrary() {
    buildFactoryContent(*this);
}

inline SampleLibrary &SampleLibrary::shared() {
    static SampleLibrary library;
    return library;
}

} // namespace r50
