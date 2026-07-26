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
#include "R50FactoryFiles.hpp"
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

enum class SustainVoicing {
    Choir, Strings, WarmPad, GlassPad,
    VoiceOoh, Flute, Trumpet, Organ, NylonGuitar, Piano, Gong, Nasty, FatBlock,
    // The D-50's Spectrum waves are the half of LA synthesis that is not an
    // imitation of anything: bright, cold, deliberately synthetic loops that
    // the acoustic attack transients are layered onto. They are what "many
    // patches use the Spectrum waveforms" refers to, and R50 had no equivalent
    // at all — only Nasty sat anywhere near this territory.
    Spectrum1, Spectrum2, Spectrum3, Spectrum4, Spectrum5,
    Spectrum6, Spectrum7, Spectrum8, Spectrum9
};

static constexpr int kSustainVoicingCount = 22;

/// Magnitude of one two-pole resonance. The shape matters more than it looks:
/// a resonance is *flat* below its centre frequency and only falls away above
/// it, whereas a Gaussian — which is what this used to be — dies on both sides.
///
/// That asymmetry is the whole difference between a voice and a string. With
/// Gaussian formants everything below F1 collapsed to an arbitrary floor, so
/// the spectrum was carried by the 1/harmonic source tilt with a few shallow
/// bumps on top: which is a string. A real tract passes the fundamental at
/// full level, puts sharp peaks at the formants and leaves deep valleys
/// between them. It also means the floor that kept the Vocal Ah fundamental
/// alive is no longer needed — the model produces it.
inline double resonanceMagnitude(double hz, double centre, double q) {
    const double ratio = hz / centre;
    const double real  = 1.0 - ratio * ratio;
    const double imag  = ratio / q;
    return 1.0 / std::sqrt(real * real + imag * imag);
}

