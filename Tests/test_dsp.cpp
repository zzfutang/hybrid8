//
//  test_dsp.cpp
//  Offline regression tests for the DSP core (header-only C++). Verifies
//  oscillator tuning, alias containment, and filter stability at extreme
//  settings. Build & run:
//    clang++ -std=c++17 -O2 Tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp
//

#include "../Extension/DSP/SynthEngine.hpp"
#include <vector>
#include <cmath>
#include <cstdio>

using namespace synth;

static int g_failures = 0;
static void check(bool cond, const char* name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_failures;
}

static bool allFinite(const std::vector<float>& b) {
    for (float x : b) if (!std::isfinite(x)) return false;
    return true;
}
static float peakAbs(const std::vector<float>& b) {
    float p = 0.f; for (float x : b) p = std::max(p, std::fabs(x)); return p;
}
// Magnitude of a single DFT bin at frequency f (Goertzel-ish direct sum).
static double magAt(const std::vector<float>& b, double f, double sr) {
    double re = 0, im = 0;
    for (size_t n = 0; n < b.size(); ++n) {
        double ph = kTwoPi * f * (double)n / sr;
        re += b[n] * std::cos(ph);
        im -= b[n] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im) / (double)b.size();
}

int main() {
    const double sr = 48000.0;
    const int    N  = (int)sr; // 1 second

    // ---- Test A: oscillator tuning (saw @ A4 = 440 Hz) --------------------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOsc2Level, 0.0f);
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamFilterResonance, 0.0f);
        e.setParameter(SynthParamAnalogAmount, 0.0f); // no drift -> stable pitch
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.noteOn(69, 100);
        std::vector<float> L(N), R(N);
        e.render(L.data(), R.data(), N);

        double m440 = magAt(L, 440.0, sr);   // fundamental
        double m500 = magAt(L, 500.0, sr);   // non-harmonic (should be tiny)
        printf("Test A (tuning): finite=%d  mag440=%.5f  mag500=%.5f\n",
               (int)allFinite(L), m440, m500);
        check(allFinite(L), "A: output finite");
        check(m440 > 0.02, "A: fundamental present at 440 Hz");
        check(m440 > m500 * 20.0, "A: little energy at non-harmonic 500 Hz (low alias/noise)");
    }

    // ---- Test B: aliasing containment for hard sync ----------------------
    // A synced saw with osc2 well above osc1 is a classic alias generator.
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOsc1Level, 0.0f); e.setParameter(SynthParamOsc2Level, 1.0f);
        e.setParameter(SynthParamOsc2Sync, 1.0f);
        e.setParameter(SynthParamOsc2Semitone, 12.0f);  // an octave up
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamAnalogAmount, 0.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        const double f0 = 110.0; // A2 fundamental
        e.noteOn(45, 100);
        std::vector<float> L(N), R(N);
        e.render(L.data(), R.data(), N);

        // Aliasing shows as energy at frequencies that are NOT harmonics of f0.
        // Compare energy at harmonics vs. at the inter-harmonic midpoints
        // (f0*k + f0/2), which are maximally far from any harmonic.
        double harm = 0, alias = 0;
        for (int k = 1; k <= 30; ++k) {
            double fh = f0 * k;
            if (fh < sr * 0.5) harm += magAt(L, fh, sr);
            double fm = f0 * k + f0 * 0.5;
            if (fm < sr * 0.5) alias += magAt(L, fm, sr);
        }
        double ratio = alias / (harm + 1e-12);
        printf("Test B (sync alias): finite=%d  harmSum=%.5f  aliasSum=%.5f  ratio=%.4f\n",
               (int)allFinite(L), harm, alias, ratio);
        check(allFinite(L), "B: output finite");
        check(peakAbs(L) < 2.0f, "B: output bounded");
        check(ratio < 0.12, "B: inter-harmonic (alias) energy < 12% of harmonic energy");
    }

    // ---- Test C: filter stability at extreme settings --------------------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamFilterResonance, 1.0f);
        e.setParameter(SynthParamFilterSlope, 1.0f);     // 24 dB
        e.setParameter(SynthParamFilterDrive, 1.0f);
        e.setParameter(SynthParamOsc1Level, 0.4f); e.setParameter(SynthParamOsc2Level, 0.6f);
        e.setParameter(SynthParamOscCrossMod, 0.8f);
        e.setParameter(SynthParamOscCrossModTZ, 1.0f);
        e.setParameter(SynthParamOsc2PitchEnv, 0.9f);
        e.setParameter(SynthParamAnalogAmount, 1.0f);
        e.noteOn(60, 127);
        std::vector<float> L(N), R(N);
        e.render(L.data(), R.data(), N);
        float pk = peakAbs(L);
        printf("Test C (extreme stability): finite=%d  peak=%.3f\n",
               (int)allFinite(L), pk);
        check(allFinite(L), "C: output finite under extreme sync+FM+reso+drive");
        check(pk <= 1.0001f, "C: output stays within the master limiter (<= 1.0)");
    }

    // ---- Test D: slope switch is click-free ------------------------------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamFilterResonance, 0.8f);
        e.setParameter(SynthParamFilterCutoff, 1500.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.noteOn(60, 100);
        std::vector<float> a(4096), b(4096), dummy(4096);
        e.render(a.data(), dummy.data(), 4096);          // warm up @ 12 dB
        e.setParameter(SynthParamFilterSlope, 1.0f);      // switch to 24 dB
        e.render(b.data(), dummy.data(), 4096);
        // Largest sample-to-sample jump right at the boundary / early in b.
        float maxJump = std::fabs(b[0] - a[4095]);
        for (int i = 1; i < 512; ++i) maxJump = std::max(maxJump, std::fabs(b[i] - b[i-1]));
        printf("Test D (slope switch): maxJump=%.4f\n", maxJump);
        check(allFinite(b), "D: output finite across slope switch");
        check(maxJump < 0.2f, "D: no large discontinuity when slope is automated");
    }

    printf("\n%s (%d failure%s)\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
