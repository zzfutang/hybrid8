//
//  test_r50.cpp
//  Offline regression tests for the R50 engine: voice/MIDI behaviour, sustain
//  pedal semantics, parameter plumbing and filter stability. Build & run:
//    ./scripts/test-r50.sh
//

#include "R50Engine.hpp"

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
        check(loopedCount >= 28, "sustains are generated for every key zone");

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

    printf(g_failures == 0 ? "\nAll R50 tests passed.\n"
                           : "\n%d R50 test(s) FAILED.\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