/// Source-filter vowel: a glottal source tilted at -6 dB/oct (that is the
/// -12 dB/oct of the glottal pulse plus the +6 dB/oct of lip radiation) shaped
/// by tract resonances given as {centre Hz, Q, weight}.
///
/// The resonances are summed, not cascaded. Cascading four of them rolls off at
/// -24 dB/oct above the top formant, -30 with the source tilt, which measured
/// as nothing at all above the tenth harmonic and sounded as muffled as that
/// implies. A parallel bank keeps each peak but leaves the top falling at the
/// -12 dB/oct of a single resonance, which is what a real vowel does.
inline float formantAmplitude(double hz, int harmonic,
                              const double formants[][3], int count) {
    double shaped = 0.0;
    for (int i = 0; i < count; ++i) {
        shaped += formants[i][2]
                * resonanceMagnitude(hz, formants[i][0], formants[i][1]);
    }
    return static_cast<float>(shaped / harmonic);
}

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
                // An open "aah": F1 and F2 wide apart, plus the singer's
                // formant near 2.9 kHz that is the signature of a trained
                // choral voice and the thing that stops it reading as strings.
                const double hz = bin * sampleRate / length;
                static const double formants[4][3] = {{700,  9.0, 1.00},
                                                      {1150, 11.0, 0.50},
                                                      {2600, 14.0, 0.16},
                                                      {2900, 16.0, 0.22}};
                amplitude = formantAmplitude(hz, harmonic, formants, 4) * 0.22f;
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

            case SustainVoicing::VoiceOoh: {
                // A closed vowel: both formants low and close together, which
                // is what separates "ooh" from the open "aah" of Choir.
                const double hz = bin * sampleRate / length;
                static const double formants[4][3] = {{330, 10.0, 1.00},
                                                      {760, 12.0, 0.28},
                                                      {2400, 14.0, 0.07},
                                                      {2850, 16.0, 0.10}};
                amplitude = formantAmplitude(hz, harmonic, formants, 4) * 0.22f;
                break;
            }

            case SustainVoicing::Flute: {
                // A triangle: odd harmonics only, falling as 1/n^2. That is the
                // shape a stopped pipe actually produces, and it is hollow in a
                // way a near-sine is not — a sine has no character to hear.
                // The trace of even harmonics and the quiet top are what keep
                // it from sounding like a synthesised triangle.
                const double hz = bin * sampleRate / length;
                const float n = static_cast<float>(harmonic);
                if ((harmonic % 2) == 1) amplitude = 1.0f / (n * n);
                else                     amplitude = 0.05f / (n * n);
                if (hz > 9000.0) amplitude *= 0.25f;
                break;
            }

            case SustainVoicing::Trumpet: {
                // Brass energy peaks well above the fundamental, around the
                // 1200 Hz formant, which is what makes it read as brass rather
                // than as a bright saw.
                const double hz = bin * sampleRate / length;
                static const double formants[2][3] = {{1200, 2.2, 1.00},
                                                      {2400, 3.0, 0.45}};
                amplitude = formantAmplitude(hz, harmonic, formants, 2)
                          * (harmonic < 3 ? 0.55f : 1.0f);
                break;
            }

            case SustainVoicing::Organ: {
                // Drawbars: octaves and fifths only, all held at full level.
                // A sustained organ has no rolloff to speak of.
                switch (harmonic) {
                    case 1:  amplitude = 1.00f; break;
                    case 2:  amplitude = 0.80f; break;
                    case 3:  amplitude = 0.45f; break;
                    case 4:  amplitude = 0.60f; break;
                    case 6:  amplitude = 0.30f; break;
                    case 8:  amplitude = 0.40f; break;
                    case 12: amplitude = 0.15f; break;
                    case 16: amplitude = 0.20f; break;
                    default: amplitude = 0.0f; break;
                }
                break;
            }

            case SustainVoicing::NylonGuitar:
                // A plucked string's body: strong low harmonics falling away
                // quickly, with the even ones slightly weaker.
                amplitude = (1.0f / harmonic) * std::exp(-harmonic / 9.0f)
                          * ((harmonic % 2) ? 1.0f : 0.7f);
                break;

            case SustainVoicing::Piano:
                // A dip through the middle harmonics is what stops a piano
                // sounding like a saw — it is the reason for the notch here.
                amplitude = (1.0f / harmonic)
                          * (1.0f - 0.55f * std::exp(-((harmonic - 6.0f)
                                                     * (harmonic - 6.0f)) / 12.0f))
                          * std::exp(-harmonic / 16.0f);
                break;

            // --- Spectrum family -------------------------------------------
            // Each is a different rule for *which* harmonics exist, not a
            // different rolloff. That is what separates them from each other
            // and from the imitative voicings above, all of which are some
            // curve applied to a full harmonic series.
            //
            // A seamless loop is periodic by construction, so genuinely
            // inharmonic partials are impossible — the same wall Gong ran into.
            // Sparse and uneven harmonic subsets are how you get the cold,
            // struck-metal quality without them.
            case SustainVoicing::Spectrum1: {
                // Hollow and bright: odd harmonics with a peak high up rather
                // than at the fundamental.
                if ((harmonic % 2) == 0) break;
                const float n = static_cast<float>(harmonic);
                amplitude = std::exp(-std::pow((n - 7.0f) / 5.0f, 2.0f)) * 0.9f
                          + 0.12f / n;
                break;
            }
            case SustainVoicing::Spectrum2:
                // A comb: every third harmonic, evenly weighted. The gaps are
                // wide enough to read as a chord rather than as a timbre.
                amplitude = (harmonic % 3) == 1
                    ? 0.75f / std::sqrt(static_cast<float>(harmonic)) : 0.0f;
                break;
            case SustainVoicing::Spectrum3: {
                // Almost no fundamental — the energy sits in a band two to
                // three octaves up, which is what makes it read as glass. The
                // thin trickle underneath is not decoration: in the top key
                // zone the whole band lands above Nyquist, and without it the
                // asset generates as silence.
                const float n = static_cast<float>(harmonic);
                amplitude = std::exp(-std::pow((n - 14.0f) / 3.5f, 2.0f)) * 1.0f
                          + 0.10f / n;
                break;
            }
            case SustainVoicing::Spectrum4: {
                // Prime harmonics only. They share no common divisor above the
                // fundamental, so the result beats against itself the way a
                // struck bell does without leaving the periodic grid.
                static const int primes[9] = {1, 2, 3, 5, 7, 11, 13, 17, 19};
                amplitude = 0.0f;
                for (int prime : primes) {
                    if (harmonic == prime) {
                        amplitude = 0.9f / std::sqrt(static_cast<float>(harmonic));
                        break;
                    }
                }
                break;
            }
            case SustainVoicing::Spectrum5: {
                // Two narrow bands far apart, with a hole between them. The
                // most used Spectrum wave in the bank, so it earns the most
                // distinctive shape.
                const float n = static_cast<float>(harmonic);
                amplitude = std::exp(-std::pow((n - 2.0f) / 1.4f, 2.0f)) * 0.9f
                          + std::exp(-std::pow((n - 15.0f) / 3.0f, 2.0f)) * 0.55f;
                break;
            }
            case SustainVoicing::Spectrum6: {
                // Even harmonics only — the mirror of Spectrum 1. The missing
                // fundamental pushes the perceived pitch an octave up while the
                // note keeps its real one, which is a distinctly digital effect
                // and nothing else here does it. It replaced a 1/n series with
                // notches, which measured too close to both Spectrum 4 and
                // Spectrum 9 to be worth its own slot.
                if ((harmonic % 2) != 0) break;
                amplitude = 0.9f / static_cast<float>(harmonic / 2);
                break;
            }
            case SustainVoicing::Spectrum7: {
                // A single tight cluster high up: a ringing metallic band with
                // just enough fundamental to give it a pitch.
                const float n = static_cast<float>(harmonic);
                amplitude = std::exp(-std::pow((n - 9.0f) / 2.0f, 2.0f)) * 1.0f
                          + (harmonic == 1 ? 0.3f : 0.0f);
                break;
            }
            case SustainVoicing::Spectrum8: {
                // Widely spaced and sparse — octaves and their neighbours only.
                switch (harmonic) {
                    case 1:  amplitude = 0.85f; break;
                    case 2:  amplitude = 0.30f; break;
                    case 4:  amplitude = 0.50f; break;
                    case 8:  amplitude = 0.40f; break;
                    case 9:  amplitude = 0.22f; break;
                    case 16: amplitude = 0.30f; break;
                    case 17: amplitude = 0.16f; break;
                    default: amplitude = 0.0f; break;
                }
                break;
            }
            case SustainVoicing::Spectrum9: {
                // Every harmonic at nearly the same level. No rolloff at all is
                // a thing only a digital instrument does, and it is the
                // harshest wave here after Nasty. Written as 1/n with a shelf
                // first, which measured as just another rolloff.
                amplitude = 0.30f - 0.006f * static_cast<float>(harmonic);
                break;
            }

            case SustainVoicing::Gong: {
                // Metallic wash. A loop is periodic, so genuinely inharmonic
                // partials are impossible; picking a sparse, uneven harmonic
                // subset gets most of the way there.
                static const int metallic[] = {1, 3, 7, 11, 13, 17, 19, 23};
                amplitude = 0.0f;
                for (int m : metallic) {
                    if (harmonic == m) {
                        amplitude = 0.55f / std::sqrt(static_cast<float>(harmonic));
                        break;
                    }
                }
                break;
            }

            case SustainVoicing::Nasty:
                // Deliberately harsh: a rising tilt into the upper harmonics,
                // which is the digital-buzz corner of the D-50 palette.
                amplitude = (harmonic <= 24)
                    ? 0.12f + 0.5f * std::exp(-((harmonic - 14.0f)
                                              * (harmonic - 14.0f)) / 40.0f)
                    : 0.0f;
                amplitude /= std::sqrt(static_cast<float>(harmonic));
                break;

            case SustainVoicing::FatBlock:
                // Odd harmonics only, rolled off gently: a thick hollow square
                // rather than the thin one a pure 1/k square gives.
                amplitude = (harmonic % 2)
                    ? (1.0f / harmonic) * std::exp(-harmonic / 26.0f) * 1.3f
                    : 0.0f;
                break;
        }
        // Skip partials nobody can hear. The rolloffs above trail off into
        // amplitudes three orders of magnitude below the fundamental, and
        // synthesising them costs as much per sample as an audible one.
        if (amplitude <= 0.004f) continue;

        // Detuned copies, offset by whole bins so periodicity is preserved.
        // The beating they create is the character of a string or choir
        // ensemble; a flute or an organ pipe wants far less of it, and each
        // extra copy is another full pass over the loop to synthesise.
        const bool ensemble = voicing == SustainVoicing::Choir
                           || voicing == SustainVoicing::Strings
                           || voicing == SustainVoicing::WarmPad
                           || voicing == SustainVoicing::GlassPad;
        const int spread = ensemble ? 2 : 1;
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
    Anvil, TaikoDrum, LipBuzz, Breath, BowScrape,
    Pizzicato
};

