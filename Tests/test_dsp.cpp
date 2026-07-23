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
    // The ratio is a *fifth* (non-integer), NOT an octave: with an exact octave
    // the synced tone stays periodic at f0 and folded aliases land back on the
    // f0 harmonic grid, so an inter-harmonic-bin measurement can't see them. A
    // non-octave ratio scatters aliases off-grid where this test can catch them.
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOsc1Level, 0.0f); e.setParameter(SynthParamOsc2Level, 1.0f);
        e.setParameter(SynthParamOsc2Sync, 1.0f);
        e.setParameter(SynthParamOsc2Semitone, 7.0f);   // a fifth up (non-octave)
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
        check(peakAbs(L) < 1.05f, "B: output bounded");
        check(ratio < 0.05, "B: off-grid (alias) energy < 5% of harmonic energy");
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

    // ---- Test E: through-zero FM drives the carrier to negative frequency --
    // With a reversed phase increment the PolyBLEP must still band-limit the
    // wrap. A wrong direction-of-traversal sign reinforces the step and pushes
    // reverse-running saw/pulse toward +/-2. Deep TZ-FM exercises this path.
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOsc1Level, 1.0f); e.setParameter(SynthParamOsc2Level, 0.0f);
        e.setParameter(SynthParamOscWaveform, 0);   // saw carrier
        e.setParameter(SynthParamOscCrossMod, 1.0f);        // full depth
        e.setParameter(SynthParamOscCrossModTZ, 1.0f);      // through-zero
        e.setParameter(SynthParamOsc2Semitone, 7.0f);
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamAnalogAmount, 0.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.noteOn(45, 110);
        std::vector<float> L(N), R(N);
        e.render(L.data(), R.data(), N);
        printf("Test E (TZ-FM reverse): finite=%d  peak=%.3f\n", (int)allFinite(L), peakAbs(L));
        check(allFinite(L), "E: through-zero FM output finite");
        check(peakAbs(L) < 1.05f, "E: reverse-running oscillator stays bounded (no +/-2 BLEP blow-up)");
    }

    // ---- Test F: MIDI All Sound Off (CC120) silences immediately ----------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamAmpRelease, 1.0f); // long release: allNotesOff would ring
        e.noteOn(60, 100);
        std::vector<float> a(4096), b(4096), dummy(4096);
        e.render(a.data(), dummy.data(), 4096);
        e.allSoundOff();
        e.render(b.data(), dummy.data(), 4096);     // should be silent at once
        printf("Test F (all sound off): before=%.4f  after=%.6f\n", peakAbs(a), peakAbs(b));
        check(peakAbs(a) > 0.05f, "F: voice was sounding before All Sound Off");
        check(peakAbs(b) < 1e-4f, "F: All Sound Off silences immediately (not a slow release)");
    }

    // ---- Test G: wavetable mip crossfade is continuous across octaves ------
    // Gliding two octaves crosses several mip boundaries. Without crossfading
    // adjacent mips the harmonic content would step, producing a jump/click.
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOscWaveform, 3);   // wavetable
        e.setParameter(SynthParamWavetable, 0);
        e.setParameter(SynthParamWTFrame, 0.5f);
        e.setParameter(SynthParamWTLiveness, 0.0f);
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamGlideTime, 0.4f);
        e.noteOn(48, 100);
        std::vector<float> warm(2048), dummy(2048);
        e.render(warm.data(), dummy.data(), 2048);
        e.noteOn(72, 100);                          // glide up two octaves
        int M = (int)(sr * 0.5);
        std::vector<float> L(M), R(M);
        e.render(L.data(), R.data(), M);
        float maxJump = 0.f;
        for (int i = 1; i < M; ++i) maxJump = std::max(maxJump, std::fabs(L[i] - L[i-1]));
        printf("Test G (WT octave glide): finite=%d  maxSampleJump=%.4f\n", (int)allFinite(L), maxJump);
        check(allFinite(L), "G: wavetable glide output finite");
        check(maxJump < 0.5f, "G: no timbre step/click crossing mip octave boundaries");
    }

    // ---- Test H: arpeggiator steps through held notes in order -----------
    {
        SynthEngine e; e.setSampleRate(sr);
        const int step = (int)sr / 8;                  // 1/16 at 120 BPM
        e.setTempo(120.0);
        e.setParameter(SynthParamArpOn, 1.0f);
        e.setParameter(SynthParamArpMode, 0.0f);      // Up
        e.setParameter(SynthParamArpOctaves, 1.0f);
        e.setParameter(SynthParamArpRate, SYNTH_SYNC_DEFAULT_ARP);
        e.setParameter(SynthParamArpGate, 0.9f);
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamAmpAttack, 0.001f);
        e.noteOn(60, 100); e.noteOn(64, 100); e.noteOn(67, 100);  // C E G
        std::vector<float> L(step * 4), R(step * 4);
        e.render(L.data(), R.data(), (int)L.size());
        auto note2hz = [](int n){ return 440.0 * std::pow(2.0, (n - 69) / 12.0); };
        auto winMag = [&](int k, int note){
            double re = 0, im = 0; int a = k*step + 800, c = k*step + step - 400;
            for (int nn = a; nn < c; ++nn) { double ph = kTwoPi*note2hz(note)*nn/sr; re += L[nn]*std::cos(ph); im -= L[nn]*std::sin(ph); }
            return std::sqrt(re*re + im*im);
        };
        // Step 0 should be the lowest held note (60), step 2 the highest (67).
        bool s0 = winMag(0,60) > winMag(0,67) && winMag(0,60) > winMag(0,64);
        bool s2 = winMag(2,67) > winMag(2,60) && winMag(2,67) > winMag(2,64);
        printf("Test H (arp Up): step0 dominant-low=%d  step2 dominant-high=%d\n", (int)s0, (int)s2);
        check(allFinite(L), "H: arpeggiator output finite");
        check(s0 && s2, "H: arp plays held chord low->high (Up mode)");
    }

    // ---- Test I: global stereo effects are finite, wide, and produce tails --
    {
        GlobalEffects fx;
        fx.setup(sr);
        fx.setParams(1.0f, 0.55f, 0.8f,   // chorus: wet, rate, depth
                     0.35f, 0.05f, 0.65f, // delay: mix, 50 ms, feedback
                     0.6f, 1.0f);         // tone, ping-pong

        const int M = static_cast<int>(sr * 0.35);
        std::vector<float> L(M), R(M);
        bool finite = true;
        double stereoEnergy = 0.0, tailEnergy = 0.0;
        for (int n = 0; n < M; ++n) {
            float input = (n < static_cast<int>(sr * 0.08))
                            ? 0.4f * std::sin(static_cast<float>(kTwoPi * 220.0 * n / sr))
                            : 0.0f;
            StereoSample y = fx.process(input);
            L[n] = y.l; R[n] = y.r;
            finite = finite && std::isfinite(y.l) && std::isfinite(y.r);
            if (n > static_cast<int>(sr * 0.03))
                stereoEnergy += std::fabs(static_cast<double>(y.l - y.r));
            if (n > static_cast<int>(sr * 0.14))
                tailEnergy += std::fabs(static_cast<double>(y.l))
                            + std::fabs(static_cast<double>(y.r));
        }
        printf("Test I (stereo FX): finite=%d  width=%.3f  tail=%.3f\n",
               (int)finite, stereoEnergy, tailEnergy);
        check(finite, "I: chorus and delay output remains finite");
        check(stereoEnergy > 1.0, "I: chorus/ping-pong chain creates stereo width");
        check(tailEnergy > 1.0, "I: feedback delay continues after input stops");

        fx.reset();
        float resetPeak = 0.0f;
        for (int n = 0; n < 4096; ++n) {
            StereoSample y = fx.process(0.0f);
            resetPeak = std::max(resetPeak, std::max(std::fabs(y.l), std::fabs(y.r)));
        }
        check(resetPeak < 1e-6f, "I: resetting effects clears delay and chorus memory");
    }

    printf("\n%s (%d failure%s)\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
