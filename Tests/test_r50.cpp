//
//  test_r50.cpp
//  Offline regression tests for the R50 engine: voice/MIDI behaviour, sustain
//  pedal semantics, parameter plumbing and filter stability. Build & run:
//    ./scripts/test-r50.sh
//

#include "R50Engine.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
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
