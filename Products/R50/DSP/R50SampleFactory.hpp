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
#include "R50Levels.hpp"
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

/// Match a looped sustain to the same RMS the wave tables use, so switching a
/// Partial between a wave and a sample is a change of timbre and not of level.
inline void normaliseRms(std::vector<float> &buffer) {
    double sumSquares = 0.0;
    float maximum = 1e-9f;
    for (float value : buffer) {
        sumSquares += static_cast<double>(value) * value;
        maximum = std::max(maximum, std::fabs(value));
    }
    const float rms = static_cast<float>(
        std::sqrt(sumSquares / std::max<size_t>(1, buffer.size())));
    float scale = kSourceTargetRms / std::max(rms, 1e-9f);
    if (maximum * scale > kSourcePeakCeiling) scale = kSourcePeakCeiling / maximum;
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
    normaliseRms(data.samples);
    data.sourceSampleRate = sampleRate;
    data.rootKey  = rootKey;
    data.loopStart = 0;
    data.loopEnd   = static_cast<uint32_t>(length);
    data.loopMode  = LoopMode::Forward;
    return data;
}

// ---- Attacks --------------------------------------------------------------
//
// Attack transients are what make LA synthesis work: the ear identifies an
// instrument almost entirely from its first few tens of milliseconds, and a
// sampled attack over a synthesised body is the whole D-50 idea. The families
// here follow what a real D-50 bank actually leans on — struck bars, bass
// attacks, plucks, and breath dominate its wave usage.
//
// Each attack is a set of decaying partials plus an optional filtered noise
// component. Struck bars are the reason this is data rather than a switch: a
// marimba's bar modes sit near 1 : 4 : 10, a xylophone's near 1 : 3 : 6, and a
// metal bar rings far longer than a wooden one. Those ratios are the
// difference between "a mallet" and "a marimba".

enum class AttackKind {
    Mallet, Pluck, Chiff, NoiseBurst, TineStrike,
    Marimba, Vibraphone, Xylophone, Kalimba,
    SlapBass, PullBass, Pick, PianoHammer,
    Anvil, TaikoDrum, LipBuzz, Breath, BowScrape
};

static constexpr int kAttackKindCount = 18;

struct AttackPartial {
    float ratio;    // multiple of the fundamental; non-integer for struck bars
    float amp;
    float decay;    // per second
};

struct AttackRecipe {
    double seconds;
    AttackPartial partials[6];
    int    partialCount;
    float  noiseAmp;
    float  noiseDecay;
    double noiseCentre;      // 0 = unfiltered
    double noiseQ;
    float  pitchDropOctaves; // drums fall in pitch as the head relaxes
    float  pitchDropRate;
};

