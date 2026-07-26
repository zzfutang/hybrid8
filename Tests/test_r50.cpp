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