static constexpr int kAttackKindCount = 19;

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

    // A struck transient starts at full amplitude on its first sample, which is
    // correct for a mallet and wrong for anything blown or bowed: air and rosin
    // arrive, they do not hit. Without a rise time the breath sample read as a
    // hi-hat and the bow scrape as an explosion — both were percussion.
    float  riseSeconds;      // onset ramp for the whole transient; 0 = a strike
    double noiseHighpass;    // Hz; removes the low rumble a wide band lets through
    float  noiseGrain;       // 0..1 depth of the stick-slip flutter
    double noiseGrainRate;   // Hz, how fast that flutter stirs
};

inline AttackRecipe attackRecipe(AttackKind kind) {
    switch (kind) {
        // --- the originals, unchanged in character ---------------------------
        case AttackKind::Mallet:
            return {0.12, {{2.0f, 0.7f, 38.0f}, {5.4f, 0.3f, 38.0f}}, 2,
                    0.25f, 260.0f, 0.0, 1.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        case AttackKind::Pluck:
            return {0.09, {{1.0f, 0.35f, 45.0f}}, 1,
                    1.6f, 45.0f, 1400.0, 2.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        case AttackKind::Chiff:
            return {0.07, {}, 0, 2.0f, 55.0f, 2600.0, 2.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        case AttackKind::NoiseBurst:
            return {0.05, {}, 0, 1.0f, 90.0f, 0.0, 1.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        case AttackKind::TineStrike:
            return {0.11, {{4.0f, 0.6f, 40.0f}, {9.2f, 0.4f, 40.0f}}, 2,
                    0.15f, 300.0f, 0.0, 1.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};

        // --- struck bars ------------------------------------------------------
        // Wooden bars: strong fundamental, upper modes dying much faster.
        case AttackKind::Marimba:
            return {0.30, {{1.0f, 1.0f, 11.0f}, {3.9f, 0.45f, 26.0f},
                           {9.2f, 0.18f, 45.0f}}, 3,
                    0.20f, 320.0f, 2200.0, 1.4, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        // Metal rings far longer than wood, and keeps its upper modes.
        case AttackKind::Vibraphone:
            return {0.45, {{1.0f, 1.0f, 5.0f}, {4.0f, 0.55f, 8.0f},
                           {10.8f, 0.25f, 12.0f}}, 3,
                    0.10f, 400.0f, 3000.0, 1.6, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        // Xylophone bars are tuned to the third, not the fourth, and are short.
        case AttackKind::Xylophone:
            return {0.16, {{1.0f, 1.0f, 26.0f}, {3.0f, 0.6f, 34.0f},
                           {6.0f, 0.3f, 48.0f}}, 3,
                    0.30f, 380.0f, 3200.0, 1.3, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        // A plucked metal tine: inharmonic and buzzy at the very start.
        case AttackKind::Kalimba:
            return {0.28, {{1.0f, 1.0f, 9.0f}, {5.4f, 0.35f, 22.0f},
                           {13.1f, 0.12f, 40.0f}}, 3,
                    0.22f, 300.0f, 1800.0, 1.5, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};

        // --- bass and string attacks -----------------------------------------
        // Slap is a bright click sitting on a low thump. It was two thirds
        // noise, which is a snare; the click is a *transient*, not a texture,
        // so the noise is now a third of the level, gone in 20 ms, and narrow
        // enough to read as string-against-fret rather than as hiss.
        case AttackKind::SlapBass:
            return {0.09, {{1.0f, 1.0f, 26.0f}, {2.0f, 0.45f, 38.0f},
                           {3.0f, 0.2f, 52.0f}}, 3,
                    1.0f, 260.0f, 2800.0, 2.6, 0.0f, 0.0f, 0.0f, 900.0, 0.0f, 0.0};
        case AttackKind::PullBass:
            return {0.16, {{1.0f, 0.9f, 16.0f}, {3.0f, 0.3f, 40.0f}}, 2,
                    0.9f, 90.0f, 900.0, 1.8, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        // Scratch is not noise level, it is noise *structure*. At Q 1.1 the
        // band was three octaves wide, which is white noise with a tilt — and
        // that is exactly what it sounded like. A narrow high band gives it a
        // pitch to scrape at, the fast grain flutter gives the plectrum teeth
        // catching the winding, and the two inharmonic partials are the click
        // of the pick letting go.
        case AttackKind::Pick:
            return {0.05, {{1.0f, 0.06f, 130.0f}, {7.3f, 0.26f, 90.0f},
                           {13.7f, 0.16f, 120.0f}}, 3,
                    1.1f, 150.0f, 3900.0, 5.0, 0.0f, 0.0f,
                    0.0f, 1400.0, 0.85f, 2600.0};
        // Felt on string: a soft low thud with very little top.
        case AttackKind::PianoHammer:
            return {0.10, {{1.0f, 0.7f, 30.0f}, {2.0f, 0.3f, 45.0f}}, 2,
                    0.6f, 150.0f, 500.0, 1.4, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};

        // --- struck metal and membranes ---------------------------------------
        // Dense and wholly inharmonic: no ratio here is a whole number.
        case AttackKind::Anvil:
            return {0.40, {{1.0f, 0.7f, 7.0f}, {2.7f, 0.8f, 9.0f},
                           {4.3f, 0.6f, 11.0f}, {6.1f, 0.45f, 14.0f},
                           {8.9f, 0.3f, 18.0f}}, 5,
                    0.25f, 200.0f, 5000.0, 1.2, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
        // A struck head falls in pitch as its tension relaxes; without that
        // drop a drum reads as a tuned tom.
        case AttackKind::TaikoDrum:
            return {0.35, {{1.0f, 1.0f, 9.0f}, {1.6f, 0.4f, 14.0f},
                           {2.3f, 0.2f, 20.0f}}, 3,
                    0.7f, 45.0f, 260.0, 1.1, 0.55f, 26.0f, 0.0f, 0.0, 0.0f, 0.0};

        // --- wind and bowed ---------------------------------------------------
        // Lips buzzing are a *pitched* sound — a rich, almost sawtooth rasp —
        // and the noise is only the air escaping around them. At 0.8 the noise
        // was louder than the buzz it was supposed to edge, so the pitch went
        // with it. Six harmonics carry it now and the noise is a seasoning.
        case AttackKind::LipBuzz:
            return {0.15, {{1.0f, 0.9f, 12.0f}, {2.0f, 0.7f, 14.0f},
                           {3.0f, 0.5f, 17.0f}, {4.0f, 0.34f, 20.0f},
                           {5.0f, 0.22f, 24.0f}, {6.0f, 0.14f, 30.0f}}, 6,
                    0.16f, 55.0f, 2200.0, 2.4, 0.0f, 0.0f,
                    0.006f, 700.0, 0.0f, 0.0};
        // Pure air, no pitch at all. Two things made this a hi-hat: it started
        // at full amplitude on sample zero, which is a strike, and at Q 1.1 the
        // band passed everything from 400 Hz up, which is the low thump a hat
        // has and breath does not. A 40 ms rise, a high-pass at 1.4 kHz and a
        // narrower band leave air arriving instead of a cymbal being hit.
        case AttackKind::Breath:
            return {0.30, {}, 0, 1.5f, 9.0f, 3000.0, 1.9, 0.0f, 0.0f,
                    0.040f, 1400.0, 0.25f, 40.0};
        // A plucked-and-damped string. Distinct from Pluck, which is mostly
        // noise, and from Pick, which is a scrape: a pizzicato is *pitched*,
        // with the body of the instrument ringing briefly behind the finger.
        // It is what Pizzagogo is built on, and R50 had nothing like it.
        case AttackKind::Pizzicato:
            return {0.22, {{1.0f, 1.0f, 14.0f}, {2.0f, 0.55f, 20.0f},
                           {3.0f, 0.30f, 28.0f}, {4.0f, 0.16f, 38.0f},
                           {5.9f, 0.10f, 50.0f}}, 5,
                    0.30f, 150.0f, 2400.0, 2.8, 0.0f, 0.0f,
                    0.0f, 700.0, 0.0f, 0.0};

        // "Slower to arrive than a strike" was in the comment but not in the
        // code — the rise time did not exist yet, so a wide band at full level
        // on the first sample made a detonation. Rosin is a stick-slip process:
        // it arrives over 70 ms, sits in a narrow mid band, and stirs
        // irregularly. The high-pass is what takes the boom out.
        case AttackKind::BowScrape:
            return {0.34, {{2.0f, 0.10f, 9.0f}, {3.0f, 0.07f, 13.0f}}, 2,
                    0.7f, 7.0f, 1700.0, 3.2, 0.0f, 0.0f,
                    0.070f, 900.0, 0.7f, 90.0};
    }
    return {0.08, {}, 0, 1.0f, 50.0f, 0.0, 1.0, 0.0f, 0.0f, 0.0f, 0.0, 0.0f, 0.0};
}

/// A short one-shot transient, built in the time domain from its recipe.
inline SampleData generateAttack(AttackKind kind, double sampleRate) {
    const AttackRecipe recipe = attackRecipe(kind);
    const int length = static_cast<int>(recipe.seconds * sampleRate);
    const double fundamental = 261.6255;   // built at C4, transposed on playback
    synth::FastRandom random(0xA1B2C3D4ULL + static_cast<uint64_t>(kind) * 7919ULL);

    std::vector<float> buffer(length, 0.0f);
    synth::SVFStage band, highpass;
    band.setSampleRate(sampleRate);
    highpass.setSampleRate(sampleRate);
    if (recipe.noiseCentre > 0.0) {
        band.setCoefficients(recipe.noiseCentre, recipe.noiseQ, 0.0f, 0.0f);
    }
    if (recipe.noiseHighpass > 0.0) {
        highpass.setCoefficients(recipe.noiseHighpass, 0.707, 0.0f, 0.0f);
    }

    // Stick-slip flutter: a smoothed random walk, not a periodic wobble. Rosin
    // grabbing and releasing is irregular, and anything periodic here reads as
    // a tremolo effect laid over the sample instead of as the sample's texture.
    double grainPhase = 0.0, grainTarget = 1.0, grainValue = 1.0;
    const double grainStep = recipe.noiseGrainRate > 0.0
        ? recipe.noiseGrainRate / sampleRate : 0.0;
    // Smoothing has to track the grain rate. A fixed coefficient smoothed over
    // ~6 ms, which is slower than the 0.4 ms a pick's flutter asks for, so the
    // value never reached its target and the flutter did nothing at all — the
    // pick stayed the featureless band of noise it was reported as.
    const double grainSmooth = std::min(1.0, grainStep * 3.0);

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
            float shaped = recipe.noiseCentre > 0.0
                ? band.process(noise).bp : noise;
            if (recipe.noiseHighpass > 0.0) shaped = highpass.process(shaped).hp;

            if (grainStep > 0.0) {
                grainPhase += grainStep;
                if (grainPhase >= 1.0) {
                    grainPhase -= std::floor(grainPhase);
                    grainTarget = 1.0 - recipe.noiseGrain
                                * (0.5 + 0.5 * random.nextUnipolar());
                }
                grainValue += (grainTarget - grainValue) * grainSmooth;
                shaped *= static_cast<float>(grainValue);
            }

            value += shaped * recipe.noiseAmp
                   * std::exp(-recipe.noiseDecay * static_cast<float>(t));
        }

        // The onset ramp is applied to everything, not just the noise. Bow
        // Scrape kept its bang with a 70 ms noise rise because its two pitched
        // partials still arrived at full level on sample zero — and the ear
        // takes the earliest edge as the attack.
        if (recipe.riseSeconds > 0.0f) {
            value *= 1.0f - std::exp(-3.0f * static_cast<float>(t)
                                           / recipe.riseSeconds);
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
    // A zone every octave and a half. Transposition inside a zone stays under
    // nine semitones, so a loop keeps most of its generated duration — with the
    // original three zones the top one stretched from root 72 to key 127 and
    // turned a 0.5 s loop into a 21 ms, 48 Hz buzz.
    //
    // Five zones rather than seven: every asset is generated at startup, and
    // thirteen sustains across seven zones cost 400 ms and 13 MB. Worst-case
    // loop repetition is unchanged at 3.2 Hz, because it is set by the top
    // zone reaching key 127 either way.
    static const ZoneSpec zones[5] = {
        { 36,   0,  44},
        { 54,  45,  62},
        { 72,  63,  80},
        { 90,  81,  98},
        {108,  99, 127},
    };

    // Instrument indices are stable — a preset stores one, and the sample
    // browser shows them in this order. So new content is registered *after*
    // everything that already existed, never woven into it. Appending the nine
    // Spectrum waves to the sustain list was enough to shift every attack by
    // nine and repoint all 29 presets at the wrong samples.
    struct SustainSpec { SustainVoicing voicing; const char *name; };
    static constexpr int kOriginalSustains = 13;
    static const SustainSpec sustains[kSustainVoicingCount] = {
        {SustainVoicing::Choir,       "Choir"},
        {SustainVoicing::Strings,     "Strings"},
        {SustainVoicing::WarmPad,     "Warm Pad"},
        {SustainVoicing::GlassPad,    "Glass Pad"},
        {SustainVoicing::VoiceOoh,    "Voice Ooh"},
        {SustainVoicing::Flute,       "Flute"},
        {SustainVoicing::Trumpet,     "Trumpet"},
        {SustainVoicing::Organ,       "Organ"},
        {SustainVoicing::NylonGuitar, "Nylon Guitar"},
        {SustainVoicing::Piano,       "Piano"},
        {SustainVoicing::Gong,        "Gong"},
        {SustainVoicing::Nasty,       "Nasty"},
        {SustainVoicing::FatBlock,    "Fat Block"},
        {SustainVoicing::Spectrum1,   "Spectrum 1"},
        {SustainVoicing::Spectrum2,   "Spectrum 2"},
        {SustainVoicing::Spectrum3,   "Spectrum 3"},
        {SustainVoicing::Spectrum4,   "Spectrum 4"},
        {SustainVoicing::Spectrum5,   "Spectrum 5"},
        {SustainVoicing::Spectrum6,   "Spectrum 6"},
        {SustainVoicing::Spectrum7,   "Spectrum 7"},
        {SustainVoicing::Spectrum8,   "Spectrum 8"},
        {SustainVoicing::Spectrum9,   "Spectrum 9"},
    };

    auto addSustain = [&](const SustainSpec &spec) {
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
    };

    for (int i = 0; i < kOriginalSustains; ++i) addSustain(sustains[i]);

    struct AttackSpec { AttackKind kind; const char *name; };
    static constexpr int kOriginalAttacks = 18;
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
        // New content goes on the end — see kOriginalSustains above.
        {AttackKind::Pizzicato,   "Pizzicato"},
    };

    auto addAttack = [&](const AttackSpec &spec) {
        SampleData data = generateAttack(spec.kind, sampleRate);
        const int slot = library.addSample(std::move(data));
        if (slot < 0) return;

        Multisample instrument;
        instrument.setName(spec.name);
        SampleRegion region;
        region.rootKey = 60;
        region.slot    = slot;
        instrument.regions[0] = region;
        instrument.regionCount = 1;
        library.addInstrument(instrument);
    };

    // Registration order, and therefore instrument index order: everything that
    // shipped before, in the order it shipped, then everything new.
    for (int i = 0; i < kOriginalAttacks; ++i) addAttack(attacks[i]);
    for (int i = kOriginalSustains; i < kSustainVoicingCount; ++i) addSustain(sustains[i]);
    for (int i = kOriginalAttacks; i < kAttackKindCount; ++i) addAttack(attacks[i]);
}

/// Where the shipped factory WAVs live. Set before the library is first
/// touched — see the adapter's +load, which runs long before any audio unit
/// exists. Empty in the offline tests, which is what makes them exercise the
/// generator rather than the files.
inline std::string &factoryContentDirectory() {
    static std::string directory;
    return directory;
}

inline SampleLibrary::SampleLibrary() {
    // Files first, generator second. The generator is still the origin of the
    // content and the fallback when the files are missing or unreadable, but
    // once they exist they are what ships and what can be edited — and
    // generating a set only to throw it away costs 280 ms of every launch.
    if (!factoryContentDirectory().empty()
     && loadFactoryManifest(*this, factoryContentDirectory())) {
        return;
    }
    buildFactoryContent(*this);
}

inline SampleLibrary &SampleLibrary::shared() {
    static SampleLibrary library;
    return library;
}

} // namespace r50