inline AttackRecipe attackRecipe(AttackKind kind) {
    switch (kind) {
        // --- the originals, unchanged in character ---------------------------
        case AttackKind::Mallet:
            return {0.12, {{2.0f, 0.7f, 38.0f}, {5.4f, 0.3f, 38.0f}}, 2,
                    0.25f, 260.0f, 0.0, 1.0, 0.0f, 0.0f};
        case AttackKind::Pluck:
            return {0.09, {{1.0f, 0.35f, 45.0f}}, 1,
                    1.6f, 45.0f, 1400.0, 2.0, 0.0f, 0.0f};
        case AttackKind::Chiff:
            return {0.07, {}, 0, 2.0f, 55.0f, 2600.0, 2.0, 0.0f, 0.0f};
        case AttackKind::NoiseBurst:
            return {0.05, {}, 0, 1.0f, 90.0f, 0.0, 1.0, 0.0f, 0.0f};
        case AttackKind::TineStrike:
            return {0.11, {{4.0f, 0.6f, 40.0f}, {9.2f, 0.4f, 40.0f}}, 2,
                    0.15f, 300.0f, 0.0, 1.0, 0.0f, 0.0f};

        // --- struck bars ------------------------------------------------------
        // Wooden bars: strong fundamental, upper modes dying much faster.
        case AttackKind::Marimba:
            return {0.30, {{1.0f, 1.0f, 11.0f}, {3.9f, 0.45f, 26.0f},
                           {9.2f, 0.18f, 45.0f}}, 3,
                    0.20f, 320.0f, 2200.0, 1.4, 0.0f, 0.0f};
        // Metal rings far longer than wood, and keeps its upper modes.
        case AttackKind::Vibraphone:
            return {0.45, {{1.0f, 1.0f, 5.0f}, {4.0f, 0.55f, 8.0f},
                           {10.8f, 0.25f, 12.0f}}, 3,
                    0.10f, 400.0f, 3000.0, 1.6, 0.0f, 0.0f};
        // Xylophone bars are tuned to the third, not the fourth, and are short.
        case AttackKind::Xylophone:
            return {0.16, {{1.0f, 1.0f, 26.0f}, {3.0f, 0.6f, 34.0f},
                           {6.0f, 0.3f, 48.0f}}, 3,
                    0.30f, 380.0f, 3200.0, 1.3, 0.0f, 0.0f};
        // A plucked metal tine: inharmonic and buzzy at the very start.
        case AttackKind::Kalimba:
            return {0.28, {{1.0f, 1.0f, 9.0f}, {5.4f, 0.35f, 22.0f},
                           {13.1f, 0.12f, 40.0f}}, 3,
                    0.22f, 300.0f, 1800.0, 1.5, 0.0f, 0.0f};

        // --- bass and string attacks -----------------------------------------
        // Slap is a bright click sitting on a low thump — the click is most of
        // the identity, which is why its noise band is high and very short.
        case AttackKind::SlapBass:
            return {0.14, {{1.0f, 0.8f, 22.0f}, {2.0f, 0.35f, 34.0f}}, 2,
                    1.5f, 120.0f, 2600.0, 1.2, 0.0f, 0.0f};
        case AttackKind::PullBass:
            return {0.16, {{1.0f, 0.9f, 16.0f}, {3.0f, 0.3f, 40.0f}}, 2,
                    0.9f, 90.0f, 900.0, 1.8, 0.0f, 0.0f};
        // A pick is almost entirely a short scrape of high noise.
        case AttackKind::Pick:
            return {0.06, {{1.0f, 0.25f, 60.0f}}, 1,
                    2.0f, 130.0f, 3400.0, 1.1, 0.0f, 0.0f};
        // Felt on string: a soft low thud with very little top.
        case AttackKind::PianoHammer:
            return {0.10, {{1.0f, 0.7f, 30.0f}, {2.0f, 0.3f, 45.0f}}, 2,
                    0.6f, 150.0f, 500.0, 1.4, 0.0f, 0.0f};

        // --- struck metal and membranes ---------------------------------------
        // Dense and wholly inharmonic: no ratio here is a whole number.
        case AttackKind::Anvil:
            return {0.40, {{1.0f, 0.7f, 7.0f}, {2.7f, 0.8f, 9.0f},
                           {4.3f, 0.6f, 11.0f}, {6.1f, 0.45f, 14.0f},
                           {8.9f, 0.3f, 18.0f}}, 5,
                    0.25f, 200.0f, 5000.0, 1.2, 0.0f, 0.0f};
        // A struck head falls in pitch as its tension relaxes; without that
        // drop a drum reads as a tuned tom.
        case AttackKind::TaikoDrum:
            return {0.35, {{1.0f, 1.0f, 9.0f}, {1.6f, 0.4f, 14.0f},
                           {2.3f, 0.2f, 20.0f}}, 3,
                    0.7f, 45.0f, 260.0, 1.1, 0.55f, 26.0f};

        // --- wind and bowed ---------------------------------------------------
        // Brass lips buzz: harmonics plus a noisy edge.
        case AttackKind::LipBuzz:
            return {0.13, {{1.0f, 0.6f, 18.0f}, {2.0f, 0.4f, 22.0f},
                           {3.0f, 0.25f, 28.0f}}, 3,
                    0.8f, 60.0f, 1600.0, 1.6, 0.0f, 0.0f};
        // Pure air, no pitch at all — the flute-steam and clarinet-breathe
        // family, and the one R50 was most obviously missing.
        case AttackKind::Breath:
            return {0.22, {}, 0, 1.8f, 18.0f, 1900.0, 1.1, 0.0f, 0.0f};
        // A bow catching the string: noisy, slower to arrive than a strike.
        case AttackKind::BowScrape:
            return {0.26, {{1.0f, 0.3f, 12.0f}}, 1,
                    1.4f, 14.0f, 1200.0, 2.2, 0.0f, 0.0f};
    }
    return {0.08, {}, 0, 1.0f, 50.0f, 0.0, 1.0, 0.0f, 0.0f};
}

