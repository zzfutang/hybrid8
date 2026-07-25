//
//  test_dsp.cpp
//  Offline regression tests for the DSP core (header-only C++). Verifies
//  oscillator tuning, alias containment, and filter stability at extreme
//  settings. Build & run:
//    clang++ -std=c++17 -O2 Tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp
//

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"
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

    // ---- Test I2: FDN reverb produces a stable, decorrelated stereo tail ---
    {
        StereoReverb reverb;
        reverb.setup(sr);
        reverb.setParams(1.0f, 0.72f, 4.0f, 0.62f, 0.0f);

        const int M = static_cast<int>(sr * 1.2);
        bool finite = true;
        double tailEnergy = 0.0, stereoEnergy = 0.0;
        float peak = 0.0f;
        for (int n = 0; n < M; ++n) {
            const float impulse = n == 0 ? 0.5f : 0.0f;
            StereoSample y = reverb.process(impulse, impulse);
            finite = finite && std::isfinite(y.l) && std::isfinite(y.r);
            peak = std::max(peak, std::max(std::fabs(y.l), std::fabs(y.r)));
            if (n > static_cast<int>(sr * 0.12)) {
                tailEnergy += std::fabs(static_cast<double>(y.l))
                            + std::fabs(static_cast<double>(y.r));
                stereoEnergy += std::fabs(static_cast<double>(y.l - y.r));
            }
        }
        printf("Test I2 (FDN reverb): finite=%d peak=%.3f tail=%.3f width=%.3f\n",
               (int)finite, peak, tailEnergy, stereoEnergy);
        check(finite, "I2: reverb remains finite at a long decay");
        check(peak < 2.0f, "I2: reverb impulse response remains bounded");
        check(tailEnergy > 1.0, "I2: reverb produces a sustained tail");
        check(stereoEnergy > 0.1, "I2: reverb return is decorrelated stereo");

        reverb.reset();
        float resetPeak = 0.0f;
        for (int n = 0; n < 4096; ++n) {
            StereoSample y = reverb.process(0.0f, 0.0f);
            resetPeak = std::max(resetPeak, std::max(std::fabs(y.l), std::fabs(y.r)));
        }
        check(resetPeak < 1e-6f, "I2: resetting reverb clears its tail");
    }

    // ---- Test J: maximum polyphony remains bounded at unity master --------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamMasterGain, 1.0f);
        e.setParameter(SynthParamOsc1Level, 1.0f);
        e.setParameter(SynthParamOsc2Level, 1.0f);
        e.setParameter(SynthParamFilterCutoff, 20000.0f);
        e.setParameter(SynthParamFilterResonance, 0.8f);
        e.setParameter(SynthParamFilterDrive, 1.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        for (int n = 0; n < 8; ++n) e.noteOn(48 + n, 127);
        std::vector<float> L(N), R(N);
        e.render(L.data(), R.data(), N);
        const float peak = peakAbs(L);
        printf("Test J (8-voice gain): finite=%d  peak=%.3f\n",
               (int)allFinite(L), peak);
        check(allFinite(L), "J: full polyphony output finite");
        check(peak <= 1.0f, "J: full polyphony remains within the soft ceiling");
    }

    // ---- Test K: 24 dB ladder enters bounded self-oscillation --------------
    {
        const double filterRate = sr * 2.0;
        const int M = static_cast<int>(filterRate * 1.5);
        LadderFilter filter;
        filter.setSampleRate(filterRate);
        filter.reset();
        filter.setParams(1000.0, 0.98f, 1.0f, 0.0f, 0.0f);

        double energy = 0.0;
        int positiveCrossings = 0;
        float previous = 0.0f;
        float peak = 0.0f;
        for (int n = 0; n < M; ++n) {
            const float input = n == 0 ? 0.01f : 0.0f;
            const float y = filter.process(input);
            peak = std::max(peak, std::fabs(y));
            if (n >= M / 2) {
                energy += static_cast<double>(y) * y;
                if (previous <= 0.0f && y > 0.0f) ++positiveCrossings;
            }
            previous = y;
        }
        const double rms = std::sqrt(energy / (M / 2));
        const double measuredHz = positiveCrossings / 0.75;
        printf("Test K (ladder self-osc): rms=%.5f  peak=%.3f  freq=%.1f Hz\n",
               rms, peak, measuredHz);
        check(rms > 0.001, "K: maximum ladder resonance sustains oscillation");
        check(peak < 1.0f, "K: nonlinear ladder oscillation remains bounded");
        check(measuredHz > 850.0 && measuredHz < 1150.0,
              "K: self-oscillation tracks the requested cutoff");
    }

    // ---- Test L: nonlinear 12 dB feedback preserves its small-signal model -
    {
        const double filterRate = sr * 2.0;
        const int M = static_cast<int>(filterRate * 0.25);
        auto differenceRMS = [&](float amplitude) {
            SVFStage clean, driven;
            clean.setSampleRate(filterRate);
            driven.setSampleRate(filterRate);
            clean.setCoefficients(1200.0, 5.0, 0.0f, 0.0f);
            driven.setCoefficients(1200.0, 5.0, 1.0f, 0.0f);
            double difference = 0.0, reference = 0.0;
            for (int n = 0; n < M; ++n) {
                const float x = amplitude
                              * std::sin(static_cast<float>(kTwoPi * 700.0 * n / filterRate));
                const float a = clean.processLP(x);
                const float b = driven.processLP(x);
                if (n > M / 4) {
                    difference += static_cast<double>(a - b) * (a - b);
                    reference += static_cast<double>(a) * a;
                }
            }
            return std::sqrt(difference / std::max(reference, 1.0e-20));
        };
        const double quietDifference = differenceRMS(0.001f);
        const double loudDifference = differenceRMS(0.9f);
        printf("Test L (12 dB nonlinearity): quietDiff=%.6f  loudDiff=%.4f\n",
               quietDifference, loudDifference);
        check(quietDifference < 0.001,
              "L: Drive preserves the 12 dB small-signal response");
        check(loudDifference > 0.01,
              "L: Drive changes the 12 dB response at musical signal levels");
    }

    // ---- Test M: voice-card VCF tolerances are bounded and deterministic ---
    {
        bool bounded = true;
        bool distinct = false;
        const VCFTolerance first = VCFTolerance::fromSeed(0x1234ULL + 0x9e37U);
        const VCFTolerance repeat = VCFTolerance::fromSeed(0x1234ULL + 0x9e37U);
        for (int i = 0; i < 8; ++i) {
            const VCFTolerance t =
                VCFTolerance::fromSeed(0x1234ULL + 0x9e37U * (i + 1));
            bounded = bounded
                   && std::fabs(t.cutoffCents) <= 18.0f
                   && std::fabs(t.resonanceScale) <= 0.03f
                   && std::fabs(t.saturationScale) <= 0.04f;
            if (i > 0 && std::fabs(t.cutoffCents - first.cutoffCents) > 0.01f)
                distinct = true;
        }
        const bool deterministic =
            first.cutoffCents == repeat.cutoffCents
            && first.resonanceScale == repeat.resonanceScale
            && first.saturationScale == repeat.saturationScale;
        printf("Test M (VCF tolerances): firstCutoff=%+.2fc  bounded=%d  distinct=%d\n",
               first.cutoffCents, (int)bounded, (int)distinct);
        check(bounded, "M: individual VCF component tolerances stay subtle");
        check(distinct, "M: the eight voice cards have distinct VCF calibration");
        check(deterministic, "M: VCF character is repeatable across sessions");
    }

    // ---- Test N: stealing a releasing voice fades before state reset -------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamVoiceCount, 1.0f);
        e.setParameter(SynthParamLegato, 0.0f);
        e.setParameter(SynthParamOscPhaseSpread, 1.0f);
        e.setParameter(SynthParamFilterCutoff, 16000.0f);
        e.setParameter(SynthParamAmpAttack, normFromTime(0.001f));
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamAmpRelease, normFromTime(2.0f));
        e.noteOn(60, 127);
        std::vector<float> warm(4096), dummy(4096);
        e.render(warm.data(), dummy.data(), 4096);
        e.noteOff(60);
        std::vector<float> release(256);
        e.render(release.data(), dummy.data(), 256);
        const float before = release.back();

        e.noteOn(84, 127); // forced steal while the old note is still audible
        std::vector<float> stolen(512), right(512);
        e.render(stolen.data(), right.data(), 512);
        const int fadeSamples = static_cast<int>(sr * 0.003);
        float boundaryJump = std::fabs(stolen.front() - before);
        float endOfFade = std::fabs(stolen[fadeSamples - 1]);
        printf("Test N (voice steal): boundaryJump=%.5f  fadeEnd=%.6f\n",
               boundaryJump, endOfFade);
        check(boundaryJump < 0.03f,
              "N: stealing does not reset an audible voice at the boundary");
        check(endOfFade < 0.01f,
              "N: the old voice fades near zero before retrigger");
        check(allFinite(stolen), "N: voice-steal transition remains finite");
    }

    // ---- Test O: FX crossfades preserve power instead of dipping at 50% ---
    {
        const StereoSample midpoint =
            equalPowerMix(1.0f, 0.0f, 0.0f, 1.0f, 0.5f);
        const StereoSample dry =
            equalPowerMix(1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        const StereoSample wet =
            equalPowerMix(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        const float midpointPower =
            midpoint.l * midpoint.l + midpoint.r * midpoint.r;
        printf("Test O (FX gain): midpointPower=%.6f  dry=(%.3f, %.3f)"
               "  wet=(%.3f, %.3f)\n",
               midpointPower, dry.l, dry.r, wet.l, wet.r);
        check(std::fabs(midpointPower - 1.0f) < 1.0e-5f,
              "O: the FX midpoint preserves power for decorrelated signals");
        check(std::fabs(dry.l - 1.0f) < 1.0e-6f
              && std::fabs(dry.r) < 1.0e-6f,
              "O: zero mix is fully dry");
        check(std::fabs(wet.l) < 1.0e-6f
              && std::fabs(wet.r - 1.0f) < 1.0e-6f,
              "O: full mix is fully wet");
    }

    // ---- Test P: voice-card spread pans voices without changing mono -------
    {
        auto renderVoice = [&](float spread) {
            SynthEngine e; e.setSampleRate(sr);
            e.setParameter(SynthParamVoiceCount, 1.0f);
            e.setParameter(SynthParamStereoSpread, spread);
            e.setParameter(SynthParamOscPhaseSpread, 0.0f);
            e.setParameter(SynthParamFilterCutoff, 18000.0f);
            e.setParameter(SynthParamAmpSustain, 1.0f);
            e.noteOn(60, 127);
            std::array<std::vector<float>, 2> output = {
                std::vector<float>(4096), std::vector<float>(4096)
            };
            e.render(output[0].data(), output[1].data(), 4096);
            return output;
        };
        const auto mono = renderVoice(0.0f);
        const auto wide = renderVoice(1.0f);
        double monoDifference = 0.0, wideL = 0.0, wideR = 0.0;
        for (int n = 2048; n < 4096; ++n) {
            monoDifference += std::fabs(mono[0][n] - mono[1][n]);
            wideL += static_cast<double>(wide[0][n]) * wide[0][n];
            wideR += static_cast<double>(wide[1][n]) * wide[1][n];
        }
        printf("Test P (voice spread): monoDiff=%.8f  wideL=%.4f  wideR=%.6f\n",
               monoDifference, std::sqrt(wideL), std::sqrt(wideR));
        check(monoDifference < 1.0e-5,
              "P: zero stereo spread remains exactly centred");
        check(wideL > wideR * 20.0,
              "P: full spread places the first voice toward its calibrated side");
        check(allFinite(wide[0]) && allFinite(wide[1]),
              "P: stereo voice panning remains finite");
    }

    // ---- Test Q: MIDI sustain holds released keys until pedal-up ----------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamFilterCutoff, 18000.0f);
        e.setParameter(SynthParamAmpAttack, normFromTime(0.001f));
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamAmpRelease, normFromTime(0.015f));
        e.noteOn(60, 127);
        std::vector<float> warm(2048), right(8192);
        e.render(warm.data(), right.data(), 2048);
        e.sustainPedal(true);
        e.noteOff(60);
        std::vector<float> held(2048);
        e.render(held.data(), right.data(), 2048);
        e.sustainPedal(false);
        std::vector<float> released(8192);
        e.render(released.data(), right.data(), 8192);
        float releasedTail = 0.0f;
        for (int n = 6144; n < 8192; ++n)
            releasedTail = std::max(releasedTail, std::fabs(released[n]));
        printf("Test Q (sustain): heldPeak=%.5f  releasedTail=%.7f\n",
               peakAbs(held), releasedTail);
        check(peakAbs(held) > 0.01f,
              "Q: CC64 holds a voice after its key is released");
        check(releasedTail < 0.001f,
              "Q: pedal-up releases keys that are no longer physically held");
        check(allFinite(released), "Q: sustain transitions remain finite");
    }

    // ---- Test R: host automation ramps retain duration across renders -----
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamFilterCutoff, 200.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.noteOn(60, 127);
        std::vector<float> left(1024), right(1024);
        e.render(left.data(), right.data(), 1024);

        e.startParameterRamp(SynthParamFilterCutoff, 10000.0f, 1024);
        e.render(left.data(), right.data(), 512);
        const float midpoint = e.getParameter(SynthParamFilterCutoff);
        e.render(left.data() + 512, right.data() + 512, 512);
        const float endpoint = e.getParameter(SynthParamFilterCutoff);
        float maxJump = 0.0f;
        for (int n = 1; n < 1024; ++n)
            maxJump = std::max(maxJump, std::fabs(left[n] - left[n - 1]));
        printf("Test R (automation ramp): midpoint=%.1f endpoint=%.1f jump=%.5f\n",
               midpoint, endpoint, maxJump);
        check(std::fabs(midpoint - 5100.0f) < 1.0f,
              "R: automation ramp reaches its midpoint after half the duration");
        check(std::fabs(endpoint - 10000.0f) < 0.01f,
              "R: automation ramp reaches the exact scheduled target");
        check(maxJump < 0.5f && allFinite(left),
              "R: ramped filter automation remains continuous and finite");
    }

    // ---- Test S: LP, BP and HP modes select the intended spectrum ---------
    {
        const double filterRate = sr * 2.0;
        auto response = [&](float slope, float mode, double frequency) {
            LadderFilter filter;
            filter.setSampleRate(filterRate);
            filter.reset();
            filter.setParams(1000.0, 0.1f, slope, 0.0f, 0.0f, mode);
            const int count = static_cast<int>(filterRate * 0.15);
            double energy = 0.0;
            for (int n = 0; n < count; ++n) {
                const float input = 0.1f * std::sin(
                    static_cast<float>(kTwoPi * frequency * n / filterRate));
                const float output = filter.process(input);
                if (n > count / 3)
                    energy += static_cast<double>(output) * output;
            }
            return std::sqrt(energy / (count - count / 3));
        };
        bool shaped = true;
        for (float slope : {0.0f, 1.0f}) {
            const double lpLow = response(slope, 0.0f, 100.0);
            const double lpHigh = response(slope, 0.0f, 8000.0);
            const double bpLow = response(slope, 1.0f, 100.0);
            const double bpMid = response(slope, 1.0f, 1000.0);
            const double bpHigh = response(slope, 1.0f, 8000.0);
            const double hpLow = response(slope, 2.0f, 100.0);
            const double hpHigh = response(slope, 2.0f, 8000.0);
            printf("Test S (%s modes): LP %.4f/%.5f  BP %.5f/%.4f/%.5f"
                   "  HP %.5f/%.4f\n",
                   slope < 0.5f ? "12 dB" : "24 dB",
                   lpLow, lpHigh, bpLow, bpMid, bpHigh, hpLow, hpHigh);
            shaped = shaped && lpLow > lpHigh * 3.0
                            && bpMid > bpLow * 1.5
                            && bpMid > bpHigh * 1.5
                            && hpHigh > hpLow * 3.0;
        }
        check(shaped,
              "S: both filter slopes provide distinct LP, BP and HP responses");
    }

    // ---- Test T: poly unison creates symmetric stereo-detuned voice pairs --
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamVoiceCount, 8.0f);
        e.setParameter(SynthParamUnison, 1.0f);
        e.setParameter(SynthParamUnisonDetune, 1.0f); // +/-50 cents
        e.setParameter(SynthParamStereoSpread, 1.0f);
        e.setParameter(SynthParamOscPhaseSpread, 0.0f);
        e.setParameter(SynthParamAnalogAmount, 0.0f);
        e.setParameter(SynthParamFilterCutoff, 18000.0f);
        e.setParameter(SynthParamFilterEnvAmount, 0.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.setParameter(SynthParamAmpRelease, normFromTime(0.015f));
        e.noteOn(69, 127);
        std::vector<float> left(N), right(N);
        e.render(left.data(), right.data(), N);
        const double lowFrequency = 440.0 * std::pow(2.0, -0.5 / 12.0);
        const double highFrequency = 440.0 * std::pow(2.0, 0.5 / 12.0);
        const double leftLow = magAt(left, lowFrequency, sr);
        const double leftHigh = magAt(left, highFrequency, sr);
        const double rightLow = magAt(right, lowFrequency, sr);
        const double rightHigh = magAt(right, highFrequency, sr);
        e.noteOff(69);
        std::vector<float> tail(8192);
        e.render(tail.data(), right.data(), 8192);
        float tailEnd = 0.0f;
        for (int n = 6144; n < 8192; ++n)
            tailEnd = std::max(tailEnd, std::fabs(tail[n]));
        printf("Test T (unison): L %.5f/%.5f  R %.5f/%.5f"
               " peak=%.3f tail=%.7f\n",
               leftLow, leftHigh, rightLow, rightHigh,
               std::max(peakAbs(left), peakAbs(right)), tailEnd);
        check(leftLow > leftHigh * 2.0 && rightHigh > rightLow * 2.0,
              "T: unison voices are symmetrically detuned and stereo-separated");
        check(peakAbs(left) <= 1.0f && peakAbs(right) <= 1.0f,
              "T: unison output remains within the soft ceiling");
        check(tailEnd < 0.001f,
              "T: note-off releases both voice cards in the unison pair");
    }

    // ---- Test U: expanded matrix controls mixer levels and per-voice pan ---
    {
        SynthEngine muted; muted.setSampleRate(sr);
        muted.setParameter(SynthParamFilterCutoff, 18000.0f);
        muted.setParameter(SynthParamFilterEnvAmount, 0.0f);
        muted.setParameter(SynthParamAmpSustain, 1.0f);
        muted.setParameter(SynthParamMod1Source, ModSrcModWheel);
        muted.setParameter(SynthParamMod1Dest, ModDstOsc1Level);
        muted.setParameter(SynthParamMod1Amount, -1.0f);
        muted.modWheel(1.0f);
        muted.noteOn(60, 127);
        std::vector<float> silence(4096), scratch(4096);
        muted.render(silence.data(), scratch.data(), 4096);

        SynthEngine panned; panned.setSampleRate(sr);
        panned.setParameter(SynthParamStereoSpread, 0.0f);
        panned.setParameter(SynthParamFilterCutoff, 18000.0f);
        panned.setParameter(SynthParamFilterEnvAmount, 0.0f);
        panned.setParameter(SynthParamAmpSustain, 1.0f);
        panned.setParameter(SynthParamMod1Source, ModSrcModWheel);
        panned.setParameter(SynthParamMod1Dest, ModDstVoicePan);
        panned.setParameter(SynthParamMod1Amount, 1.0f);
        panned.modWheel(1.0f);
        panned.noteOn(60, 127);
        std::vector<float> left(4096), right(4096);
        panned.render(left.data(), right.data(), 4096);
        double leftEnergy = 0.0, rightEnergy = 0.0;
        for (int n = 2048; n < 4096; ++n) {
            leftEnergy += static_cast<double>(left[n]) * left[n];
            rightEnergy += static_cast<double>(right[n]) * right[n];
        }
        printf("Test U (expanded mod): mute=%.7f panEnergy=%.6f/%.4f\n",
               peakAbs(silence), std::sqrt(leftEnergy), std::sqrt(rightEnergy));
        check(peakAbs(silence) < 0.001f,
              "U: matrix modulation can fully close an oscillator mixer level");
        check(rightEnergy > leftEnergy * 100.0,
              "U: voice-pan destination moves a centred voice across the stage");
        check(allFinite(left) && allFinite(right),
              "U: expanded modulation destinations remain finite");
    }

    // ---- Test V: chord trigger expands before the arpeggiator -------------
    {
        auto configure = [&](SynthEngine& e) {
            e.setSampleRate(sr);
            e.setParameter(SynthParamChordOn, 1.0f);
            e.setParameter(SynthParamChordType, 0.0f); // major
            e.setParameter(SynthParamChordInversion, 0.0f);
            e.setParameter(SynthParamOscPhaseSpread, 0.0f);
            e.setParameter(SynthParamAnalogAmount, 0.0f);
            e.setParameter(SynthParamFilterCutoff, 18000.0f);
            e.setParameter(SynthParamFilterEnvAmount, 0.0f);
            e.setParameter(SynthParamAmpAttack, 0.0f);
            e.setParameter(SynthParamAmpSustain, 1.0f);
        };
        auto noteHz = [](int note) {
            return 440.0 * std::pow(2.0, (note - 69) / 12.0);
        };

        SynthEngine chord; configure(chord);
        chord.noteOn(60, 127);
        std::vector<float> direct(16384), scratch(16384);
        chord.render(direct.data(), scratch.data(), static_cast<int>(direct.size()));
        const double c = magAt(direct, noteHz(60), sr);
        const double e = magAt(direct, noteHz(64), sr);
        const double g = magAt(direct, noteHz(67), sr);
        printf("Test V (chord direct): C=%.5f E=%.5f G=%.5f\n", c, e, g);
        check(c > 0.002 && e > 0.002 && g > 0.002,
              "V: one C key generates the C-major chord tones");

        SynthEngine arpChord; configure(arpChord);
        arpChord.setTempo(120.0);
        arpChord.setParameter(SynthParamArpOn, 1.0f);
        arpChord.setParameter(SynthParamArpMode, 0.0f);
        arpChord.setParameter(SynthParamArpOctaves, 1.0f);
        arpChord.setParameter(SynthParamArpRate, 13.0f); // 1/32 = 3000 samples
        arpChord.setParameter(SynthParamArpGate, 0.9f);
        arpChord.noteOn(60, 127);
        const int step = 3000;
        std::vector<float> sequence(step * 3), seqR(step * 3);
        arpChord.render(sequence.data(), seqR.data(), static_cast<int>(sequence.size()));
        auto windowMagnitude = [&](int stepIndex, int note) {
            double re = 0.0, im = 0.0;
            const int begin = stepIndex * step + 400;
            const int end = (stepIndex + 1) * step - 300;
            for (int n = begin; n < end; ++n) {
                const double phase = kTwoPi * noteHz(note) * n / sr;
                re += sequence[n] * std::cos(phase);
                im -= sequence[n] * std::sin(phase);
            }
            return std::sqrt(re * re + im * im);
        };
        const bool ordered =
            windowMagnitude(0, 60) > windowMagnitude(0, 64)
            && windowMagnitude(1, 64) > windowMagnitude(1, 60)
            && windowMagnitude(2, 67) > windowMagnitude(2, 64);
        printf("Test V (chord -> arp): ordered=%d finite=%d\n",
               (int)ordered, (int)allFinite(sequence));
        check(ordered,
              "V: generated C-major tones reach the arp in ascending order");
        check(allFinite(sequence),
              "V: chord-to-arp processing remains finite");
    }

    // ---- Test W: first-stage compressor is linked, bounded, and bypassable -
    {
        StereoCompressor compressor;
        compressor.setup(sr);
        compressor.setParams(0.0f, -18.0f, 4.0f, 0.001f, 0.1f, 0.0f);
        StereoSample bypassed{};
        for (int n = 0; n < 4096; ++n)
            bypassed = compressor.process(0.8f, 0.4f);
        const bool unityBypass = std::fabs(bypassed.l - 0.8f) < 1.0e-6f
                              && std::fabs(bypassed.r - 0.4f) < 1.0e-6f;

        compressor.setParams(1.0f, -18.0f, 4.0f, 0.001f, 0.1f, 0.0f);
        StereoSample compressed{};
        bool finite = true;
        for (int n = 0; n < static_cast<int>(sr * 0.25); ++n) {
            compressed = compressor.process(0.8f, 0.4f);
            finite = finite && std::isfinite(compressed.l)
                            && std::isfinite(compressed.r);
        }
        const float stereoRatio = compressed.r != 0.0f
                                ? compressed.l / compressed.r : 0.0f;
        printf("Test W (compressor): bypass=%.3f compressed=%.3f ratio=%.3f\n",
               bypassed.l, compressed.l, stereoRatio);
        check(unityBypass,
              "W: disabled compressor is exact unity gain");
        check(compressed.l < 0.35f && compressed.l > 0.12f,
              "W: enabled compressor applies the expected gain reduction");
        check(std::fabs(stereoRatio - 2.0f) < 0.001f,
              "W: stereo linking preserves the left/right image");
        check(finite,
              "W: compressor output remains finite");
    }

    // ---- Test X: imported tables retain dynamic frame counts and tuning ----
    {
        constexpr int frameLength = 256;
        constexpr int frameCount = 5;
        std::vector<float> source(frameLength * frameCount);
        for (int frame = 0; frame < frameCount; ++frame) {
            for (int n = 0; n < frameLength; ++n) {
                const double phase = kTwoPi * n / frameLength;
                source[frame * frameLength + n] =
                    static_cast<float>(std::sin(phase)
                    + 0.08 * frame * std::sin(phase * (frame + 2)));
            }
        }
        const int userSlot = WT_NUM_SETS;   // first user slot (after the factory sets)
        const bool installed = wtInstallImportedTable(
            userSlot, source.data(), static_cast<int>(source.size()), frameLength);
        const WavetableSet* table = wtTableAt(userSlot);
        WavetableOscillator osc;
        osc.setSampleRate(sr);
        osc.reset(0.0f, 0.0f);
        osc.setTable(table);
        osc.setFrame(1.0f);
        osc.setLiveness(0.0f);
        osc.setFrequency(440.0);
        std::vector<float> rendered(N);
        for (float& sample : rendered) sample = osc.process();
        const double fundamental = magAt(rendered, 440.0, sr);
        printf("Test X (WT import): installed=%d frames=%d mag440=%.5f\n",
               (int)installed, table->frameCount, fundamental);
        check(installed && table->frameCount == frameCount,
              "X: imported wavetable preserves its dynamic frame count");
        check(allFinite(rendered) && fundamental > 0.1,
              "X: imported wavetable is finite and remains correctly tuned");
    }

    // ---- Test Y: bipolar LFO modulation stays centred on WT Frame ----------
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOscWaveform, 3.0f);
        e.setParameter(SynthParamWTFrame, 0.6f);
        e.setParameter(SynthParamLFO2Waveform, 0.0f); // sine, bipolar
        e.setParameter(SynthParamLFO2Rate, 5.0f);
        e.setParameter(SynthParamMod1Source, ModSrcLFO2);
        e.setParameter(SynthParamMod1Dest, ModDstWTFrame);
        e.setParameter(SynthParamMod1Amount, 0.1f);
        e.setParameter(SynthParamAmpSustain, 1.0f);
        e.noteOn(60, 100);

        std::vector<float> L(64), R(64);
        // Let the 20 ms anti-click smoother reach the knob value before
        // measuring the bipolar modulation range.
        for (int block = 0; block < 32; ++block)
            e.render(L.data(), R.data(), 64);
        float lo = 1.0f, hi = 0.0f;
        for (int block = 0; block < 300; ++block) {
            e.render(L.data(), R.data(), 64);
            const float frame = e.getEffectiveParameter(SynthParamWTFrame);
            lo = std::min(lo, frame);
            hi = std::max(hi, frame);
        }
        const float centre = 0.5f * (lo + hi);
        printf("Test Y (WT frame centre): lo=%.4f hi=%.4f centre=%.4f\n",
               lo, hi, centre);
        check(std::fabs(centre - 0.6f) < 0.01f,
              "Y: sine WT-frame modulation is centred on the knob value");
        check(lo < 0.51f && hi > 0.69f,
              "Y: WT-frame modulation has equal positive and negative depth");
    }

    // ---- Test Z: matrix WT-frame route replaces its hidden legacy route ----
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOscWaveform, 3.0f);
        e.setParameter(SynthParamWTFrame, 0.4f);
        // Simulate an old saved patch containing the hidden fixed route.
        e.setParameter(SynthParamWTFrameEnv, -0.6f);
        e.setParameter(SynthParamMod1Source, ModSrcFilterEnv);
        e.setParameter(SynthParamMod1Dest, ModDstWTFrame);
        e.setParameter(SynthParamMod1Amount, 0.3f);
        e.setParameter(SynthParamFilterAttack, 0.0f);
        e.setParameter(SynthParamFilterDecay, 0.0f);
        e.setParameter(SynthParamFilterSustain, 1.0f);
        e.setParameter(SynthParamAmpSustain, 1.0f);

        std::vector<float> L(64), R(64);
        for (int block = 0; block < 128; ++block)
            e.render(L.data(), R.data(), 64); // settle frame smoother
        e.noteOn(60, 100);
        e.render(L.data(), R.data(), 64);
        const float frame = e.getEffectiveParameter(SynthParamWTFrame);
        printf("Test Z (WT route precedence): frame=%.4f\n", frame);
        check(frame > 0.69f && frame <= 0.701f,
              "Z: explicit matrix route replaces hidden legacy WT-frame route");
    }

    // ---- Test AA: expanded LFO waves, phase and polarity -------------------
    {
        LFO lfo;
        lfo.setSampleRate(100.0);
        lfo.setRate(10.0);
        lfo.setWave(LFOWave::SawDown);
        lfo.setPhase(0.25f);
        lfo.setPolarity(false);
        lfo.reset();
        const float bipolar = lfo.process();

        lfo.reset();
        lfo.setPolarity(true);
        const float unipolar = lfo.process();

        lfo.reset();
        lfo.setPhase(0.0f);
        lfo.setPolarity(false);
        lfo.setWave(LFOWave::SampleHold);
        const float held = lfo.process();
        bool stableHold = true;
        for (int i = 0; i < 8; ++i)
            stableHold &= lfo.process() == held;
        lfo.process(); // wrap the 10-sample cycle and choose a new held value
        const float nextHold = lfo.process();

        printf("Test AA (expanded LFO): bi=%.3f uni=%.3f holdChanged=%d\n",
               bipolar, unipolar, (int)(nextHold != held));
        check(std::fabs(bipolar - 0.5f) < 0.001f
              && std::fabs(unipolar - 0.75f) < 0.001f,
              "AA: phase and polarity produce the expected LFO range");
        check(stableHold && nextHold != held,
              "AA: sample-and-hold stays constant for one LFO cycle");
        check(ModSrcLFO3 == 10,
              "AA: LFO 3 is appended without renumbering existing sources");
    }

    // ---- Test AB: wavetable liveness remains frame-coherent ---------------
    {
        const WTSpectrum frame0 = wtSpectrumPiano(0);
        const WTSpectrum frame1 = wtSpectrumPiano(1);
        float maxPhaseStep = 0.0f;
        float maxFrameDeltaError = 0.0f;
        for (int variant = 0; variant < WT_NUM_VARIANTS; ++variant) {
            const int next = (variant + 1) % WT_NUM_VARIANTS;
            const WTSpectrum a0 = wtVariant(frame0, variant, 124);
            const WTSpectrum b0 = wtVariant(frame0, next, 124);
            const WTSpectrum a1 = wtVariant(frame1, variant, 124);
            for (int harmonic = 1; harmonic < (int)frame0.phase.size(); ++harmonic) {
                maxPhaseStep = std::max(
                    maxPhaseStep,
                    std::fabs(b0.phase[harmonic] - a0.phase[harmonic]));
                const float delta0 =
                    a0.phase[harmonic] - frame0.phase[harmonic];
                const float delta1 =
                    a1.phase[harmonic] - frame1.phase[harmonic];
                maxFrameDeltaError = std::max(
                    maxFrameDeltaError, std::fabs(delta0 - delta1));
            }
        }
        printf("Test AB (WT liveness): variants=%d maxStep=%.3f frameError=%.7f\n",
               WT_NUM_VARIANTS, maxPhaseStep, maxFrameDeltaError);
        check(WT_NUM_VARIANTS >= 4 && maxPhaseStep <= 0.61f,
              "AB: adjacent liveness variants use small phase steps");
        check(maxFrameDeltaError < 1.0e-6f,
              "AB: liveness phase trajectory is coherent across frames");
    }

    printf("\n%s (%d failure%s)\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
