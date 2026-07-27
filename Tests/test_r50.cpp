//
//  test_r50.cpp
//  Offline regression tests for the R50 engine: voice/MIDI behaviour, sustain
//  pedal semantics, parameter plumbing and filter stability. Build & run:
//    ./scripts/test-r50.sh
//

#include "R50Engine.hpp"
#include "R50PitchDetect.hpp"
#include "R50WavWriter.hpp"
#include "R50FactoryFiles.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <tuple>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char *name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_failures;
}

static constexpr double kSR = 44100.0;

/// Render `seconds` of audio and return the peak absolute level of the final
/// `tailSeconds` (0 = measure the whole span). Renders in odd-sized chunks so
/// the control-block boundary never lines up with the buffer boundary.
static float render(r50::R50Engine &engine, double seconds,
                    double tailSeconds = 0.0) {
    const int total = static_cast<int>(seconds * kSR);
    const int tailStart = tailSeconds > 0.0
        ? total - static_cast<int>(tailSeconds * kSR) : 0;
    std::vector<float> left(total, 0.0f), right(total, 0.0f);

    const int chunk = 137;
    for (int offset = 0; offset < total; offset += chunk) {
        const int frames = std::min(chunk, total - offset);
        engine.render(left.data() + offset, right.data() + offset, frames);
    }

    float peak = 0.0f;
    for (int i = std::max(0, tailStart); i < total; ++i) {
        peak = std::max(peak, std::fabs(left[i]));
        if (!std::isfinite(left[i]) || !std::isfinite(right[i])) return NAN;
    }
    return peak;
}

/// Render `seconds` and return the left channel.
static std::vector<float> renderBuffer(r50::R50Engine &engine, double seconds) {
    const int total = static_cast<int>(seconds * kSR);
    std::vector<float> left(total, 0.0f), right(total, 0.0f);
    const int chunk = 137;
    for (int offset = 0; offset < total; offset += chunk) {
        const int frames = std::min(chunk, total - offset);
        engine.render(left.data() + offset, right.data() + offset, frames);
    }
    return left;
}

/// Number of samples analysed by the spectral helpers.
static constexpr int kAnalysisLength = 4096;

/// Hann window. Without it, rectangular-window leakage from the harmonics
/// swamps everything between them and no aliasing measurement is possible.
static const std::vector<double> &hannWindow() {
    static const std::vector<double> window = [] {
        std::vector<double> w(kAnalysisLength);
        for (int n = 0; n < kAnalysisLength; ++n)
            w[n] = 0.5 - 0.5 * std::cos(synth::kTwoPi * n / (kAnalysisLength - 1));
        return w;
    }();
    return window;
}

/// Magnitude of one windowed DFT bin at frequency f, starting at `from`.
/// Uses a phasor recurrence so the inner loop has no trig call — this is called
/// for hundreds of probe frequencies per test.
static double magAt(const std::vector<float> &buffer, double f, double sr,
                    int from) {
    const std::vector<double> &window = hannWindow();
    const double w = synth::kTwoPi * f / sr;
    const double cw = std::cos(w), sw = std::sin(w);
    double real = 1.0, imag = 0.0;
    double re = 0.0, im = 0.0;

    for (int n = 0; n < kAnalysisLength; ++n) {
        const double x = buffer[from + n] * window[n];
        re += x * real;
        im -= x * imag;
        const double nextReal = real * cw - imag * sw;
        const double nextImag = real * sw + imag * cw;
        real = nextReal;
        imag = nextImag;
    }
    return std::sqrt(re * re + im * im) / kAnalysisLength;
}

/// Open the filter and flatten the envelopes so a rendered tone is a steady,
/// unshaped view of the oscillator itself.
static void setupSpectralTone(r50::R50Engine &engine, int waveIndex) {
    engine.setSampleRate(kSR);
    engine.setParameter(R50ParamOscWave, static_cast<float>(waveIndex));
    engine.setParameter(R50ParamCutoff, 18000.0f);
    engine.setParameter(R50ParamResonance, 0.0f);
    engine.setParameter(R50ParamDrive, 0.0f);
    engine.setParameter(R50ParamSlope, 0.0f);
    engine.setParameter(R50ParamKeyTrack, 0.0f);
    engine.setParameter(R50ParamFilterEnvAmount, 0.0f);
    engine.setParameter(R50ParamAmpAttack, 0.001f);
    engine.setParameter(R50ParamAmpDecay, 0.01f);
    engine.setParameter(R50ParamAmpSustain, 1.0f);
    engine.setParameter(R50ParamOctave, 0.0f);
}

static double midiToHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

/// Worst inharmonic partial, relative to the fundamental.
///
/// Folded partials land at |k*f0 - n*sr|, which for an arbitrary f0 is
/// scattered — not at the midpoints between harmonics. So this sweeps a dense
/// grid of probe frequencies, skips anything close to a real harmonic, and
/// reports the loudest thing left. That is the aliasing.
static double aliasRatio(int waveIndex, int note) {
    r50::R50Engine engine;
    setupSpectralTone(engine, waveIndex);
    engine.noteOn(static_cast<uint8_t>(note), 100);

    const std::vector<float> buffer = renderBuffer(engine, 0.5);
    const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
    const double f0 = midiToHz(note);

    const double fundamental = magAt(buffer, f0, kSR, from);
    double worst = 0.0;
    for (double f = 40.0; f < kSR * 0.47; f += 20.0) {
        const double nearestHarmonic = std::round(f / f0) * f0;
        if (std::fabs(f - nearestHarmonic) < 0.3 * f0) continue;
        worst = std::max(worst, magAt(buffer, f, kSR, from));
    }
    return worst / (fundamental + 1e-12);
}