/// A short one-shot transient, built in the time domain from its recipe.
inline SampleData generateAttack(AttackKind kind, double sampleRate) {
    const AttackRecipe recipe = attackRecipe(kind);
    const int length = static_cast<int>(recipe.seconds * sampleRate);
    const double fundamental = 261.6255;   // built at C4, transposed on playback
    synth::FastRandom random(0xA1B2C3D4ULL + static_cast<uint64_t>(kind) * 7919ULL);

    std::vector<float> buffer(length, 0.0f);
    synth::SVFStage band;
    band.setSampleRate(sampleRate);
    if (recipe.noiseCentre > 0.0) {
        band.setCoefficients(recipe.noiseCentre, recipe.noiseQ, 0.0f, 0.0f);
    }

    // Partial phases are accumulated rather than evaluated from t, so a pitch
    // drop can bend them.
    double phase[6] = {0, 0, 0, 0, 0, 0};

    for (int n = 0; n < length; ++n) {
        const double t = n / sampleRate;
        const float bend = recipe.pitchDropOctaves > 0.0f
            ? std::pow(2.0f, recipe.pitchDropOctaves
                             * std::exp(-recipe.pitchDropRate * static_cast<float>(t)))
            : 1.0f;

        float value = 0.0f;
        for (int i = 0; i < recipe.partialCount; ++i) {
            const AttackPartial &partial = recipe.partials[i];
            value += partial.amp
                   * static_cast<float>(std::sin(synth::kTwoPi * phase[i]))
                   * std::exp(-partial.decay * static_cast<float>(t));
            phase[i] += fundamental * partial.ratio * bend / sampleRate;
            if (phase[i] >= 1.0) phase[i] -= std::floor(phase[i]);
        }

        if (recipe.noiseAmp > 0.0f) {
            const float noise = random.nextBipolar();
            const float shaped = recipe.noiseCentre > 0.0
                ? band.process(noise).bp : noise;
            value += shaped * recipe.noiseAmp
                   * std::exp(-recipe.noiseDecay * static_cast<float>(t));
        }
        buffer[n] = value;
    }

    SampleData data;
    data.samples = std::move(buffer);
    // Transients keep peak normalisation: RMS is meaningless for something that
    // decays to nothing, and what matters is that the strike sits at a
    // consistent height against the sustain it is layered onto.
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
    static const AttackSpec attacks[kAttackKindCount] = {
        {AttackKind::Mallet,      "Mallet"},
        {AttackKind::Pluck,       "Pluck"},
        {AttackKind::Chiff,       "Chiff"},
        {AttackKind::NoiseBurst,  "Noise Burst"},
        {AttackKind::TineStrike,  "Tine Strike"},
        {AttackKind::Marimba,     "Marimba"},
        {AttackKind::Vibraphone,  "Vibraphone"},
        {AttackKind::Xylophone,   "Xylophone"},
        {AttackKind::Kalimba,     "Kalimba"},
        {AttackKind::SlapBass,    "Slap Bass"},
        {AttackKind::PullBass,    "Pull Bass"},
        {AttackKind::Pick,        "Pick"},
        {AttackKind::PianoHammer, "Piano Hammer"},
        {AttackKind::Anvil,       "Anvil"},
        {AttackKind::TaikoDrum,   "Taiko Drum"},
        {AttackKind::LipBuzz,     "Lip Buzz"},
        {AttackKind::Breath,      "Breath"},
        {AttackKind::BowScrape,   "Bow Scrape"},
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