int main() {
    printf("R50 engine tests\n");

    // --- Basic voicing -----------------------------------------------------
    {
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.noteOn(60, 100);
        const float peak = render(engine, 0.25);
        check(std::isfinite(peak) && peak > 0.02f, "note on produces audio");

        engine.noteOff(60);
        const float tail = render(engine, 2.0, 0.2);
        check(tail < 0.001f, "note off decays to silence");
    }

    // --- Sustain pedal -----------------------------------------------------
    {
        // Key released under the pedal keeps sounding, then releases on
        // pedal-up.
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.sustainPedal(true);
        engine.noteOn(60, 100);
        render(engine, 0.1);
        engine.noteOff(60);
        const float sustained = render(engine, 0.3, 0.1);
        check(sustained > 0.02f, "pedal holds a released key");

        engine.sustainPedal(false);
        const float afterPedalUp = render(engine, 2.0, 0.2);
        check(afterPedalUp < 0.001f, "pedal up releases the held note");
    }
    {
        // Regression: a note retriggered while the pedal is down is physically
        // held, so pedal-up must NOT release it.
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.sustainPedal(true);
        engine.noteOn(60, 100);
        render(engine, 0.1);
        engine.noteOff(60);        // key up, pedal keeps the gate open
        engine.noteOn(60, 100);    // same key pressed again, still pedalled
        render(engine, 0.1);

        engine.sustainPedal(false);
        const float held = render(engine, 1.0, 0.2);
        check(held > 0.02f, "pedal up keeps a note retriggered under the pedal");

        engine.noteOff(60);
        const float released = render(engine, 2.0, 0.2);
        check(released < 0.001f, "that note still releases on its own key-up");
    }
    {
        // A second key pressed under the pedal and released before pedal-up
        // must still be released by pedal-up.
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.sustainPedal(true);
        engine.noteOn(64, 100);
        render(engine, 0.05);
        engine.noteOff(64);
        engine.sustainPedal(false);
        const float peak = render(engine, 2.0, 0.2);
        check(peak < 0.001f, "pedal up releases a key-up note pressed under it");
    }

    // --- All notes / sound off --------------------------------------------
    {
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        for (int note = 60; note < 68; ++note) engine.noteOn(note, 100);
        check(render(engine, 0.1) > 0.02f, "eight voices sound");
        engine.allSoundOff();
        check(render(engine, 0.05) < 0.001f, "all sound off silences immediately");

        engine.sustainPedal(true);
        engine.noteOn(60, 100);
        render(engine, 0.05);
        engine.allNotesOff();
        check(render(engine, 2.0, 0.2) < 0.001f, "all notes off ignores the pedal");
    }

    // --- Voice stealing ----------------------------------------------------
    {
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        for (int note = 48; note < 72; ++note) engine.noteOn(note, 100);  // 24 > 8
        const float peak = render(engine, 0.25);
        check(std::isfinite(peak) && peak > 0.02f,
              "over-allocating voices stays finite and audible");
    }

    // --- Parameters --------------------------------------------------------
    {
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.setParameter(R50ParamCutoff, 1234.0f);
        check(std::fabs(engine.getParameter(R50ParamCutoff) - 1234.0f) < 1e-3f,
              "parameter round-trips through the store");

        // A parameter written between render calls must reach the voices.
        engine.noteOn(60, 100);
        render(engine, 0.1);
        engine.setParameter(R50ParamMasterGain, 0.0f);
        check(render(engine, 0.2, 0.05) < 0.001f,
              "master gain takes effect on the next control block");

        engine.setParameter(R50ParamMasterGain, 0.8f);
        check(render(engine, 0.2, 0.05) > 0.02f, "master gain restores");

        // Out-of-range addresses must be ignored, not written past the store.
        engine.setParameter(R50ParamCount, 1.0f);
        engine.setParameter(9999, 1.0f);
        check(engine.getParameter(9999) == 0.0f, "out-of-range address is ignored");
    }

    // --- Wave tables: mip pyramid ------------------------------------------
    {
        // Each level must hold strictly fewer harmonics than the one below it,
        // or the pyramid is not band-limiting anything.
        bool decreasing = true;
        for (int level = 1; level < r50::kWaveNumLevels; ++level) {
            if (r50::waveMaxHarmonic(level) > r50::waveMaxHarmonic(level - 1))
                decreasing = false;
        }
        check(decreasing, "mip levels hold monotonically fewer harmonics");

        // One level per octave, starting at 20 Hz.
        check(std::fabs(r50::waveLevelForFreq(20.0) - 0.0f) < 1e-4f
           && std::fabs(r50::waveLevelForFreq(40.0) - 1.0f) < 1e-4f
           && std::fabs(r50::waveLevelForFreq(320.0) - 4.0f) < 1e-4f,
              "fractional mip level tracks octaves above 20 Hz");

        // Crossfading the two adjacent levels must be continuous across a
        // level boundary — a step here is audible as a click during glide.
        const r50::WavePyramid &saw = r50::waveLibrary().pyramids[r50::kPyramidSaw];
        float maxJump = 0.0f;
        for (int i = 0; i < 512; ++i) {
            const double phase = i / 512.0;
            const float below = r50::waveSample(saw, 2.999f, phase);
            const float above = r50::waveSample(saw, 3.001f, phase);
            maxJump = std::max(maxJump, std::fabs(above - below));
        }
        check(maxJump < 0.02f, "mip crossfade is continuous across a boundary");

        // Every level normalised by the level-0 peak: no loudness step.
        float peak0 = 0.0f, peak5 = 0.0f;
        for (float x : saw.levels[0].samples) peak0 = std::max(peak0, std::fabs(x));
        for (float x : saw.levels[5].samples) peak5 = std::max(peak5, std::fabs(x));
        check(peak0 > 0.9f && peak0 < 1.1f && peak5 > 0.55f && peak5 < 1.1f,
              "pyramid levels share a common normalisation");
    }

    // --- Aliasing gate ------------------------------------------------------
    // The reason this phase exists: without PolyBLEP, nothing else band-limits
    // the output. Single-cycle tables must not fold energy back at high notes.
    {
        // Threshold is 0.5% of the fundamental. With the pyramid these measure
        // below 0.005%, so there is ~100x headroom; without it, saw at C8
        // reaches 11% and a 10% pulse 29%.
        const double limit = 0.005;
        const double sawHigh  = aliasRatio(0, 96);    // saw, C7
        const double sawTop   = aliasRatio(0, 108);   // saw, C8
        const double pulseTop = aliasRatio(3, 108);   // 10% pulse, C8
        const double bellTop  = aliasRatio(10, 108);  // bell: partials to the 27th
        const double vocalTop = aliasRatio(9, 96);    // vocal: dense low spectrum
        printf("       worst inharmonic partial, relative to fundamental:\n"
               "         saw C7=%.5f  saw C8=%.5f  pulse C8=%.5f"
               "  bell C8=%.5f  vocal C7=%.5f\n",
               sawHigh, sawTop, pulseTop, bellTop, vocalTop);
        check(sawHigh  < limit, "saw at C7 is free of audible aliasing");
        check(sawTop   < limit, "saw at C8 is free of audible aliasing");
        check(pulseTop < limit, "10% pulse at C8 is free of audible aliasing");
        check(bellTop  < limit, "bell at C8 is free of audible aliasing");
        check(vocalTop < limit, "vocal Ah at C7 is free of audible aliasing");
    }

    // --- Tuning -------------------------------------------------------------
    {
        bool tuned = true;
        for (int note : {36, 60, 84}) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.noteOn(static_cast<uint8_t>(note), 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.4);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;

            const double f0 = midiToHz(note);
            const double atF0      = magAt(buffer, f0, kSR, from);
            const double offPitch  = magAt(buffer, f0 * 1.4, kSR, from);
            if (!(atF0 > offPitch * 20.0)) tuned = false;
        }
        check(tuned, "fundamental lands on the expected frequency across octaves");
    }

    // --- Pulse width --------------------------------------------------------
    {
        // saw(t) - saw(t - 0.5) cancels every even harmonic; a 10% pulse does not.
        auto evenOddRatio = [](int waveIndex) {
            r50::R50Engine engine;
            setupSpectralTone(engine, waveIndex);
            engine.noteOn(48, 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.4);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(48);
            double even = 0.0, odd = 0.0;
            for (int k = 1; k <= 12; ++k) {
                const double m = magAt(buffer, k * f0, kSR, from);
                if (k & 1) odd += m; else even += m;
            }
            return even / (odd + 1e-12);
        };
        const double square = evenOddRatio(2);
        const double pulse  = evenOddRatio(3);
        printf("       even/odd: square=%.4f  pulse10=%.4f\n", square, pulse);
        check(square < 0.05, "square suppresses even harmonics");
        check(pulse  > 0.30, "10% pulse retains even harmonics");
    }

    // --- Every wave sounds and stays bounded --------------------------------
    {
        bool allGood = true;
        for (int wave = 0; wave < r50::kWaveCount; ++wave) {
            r50::R50Engine engine;
            setupSpectralTone(engine, wave);
            engine.noteOn(60, 100);
            const float peak = render(engine, 0.3);
            if (!std::isfinite(peak) || peak < 0.02f || peak > 1.05f) {
                printf("       wave %d out of range: peak=%.3f\n", wave, peak);
                allGood = false;
            }
        }
        check(allGood, "all 11 waves are audible and bounded");
    }

    // --- Every wave has a fundamental ---------------------------------------
    {
        // A spectrum generator that shapes harmonics without a source floor can
        // silence the fundamental entirely — the wave then reads an octave or
        // more too high. Caught exactly this in the Vocal Ah spectrum.
        bool allGood = true;
        for (int wave = 0; wave < r50::kWaveCount; ++wave) {
            r50::R50Engine engine;
            setupSpectralTone(engine, wave);
            engine.noteOn(48, 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.4);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(48);

            double strongest = 0.0;
            for (int k = 1; k <= 12; ++k)
                strongest = std::max(strongest, magAt(buffer, k * f0, kSR, from));
            const double fundamental = magAt(buffer, f0, kSR, from);

            if (fundamental < 0.10 * strongest) {
                printf("       wave %d fundamental is %.1f%% of its strongest "
                       "partial\n", wave, 100.0 * fundamental / (strongest + 1e-12));
                allGood = false;
            }
        }
        check(allGood, "every wave has real energy at its fundamental");
    }

    // --- Noise source -------------------------------------------------------
    {
        // Average magnitude over a band of probes: a single DFT bin of noise is
        // far too variable to assert on.
        auto bandMagnitude = [](const std::vector<float> &buffer, double centre,
                                int from) {
            double sum = 0.0;
            int count = 0;
            for (int i = -10; i <= 10; ++i) {
                sum += magAt(buffer, centre * (1.0 + i * 0.02), kSR, from);
                ++count;
            }
            return sum / count;
        };

        // Slope over four octaves, 200 Hz -> 3200 Hz. White is flat, pink
        // -3 dB/oct (0.25x), brown -6 (0.06x), blue +3 (4x), violet +6 (16x).
        auto slope = [&](int spectrum) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamNoiseMix, 1.0f);       // noise only
            engine.setParameter(R50ParamNoiseSpectrum, static_cast<float>(spectrum));
            engine.noteOn(60, 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.6);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            return bandMagnitude(buffer, 3200.0, from)
                 / (bandMagnitude(buffer, 200.0, from) + 1e-12);
        };

        const double white  = slope(0);
        const double pink   = slope(1);
        const double brown  = slope(2);
        const double blue   = slope(3);
        const double violet = slope(4);
        printf("       noise 3200/200 Hz ratio: white=%.3f pink=%.3f brown=%.3f "
               "blue=%.3f violet=%.3f\n", white, pink, brown, blue, violet);

        check(white > 0.55 && white < 1.8,  "white noise is spectrally flat");
        check(pink  > 0.12 && pink  < 0.50, "pink noise falls ~3 dB/octave");
        check(brown < 0.20,                 "brown noise falls ~6 dB/octave");
        check(blue  > 2.0,                  "blue noise rises ~3 dB/octave");
        check(violet > 6.0,                 "violet noise rises ~6 dB/octave");
        check(brown < pink && pink < white && white < blue && blue < violet,
              "noise colours are ordered by spectral tilt");
    }
    {
        // Band-passed noise tracking the note must put its energy near the
        // note, not at a fixed frequency.
        auto peakNear = [](double centre, int note) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamNoiseMix, 1.0f);
            engine.setParameter(R50ParamNoiseSpectrum, 5.0f);   // Band
            engine.setParameter(R50ParamNoiseTone, 0.25f);
            engine.setParameter(R50ParamNoisePitchTrack, 1.0f);
            engine.noteOn(static_cast<uint8_t>(note), 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.6);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            return magAt(buffer, centre, kSR, from);
        };
        // Tone 0.25 -> centre = f0 * 2.0.
        const double lowNote  = peakNear(midiToHz(48) * 2.0, 48);
        const double highNote = peakNear(midiToHz(48) * 2.0, 72);
        check(lowNote > highNote * 3.0,
              "band-passed noise follows the note when tracking is on");
    }
    {
        // Mix is a crossfade: 0 is the oscillator alone, 1 the noise alone.
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        engine.setParameter(R50ParamNoiseSpectrum, 0.0f);
        engine.noteOn(48, 100);
        const double f0 = midiToHz(48);

        engine.setParameter(R50ParamNoiseMix, 0.0f);
        std::vector<float> oscOnly = renderBuffer(engine, 0.4);
        const int fromA = static_cast<int>(oscOnly.size()) - kAnalysisLength;
        const double tonalDry = magAt(oscOnly, f0, kSR, fromA);

        engine.setParameter(R50ParamNoiseMix, 1.0f);
        std::vector<float> noiseOnly = renderBuffer(engine, 0.4);
        const int fromB = static_cast<int>(noiseOnly.size()) - kAnalysisLength;
        const double tonalWet = magAt(noiseOnly, f0, kSR, fromB);

        check(tonalDry > tonalWet * 10.0,
              "noise mix crossfades the oscillator away");
    }
    {
        // Voices must not share a noise stream, or an 8-note chord would sound
        // like one noise source 8 times louder rather than eight sources.
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        engine.setParameter(R50ParamNoiseMix, 1.0f);
        engine.noteOn(60, 100);
        const float onePeak = render(engine, 0.2);

        r50::R50Engine chord;
        setupSpectralTone(chord, 0);
        chord.setParameter(R50ParamNoiseMix, 1.0f);
        for (int note = 60; note < 68; ++note) chord.noteOn(note, 100);
        const float eightPeak = render(chord, 0.2);

        // Correlated voices would sum to ~8x; decorrelated ones to ~sqrt(8).
        check(eightPeak < onePeak * 6.0f,
              "voices use decorrelated noise streams");
    }

    // --- Sample player: loop-boundary interpolation -------------------------
    {
        // The gate for the sample engine. Build a ramp whose loop region is a
        // straight line when read cyclically; a reader whose taps wrap
        // correctly reproduces that line exactly, one whose taps run off the
        // end of the loop does not.
        r50::SampleData data;
        const int length = 600;
        data.samples.resize(length);
        for (int i = 0; i < length; ++i) {
            // Inside [100, 500) the signal is a pure sinusoid with exactly 8
            // periods across the loop, so it is continuous when wrapped.
            // Everything outside the loop is silence — deliberately NOT a
            // continuation of the sinusoid, so a reader whose taps run off the
            // end of the loop instead of wrapping produces a visible error.
            data.samples[i] = (i >= 100 && i < 500)
                ? std::sin(synth::kTwoPi * 8.0 * (i - 100) / 400.0)
                : 0.0f;
        }
        data.loopStart = 100;
        data.loopEnd   = 500;
        data.loopMode  = r50::LoopMode::Forward;
        data.sourceSampleRate = kSR;
        data.rootKey = 60;

        r50::SampleRegion region;
        region.rootKey = 60;

        r50::SamplePlayer player;
        player.start(&data, &region, 100.0f / length);   // start at the loop
        // A fractional rate is essential: at exactly 1.0 the interpolation
        // fraction is always zero, Catmull-Rom returns the centre tap alone,
        // and the wrapping of the neighbouring taps is never exercised.
        player.setPlaybackRatio(0.75, kSR);

        // Run several loop passes and compare against the analytic sinusoid,
        // tracking the expected read position by hand.
        double position = 100.0;
        double worst = 0.0;
        bool finite = true;
        for (int n = 0; n < 4000; ++n) {
            const float actual = player.process();
            const double expected =
                std::sin(synth::kTwoPi * 8.0 * (position - 100.0) / 400.0);
            if (!std::isfinite(actual)) finite = false;
            if (n > 600) worst = std::max(worst, std::fabs(actual - expected));

            position += 0.75;
            while (position >= 500.0) position -= 400.0;
        }
        printf("       loop-boundary worst interpolation error: %.6f\n", worst);
        check(finite, "looped playback stays finite");
        check(worst < 0.002,
              "interpolation taps wrap correctly across the loop boundary");
    }
    {
        // Continuity: no single-sample jump at the join beyond what the signal
        // itself contains.
        r50::SampleData data;
        const int length = 512;
        data.samples.resize(length);
        for (int i = 0; i < length; ++i)
            data.samples[i] = std::sin(synth::kTwoPi * 4.0 * i / length);
        data.loopStart = 0;
        data.loopEnd   = length;
        data.loopMode  = r50::LoopMode::Forward;
        data.sourceSampleRate = kSR;

        r50::SampleRegion region;
        r50::SamplePlayer player;
        player.start(&data, &region, 0.0f);
        player.setPlaybackRatio(1.0, kSR);

        float previous = player.process();
        float biggestStep = 0.0f;
        for (int n = 0; n < 3000; ++n) {
            const float value = player.process();
            biggestStep = std::max(biggestStep, std::fabs(value - previous));
            previous = value;
        }
        // One period of a 4-cycle/512-sample sine steps by at most ~0.05.
        check(biggestStep < 0.08f, "no discontinuity at the loop join");
    }
    {
        // Ping-pong reverses without repeating the turning sample.
        r50::SampleData data;
        const int length = 64;
        data.samples.resize(length);
        for (int i = 0; i < length; ++i) data.samples[i] = i / 63.0f;
        data.loopStart = 0;
        data.loopEnd   = length;
        data.loopMode  = r50::LoopMode::PingPong;
        data.sourceSampleRate = kSR;

        r50::SampleRegion region;
        r50::SamplePlayer player;
        player.start(&data, &region, 0.0f);
        player.setPlaybackRatio(1.0, kSR);

        float peak = 0.0f, trough = 1.0f;
        bool finite = true;
        for (int n = 0; n < 400; ++n) {
            const float value = player.process();
            if (!std::isfinite(value)) finite = false;
            peak = std::max(peak, value);
            trough = std::min(trough, value);
        }
        check(finite && peak > 0.9f && trough < 0.1f,
              "ping-pong sweeps the full loop in both directions");
    }
    {
        // One-shot must stop, not read past the end.
        r50::SampleData data;
        data.samples.assign(200, 0.5f);
        data.loopMode = r50::LoopMode::None;
        data.loopEnd  = 200;
        data.sourceSampleRate = kSR;

        r50::SampleRegion region;
        r50::SamplePlayer player;
        player.start(&data, &region, 0.0f);
        player.setPlaybackRatio(1.0, kSR);

        bool finite = true;
        for (int n = 0; n < 400; ++n) {
            const float value = player.process();
            if (!std::isfinite(value)) finite = false;
        }
        check(finite && player.isFinished(), "one-shot ends without overrunning");
    }

    // --- Region mapping -----------------------------------------------------
    {
        r50::Multisample instrument;
        instrument.regionCount = 3;
        instrument.regions[0] = {}; instrument.regions[0].lowKey = 0;
        instrument.regions[0].highKey = 53; instrument.regions[0].slot = 10;
        instrument.regions[1] = {}; instrument.regions[1].lowKey = 54;
        instrument.regions[1].highKey = 65; instrument.regions[1].slot = 11;
        instrument.regions[2] = {}; instrument.regions[2].lowKey = 66;
        instrument.regions[2].highKey = 127; instrument.regions[2].slot = 12;

        const bool boundaries =
            instrument.find(53, 100) && instrument.find(53, 100)->slot == 10 &&
            instrument.find(54, 100) && instrument.find(54, 100)->slot == 11 &&
            instrument.find(65, 100) && instrument.find(65, 100)->slot == 11 &&
            instrument.find(66, 100) && instrument.find(66, 100)->slot == 12;
        check(boundaries, "region lookup is correct at zone boundaries");

        r50::Multisample layered;
        layered.regionCount = 2;
        layered.regions[0] = {}; layered.regions[0].lowVelocity = 1;
        layered.regions[0].highVelocity = 63; layered.regions[0].slot = 20;
        layered.regions[1] = {}; layered.regions[1].lowVelocity = 64;
        layered.regions[1].highVelocity = 127; layered.regions[1].slot = 21;
        check(layered.find(60, 63) && layered.find(60, 63)->slot == 20 &&
              layered.find(60, 64) && layered.find(60, 64)->slot == 21,
              "region lookup splits on velocity");

        check(instrument.find(60, 0) == nullptr ||
              instrument.find(60, 0)->slot == 11,
              "out-of-range velocity does not match a keyed-only region wrongly");
    }

    // --- Generated factory content ------------------------------------------
    {
        const r50::SampleLibrary &library = r50::SampleLibrary::shared();
        check(library.instrumentCount() >= 9,
              "factory instruments are generated");

        // Every declared voicing and attack has to actually arrive. Running out
        // of sample slots is silent — addSample returns -1, the region is
        // skipped, and the instrument never appears — and when the Spectrum
        // waves pushed the count past the limit the casualty was the last
        // attack in the list, nowhere near the change that caused it.
        check(library.instrumentCount()
                  == r50::kSustainVoicingCount + r50::kAttackKindCount,
              "every declared instrument survives into the library");
        int assets = 0;
        for (int i = 0; i < library.instrumentCount(); ++i) {
            assets += library.instrument(i)->regionCount;
        }
        check(assets < r50::kMaxSampleSlots,
              "the library has room to spare for its own content");

        // Presets store an instrument index, so the order content is
        // registered in is a compatibility surface. Adding the Spectrum waves
        // to the sustain list moved every attack down by nine and silently
        // repointed all 29 presets; nothing failed except the sound.
        struct Anchor { int index; const char *name; };
        static const Anchor anchors[] = {
            {0,  "Choir"},   {12, "Fat Block"}, {13, "Mallet"},
            {22, "Slap Bass"}, {30, "Bow Scrape"},
        };
        bool ordered = true;
        for (const Anchor &anchor : anchors) {
            const r50::Multisample *instrument = library.instrument(anchor.index);
            if (instrument == nullptr
             || std::string(instrument->name) != anchor.name) {
                ordered = false;
                printf("       index %d is '%s', expected '%s'\n", anchor.index,
                       instrument ? instrument->name : "(none)", anchor.name);
            }
        }
        check(ordered, "instrument indices presets depend on have not moved");

        bool allGood = true;
        int loopedCount = 0;
        for (int i = 0; i < library.instrumentCount(); ++i) {
            const r50::Multisample *instrument = library.instrument(i);
            if (instrument == nullptr || instrument->regionCount == 0) {
                allGood = false;
                continue;
            }
            for (int r = 0; r < instrument->regionCount; ++r) {
                const r50::SampleData *data =
                    library.sample(instrument->regions[r].slot);
                if (data == nullptr || data->samples.empty()) { allGood = false; continue; }

                float peak = 0.0f;
                for (float value : data->samples) {
                    if (!std::isfinite(value)) allGood = false;
                    peak = std::max(peak, std::fabs(value));
                }
                // Sources are RMS-matched with a peak ceiling above unity, so
                // the bound tracks that convention rather than a bare 1.0.
                if (peak < 0.2f || peak > r50::kSourcePeakCeiling + 0.01f)
                    allGood = false;

                if (data->loopMode == r50::LoopMode::Forward) {
                    ++loopedCount;
                    // Seamless by construction: every partial is an integer
                    // multiple of sr/L, so s[L] == s[0] and the wrap is just
                    // one more step of the waveform. Compare it against the
                    // largest step the waveform takes anywhere — comparing it
                    // against the immediately preceding step instead is
                    // meaningless, because a loop that happens to end at a
                    // flat spot makes any legitimate step look like a jump.
                    const int last = static_cast<int>(data->loopEnd) - 1;
                    const float across =
                        std::fabs(data->samples[data->loopStart] - data->samples[last]);
                    float largestStep = 0.0f;
                    for (int n = 1; n <= last; ++n) {
                        largestStep = std::max(largestStep,
                            std::fabs(data->samples[n] - data->samples[n - 1]));
                    }
                    if (across > largestStep * 1.05f + 0.005f) allGood = false;
                }
            }
        }
        check(allGood, "generated assets are finite, audible and seamless");
        check(loopedCount >= 65, "sustains are generated for every key zone");

        // A zone that fails to generate leaves a hole in the map, and a note
        // landing in it is silent rather than wrong — easy to miss by ear at
        // the extremes of the keyboard. Caught exactly that: the top octave
        // had no region because the loop-length search never reached the
        // cycle counts a high root needs.
        bool fullyMapped = true;
        for (int i = 0; i < library.instrumentCount(); ++i) {
            const r50::Multisample *instrument = library.instrument(i);
            if (instrument == nullptr) { fullyMapped = false; continue; }
            for (int key = 0; key <= 127; ++key) {
                const r50::SampleRegion *region = instrument->find(key, 100);
                if (region == nullptr || library.sample(region->slot) == nullptr) {
                    printf("       instrument %d has no sample for key %d\n", i, key);
                    fullyMapped = false;
                    key = 127;   // one report per instrument is enough
                }
            }
        }
        check(fullyMapped, "every instrument covers all 128 keys");

        // Attack transients are one-shots: they must decay to near nothing by
        // their end, or they are sustains that happen to stop. Their whole job
        // is to be the first few tens of milliseconds of a sound.
        bool decays = true;
        int oneShots = 0;
        for (int i = 0; i < library.instrumentCount(); ++i) {
            const r50::Multisample *instrument = library.instrument(i);
            const r50::SampleData *data = library.sample(instrument->regions[0].slot);
            if (data == nullptr || data->loopMode != r50::LoopMode::None) continue;
            ++oneShots;

            const int count = static_cast<int>(data->samples.size());
            const int window = std::max(1, count / 10);
            double head = 0.0, tail = 0.0;
            for (int n = 0; n < window; ++n) head += std::fabs(data->samples[n]);
            for (int n = count - window; n < count; ++n) tail += std::fabs(data->samples[n]);

            const double seconds = count / data->sourceSampleRate;
            if (tail > head * 0.25 || seconds < 0.02 || seconds > 1.0) {
                printf("       %s: tail/head %.3f, %.0f ms\n",
                       instrument->name, tail / (head + 1e-12), seconds * 1000);
                decays = false;
            }
        }
        printf("       one-shot transients: %d\n", oneShots);
        check(decays, "every transient decays and is a sensible length");
        check(oneShots >= 18, "the transient set covers the attack families");

        // The loop repeats at (loop duration / transposition), and that rate
        // is what the ear hears as flutter. Keep it low across every zone.
        double fastestRepeat = 0.0;
        for (int i = 0; i < library.instrumentCount(); ++i) {
            const r50::Multisample *instrument = library.instrument(i);
            for (int r = 0; r < instrument->regionCount; ++r) {
                const r50::SampleRegion &region = instrument->regions[r];
                const r50::SampleData *data = library.sample(region.slot);
                if (data == nullptr || data->loopMode != r50::LoopMode::Forward)
                    continue;
                const double seconds = data->samples.size() / data->sourceSampleRate;
                const double ratio =
                    std::pow(2.0, (region.highKey - region.rootKey) / 12.0);
                fastestRepeat = std::max(fastestRepeat, ratio / seconds);
            }
        }
        printf("       fastest loop repeat across all zones: %.1f Hz\n", fastestRepeat);
        check(fastestRepeat < 5.0, "loop repetition stays below 5 Hz everywhere");
    }

    // --- Sample playback through the engine ---------------------------------
    {
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        engine.setParameter(R50ParamSourceType, 1.0f);       // sample
        engine.setParameter(R50ParamSampleInstrument, 0.0f); // Choir
        engine.noteOn(60, 100);
        const float peak = render(engine, 0.4);
        check(std::isfinite(peak) && peak > 0.02f, "sample source produces audio");

        engine.noteOff(60);
        check(render(engine, 2.0, 0.2) < 0.001f, "sample voice releases to silence");
    }
    {
        // Each zone must play at the pitch asked for, not at its root.
        auto fundamentalError = [](int note) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamSourceType, 1.0f);
            engine.setParameter(R50ParamSampleInstrument, 1.0f);   // Strings
            engine.noteOn(static_cast<uint8_t>(note), 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.5);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(note);
            const double atF0 = magAt(buffer, f0, kSR, from);
            const double detuned = magAt(buffer, f0 * 1.35, kSR, from);
            return atF0 / (detuned + 1e-12);
        };
        // One note per zone: below 54, inside 54..65, above 65.
        const bool tuned = fundamentalError(45) > 8.0
                        && fundamentalError(60) > 8.0
                        && fundamentalError(72) > 8.0;
        check(tuned, "every key zone plays at the requested pitch");
    }

    // --- Partial addressing -------------------------------------------------
    {
        // Partial 1 must still live at the original addresses, or every saved
        // preset and automation lane silently means something else.
        const struct { R50PartialField field; R50Param legacy; } mapping[] = {
            {R50FieldSourceType, R50ParamSourceType},
            {R50FieldSampleInstrument, R50ParamSampleInstrument},
            {R50FieldSampleStart, R50ParamSampleStart},
            {R50FieldOscWave, R50ParamOscWave},
            {R50FieldPulseWidth, R50ParamPulseWidth},
            {R50FieldOctave, R50ParamOctave},
            {R50FieldNoiseMix, R50ParamNoiseMix},
            {R50FieldNoiseSpectrum, R50ParamNoiseSpectrum},
            {R50FieldNoiseTone, R50ParamNoiseTone},
            {R50FieldNoiseRate, R50ParamNoiseRate},
            {R50FieldNoisePitchTrack, R50ParamNoisePitchTrack},
            {R50FieldCutoff, R50ParamCutoff},
            {R50FieldResonance, R50ParamResonance},
            {R50FieldDrive, R50ParamDrive},
            {R50FieldSlope, R50ParamSlope},
            {R50FieldKeyTrack, R50ParamKeyTrack},
            {R50FieldFilterEnvAmount, R50ParamFilterEnvAmount},
            {R50FieldAmpAttack, R50ParamAmpAttack},
            {R50FieldAmpDecay, R50ParamAmpDecay},
            {R50FieldAmpSustain, R50ParamAmpSustain},
            {R50FieldAmpRelease, R50ParamAmpRelease},
            {R50FieldFilterAttack, R50ParamFilterAttack},
            {R50FieldFilterDecay, R50ParamFilterDecay},
            {R50FieldFilterSustain, R50ParamFilterSustain},
            {R50FieldFilterRelease, R50ParamFilterRelease},
        };
        bool stable = true;
        for (const auto &entry : mapping) {
            if (r50PartialParam(0, entry.field) != entry.legacy) stable = false;
        }
        check(stable, "Partial 1 keeps the original parameter addresses");

        // Partial 2's block must be contiguous, in range, and not collide.
        bool blockOk = true;
        for (int f = 0; f < R50PartialFieldCount; ++f) {
            const R50Param address =
                r50PartialParam(1, static_cast<R50PartialField>(f));
            if (address != R50ParamP2Base + f || address >= R50ParamCount) {
                blockOk = false;
            }
            if (r50PartialParam(0, static_cast<R50PartialField>(f)) == address) {
                blockOk = false;   // the two Partials must never share storage
            }
        }
        check(blockOk, "Partial 2 occupies its own contiguous block");
    }

    // --- Partial 2 disabled reproduces the single-Partial engine ------------
    {
        // The refactor must move code without changing sound. Partial 2 is off
        // by default, so a default patch has to render exactly as before.
        auto renderPatch = [](int wave) {
            r50::R50Engine engine;
            setupSpectralTone(engine, wave);
            engine.noteOn(60, 100);
            engine.noteOn(64, 90);
            return renderBuffer(engine, 0.4);
        };
        const std::vector<float> a = renderPatch(0);
        const std::vector<float> b = renderPatch(0);
        check(a == b && !a.empty(), "default two-Partial engine is deterministic");

        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        check(engine.getParameter(r50PartialParam(1, R50FieldEnabled)) < 0.5f,
              "Partial 2 is disabled by default");

        engine.noteOn(60, 100);
        const float withPartial2Off = render(engine, 0.3);
        check(std::isfinite(withPartial2Off) && withPartial2Off > 0.02f,
              "a one-Partial patch still sounds");
    }

    // --- Tone structures ----------------------------------------------------
    {
        // Set up both Partials with the same wave so the structures are
        // comparable, then measure what each one does.
        auto twoPartialPeak = [](int structure, float velocity,
                                 int note, float p2Level) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
            engine.setParameter(r50PartialParam(1, R50FieldOscWave), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldLevel), p2Level);
            engine.setParameter(r50PartialParam(1, R50FieldCutoff), 18000.0f);
            engine.setParameter(r50PartialParam(1, R50FieldFilterEnvAmount), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldKeyTrack), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldAmpAttack), 0.001f);
            engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
            engine.setParameter(R50ParamToneStructure,
                                static_cast<float>(structure));
            engine.noteOn(static_cast<uint8_t>(note),
                          static_cast<uint8_t>(velocity * 127.0f));
            return render(engine, 0.3, 0.1);
        };

        // Mix: adding a second Partial adds level.
        const float one = twoPartialPeak(0, 0.8f, 60, 0.0f);
        const float two = twoPartialPeak(0, 0.8f, 60, 1.0f);
        check(two > one * 1.4f, "Mix sums both Partials");

        // Velocity crossfade: low velocity favours Partial 1, high favours 2.
        // With only Partial 2 carrying level, loud notes must be louder.
        const float soft = twoPartialPeak(3, 0.15f, 60, 1.0f);
        const float loud = twoPartialPeak(3, 0.99f, 60, 1.0f);
        check(loud > soft, "velocity crossfade moves towards Partial 2");

        // Key crossfade: same, across the key range 48..72.
        const float low  = twoPartialPeak(4, 0.8f, 48, 1.0f);
        const float high = twoPartialPeak(4, 0.8f, 72, 1.0f);
        check(std::isfinite(low) && std::isfinite(high) && high > 0.0f && low > 0.0f,
              "key crossfade renders across the zone");
    }
    {
        // Ring modulation: two Partials a fifth apart must produce sum and
        // difference frequencies that neither source contains.
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        engine.setParameter(r50PartialParam(0, R50FieldNoiseMix), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
        engine.setParameter(r50PartialParam(1, R50FieldOscWave), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldSemitone), 7.0f);
        engine.setParameter(r50PartialParam(1, R50FieldCutoff), 18000.0f);
        engine.setParameter(r50PartialParam(1, R50FieldFilterEnvAmount), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldKeyTrack), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
        engine.setParameter(R50ParamToneStructure, 1.0f);   // RingMod
        engine.setParameter(R50ParamToneRingLevel, 1.0f);
        engine.noteOn(48, 100);

        const std::vector<float> buffer = renderBuffer(engine, 0.5);
        const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
        const double f1 = midiToHz(48);
        const double f2 = midiToHz(55);
        const double sum  = magAt(buffer, f1 + f2, kSR, from);
        const double diff = magAt(buffer, f2 - f1, kSR, from);
        const double carrier = magAt(buffer, f1, kSR, from);
        printf("       ringmod: f1=%.4f sum=%.4f diff=%.4f\n", carrier, sum, diff);
        check(sum > carrier * 0.02 && diff > carrier * 0.02,
              "ring modulation produces sum and difference frequencies");
    }
    {
        // How much does ring modulation alias? The multiply doubles bandwidth
        // and this phase does not oversample, so the size of the problem is
        // measured rather than assumed. Tuning Partial 2 an octave above
        // Partial 1 makes every ring product a harmonic of Partial 1, so
        // anything landing off that grid is folded energy and nothing else.
        auto ringAlias = [](int note) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
            engine.setParameter(r50PartialParam(1, R50FieldOscWave), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldSemitone), 12.0f);
            engine.setParameter(r50PartialParam(1, R50FieldCutoff), 18000.0f);
            engine.setParameter(r50PartialParam(1, R50FieldFilterEnvAmount), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldKeyTrack), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
            engine.setParameter(R50ParamToneStructure, 1.0f);
            engine.setParameter(R50ParamToneRingLevel, 1.0f);
            engine.noteOn(static_cast<uint8_t>(note), 100);

            const std::vector<float> buffer = renderBuffer(engine, 0.5);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(note);
            const double fundamental = magAt(buffer, f0, kSR, from);
            double worst = 0.0;
            for (double f = 40.0; f < kSR * 0.47; f += 20.0) {
                const double nearest = std::round(f / f0) * f0;
                if (std::fabs(f - nearest) < 0.3 * f0) continue;
                worst = std::max(worst, magAt(buffer, f, kSR, from));
            }
            return worst / (fundamental + 1e-12);
        };
        const double low  = ringAlias(48);
        const double high = ringAlias(84);
        printf("       ring-mod off-grid energy: C3=%.4f  C6=%.4f "
               "(no oversampling yet)\n", low, high);
        check(std::isfinite(low) && std::isfinite(high),
              "ring modulation stays finite across the range");
    }
    {
        // AttackSustain hands over from Partial 1 to Partial 2 within blendTime.
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        // Partial 1 silent, Partial 2 loud: the output must therefore *rise*
        // as the blend moves across.
        engine.setParameter(r50PartialParam(0, R50FieldLevel), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
        engine.setParameter(r50PartialParam(1, R50FieldLevel), 1.0f);
        engine.setParameter(r50PartialParam(1, R50FieldOscWave), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldCutoff), 18000.0f);
        engine.setParameter(r50PartialParam(1, R50FieldFilterEnvAmount), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldKeyTrack), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldAmpAttack), 0.001f);
        engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
        engine.setParameter(R50ParamToneStructure, 2.0f);   // AttackSustain
        engine.setParameter(R50ParamToneBlendTime, 0.20f);
        engine.noteOn(60, 100);

        const std::vector<float> buffer = renderBuffer(engine, 0.6);
        auto peakBetween = [&buffer](double fromSec, double toSec) {
            float peak = 0.0f;
            for (int n = static_cast<int>(fromSec * kSR);
                 n < static_cast<int>(toSec * kSR) && n < (int)buffer.size(); ++n) {
                peak = std::max(peak, std::fabs(buffer[n]));
            }
            return peak;
        };
        const float early = peakBetween(0.00, 0.03);
        const float late  = peakBetween(0.35, 0.55);
        printf("       attack/sustain handover: early=%.4f late=%.4f\n", early, late);
        check(late > early * 3.0f, "AttackSustain hands over to Partial 2");
    }

    // --- Panning ------------------------------------------------------------
    {
        auto channelPeaks = [](float pan) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(0, R50FieldPan), pan);
            engine.noteOn(60, 100);

            const int total = static_cast<int>(0.3 * kSR);
            std::vector<float> left(total, 0.0f), right(total, 0.0f);
            for (int offset = 0; offset < total; offset += 137) {
                const int frames = std::min(137, total - offset);
                engine.render(left.data() + offset, right.data() + offset, frames);
            }
            float peakL = 0.0f, peakR = 0.0f;
            for (int n = 0; n < total; ++n) {
                peakL = std::max(peakL, std::fabs(left[n]));
                peakR = std::max(peakR, std::fabs(right[n]));
            }
            return std::pair<float, float>(peakL, peakR);
        };

        const auto centre = channelPeaks(0.0f);
        const auto hardLeft = channelPeaks(-1.0f);
        const auto hardRight = channelPeaks(1.0f);
        check(std::fabs(centre.first - centre.second) < 0.001f,
              "a centred Partial is balanced");
        check(hardLeft.first > hardLeft.second * 20.0f,
              "pan -1 places the Partial in the left channel");
        check(hardRight.second > hardRight.first * 20.0f,
              "pan +1 places the Partial in the right channel");
    }

    // --- Mixed sources across Partials --------------------------------------
    {
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        // Partial 1: sample. Partial 2: noise.
        engine.setParameter(r50PartialParam(0, R50FieldSourceType), 1.0f);
        engine.setParameter(r50PartialParam(0, R50FieldSampleInstrument), 0.0f);
        engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
        engine.setParameter(r50PartialParam(1, R50FieldNoiseMix), 1.0f);
        engine.setParameter(r50PartialParam(1, R50FieldCutoff), 12000.0f);
        engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
        engine.noteOn(60, 100);
        const float peak = render(engine, 0.4);
        check(std::isfinite(peak) && peak > 0.02f && peak <= 1.05f,
              "sample and noise Partials render together");
    }

    // --- Workstation EG -----------------------------------------------------
    {
        // Sample the envelope's trajectory so each segment can be checked at
        // the point it should have arrived.
        auto trajectory = [](float attackLevel, float breakPoint, float slope,
                             float sustain, int samples) {
            r50::R50Envelope env;
            env.setSampleRate(kSR);
            env.setAttack(0.01f);
            env.setAttackLevel(attackLevel);
            env.setDecay(0.05f);
            env.setBreakPoint(breakPoint);
            env.setSlope(slope);
            env.setSustain(sustain);
            env.setRelease(0.1f);
            env.gate(true);
            std::vector<float> out(samples);
            for (int n = 0; n < samples; ++n) out[n] = env.process();
            return out;
        };

        // A long slope holds the envelope near the break point long enough to
        // observe it. With a short slope the decay has already finished and the
        // slope is well underway by the time it could be sampled.
        const std::vector<float> held =
            trajectory(1.0f, 0.6f, 4.0f, 0.2f, static_cast<int>(0.2 * kSR));
        const float peak = *std::max_element(held.begin(), held.end());
        const float atBreak = held[static_cast<int>(0.065 * kSR)];

        // A short slope settles on sustain quickly.
        const std::vector<float> settled =
            trajectory(1.0f, 0.6f, 0.2f, 0.2f, static_cast<int>(0.5 * kSR));
        const float atSustain = settled[static_cast<int>(0.45 * kSR)];

        printf("       EG: peak=%.3f break=%.3f sustain=%.3f\n",
               peak, atBreak, atSustain);
        check(peak > 0.98f, "EG attack reaches the attack level");
        check(std::fabs(atBreak - 0.6f) < 0.05f, "EG decay lands on the break point");
        check(std::fabs(atSustain - 0.2f) < 0.02f, "EG slope settles on sustain");

        // Attack level below full caps the peak.
        const std::vector<float> capped =
            trajectory(0.5f, 1.0f, 0.0f, 0.4f, static_cast<int>(0.1 * kSR));
        check(*std::max_element(capped.begin(), capped.end()) < 0.55f,
              "EG attack level caps the peak");

        // A break point below sustain dips and recovers — a legitimate shape,
        // and the one that catches a segment which only handles falling.
        const std::vector<float> dip =
            trajectory(1.0f, 0.1f, 0.15f, 0.6f, static_cast<int>(0.5 * kSR));
        check(std::fabs(dip[static_cast<int>(0.45 * kSR)] - 0.6f) < 0.03f,
              "EG recovers when the break point sits below sustain");
    }
    {
        // With slope at the minimum the EG must behave exactly as the ADSR it
        // replaced — this is what keeps every existing preset intact.
        r50::R50Envelope eg;
        synth::ADSR adsr;
        eg.setSampleRate(kSR);  adsr.setSampleRate(kSR);
        eg.setAttack(0.02f);    adsr.setAttack(0.02f);
        eg.setDecay(0.3f);      adsr.setDecay(0.3f);
        eg.setSustain(0.45f);   adsr.setSustain(0.45f);
        eg.setRelease(0.25f);   adsr.setRelease(0.25f);
        eg.setAttackLevel(1.0f);
        eg.setBreakPoint(1.0f);
        eg.setSlope(0.0f);      // break stage disabled
        eg.gate(true);          adsr.gate(true);

        double worst = 0.0;
        for (int n = 0; n < static_cast<int>(1.5 * kSR); ++n) {
            if (n == static_cast<int>(1.0 * kSR)) { eg.gate(false); adsr.gate(false); }
            worst = std::max(worst,
                std::fabs(static_cast<double>(eg.process()) - adsr.process()));
        }
        printf("       EG vs ADSR worst difference: %.3e\n", worst);
        check(worst < 1e-6, "EG with slope off reproduces plain ADSR");
    }

    // --- Pitch envelope -----------------------------------------------------
    {
        auto pitchAt = [](double seconds, float amount) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(0, R50FieldPitchAmount), amount);
            engine.setParameter(r50PartialParam(0, R50FieldPitchAttack), 0.001f);
            engine.setParameter(r50PartialParam(0, R50FieldPitchDecay), 0.25f);
            engine.noteOn(60, 100);
            const std::vector<float> buffer = renderBuffer(engine, seconds + 0.2);
            // Analyse a window starting at `seconds`.
            const int from = static_cast<int>(seconds * kSR);
            const double f0 = midiToHz(60);
            const double atPitch = magAt(buffer, f0, kSR, from);
            const double aboveIt = magAt(buffer, f0 * 1.5, kSR, from);
            return atPitch / (aboveIt + 1e-12);
        };
        // +12 semitones at note-on: the fundamental is absent early on and
        // present once the envelope has decayed.
        const double early = pitchAt(0.01, 12.0f);
        const double late  = pitchAt(0.60, 12.0f);
        printf("       pitch env: f0 ratio early=%.2f late=%.2f\n", early, late);
        check(late > early * 2.0, "pitch envelope bends the note and returns");
    }

    // --- Waveshaper ---------------------------------------------------------
    {
        auto shaperEnergy = [](int type, int note) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(0, R50FieldShaperType),
                                static_cast<float>(type));
            engine.setParameter(r50PartialParam(0, R50FieldShaperDrive), 0.7f);
            engine.setParameter(r50PartialParam(0, R50FieldAmpSustain), 1.0f);
            engine.noteOn(static_cast<uint8_t>(note), 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.5);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(note);

            double harmonic = 0.0, offGrid = 0.0;
            const double fundamental = magAt(buffer, f0, kSR, from);
            for (int k = 2; k * f0 < kSR * 0.47; ++k)
                harmonic += magAt(buffer, k * f0, kSR, from);
            for (double f = 40.0; f < kSR * 0.47; f += 20.0) {
                const double nearest = std::round(f / f0) * f0;
                if (std::fabs(f - nearest) < 0.3 * f0) continue;
                offGrid = std::max(offGrid, magAt(buffer, f, kSR, from));
            }
            return std::tuple<double, double, double>(
                fundamental, harmonic, offGrid / (fundamental + 1e-12));
        };

        static const char *names[] = {"Off", "SoftClip", "HardClip", "Fold", "Rectify"};
        bool allGood = true;
        double worstAlias = 0.0;
        for (int type = 1; type < r50::kShaperTypeCount; ++type) {
            const auto low  = shaperEnergy(type, 48);
            const auto high = shaperEnergy(type, 84);
            const double harmonics = std::get<1>(low);
            const double alias = std::max(std::get<2>(low), std::get<2>(high));
            worstAlias = std::max(worstAlias, alias);
            printf("       shaper %-9s harmonics=%.4f  worst off-grid=%.4f\n",
                   names[type], harmonics, alias);
            if (!std::isfinite(harmonics) || harmonics <= 0.0) allGood = false;
        }
        check(allGood, "every waveshaper type adds harmonics and stays finite");
        // Folding measured 21% before its depth was bounded; this holds that.
        printf("       worst shaper aliasing across all types: %.4f\n", worstAlias);
        check(worstAlias < 0.10, "no waveshaper aliases beyond 10% of the fundamental");

        // Bounded output regardless of type or drive.
        bool bounded = true;
        for (int type = 0; type < r50::kShaperTypeCount; ++type) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(0, R50FieldShaperType),
                                static_cast<float>(type));
            engine.setParameter(r50PartialParam(0, R50FieldShaperDrive), 1.0f);
            engine.setParameter(r50PartialParam(0, R50FieldShaperPosition), 1.0f);
            engine.noteOn(48, 127);
            const float peak = render(engine, 0.3);
            if (!std::isfinite(peak) || peak > 1.05f) bounded = false;
        }
        check(bounded, "waveshapers stay bounded at full drive, post-filter");
    }

    // --- Effects ------------------------------------------------------------
    {
        // Every effect defaults to silent, so a patch that names none of them
        // must render exactly as it did before the rack existed. This is the
        // gate: a rack that colours the signal at zero mix would change every
        // preset in the instrument.
        auto renderWith = [](int address, float value) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            if (address >= 0) engine.setParameter(address, value);
            engine.noteOn(60, 100);
            return renderBuffer(engine, 0.4);
        };
        const std::vector<float> dry = renderWith(-1, 0);
        check(!dry.empty(), "dry render produced audio");

        for (int address : {R50ParamFxChorusMix, R50ParamFxDelayMix,
                            R50ParamFxReverbMix, R50ParamFxCompressor}) {
            const std::vector<float> zeroed = renderWith(address, 0.0f);
            check(zeroed == dry, "effect at zero mix is a true bypass");
        }

        // And each one audibly changes the signal when turned up.
        bool allChange = true;
        for (int address : {R50ParamFxChorusMix, R50ParamFxDelayMix,
                            R50ParamFxReverbMix}) {
            const std::vector<float> wet = renderWith(address, 0.8f);
            double difference = 0.0;
            for (size_t n = 0; n < wet.size(); ++n)
                difference = std::max(difference,
                                      std::fabs(double(wet[n]) - dry[n]));
            if (difference < 0.005) allChange = false;
        }
        check(allChange, "each effect changes the signal when mixed in");
    }
    {
        // Tails must stay bounded and eventually die. A feedback path that does
        // not decay is the classic way an effects rack ruins a session.
        r50::R50Engine engine;
        setupSpectralTone(engine, 0);
        engine.setParameter(R50ParamFxDelayMix, 0.7f);
        engine.setParameter(R50ParamFxDelayFeedback, 0.85f);
        engine.setParameter(R50ParamFxReverbMix, 0.7f);
        engine.setParameter(R50ParamFxReverbDecay, 6.0f);
        engine.noteOn(60, 110);
        render(engine, 0.5);
        engine.noteOff(60);

        const float early = render(engine, 2.0, 0.2);
        const float late  = render(engine, 20.0, 0.2);
        printf("       fx tail: at 2 s=%.5f  at 22 s=%.5f\n", early, late);
        check(std::isfinite(early) && std::isfinite(late) && early <= 1.05f,
              "effect tails stay finite and bounded");
        check(late < early, "effect tails decay rather than sustain");
    }
    {
        // The compressor should reduce the level of a loud passage.
        auto peakWith = [](float compressor) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamFxCompressor, compressor);
            for (int note = 55; note < 63; ++note) engine.noteOn(note, 127);
            return render(engine, 0.6, 0.2);
        };
        const float open = peakWith(0.0f);
        const float squashed = peakWith(1.0f);
        printf("       compressor: off=%.4f  full=%.4f\n", open, squashed);
        check(squashed < open, "the compressor reduces a loud passage");
    }

    // --- Modulation ---------------------------------------------------------
    {
        // Nothing is routed by default, so the matrix must be inert. A
        // modulation system that is not fully off would change every patch in
        // the instrument the moment it shipped.
        auto renderDefault = [] {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.noteOn(60, 100);
            return renderBuffer(engine, 0.3);
        };
        const std::vector<float> quiet = renderDefault();

        r50::R50Engine routed;
        setupSpectralTone(routed, 0);
        // A slot with a source and destination but zero amount must also be
        // inert — amount is what arms it.
        routed.setParameter(r50ModSlotParam(0, R50ModFieldSource),
                            float(int(r50::ModSource::Lfo1)));
        routed.setParameter(r50ModSlotParam(0, R50ModFieldDestination),
                            float(int(r50::ModDestination::Cutoff)));
        routed.setParameter(r50ModSlotParam(0, R50ModFieldAmount), 0.0f);
        routed.noteOn(60, 100);
        check(renderBuffer(routed, 0.3) == quiet,
              "a slot with zero amount changes nothing");
    }
    {
        // Each destination must actually move.
        auto renderRouted = [](r50::ModSource source, r50::ModDestination dest,
                               float amount, float lfoRate, int wave = 0) {
            r50::R50Engine engine;
            setupSpectralTone(engine, wave);
            engine.setParameter(R50ParamLfo1Rate, lfoRate);
            engine.setParameter(r50ModSlotParam(0, R50ModFieldSource),
                                float(int(source)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldDestination),
                                float(int(dest)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldAmount), amount);
            engine.noteOn(60, 100);
            return renderBuffer(engine, 0.5);
        };

        struct Case {
            r50::ModSource source; r50::ModDestination dest;
            const char *name; int wave;
        };
        const Case cases[] = {
            {r50::ModSource::Lfo1, r50::ModDestination::Pitch, "pitch", 0},
            {r50::ModSource::Lfo1, r50::ModDestination::Cutoff, "cutoff", 0},
            {r50::ModSource::Lfo1, r50::ModDestination::Level, "level", 0},
            // Pulse width only exists for the difference-read waves, so this
            // one has to be tested on the variable pulse rather than a saw.
            {r50::ModSource::Lfo1, r50::ModDestination::PulseWidth, "pulse width", 4},
            {r50::ModSource::Lfo1, r50::ModDestination::WaveIndex, "wave index", 0},
            {r50::ModSource::Velocity, r50::ModDestination::Cutoff, "velocity to cutoff", 0},
            {r50::ModSource::AmpEnv, r50::ModDestination::Cutoff, "amp env to cutoff", 0},
        };
        bool allMove = true;
        for (const Case &test : cases) {
            const std::vector<float> reference =
                renderRouted(r50::ModSource::None, r50::ModDestination::None,
                             0, 5, test.wave);
            const std::vector<float> wet =
                renderRouted(test.source, test.dest, 0.8f, 5, test.wave);
            double difference = 0.0;
            for (size_t n = 0; n < wet.size(); ++n)
                difference = std::max(difference,
                                      std::fabs(double(wet[n]) - reference[n]));
            if (difference < 0.005) {
                printf("       %s did not move the signal\n", test.name);
                allMove = false;
            }
        }
        check(allMove, "every routed destination changes the signal");

        // Amount is bipolar: opposite signs must move opposite ways.
        auto meanPitch = [&](float amount) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamLfo1Wave, 0);      // sine
            engine.setParameter(R50ParamLfo1Rate, 0.5f);
            engine.setParameter(R50ParamLfo1Phase, 0.25f); // hold near the peak
            engine.setParameter(r50ModSlotParam(0, R50ModFieldSource),
                                float(int(r50::ModSource::Lfo1)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldDestination),
                                float(int(r50::ModDestination::Pitch)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldAmount), amount);
            engine.noteOn(60, 100);
            const std::vector<float> buffer = renderBuffer(engine, 0.25);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            const double f0 = midiToHz(60);
            return magAt(buffer, f0 * 1.15, kSR, from)
                 - magAt(buffer, f0 / 1.15, kSR, from);
        };
        check(meanPitch(0.5f) * meanPitch(-0.5f) < 0.0,
              "amount is bipolar: opposite signs bend opposite ways");
    }
    {
        // Two slots on one destination sum rather than the last one winning.
        auto cutoffEnergy = [](float amountA, float amountB) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamCutoff, 1200.0f);
            for (int slot = 0; slot < 2; ++slot) {
                engine.setParameter(r50ModSlotParam(slot, R50ModFieldSource),
                                    float(int(r50::ModSource::Velocity)));
                engine.setParameter(r50ModSlotParam(slot, R50ModFieldDestination),
                                    float(int(r50::ModDestination::Cutoff)));
            }
            engine.setParameter(r50ModSlotParam(0, R50ModFieldAmount), amountA);
            engine.setParameter(r50ModSlotParam(1, R50ModFieldAmount), amountB);
            engine.noteOn(60, 127);
            const std::vector<float> buffer = renderBuffer(engine, 0.4);
            const int from = static_cast<int>(buffer.size()) - kAnalysisLength;
            double high = 0.0;
            const double f0 = midiToHz(60);
            for (int k = 6; k * f0 < 12000; ++k) high += magAt(buffer, k * f0, kSR, from);
            return high;
        };
        const double one = cutoffEnergy(0.3f, 0.0f);
        const double two = cutoffEnergy(0.3f, 0.3f);
        printf("       summed slots: one=%.4f two=%.4f\n", one, two);
        check(two > one * 1.2, "two slots on one destination sum");
    }
    {
        // Targeting one Partial must leave the other alone.
        auto render = [](int target) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(r50PartialParam(1, R50FieldEnabled), 1.0f);
            engine.setParameter(r50PartialParam(1, R50FieldOscWave), 0.0f);
            engine.setParameter(r50PartialParam(1, R50FieldAmpSustain), 1.0f);
            engine.setParameter(r50ModSlotParam(0, R50ModFieldSource),
                                float(int(r50::ModSource::Velocity)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldDestination),
                                float(int(r50::ModDestination::Pitch)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldTarget),
                                static_cast<float>(target));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldAmount), 0.5f);
            engine.noteOn(60, 127);
            return renderBuffer(engine, 0.3);
        };
        const std::vector<float> both = render(0);
        const std::vector<float> onlyFirst = render(1);
        check(both != onlyFirst, "slot target selects which Partial is modulated");
    }
    {
        // The mod wheel has to reach the matrix from MIDI.
        auto wheelPeak = [](bool raise) {
            r50::R50Engine engine;
            setupSpectralTone(engine, 0);
            engine.setParameter(R50ParamCutoff, 900.0f);
            engine.setParameter(r50ModSlotParam(0, R50ModFieldSource),
                                float(int(r50::ModSource::ModWheel)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldDestination),
                                float(int(r50::ModDestination::Cutoff)));
            engine.setParameter(r50ModSlotParam(0, R50ModFieldAmount), 0.9f);
            if (raise) engine.modWheel(1.0f);
            engine.noteOn(60, 100);
            return render(engine, 0.3, 0.1);
        };
        check(wheelPeak(true) != wheelPeak(false),
              "the mod wheel reaches the matrix");
    }

    // --- Determinism --------------------------------------------------------
    {
        auto renderOnce = [] {
            r50::R50Engine engine;
            setupSpectralTone(engine, 10);   // bell: scattered phases
            engine.noteOn(60, 100);
            return renderBuffer(engine, 0.2);
        };
        const std::vector<float> a = renderOnce();
        const std::vector<float> b = renderOnce();
        check(a == b, "two engine instances render identical output");
    }

    // --- Concurrent parameter writes ---------------------------------------
    {
        // The host writes parameters from the UI thread while the render thread
        // is running. Only `store_` may be touched from both; every piece of
        // live DSP state is derived on the render thread. This test is a plain
        // smoke test on its own — its real value is under ThreadSanitizer,
        // which scripts/test-r50.sh runs as a second pass.
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.noteOn(60, 100);

        std::atomic<bool> stop{false};
        std::thread writer([&engine, &stop] {
            for (int i = 0; !stop.load(std::memory_order_relaxed); ++i) {
                engine.setParameter(R50ParamCutoff, 200.0f + (i % 100) * 150.0f);
                engine.setParameter(R50ParamMasterGain, (i % 11) / 10.0f);
                engine.setParameter(R50ParamAmpSustain, (i % 7) / 6.0f);
                engine.setParameter(R50ParamOscWave, static_cast<float>(i % 3));
                engine.setParameter(R50ParamSlope, static_cast<float>(i % 2));
                engine.startParameterRamp(R50ParamResonance, (i % 5) / 4.0f, 64);
            }
        });

        const float peak = render(engine, 1.0);
        stop.store(true);
        writer.join();
        check(std::isfinite(peak) && peak <= 1.05f,
              "concurrent parameter writes stay finite while rendering");
    }

    // --- Concurrent auditions ----------------------------------------------
    {
        // The browser posts previews from the UI thread. Same contract as the
        // parameter store: one atomic word crosses, everything derived from it
        // stays on the render thread. Also a smoke test on its own — TSan is
        // what makes it mean something.
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.noteOn(60, 100);
        const int instruments = r50::SampleLibrary::shared().instrumentCount();

        std::atomic<bool> stop{false};
        std::thread browser([&engine, &stop, instruments] {
            for (int i = 0; !stop.load(std::memory_order_relaxed); ++i) {
                engine.requestAudition(i % instruments, 36 + (i % 60), 100);
            }
        });

        const float peak = render(engine, 1.0);
        stop.store(true);
        browser.join();
        check(std::isfinite(peak) && peak <= 1.05f,
              "auditions posted from another thread stay finite while rendering");
    }

    // --- Filter stability --------------------------------------------------
    {
        for (float slope : {0.0f, 1.0f}) {
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.setParameter(R50ParamSlope, slope);
            engine.setParameter(R50ParamResonance, 1.0f);
            engine.setParameter(R50ParamDrive, 1.0f);
            engine.setParameter(R50ParamCutoff, 40.0f);
            engine.noteOn(36, 127);
            const float low = render(engine, 0.5);
            engine.setParameter(R50ParamCutoff, 17500.0f);
            engine.noteOn(96, 127);
            const float high = render(engine, 0.5);
            const bool ok = std::isfinite(low) && std::isfinite(high)
                         && low <= 1.05f && high <= 1.05f;
            check(ok, slope < 0.5f ? "12 dB filter bounded at extremes"
                                   : "24 dB filter bounded at extremes");
        }
    }

    // --- Pitch bend --------------------------------------------------------
    {
        r50::R50Engine engine;
        engine.setSampleRate(kSR);
        engine.noteOn(60, 100);
        engine.pitchBend(16383);
        const float up = render(engine, 0.2);
        engine.pitchBend(0);
        const float down = render(engine, 0.2);
        check(std::isfinite(up) && std::isfinite(down) && up > 0.02f && down > 0.02f,
              "pitch bend extremes stay finite and audible");
    }

    // --- One-shot samples end the note -------------------------------------
    // A one-shot cannot produce another frame once it reaches its end, so the
    // Partial has to stop even though the amp envelope is still sustaining.
    // Before this, a held one-shot note kept its voice alive forever rendering
    // silence, and the allocator ranks held voices last for stealing.
    {
        const r50::SampleLibrary &lib = r50::SampleLibrary::shared();
        int oneShot = -1, looped = -1;
        for (int i = 0; i < lib.instrumentCount() && (oneShot < 0 || looped < 0); ++i) {
            const r50::SampleData *d = lib.sample(lib.instrument(i)->regions[0].slot);
            if (d == nullptr) continue;
            if (d->loopMode == r50::LoopMode::None && oneShot < 0) oneShot = i;
            if (d->loopMode == r50::LoopMode::Forward && looped < 0) looped = i;
        }
        check(oneShot >= 0 && looped >= 0, "library has both one-shot and looped content");

        // Sustaining forever, key never released: only the source running out
        // can end these.
        auto held = [](int instrument, float noiseMix) {
            r50::PartialParams p;
            p.sourceType = r50::SourceType::Sample;
            p.sampleInstrument = instrument;
            p.noiseMix = noiseMix;
            p.cutoffHz = 18000.0f;
            p.ampSustain = 1.0f;
            p.ampRelease = 10.0f;
            return p;
        };
        auto secondsUntilInactive = [](r50::Partial &partial,
                                       const r50::PartialParams &p, double limit) {
            const r50::ModulationBlock mod;
            for (int i = 0; i < static_cast<int>(limit * kSR); ++i) {
                if (i % 32 == 0) partial.updateBlock(p, 0.0, mod);
                partial.process();
                if (!partial.isActive()) return i / kSR;
            }
            return limit;
        };

        {
            r50::Partial partial;
            partial.setSampleRate(kSR);
            const r50::PartialParams p = held(oneShot, 0.0f);
            partial.noteOn(60, 1.0f, p);
            const double stopped = secondsUntilInactive(partial, p, 4.0);
            check(stopped < 3.0, "held one-shot sample ends when the source runs out");
        }
        {
            // The cut must wait for silence, not fire at the sample's last
            // frame — a resonant filter is still ringing after the source stops.
            r50::Partial partial;
            partial.setSampleRate(kSR);
            r50::PartialParams p = held(oneShot, 0.0f);
            p.cutoffHz = 300.0f;
            p.resonance = 0.95f;
            partial.noteOn(60, 1.0f, p);
            const r50::ModulationBlock mod;
            // Peak over the trailing 20 ms. Comparing against the immediately
            // preceding sample has no teeth: a ring crosses zero every cycle,
            // so a cut fired mid-ring still looks like it landed on silence.
            const int window = static_cast<int>(0.020 * kSR);
            std::vector<float> recent(window, 0.0f);
            bool cut = false;
            for (int i = 0; i < static_cast<int>(4.0 * kSR); ++i) {
                if (i % 32 == 0) partial.updateBlock(p, 0.0, mod);
                recent[i % window] = std::fabs(partial.process());
                if (!partial.isActive()) {
                    float peak = 0.0f;
                    for (float v : recent) peak = std::max(peak, v);
                    check(peak < 1.0e-3f,
                          "one-shot cut lands after the filter ring, not during it");
                    cut = true;
                    break;
                }
            }
            check(cut, "a resonant one-shot still ends");
        }
        {
            r50::Partial partial;
            partial.setSampleRate(kSR);
            const r50::PartialParams p = held(looped, 0.0f);
            partial.noteOn(60, 1.0f, p);
            const double stopped = secondsUntilInactive(partial, p, 4.0);
            check(stopped >= 4.0, "held looped sample keeps sounding");
        }
        {
            // Noise is a source in its own right, so an exhausted one-shot must
            // not take the Partial with it while noise is still mixed in.
            r50::Partial partial;
            partial.setSampleRate(kSR);
            const r50::PartialParams p = held(oneShot, 1.0f);
            partial.noteOn(60, 1.0f, p);
            const double stopped = secondsUntilInactive(partial, p, 4.0);
            check(stopped >= 4.0, "noise keeps a Partial alive past an exhausted one-shot");
        }
        {
            r50::Partial partial;
            partial.setSampleRate(kSR);
            r50::PartialParams p = held(oneShot, 0.0f);
            p.sourceType = r50::SourceType::Wave;
            partial.noteOn(60, 1.0f, p);
            const double stopped = secondsUntilInactive(partial, p, 4.0);
            check(stopped >= 4.0, "a held wave Partial is untouched by the one-shot rule");
        }
    }

    // --- Sample browser audition -------------------------------------------
    {
        const r50::SampleLibrary &lib = r50::SampleLibrary::shared();
        int looped = -1;
        for (int i = 0; i < lib.instrumentCount() && looped < 0; ++i) {
            const r50::SampleData *d = lib.sample(lib.instrument(i)->regions[0].slot);
            if (d != nullptr && d->loopMode == r50::LoopMode::Forward) looped = i;
        }

        {
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.requestAudition(looped, 60, 100);
            check(render(engine, 0.3) > 0.02f, "audition sounds with no note playing");
        }
        {
            // The preview has to survive a patch that would silence it, or it
            // is telling you about the patch instead of the sample.
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.setParameter(R50ParamCutoff, 30.0f);
            engine.setParameter(R50ParamP1Level, 0.0f);
            engine.setParameter(R50ParamAmpAttack, 10.0f);
            engine.setParameter(R50ParamFxReverbMix, 1.0f);
            engine.requestAudition(looped, 60, 100);
            check(render(engine, 0.3) > 0.02f, "audition ignores the patch");
        }
        {
            // A looped sustain would otherwise drone until the user thought to
            // stop it, and the browser has no stop button.
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.requestAudition(looped, 60, 100);
            render(engine, 2.5);
            check(render(engine, 0.5) < 0.001f, "audition releases itself");
        }
        {
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.requestAudition(looped, 60, 100);
            render(engine, 2.5);
            engine.requestAudition(looped, 60, 100);
            check(render(engine, 0.3) > 0.02f,
                  "the same audition twice retriggers");
        }
        {
            // Auditioning must not spend a voice: all eight notes still have to
            // be there afterwards.
            r50::R50Engine reference, engine;
            reference.setSampleRate(kSR);
            engine.setSampleRate(kSR);
            for (int i = 0; i < 8; ++i) { reference.noteOn(48 + i, 100); engine.noteOn(48 + i, 100); }
            engine.requestAudition(looped, 60, 100);
            render(engine, 3.0);
            render(reference, 3.0);
            const std::vector<float> withAudition = renderBuffer(engine, 0.3);
            const std::vector<float> without = renderBuffer(reference, 0.3);
            bool identical = withAudition.size() == without.size();
            for (size_t i = 0; identical && i < without.size(); ++i) {
                identical = withAudition[i] == without[i];
            }
            check(identical, "a finished audition leaves the eight voices untouched");
        }
    }

    // --- Generated content has the character it is named for ----------------
    // Every one of these encodes a specific complaint about how a sample
    // sounded. They are here because the whole suite stayed green through the
    // rewrite that fixed them, which means nothing was guarding any of it.
    {
        struct Zone { const std::vector<float> *samples; double f0, sr; };
        auto zoneFor = [](const char *name) -> Zone {
            const r50::SampleLibrary &lib = r50::SampleLibrary::shared();
            for (int i = 0; i < lib.instrumentCount(); ++i) {
                if (std::string(lib.instrument(i)->name) != name) continue;
                const r50::SampleRegion *region = lib.instrument(i)->find(60, 100);
                const r50::SampleData *d = lib.sample(region->slot);
                // Each zone is generated at its own root key, so probing at
                // concert C would land between harmonics and measure nothing.
                return {&d->samples,
                        440.0 * std::pow(2.0, (region->rootKey - 69) / 12.0),
                        d->sourceSampleRate};
            }
            return {nullptr, 0.0, 0.0};
        };
        // A missing name is a dropped instrument, not a typo in the test — the
        // library silently skips content it has no room for.
        auto zone = [&](const char *name) {
            const Zone z = zoneFor(name);
            check(z.samples != nullptr, name);
            return z;
        };
        auto magnitude = [](const Zone &z, double hz, int count) {
            double re = 0.0, im = 0.0;
            for (int n = 0; n < count; ++n) {
                const double w = 0.5 - 0.5 * std::cos(synth::kTwoPi * n / (count - 1));
                const double phase = synth::kTwoPi * hz * n / z.sr;
                re += (*z.samples)[n] * w * std::cos(phase);
                im -= (*z.samples)[n] * w * std::sin(phase);
            }
            return std::sqrt(re * re + im * im) / count;
        };
        // Ratio of the strongest to the weakest of the first 14 harmonics: a
        // formant bank carves deep valleys, a source rolloff does not.
        auto formantDepth = [&](const char *name) {
            const Zone z = zone(name);
            if (z.samples == nullptr) return 0.0;
            const int count = std::min<int>(16384, static_cast<int>(z.samples->size()));
            double hi = 0.0, lo = 1e9;
            for (int h = 1; h <= 14; ++h) {
                const double m = magnitude(z, z.f0 * h, count);
                hi = std::max(hi, m);
                lo = std::min(lo, m);
            }
            return 20.0 * std::log10(hi / std::max(lo, 1e-12));
        };

        check(formantDepth("Choir") > formantDepth("Strings") + 15.0,
              "a vowel is peakier than a string");
        check(formantDepth("Voice Ooh") > formantDepth("Strings") + 8.0,
              "the closed vowel is peaky too");
        {
            // The Vocal Ah lesson, finally guarded: Gaussian formants killed
            // everything below F1 and the fundamental went with it.
            const Zone z = zone("Choir");
            const int count = std::min<int>(16384, static_cast<int>(z.samples->size()));
            double hi = 0.0;
            for (int h = 1; h <= 14; ++h) hi = std::max(hi, magnitude(z, z.f0 * h, count));
            check(magnitude(z, z.f0, count) > 0.2 * hi,
                  "the vowel fundamental survives its own formants");
        }
        {
            const Zone z = zone("Flute");
            const int count = std::min<int>(16384, static_cast<int>(z.samples->size()));
            double odd = 0.0, even = 0.0;
            for (int h = 1; h <= 12; ++h) {
                (h % 2 ? odd : even) += magnitude(z, z.f0 * h, count);
            }
            check(odd > 50.0 * even, "the flute is odd-harmonic, like a triangle");
        }

        // Attacks. Band energy and onset are measured on the raw one-shot.
        auto bandShare = [&](const char *name, double from, double to) {
            const Zone z = zone(name);
            if (z.samples == nullptr) return 0.0;
            const int count = std::min<int>(4096, static_cast<int>(z.samples->size()));
            double inBand = 0.0, total = 0.0;
            for (double hz = 60.0; hz < 14000.0; hz *= 1.12) {
                const double m = magnitude(z, hz, count);
                total += m;
                if (hz >= from && hz < to) inBand += m;
            }
            return inBand / std::max(total, 1e-12);
        };
        auto riseSeconds = [&](const char *name) {
            const Zone z = zone(name);
            if (z.samples == nullptr) return 0.0;
            float peak = 0.0f;
            for (float v : *z.samples) peak = std::max(peak, std::fabs(v));
            for (size_t n = 0; n < z.samples->size(); ++n) {
                if (std::fabs((*z.samples)[n]) >= 0.9f * peak) return n / z.sr;
            }
            return z.samples->size() / z.sr;
        };
        // On-harmonic against off-harmonic energy: a pitched sound peaks on the
        // grid, noise is indifferent to it.
        auto pitchedness = [&](const char *name) {
            const Zone z = zone(name);
            if (z.samples == nullptr) return 0.0;
            const int count = std::min<int>(4096, static_cast<int>(z.samples->size()));
            double on = 0.0, off = 0.0;
            for (int h = 1; h <= 10; ++h) {
                on  += magnitude(z, z.f0 * h, count);
                off += magnitude(z, z.f0 * (h + 0.5), count);
            }
            return on / std::max(off, 1e-12);
        };
        // Wobble in the short-time level. A scrape is irregular; a filtered
        // noise burst is smooth.
        auto roughness = [&](const char *name) {
            const Zone z = zone(name);
            if (z.samples == nullptr) return 0.0;
            const int window = static_cast<int>(0.001 * z.sr);
            std::vector<double> levels;
            const int limit = static_cast<int>(0.6 * z.samples->size());
            for (int n = 0; n + window < limit; n += window) {
                double sum = 0.0;
                for (int k = 0; k < window; ++k) {
                    sum += (*z.samples)[n + k] * (*z.samples)[n + k];
                }
                levels.push_back(std::sqrt(sum / window));
            }
            if (levels.empty()) return 0.0;
            double mean = 0.0, variance = 0.0;
            for (double v : levels) mean += v;
            mean /= levels.size();
            for (double v : levels) variance += (v - mean) * (v - mean);
            return std::sqrt(variance / levels.size()) / std::max(mean, 1e-12);
        };

        check(riseSeconds("Breath") > 0.005,
              "breath arrives rather than being struck");
        check(riseSeconds("Bow Scrape") > 0.010,
              "a bow arrives rather than detonating");
        check(riseSeconds("Mallet") < 0.002,
              "a struck sample is still struck");
        check(bandShare("Breath", 0.0, 500.0) < 0.05,
              "breath has no low end to make it a hi-hat");
        check(bandShare("Bow Scrape", 0.0, 500.0) < 0.10,
              "the bow scrape has no boom");
        check(bandShare("Pick", 2000.0, 20000.0) > 0.5, "a pick is bright");
        check(roughness("Pick") > 1.5 * roughness("Breath"),
              "the pick scratches rather than hissing");
        check(pitchedness("Lip Buzz") > 50.0, "lips buzz at a pitch");
        check(pitchedness("Slap Bass") > 100.0, "slap is a note, not a snare");
        check(zone("Slap Bass").samples->size() < 0.12 * kSR,
              "slap is short");

        // Pizzicato is the point of adding it: pitched, unlike Pluck, which is
        // mostly a noise burst, and unlike Pick, which is a scrape.
        check(pitchedness("Pizzicato") > 20.0, "a pizzicato has a pitch");
        check(pitchedness("Pizzicato") > 3.0 * pitchedness("Pluck"),
              "pizzicato is more pitched than pluck");

        // Nine Spectrum waves have to be nine different waves, not one rolloff
        // sampled nine times. Compared as normalised harmonic profiles, so a
        // level difference cannot pass for a spectral one.
        {
            std::vector<std::vector<double>> profiles;
            for (int i = 1; i <= 9; ++i) {
                char name[16];
                snprintf(name, sizeof name, "Spectrum %d", i);
                const Zone z = zone(name);
                if (z.samples == nullptr) continue;
                // Measured over exactly one loop period with no window. The
                // loop is periodic by construction, so every bin is resolved
                // exactly and the detuned copies one bin away — which a 16k
                // Hann window smears into the harmonic, with their randomised
                // phases scrambling the result — are separable and can be
                // summed back in deliberately.
                const int period = static_cast<int>(z.samples->size());
                const int root = static_cast<int>(std::lround(z.f0 * period / z.sr));
                std::vector<double> profile;
                double sum = 0.0;
                for (int h = 1; h <= 20; ++h) {
                    double m = 0.0;
                    for (int offset = -1; offset <= 1; ++offset) {
                        const int bin = root * h + offset;
                        if (bin <= 0 || bin >= period / 2) continue;
                        double re = 0.0, im = 0.0;
                        for (int n = 0; n < period; ++n) {
                            const double phase = synth::kTwoPi * bin * n / period;
                            re += (*z.samples)[n] * std::cos(phase);
                            im -= (*z.samples)[n] * std::sin(phase);
                        }
                        m += std::sqrt(re * re + im * im) / period;
                    }
                    profile.push_back(m);
                    sum += m;
                }
                for (double &v : profile) v /= std::max(sum, 1e-12);
                profiles.push_back(profile);
            }
            check(profiles.size() == 9, "all nine Spectrum waves are present");

            double closest = 1e9;
            for (size_t a = 0; a < profiles.size(); ++a) {
                for (size_t b = a + 1; b < profiles.size(); ++b) {
                    double distance = 0.0;
                    for (size_t h = 0; h < profiles[a].size(); ++h) {
                        distance += std::fabs(profiles[a][h] - profiles[b][h]);
                    }
                    closest = std::min(closest, distance);
                }
            }
            printf("       closest pair of Spectrum waves: %.2f\n", closest);
            check(closest > 0.5, "no two Spectrum waves are the same wave");
        }
    }

    // --- Pitch detection ----------------------------------------------------
    {
        auto tone = [](double hz, int harmonics, double seconds) {
            std::vector<float> out(static_cast<int>(seconds * kSR), 0.0f);
            for (size_t n = 0; n < out.size(); ++n) {
                double value = 0.0;
                for (int h = 1; h <= harmonics; ++h) {
                    value += std::sin(synth::kTwoPi * hz * h * n / kSR) / h;
                }
                out[n] = static_cast<float>(value * 0.3);
            }
            return out;
        };

        // Across the range a sample is likely to be recorded in. Five cents is
        // deliberately tighter than it needs to be: at the top of the range,
        // rounding the period to a whole sample instead of interpolating it
        // costs twenty-five, and a looser bound would not notice.
        double worst = 0.0;
        for (double hz : {82.41, 130.81, 261.63, 440.0, 1046.5, 1500.0,
                          1975.5, 2400.0}) {
            const std::vector<float> sine = tone(hz, 1, 0.5);
            const r50::DetectedPitch found =
                r50::detectPitch(sine.data(), static_cast<int>(sine.size()), kSR);
            const double error = found.valid
                ? 1200.0 * std::log2(found.hertz / hz) : 9999.0;
            worst = std::max(worst, std::fabs(error));
            check(found.valid && std::fabs(error) < 5.0,
                  "a sine is detected to within five cents");
        }
        printf("       worst pitch-detection error: %.2f cents\n", worst);

        {
            // The octave trap. Plain autocorrelation maximises at every
            // multiple of the period, so a harmonically rich tone reports an
            // octave or two low; this is the case YIN exists for.
            const std::vector<float> rich = tone(220.0, 24, 0.5);
            const r50::DetectedPitch found =
                r50::detectPitch(rich.data(), static_cast<int>(rich.size()), kSR);
            printf("       24-harmonic 220 Hz tone detected at %.1f Hz\n", found.hertz);
            check(found.valid && std::fabs(1200.0 * std::log2(found.hertz / 220.0)) < 20.0,
                  "a harmonically rich tone does not detect an octave low");
        }
        {
            // A sample recorded off-pitch: the residual has to be reported
            // rather than rounded away, or it plays out of tune everywhere.
            // Deliberately not 50 cents — that is exactly between two
            // semitones, and both answers are right.
            const double hz = 261.63 * std::pow(2.0, 0.30 / 12.0);
            const std::vector<float> sharp = tone(hz, 6, 0.5);
            const r50::DetectedPitch found =
                r50::detectPitch(sharp.data(), static_cast<int>(sharp.size()), kSR);
            check(found.valid && found.rootKey == 60 && found.centsSharp > 20.0
               && found.centsSharp < 40.0,
                  "an out-of-tune sample reports how sharp it is");
        }
        {
            synth::FastRandom random(12345);
            std::vector<float> noise(static_cast<int>(0.5 * kSR));
            for (float &v : noise) v = random.nextBipolar() * 0.3f;
            const r50::DetectedPitch found =
                r50::detectPitch(noise.data(), static_cast<int>(noise.size()), kSR);
            printf("       noise confidence: %.2f\n", found.confidence);
            check(!found.valid || found.confidence < 0.5,
                  "noise is not given a confident root key");
        }
        {
            // The real target: generated content, which is what an import will
            // look like. The Flute zone covering middle C is built at its own
            // root, and detection has to find that rather than middle C.
            const r50::SampleLibrary &library = r50::SampleLibrary::shared();
            for (int i = 0; i < library.instrumentCount(); ++i) {
                if (std::string(library.instrument(i)->name) != "Flute") continue;
                const r50::SampleRegion *region = library.instrument(i)->find(60, 100);
                const r50::SampleData *data = library.sample(region->slot);
                const r50::DetectedPitch found = r50::detectPitch(
                    data->samples.data(), data->length(), data->sourceSampleRate);
                check(found.valid && found.rootKey == region->rootKey,
                      "a generated sample detects the key it was generated at");
            }
        }
    }

    // --- Editable root key --------------------------------------------------
    {
        {
            r50::RootTuning tuning;
            tuning.rootKey = 43;
            tuning.tuneCents = -37.5f;
            const r50::RootTuning back =
                r50::unpackRootTuning(r50::packRootTuning(tuning));
            check(back.rootKey == 43 && std::fabs(back.tuneCents + 37.5f) < 0.1f,
                  "root and tuning survive being packed into one word");
        }

        r50::SampleLibrary &library = r50::SampleLibrary::shared();
        r50::RootTuning unused;
        check(!library.rootTuning(0, unused),
              "generated content has no root override");

        // Install a single-region instrument the way the importer does, then
        // retune it and confirm the pitch actually moves.
        r50::SampleData data;
        data.samples.resize(4410);
        for (size_t n = 0; n < data.samples.size(); ++n) {
            data.samples[n] = static_cast<float>(
                0.5 * std::sin(synth::kTwoPi * 440.0 * n / kSR));
        }
        data.sourceSampleRate = kSR;
        data.rootKey  = 60;
        data.loopMode = r50::LoopMode::Forward;
        data.loopEnd  = static_cast<uint32_t>(data.samples.size());
        const int slot = library.addSample(std::move(data));
        r50::Multisample imported;
        imported.setName("Retune Probe");
        r50::SampleRegion region;
        region.rootKey = 60;
        region.slot = slot;
        imported.regions[0] = region;
        imported.regionCount = 1;
        const int index = library.addInstrument(imported);
        check(slot >= 0 && index >= 0, "the probe instrument installs");

        auto pitchOf = [&](int instrument, int note) {
            r50::R50Engine engine;
            engine.setSampleRate(kSR);
            engine.setParameter(R50ParamSourceType, 1);
            engine.setParameter(R50ParamSampleInstrument,
                                static_cast<float>(instrument));
            engine.setParameter(R50ParamCutoff, 18000);
            engine.setParameter(R50ParamFilterEnvAmount, 0);
            engine.setParameter(R50ParamFxReverbMix, 0);
            engine.setParameter(R50ParamAmpAttack, 0.001f);
            engine.noteOn(static_cast<uint8_t>(note), 100);
            const std::vector<float> rendered = renderBuffer(engine, 0.3);
            return r50::detectPitch(rendered.data(),
                                    static_cast<int>(rendered.size()), kSR);
        };

        const r50::DetectedPitch before = pitchOf(index, 60);
        check(before.valid && std::fabs(before.hertz - 440.0) < 8.0,
              "the probe plays at its recorded pitch on its root key");

        // Declaring the sample to be A4 rather than C4 must transpose it down
        // by the three semitones that were previously being applied wrongly.
        library.setRootTuning(index, {69, 0.0f});
        const r50::DetectedPitch after = pitchOf(index, 60);
        const double expected = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        check(after.valid && std::fabs(1200.0 * std::log2(after.hertz / expected)) < 25.0,
              "correcting the root key transposes playback");

        // Positive cents transpose playback up, which is why a detection of
        // "30 cents sharp" has to be stored as a correction of -30.
        library.setRootTuning(index, {69, 50.0f});
        const r50::DetectedPitch detuned = pitchOf(index, 60);
        check(detuned.valid
           && 1200.0 * std::log2(detuned.hertz / after.hertz) > 25.0,
              "fine tune in cents shifts playback up");

        // A generated multisample must ignore an override: its zones each carry
        // their own root and one number cannot describe them.
        const r50::DetectedPitch zoneBefore = pitchOf(0, 60);
        library.setRootTuning(0, {24, 0.0f});
        const r50::DetectedPitch zoneAfter = pitchOf(0, 60);
        check(zoneBefore.valid && zoneAfter.valid
           && std::fabs(1200.0 * std::log2(zoneAfter.hertz / zoneBefore.hertz)) < 15.0,
              "a multi-zone instrument ignores a root override");
    }

    // --- WAV export ---------------------------------------------------------
    {
        // A minimal reader, deliberately independent of the writer: walking the
        // chunk list is the only way to prove the sizes are right, and reusing
        // the writer's own idea of the layout would prove nothing.
        struct Parsed {
            bool ok = false;
            uint32_t rate = 0, bits = 0, channels = 0;
            std::vector<float> samples;
            bool hasLoop = false;
            uint32_t loopStart = 0, loopEnd = 0, rootKey = 0;
        };
        auto readU32 = [](const std::vector<uint8_t> &b, size_t at) {
            return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8)
                 | (static_cast<uint32_t>(b[at + 2]) << 16)
                 | (static_cast<uint32_t>(b[at + 3]) << 24);
        };
        auto readU16 = [](const std::vector<uint8_t> &b, size_t at) {
            return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
        };
        auto tagAt = [](const std::vector<uint8_t> &b, size_t at, const char *tag) {
            return b[at] == (uint8_t)tag[0] && b[at + 1] == (uint8_t)tag[1]
                && b[at + 2] == (uint8_t)tag[2] && b[at + 3] == (uint8_t)tag[3];
        };
        auto parse = [&](const std::vector<uint8_t> &bytes) {
            Parsed out;
            if (bytes.size() < 12 || !tagAt(bytes, 0, "RIFF")
             || !tagAt(bytes, 8, "WAVE")) return out;
            // The declared RIFF size must match the file, or editors truncate.
            if (readU32(bytes, 4) != bytes.size() - 8) return out;

            size_t at = 12;
            while (at + 8 <= bytes.size()) {
                const uint32_t size = readU32(bytes, at + 4);
                const size_t body = at + 8;
                if (body + size > bytes.size()) return out;
                if (tagAt(bytes, at, "fmt ")) {
                    out.channels = readU16(bytes, body + 2);
                    out.rate     = readU32(bytes, body + 4);
                    out.bits     = readU16(bytes, body + 14);
                } else if (tagAt(bytes, at, "data")) {
                    for (uint32_t i = 0; i + 3 < size; i += 4) {
                        float v;
                        std::memcpy(&v, &bytes[body + i], sizeof(v));
                        out.samples.push_back(v);
                    }
                } else if (tagAt(bytes, at, "smpl")) {
                    out.rootKey   = readU32(bytes, body + 12);
                    out.hasLoop   = readU32(bytes, body + 28) == 1;
                    out.loopStart = readU32(bytes, body + 44);
                    out.loopEnd   = readU32(bytes, body + 48);
                }
                at = body + size + (size & 1u);
            }
            out.ok = true;
            return out;
        };

        std::vector<float> source(1000);
        for (size_t n = 0; n < source.size(); ++n) {
            source[n] = static_cast<float>(std::sin(synth::kTwoPi * 5.0 * n / source.size()));
        }
        // Above full scale on purpose: 108 of the 129 factory zones are, and an
        // integer format would silently flatten them.
        source[0] = 1.30f;
        source[1] = -1.30f;

        const std::vector<uint8_t> encoded =
            r50::encodeWav(source.data(), static_cast<int>(source.size()),
                           kSR, 55, 100, 900, true);
        const Parsed parsed = parse(encoded);
        check(parsed.ok, "the exported file parses as a RIFF WAVE");
        check(parsed.rate == 44100 && parsed.bits == 32 && parsed.channels == 1,
              "the exported file declares 32-bit mono at the source rate");
        check(parsed.samples.size() == source.size(),
              "every frame is written");

        // Seeded high so a file that failed to parse fails this rather than
        // sailing through a loop that never runs.
        double worst = parsed.samples.size() == source.size() ? 0.0 : 1.0;
        for (size_t n = 0; n < parsed.samples.size() && n < source.size(); ++n) {
            worst = std::max(worst, std::fabs(double(parsed.samples[n] - source[n])));
        }
        printf("       wav round-trip worst error: %.2e\n", worst);
        check(worst == 0.0, "float export round-trips exactly");
        // Guarded, because a failed parse leaves nothing to index and a crash
        // reports nothing at all.
        check(parsed.samples.size() > 1 && parsed.samples[0] == 1.30f
           && parsed.samples[1] == -1.30f,
              "content above full scale survives instead of being clipped");

        check(parsed.hasLoop && parsed.rootKey == 55,
              "the loop and root key travel with the file");
        // R50's loopEnd is exclusive, smpl's is inclusive. One frame out is a
        // click on a short loop.
        check(parsed.loopStart == 100 && parsed.loopEnd == 899,
              "the smpl loop end is inclusive");

        const std::vector<uint8_t> oneShot =
            r50::encodeWav(source.data(), static_cast<int>(source.size()),
                           kSR, 60, 0, 1000, false);
        check(!parse(oneShot).hasLoop, "a one-shot exports without a loop");
        // But it still states its root: the smpl chunk is the only place a root
        // key travels, and writing it only for looped audio meant every
        // exported transient came back pitchless.
        {
            const r50::LoadedWav shot = r50::decodeWav(oneShot);
            check(shot.ok && shot.hasRoot && shot.rootKey == 60 && !shot.hasLoop,
                  "a one-shot still exports its root key");
        }
        {
            const r50::LoadedWav pp = r50::decodeWav(
                r50::encodeWav(source.data(), static_cast<int>(source.size()),
                               kSR, 60, 100, 900, true, true));
            check(pp.ok && pp.hasLoop && pp.pingPong,
                  "a ping-pong loop survives the round trip as ping-pong");
            const r50::LoadedWav fwd = r50::decodeWav(
                r50::encodeWav(source.data(), static_cast<int>(source.size()),
                               kSR, 60, 100, 900, true, false));
            check(fwd.hasLoop && !fwd.pingPong,
                  "a forward loop is not read back as ping-pong");
        }

        const std::vector<uint8_t> odd =
            r50::encodeWav(source.data(), 999, kSR, 60, 100, 900, true);
        const Parsed oddParsed = parse(odd);
        check(oddParsed.ok && oddParsed.samples.size() == 999 && oddParsed.hasLoop,
              "an odd frame count still parses");
    }

    // --- Factory files ------------------------------------------------------
    {
        // Writer and reader are separate pieces of code; this is the only test
        // that proves they agree, which is what a round trip through an audio
        // editor depends on.
        std::vector<float> source(2000);
        for (size_t n = 0; n < source.size(); ++n) {
            source[n] = static_cast<float>(
                0.7 * std::sin(synth::kTwoPi * 7.0 * n / source.size()));
        }
        const r50::LoadedWav back = r50::decodeWav(
            r50::encodeWav(source.data(), static_cast<int>(source.size()),
                           48000.0, 43, 250, 1750, true));
        check(back.ok && back.samples.size() == source.size(),
              "a written file reads back with the same length");
        check(std::fabs(back.sampleRate - 48000.0) < 1.0 && back.rootKey == 43,
              "rate and root key survive the round trip");
        // Inclusive on the way out, exclusive on the way back: the two
        // conversions have to cancel or every round trip shortens the loop.
        check(back.hasLoop && back.loopStart == 250 && back.loopEnd == 1750,
              "loop points survive the round trip exactly");
        double worst = back.samples.size() == source.size() ? 0.0 : 1.0;
        for (size_t n = 0; n < back.samples.size() && n < source.size(); ++n) {
            worst = std::max(worst, std::fabs(double(back.samples[n] - source[n])));
        }
        check(worst < 1.0e-6, "audio survives the round trip");

        check(!r50::decodeWav({1, 2, 3}).ok, "a truncated file is rejected");
        std::vector<uint8_t> corrupt = r50::encodeWav(
            source.data(), 100, kSR, 60, 0, 100, false);
        corrupt[9] = 'X';   // break the WAVE tag
        check(!r50::decodeWav(corrupt).ok, "a corrupt file is rejected");
        {
            // A believable RIFF/WAVE that never declares a format. Rejecting it
            // on the header alone would not exercise the format check, and a
            // half-written file looks exactly like this.
            std::vector<uint8_t> headless = r50::encodeWav(
                source.data(), 100, kSR, 60, 0, 100, false);
            headless[12] = 'j';   // rename "fmt " so it is never seen
            check(!r50::decodeWav(headless).ok,
                  "a file with no format chunk is rejected");
        }

        // 16-bit is what an editor is most likely to hand back.


        // Replacing a slot must actually change what a voice hears.
        {
            r50::SampleLibrary &library = r50::SampleLibrary::shared();
            const r50::Multisample *flute = nullptr;
            int fluteIndex = -1;
            for (int i = 0; i < library.instrumentCount(); ++i) {
                if (std::string(library.instrument(i)->name) != "Flute") continue;
                flute = library.instrument(i);
                fluteIndex = i;
            }
            check(flute != nullptr, "the probe instrument exists");
            const r50::SampleRegion *region = flute->find(60, 100);
            const int slot = region->slot;

            auto renderNote = [&](int instrument) {
                r50::R50Engine engine;
                engine.setSampleRate(kSR);
                engine.setParameter(R50ParamSourceType, 1);
                engine.setParameter(R50ParamSampleInstrument,
                                    static_cast<float>(instrument));
                engine.setParameter(R50ParamCutoff, 18000);
                engine.setParameter(R50ParamFilterEnvAmount, 0);
                engine.setParameter(R50ParamFxReverbMix, 0);
                engine.noteOn(60, 100);
                return renderBuffer(engine, 0.25);
            };

            const std::vector<float> before = renderNote(fluteIndex);
            r50::SampleData replacement;
            replacement.samples.resize(4410, 0.0f);
            for (size_t n = 0; n < replacement.samples.size(); ++n) {
                replacement.samples[n] = static_cast<float>(
                    0.5 * std::sin(synth::kTwoPi * 300.0 * n / kSR));
            }
            replacement.sourceSampleRate = kSR;
            replacement.rootKey = region->rootKey;
            replacement.loopMode = r50::LoopMode::Forward;
            replacement.loopEnd = static_cast<uint32_t>(replacement.samples.size());
            check(library.replaceSample(slot, std::move(replacement)),
                  "a published slot can be replaced");

            const std::vector<float> after = renderNote(fluteIndex);
            bool changed = before.size() == after.size();
            if (changed) {
                double difference = 0.0;
                for (size_t n = 0; n < before.size(); ++n) {
                    difference += std::fabs(before[n] - after[n]);
                }
                changed = difference > 1.0;
            }
            check(changed, "replacing a slot changes what the instrument plays");
            // Non-empty, so it is the slot bound being tested and not the
            // emptiness check that sits in front of it.
            r50::SampleData stray;
            stray.samples.resize(64, 0.25f);
            check(!library.replaceSample(9999, std::move(stray)),
                  "an out-of-range slot is refused");
        }
    }

    // --- Drop-in sample directory -------------------------------------------
    {
        const std::string directory = "/tmp/r50_dropin_test";
        ::mkdir(directory.c_str(), 0755);
        for (const char *stale : {"/b_loop.wav", "/a_oneshot.wav", "/c_detect.wav",
                                  "/notes.txt", "/broken.wav"}) {
            std::remove((directory + stale).c_str());
        }

        auto sine = [](double hz, int count) {
            std::vector<float> out(count);
            for (int n = 0; n < count; ++n) {
                out[n] = static_cast<float>(0.6 * std::sin(synth::kTwoPi * hz * n / kSR));
            }
            return out;
        };

        // A long file with a smpl loop, a short one without, and one whose
        // pitch has to be detected because it carries no chunk.
        const std::vector<float> longTone = sine(220.0, 44100);
        const std::vector<float> shortTone = sine(440.0, 4410);
        r50::writeWholeFile(directory + "/b_loop.wav",
            r50::encodeWav(longTone.data(), static_cast<int>(longTone.size()),
                           kSR, 45, 1000, 40000, true));
        r50::writeWholeFile(directory + "/a_oneshot.wav",
            r50::encodeWav(shortTone.data(), static_cast<int>(shortTone.size()),
                           kSR, 60, 0, 0, false));
        // c_detect must genuinely carry no smpl chunk, which encodeWav no
        // longer produces — it now always states a root. Renaming the tag
        // leaves the chunk sizes valid, so the reader walks past it exactly as
        // it would walk past a chunk written by some other tool.
        {
            std::vector<uint8_t> stripped =
                r50::encodeWav(longTone.data(), static_cast<int>(longTone.size()),
                               kSR, 60, 0, 0, false);
            for (size_t at = 12; at + 4 <= stripped.size(); ++at) {
                if (std::memcmp(&stripped[at], "smpl", 4) == 0) {
                    std::memcpy(&stripped[at], "junk", 4);
                    break;
                }
            }
            r50::writeWholeFile(directory + "/c_detect.wav", stripped);
        }
        // Deliberately a *valid* WAV under a non-audio extension. A two-byte
        // file would be rejected by the decoder regardless, which would prove
        // nothing about the extension filter.
        r50::writeWholeFile(directory + "/notes.txt",
            r50::encodeWav(shortTone.data(), static_cast<int>(shortTone.size()),
                           kSR, 60, 0, 0, false));
        r50::writeWholeFile(directory + "/broken.wav", {'n', 'o', 'p', 'e'});

        r50::SampleLibrary &library = r50::SampleLibrary::shared();
        const int before = library.instrumentCount();
        const int loaded = r50::loadSampleDirectory(library, directory);
        check(loaded == 3, "every readable wav in the directory becomes an instrument");
        check(library.instrumentCount() == before + 3,
              "a non-audio file and a corrupt one are both skipped");

        // Filename order, not filesystem order: a preset stores an index, and
        // the same directory has to produce the same indices everywhere.
        check(std::string(library.instrument(before)->name) == "a_oneshot"
           && std::string(library.instrument(before + 1)->name) == "b_loop"
           && std::string(library.instrument(before + 2)->name) == "c_detect",
              "the directory loads in filename order");

        const r50::SampleData *oneShot =
            library.sample(library.instrument(before)->regions[0].slot);
        check(oneShot->loopMode == r50::LoopMode::None,
              "a short file with no loop chunk is a one-shot");

        const r50::SampleData *looped =
            library.sample(library.instrument(before + 1)->regions[0].slot);
        check(looped->loopMode == r50::LoopMode::Forward
           && looped->loopStart == 1000 && looped->loopEnd == 40000,
              "a smpl loop is honoured exactly");
        check(library.instrument(before + 1)->regions[0].rootKey == 45,
              "a smpl root key is honoured");

        // No chunk to trust, so the root has to come from the audio itself.
        check(library.instrument(before + 2)->regions[0].rootKey == 57,
              "a file without a smpl chunk has its pitch detected");

        // The other half of that: a one-shot that *does* state a root is taken
        // at its word rather than re-guessed. a_oneshot is 440 Hz declared as
        // 60, so detection would say 69 and the stated root says otherwise.
        check(library.instrument(before)->regions[0].rootKey == 60,
              "a stated root wins over detection even with no loop");

        check(library.instrument(before)->regions[0].lowKey == 0
           && library.instrument(before)->regions[0].highKey == 127,
              "a dropped-in sample covers the keyboard");
        check(r50::loadSampleDirectory(library, "/tmp/r50_no_such_directory") == 0,
              "a missing directory loads nothing rather than failing");
    }

    // --- JSON reader --------------------------------------------------------
    {
        r50::JsonValue value;
        check(r50::parseJson("{\"a\": [1, 2.5, -3], \"b\": \"x\\\\y\", \"c\": true}", value)
           && value["a"].items.size() == 3
           && value["a"].items[1].number == 2.5
           && value["a"].items[2].number == -3
           && value["b"].stringOr("") == "x\\y"
           && value["c"].boolean,
              "objects, arrays, numbers, escapes and booleans parse");
        check(value["missing"].stringOr("fallback") == "fallback",
              "a missing member reads as its fallback");

        // Every one of these would otherwise map some samples and silently drop
        // others, which is the failure mode the manifest exists to prevent.
        for (const char *bad : {"{", "{\"a\": }", "[1, 2", "{\"a\" 1}",
                                "{\"a\": 1} trailing", "{\"a\": \"\\u0041\"}",
                                "", "{\"a\": 1,}"}) {
            r50::JsonValue ignored;
            check(!r50::parseJson(bad, ignored), "malformed JSON is rejected");
        }
    }

    // --- Factory manifest ---------------------------------------------------
    {
        const std::string directory = "/tmp/r50_manifest_test";
        ::mkdir(directory.c_str(), 0755);
        std::vector<float> tone(20000);
        for (size_t n = 0; n < tone.size(); ++n) {
            tone[n] = static_cast<float>(0.5 * std::sin(synth::kTwoPi * 200.0 * n / kSR));
        }
        auto writeZone = [&](const char *name, int rootKey, bool looped) {
            r50::writeWholeFile(directory + "/" + name,
                r50::encodeWav(tone.data(), static_cast<int>(tone.size()), kSR,
                               rootKey, 500, 19000, looped));
        };
        writeZone("low.wav", 40, true);
        writeZone("high.wav", 80, true);
        writeZone("hit.wav", 60, false);

        auto writeManifest = [&](const std::string &json) {
            r50::writeWholeFile(directory + "/factory_samples.json",
                                std::vector<uint8_t>(json.begin(), json.end()));
        };
        // Deliberately alphabetically out of order: "Zebra" before "Alpha", and
        // high.wav before low.wav. If any of this were sorted the check below
        // would land on the wrong instrument.
        writeManifest(R"({"instruments": [
            {"name": "Zebra", "zones": [
                {"file": "high.wav", "rootKey": 80, "lowKey": 60, "highKey": 127},
                {"file": "low.wav",  "rootKey": 40, "lowKey": 0,  "highKey": 59}]},
            {"name": "Alpha", "zones": [{"file": "hit.wav", "rootKey": 60}]},
            {"name": "Override", "zones": [{"file": "low.wav", "rootKey": 55}]}]})");

        r50::SampleLibrary library{r50::SampleLibrary::Empty{}};
        check(r50::loadFactoryManifest(library, directory), "a manifest loads");
        check(library.instrumentCount() == 3, "it loads the instruments it names");
        check(std::string(library.instrument(0)->name) == "Zebra"
           && std::string(library.instrument(1)->name) == "Alpha",
              "manifest order wins over alphabetical order");
        check(library.instrument(0)->regionCount == 2
           && library.instrument(0)->regions[0].rootKey == 80
           && library.instrument(0)->regions[1].rootKey == 40,
              "zones keep the order and roots the manifest gives them");
        check(library.instrument(0)->find(30, 100)->rootKey == 40
           && library.instrument(0)->find(100, 100)->rootKey == 80,
              "the declared key ranges select the right zone");
        check(library.instrument(1)->regions[0].lowKey == 0
           && library.instrument(1)->regions[0].highKey == 127,
              "an omitted key range covers the keyboard");

        // low.wav carries root 40 in its own smpl chunk while the manifest
        // declares 55. The manifest is the authority on what a file *is* to
        // this synth; the chunk only records what the recording claimed.
        check(library.instrument(2)->regions[0].rootKey == 55,
              "the manifest root key overrides the one in the file");

        const r50::SampleData *looped =
            library.sample(library.instrument(0)->regions[0].slot);
        check(looped->loopMode == r50::LoopMode::Forward
           && looped->loopStart == 500 && looped->loopEnd == 19000,
              "the loop comes from the wav when the manifest says nothing");

        // The manifest's `loop` overrides the file in both directions. hit.wav
        // has no smpl chunk at all, which is the case that matters: audio
        // brought in from anywhere else arrives that way, and before this key
        // the only way to sustain it was to rewrite the file.
        {
            r50::SampleLibrary over{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Forced",  "zones": [{"file": "hit.wav", "loop": true}]},
                {"name": "Silenced","zones": [{"file": "low.wav", "loop": false}]},
                {"name": "Points",  "zones": [{"file": "hit.wav", "loop": true,
                                               "loopStart": 100,
                                               "loopEnd": 900}]},
                {"name": "TooLong", "zones": [{"file": "hit.wav", "loop": true,
                                               "loopStart": 10,
                                               "loopEnd": 999999}]}]})");
            check(r50::loadFactoryManifest(over, directory),
                  "a manifest carrying loop overrides loads");

            const r50::SampleData *forced =
                over.sample(over.instrument(0)->regions[0].slot);
            check(forced->loopMode == r50::LoopMode::Forward
               && forced->loopStart == 0
               && forced->loopEnd == static_cast<uint32_t>(forced->length()),
                  "loop:true loops a file that carries no loop at all");

            const r50::SampleData *silenced =
                over.sample(over.instrument(1)->regions[0].slot);
            check(silenced->loopMode == r50::LoopMode::None,
                  "loop:false makes a one-shot of a file that does loop");

            const r50::SampleData *points =
                over.sample(over.instrument(2)->regions[0].slot);
            check(points->loopMode == r50::LoopMode::Forward
               && points->loopStart == 100 && points->loopEnd == 900,
                  "the manifest's loop points are used when it gives them");

            // Clamped, not rejected: the alternative is losing every other
            // instrument in the set over one stale number.
            const r50::SampleData *tooLong =
                over.sample(over.instrument(3)->regions[0].slot);
            check(tooLong->loopMode == r50::LoopMode::Forward
               && tooLong->loopEnd == static_cast<uint32_t>(tooLong->length()),
                  "a loopEnd past the end of the audio clamps to the end");
        }

        // tuneCents corrects the gap between a recording's real pitch and the
        // nearest semitone, which rootKey alone cannot express.
        {
            r50::SampleLibrary tuned{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Flat",   "zones": [{"file": "low.wav", "rootKey": 40,
                                              "tuneCents": -14.6}]},
                {"name": "Plain",  "zones": [{"file": "low.wav", "rootKey": 40}]},
                {"name": "Absurd", "zones": [{"file": "low.wav", "rootKey": 40,
                                              "tuneCents": 5000}]}]})");
            check(r50::loadFactoryManifest(tuned, directory),
                  "a manifest carrying tuneCents loads");
            check(std::fabs(tuned.instrument(0)->regions[0].tuneCents + 14.6f) < 0.01f,
                  "the zone's tuneCents reaches the region");
            check(tuned.instrument(1)->regions[0].tuneCents == 0.0f,
                  "a zone without tuneCents is left untuned");
            check(tuned.instrument(2)->regions[0].tuneCents == 100.0f,
                  "tuneCents past a semitone clamps rather than transposing");
        }

        // A root key outside the keyboard fails the whole manifest. Clamping it
        // would publish an instrument that plays at the wrong pitch on every
        // key, which is exactly the silent failure the manifest exists to stop.
        {
            for (const char *bad : {"128", "-1", "600"}) {
                r50::SampleLibrary rejected{r50::SampleLibrary::Empty{}};
                writeManifest(std::string(R"({"instruments": [
                    {"name": "Bad", "zones": [{"file": "low.wav", "rootKey": )")
                              + bad + "}]}]}");
                check(!r50::loadFactoryManifest(rejected, directory),
                      "a root key outside 0..127 fails the manifest");
                check(rejected.instrumentCount() == 0,
                      "and publishes nothing");
            }
            r50::SampleLibrary edges{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Low",  "zones": [{"file": "low.wav", "rootKey": 0}]},
                {"name": "High", "zones": [{"file": "low.wav", "rootKey": 127}]}]})");
            check(r50::loadFactoryManifest(edges, directory),
                  "the ends of the keyboard are still valid roots");
        }

        // loopMode reaches the asset, and an unrecognised one fails the
        // manifest rather than quietly playing the other direction.
        {
            r50::SampleLibrary modes{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Ping", "zones": [{"file": "low.wav",
                                            "loopMode": "pingpong"}]},
                {"name": "Fwd",  "zones": [{"file": "low.wav"}]}]})");
            check(r50::loadFactoryManifest(modes, directory),
                  "a manifest naming a loop mode loads");
            check(modes.sample(modes.instrument(0)->regions[0].slot)->loopMode
                      == r50::LoopMode::PingPong,
                  "loopMode pingpong reaches the asset");
            check(modes.sample(modes.instrument(1)->regions[0].slot)->loopMode
                      == r50::LoopMode::Forward,
                  "a zone with no loopMode stays forward");

            r50::SampleLibrary bogus{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Odd", "zones": [{"file": "low.wav",
                                           "loopMode": "sideways"}]}]})");
            check(!r50::loadFactoryManifest(bogus, directory),
                  "an unrecognised loopMode fails the manifest");
        }

        // A manifest naming a file that is not there must leave the library
        // alone, so the caller can fall back rather than publish half a set.
        {
            r50::SampleLibrary partial{r50::SampleLibrary::Empty{}};
            writeManifest(R"({"instruments": [
                {"name": "Fine", "zones": [{"file": "low.wav", "rootKey": 40}]},
                {"name": "Gone", "zones": [{"file": "absent.wav", "rootKey": 60}]}]})");
            check(!r50::loadFactoryManifest(partial, directory),
                  "a manifest naming a missing file fails");
            check(partial.instrumentCount() == 0,
                  "and publishes nothing at all rather than half a set");
        }
        {
            r50::SampleLibrary broken{r50::SampleLibrary::Empty{}};
            writeManifest("{\"instruments\": [ oops ]}");
            check(!r50::loadFactoryManifest(broken, directory),
                  "a malformed manifest fails");
        }
        {
            r50::SampleLibrary empty{r50::SampleLibrary::Empty{}};
            check(!r50::loadFactoryManifest(empty, "/tmp/r50_no_manifest_here"),
                  "a missing manifest fails so the generator can take over");
        }
    }

    printf(g_failures == 0 ? "\nAll R50 tests passed.\n"
                           : "\n%d R50 test(s) FAILED.\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
