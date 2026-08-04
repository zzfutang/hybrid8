// R50 DSP stage audit: tap the signal chain at five points — oscillator,
// post-filter, post-mixer, post-volume, post-limiter — sweep the saw from A0
// to C8 at several levels, and measure the spectrum at every tap.
//
// The stages are the engine's own components (WaveOscillator,
// DigitalLowPassFilter, outputLimit) wired in the engine's per-sample order
// with envelopes held steady, so a spectral measurement sees the DSP alone.
// A second pass renders chords through the full R50Engine and checks the
// whole path for linearity and level stability.
//
// Build & run: ./scripts/audit-r50-dsp-stages.sh

#include "R50Engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static constexpr double kSR = 48000.0;
static constexpr int kFFT = 32768;          // 1.46 Hz bins: resolves A0 harmonics
static constexpr int kSettle = 24000;       // 0.5 s before capture
static constexpr int kTaps = 5;

static const char *kTapNames[kTaps] = {
    "oscillator", "post-filter", "post-mixer", "post-volume", "post-limiter"
};

static int g_failures = 0;
static void check(bool cond, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char message[512];
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", message);
    if (!cond) ++g_failures;
}

static double midiHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

static double dB(double ratio) {
    return 20.0 * std::log10(std::max(ratio, 1e-12));
}

// ---- Spectrum -------------------------------------------------------------

static void fft(std::vector<std::complex<double>> &x) {
    const int n = static_cast<int>(x.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * synth::kPi / len;
        const std::complex<double> wBase(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<double> even = x[i + k];
                const std::complex<double> odd  = x[i + k + len / 2] * w;
                x[i + k]            = even + odd;
                x[i + k + len / 2]  = even - odd;
                w *= wBase;
            }
        }
    }
}

/// Blackman-Harris 4-term: -92 dB sidelobes, so a -100 dB spur floor is
/// measurable instead of being buried under window leakage.
static const std::vector<double> &window() {
    static const std::vector<double> w = [] {
        std::vector<double> v(kFFT);
        for (int n = 0; n < kFFT; ++n) {
            const double t = synth::kTwoPi * n / (kFFT - 1);
            v[n] = 0.35875 - 0.48829 * std::cos(t)
                 + 0.14128 * std::cos(2.0 * t)
                 - 0.01168 * std::cos(3.0 * t);
        }
        return v;
    }();
    return w;
}

struct SpectrumReport {
    double fundamental = 0.0;    // linear magnitude of the fundamental
    double audibleSpurRel = 0.0; // worst non-harmonic bin below 20 kHz
    double audibleSpurHz  = 0.0;
    double ultraSpurRel   = 0.0; // worst non-harmonic bin above 20 kHz
    double ultraSpurHz    = 0.0;
    double spurPowerRel = 0.0;   // total non-harmonic power / harmonic power
};

/// Everything that is not within +/-6 bins of a k*f0 line (or DC) is a spur:
/// aliasing, intermodulation, or modulation sidebands. A clean linear chain
/// fed one band-limited saw should have nothing there.
static SpectrumReport analyse(const std::vector<double> &capture, double f0) {
    std::vector<std::complex<double>> x(kFFT);
    for (int i = 0; i < kFFT; ++i) x[i] = capture[i] * window()[i];
    fft(x);

    const int half = kFFT / 2;
    std::vector<double> mag(half);
    for (int i = 0; i < half; ++i) mag[i] = std::abs(x[i]);

    const double df = kSR / kFFT;
    const int guard = 6;
    std::vector<bool> harmonic(half, false);
    for (int i = 0; i <= guard; ++i) harmonic[i] = true;   // DC leakage
    for (int k = 1; k * f0 < kSR * 0.5 - df; ++k) {
        const int bin = static_cast<int>(std::lround(k * f0 / df));
        for (int i = std::max(0, bin - guard);
             i <= std::min(half - 1, bin + guard); ++i) {
            harmonic[i] = true;
        }
    }

    SpectrumReport report;
    const int fundamentalBin = static_cast<int>(std::lround(f0 / df));
    for (int i = std::max(0, fundamentalBin - guard);
         i <= std::min(half - 1, fundamentalBin + guard); ++i) {
        report.fundamental = std::max(report.fundamental, mag[i]);
    }

    double spurPower = 0.0, harmonicPower = 0.0;
    for (int i = 0; i < half; ++i) {
        if (harmonic[i]) {
            harmonicPower += mag[i] * mag[i];
            continue;
        }
        spurPower += mag[i] * mag[i];
        const double hz = i * df;
        if (hz < 20000.0) {
            if (mag[i] > report.audibleSpurRel) {
                report.audibleSpurRel = mag[i];
                report.audibleSpurHz = hz;
            }
        } else if (mag[i] > report.ultraSpurRel) {
            report.ultraSpurRel = mag[i];
            report.ultraSpurHz = hz;
        }
    }
    report.audibleSpurRel /= std::max(report.fundamental, 1e-12);
    report.ultraSpurRel   /= std::max(report.fundamental, 1e-12);
    report.spurPowerRel = spurPower / std::max(harmonicPower, 1e-12);
    return report;
}

/// Level stability across the capture: the wobble detector. RMS of eight
/// consecutive windows, each an exact whole number of periods so a steady
/// tone measures steady even at A0 — any variation left is real amplitude
/// modulation. Pass f0 = 0 for signals with no single period (chords); the
/// windows then fall back to kFFT/8 and beating shows up as intended.
static double wobbleDb(const std::vector<double> &capture, double f0) {
    constexpr int kWindows = 8;
    int span = kFFT / kWindows;
    if (f0 > 0.0) {
        const double period = kSR / f0;
        span = static_cast<int>(std::max(1.0, std::floor(span / period)) * period);
    }
    double minRms = 1e18, maxRms = 0.0;
    for (int w = 0; w < kWindows; ++w) {
        double sum = 0.0;
        for (int i = w * span; i < (w + 1) * span && i < kFFT; ++i) {
            sum += capture[i] * capture[i];
        }
        const double rms = std::sqrt(sum / span);
        minRms = std::min(minRms, rms);
        maxRms = std::max(maxRms, rms);
    }
    return dB(maxRms / std::max(minRms, 1e-15));
}

/// Best scalar fit y ~= k*x, residual relative to the output RMS. For a
/// memoryless linear stage (mixer, volume, limiter below the knee) this is
/// the float rounding floor; anything above it is added distortion.
static double scalarFitResidualDb(const std::vector<double> &in,
                                  const std::vector<double> &out) {
    double num = 0.0, den = 0.0;
    for (int i = 0; i < kFFT; ++i) { num += in[i] * out[i]; den += in[i] * in[i]; }
    const double k = num / std::max(den, 1e-30);
    double residual = 0.0, power = 0.0;
    for (int i = 0; i < kFFT; ++i) {
        const double e = out[i] - k * in[i];
        residual += e * e;
        power += out[i] * out[i];
    }
    return dB(std::sqrt(residual / std::max(power, 1e-30)));
}

// ---- Stage-tap sweep ------------------------------------------------------

struct FilterSetting { const char *name; float cutoff, resonance, slope; };

struct WorstCase {
    double value = -1e18;
    int note = 0;
    double level = 0.0, gain = 0.0;
    double detail = 0.0;      // spur frequency, or peak level — per use
};

static void keepWorst(WorstCase &worst, double value, int note, double level,
                      double gain, double detail) {
    if (value > worst.value) worst = {value, note, level, gain, detail};
}

int main() {
    printf("R50 DSP stage audit — saw sweep, five taps, %g kHz\n\n", kSR / 1000.0);

    const std::filesystem::path outDir = "build/dsp-stage-audit";
    std::filesystem::create_directories(outDir);
    FILE *csv = fopen((outDir / "results.csv").c_str(), "w");
    if (csv) {
        fprintf(csv, "filter,note,f0,level,gain,tap,peak,fund_db,"
                     "audible_spur_db,audible_spur_hz,ultra_spur_db,"
                     "ultra_spur_hz,spur_power_db,wobble_db\n");
    }

    const FilterSetting filters[] = {
        {"open",    18000.0f, 0.05f, 1.0f},
        {"default",  3200.0f, 0.15f, 1.0f},
    };
    const double levels[] = {0.25, 0.625, 1.0, 2.0};   // 2.0 = engine's level ceiling
    const double gains[]  = {0.74, 1.0};               // default and full master

    // Worst observations per tap, gathered across the whole sweep.
    WorstCase worstAudible[kTaps], worstUltra[kTaps];
    WorstCase worstSpurPower[kTaps], worstWobble[kTaps];
    WorstCase worstMixResidual, worstVolResidual, worstLimResidualBelowKnee;
    int limiterTransparentViolations = 0;
    int limiterEngagedCases = 0;

    for (const FilterSetting &fs : filters) {
        for (int note = 21; note <= 108; note += 3) {
            const double f0 = midiHz(note);
            for (double level : levels) {
                for (double gain : gains) {
                    r50::WaveOscillator osc;
                    r50::DigitalLowPassFilter filter;
                    osc.setSampleRate(kSR);
                    filter.setSampleRate(kSR);
                    osc.setWave(0);                        // saw
                    osc.reset(0.0f);
                    osc.setFrequency(f0);
                    filter.setParams(fs.cutoff, fs.resonance, fs.slope);

                    std::vector<std::vector<double>> tap(
                        kTaps, std::vector<double>(kFFT));

                    // The engine's per-sample order: oscillator -> filter ->
                    // mixer (level, equal-power centre pan, 0.9 headroom) ->
                    // effects rack (Off = pass-through, verified in the chord
                    // phase) -> master gain -> output limiter.
                    for (int i = -kSettle; i < kFFT; ++i) {
                        const double oscOut  = osc.process();
                        const double filtOut = filter.process(
                            static_cast<float>(oscOut));
                        const double mixOut  = filtOut * level
                                             * 0.70710678 * 0.9;
                        const double volOut  = mixOut * gain;
                        const double limOut  = r50::outputLimit(
                            static_cast<float>(volOut));
                        if (i < 0) continue;
                        tap[0][i] = oscOut;
                        tap[1][i] = filtOut;
                        tap[2][i] = mixOut;
                        // Stored at float precision — outputLimit() receives a
                        // float, so the transparency check below compares what
                        // the limiter actually saw.
                        tap[3][i] = static_cast<float>(volOut);
                        tap[4][i] = limOut;
                    }

                    double volPeak = 0.0;
                    for (double v : tap[3]) volPeak = std::max(volPeak, std::fabs(v));
                    const bool aboveKnee = volPeak > r50::kLimiterCeiling;
                    if (aboveKnee) ++limiterEngagedCases;

                    for (int t = 0; t < kTaps; ++t) {
                        const SpectrumReport r = analyse(tap[t], f0);
                        const double wobble = wobbleDb(tap[t], f0);
                        // The limiter above its knee adds harmonics by design;
                        // its spur (aliasing) is still tracked, its residual
                        // vs input is only checked below the knee. Skip its
                        // spur bookkeeping when engaged so the gate reflects
                        // normal operation; the engaged case is reported
                        // separately below.
                        if (t < 4 || !aboveKnee) {
                            keepWorst(worstAudible[t], r.audibleSpurRel, note,
                                      level, gain, r.audibleSpurHz);
                            keepWorst(worstUltra[t], r.ultraSpurRel, note,
                                      level, gain, r.ultraSpurHz);
                            keepWorst(worstSpurPower[t], r.spurPowerRel, note,
                                      level, gain, 0.0);
                        }
                        keepWorst(worstWobble[t], wobble, note, level, gain, 0.0);
                        if (csv) {
                            double peak = 0.0;
                            for (double v : tap[t])
                                peak = std::max(peak, std::fabs(v));
                            fprintf(csv,
                                "%s,%d,%.2f,%.3f,%.2f,%s,%.6f,%.1f,"
                                "%.1f,%.0f,%.1f,%.0f,%.1f,%.3f\n",
                                fs.name, note, f0, level, gain, kTapNames[t],
                                peak, dB(r.fundamental),
                                dB(r.audibleSpurRel), r.audibleSpurHz,
                                dB(r.ultraSpurRel), r.ultraSpurHz,
                                10.0 * std::log10(std::max(r.spurPowerRel, 1e-24)),
                                wobble);
                        }
                    }

                    keepWorst(worstMixResidual,
                              scalarFitResidualDb(tap[1], tap[2]),
                              note, level, gain, 0.0);
                    keepWorst(worstVolResidual,
                              scalarFitResidualDb(tap[2], tap[3]),
                              note, level, gain, 0.0);
                    if (!aboveKnee) {
                        // Below the knee the limiter must be bit-transparent.
                        double maxDelta = 0.0;
                        for (int i = 0; i < kFFT; ++i) {
                            maxDelta = std::max(maxDelta,
                                std::fabs(tap[4][i] - tap[3][i]));
                        }
                        if (maxDelta != 0.0) ++limiterTransparentViolations;
                        keepWorst(worstLimResidualBelowKnee, maxDelta,
                                  note, level, gain, volPeak);
                    }
                }
            }
        }
    }
    if (csv) fclose(csv);

    printf("Worst audible-band spur per tap (< 20 kHz, relative to the fundamental):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstAudible[t];
        printf("  %-13s %7.1f dB   note %3d, level %.3f, gain %.2f, spur at %.0f Hz\n",
               kTapNames[t], dB(w.value), w.note, w.level, w.gain, w.detail);
    }
    printf("\nWorst ultrasonic spur per tap (>= 20 kHz, inaudible, for reference):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstUltra[t];
        printf("  %-13s %7.1f dB   note %3d, level %.3f, gain %.2f, spur at %.0f Hz\n",
               kTapNames[t], dB(w.value), w.note, w.level, w.gain, w.detail);
    }
    printf("\nWorst non-harmonic power (relative to harmonic power):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstSpurPower[t];
        printf("  %-13s %7.1f dB   note %3d, level %.3f, gain %.2f\n",
               kTapNames[t], 10.0 * std::log10(std::max(w.value, 1e-24)),
               w.note, w.level, w.gain);
    }
    printf("\nLevel stability across the capture (wobble, max window-to-window):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstWobble[t];
        printf("  %-13s %7.3f dB  note %3d, level %.3f, gain %.2f\n",
               kTapNames[t], w.value, w.note, w.level, w.gain);
    }
    printf("\nStage linearity (scalar-fit residual, relative to output):\n");
    printf("  filter->mixer  %7.1f dB   note %3d, level %.3f\n",
           worstMixResidual.value, worstMixResidual.note, worstMixResidual.level);
    printf("  mixer->volume  %7.1f dB   note %3d, level %.3f\n",
           worstVolResidual.value, worstVolResidual.note, worstVolResidual.level);
    printf("  limiter below knee: max |out-in| = %.3g (peak %.3f), "
           "%d cases engaged the limiter\n\n",
           worstLimResidualBelowKnee.value, worstLimResidualBelowKnee.detail,
           limiterEngagedCases);

    // 0.5% of the fundamental is the repo's audible-aliasing gate
    // (Tests/test_r50.cpp). Only spurs below 20 kHz count against it — the
    // pyramid deliberately band-limits to 20 kHz and lets fold-back above
    // that pass as inaudible.
    const double spurLimit = 0.005;
    for (int t = 0; t < kTaps; ++t) {
        check(worstAudible[t].value < spurLimit,
              "%s: worst audible spur %.4f%% of fundamental (< 0.5%%)",
              kTapNames[t], worstAudible[t].value * 100.0);
    }
    check(worstMixResidual.value < -120.0,
          "mixer stage is linear (residual %.1f dB)", worstMixResidual.value);
    check(worstVolResidual.value < -120.0,
          "volume stage is linear (residual %.1f dB)", worstVolResidual.value);
    check(limiterTransparentViolations == 0,
          "limiter is bit-transparent below the %.2f ceiling (%d violations)",
          r50::kLimiterCeiling, limiterTransparentViolations);
    for (int t = 0; t < kTaps; ++t) {
        check(worstWobble[t].value < 0.2,
              "%s level is steady (%.3f dB wobble)", kTapNames[t],
              worstWobble[t].value);
    }

    // ---- Full-engine chord pass -------------------------------------------
    //
    // The tap harness cannot see the effects rack or voice mixing bugs, so
    // render real chords through R50Engine twice: once at master gain g and
    // once at g/10. If everything from the voices to the output is linear,
    // scaling the quiet render by 10 reproduces the loud one exactly; the
    // difference IS the chain's nonlinearity (normally: only the limiter).
    printf("\nFull-engine chord linearity (default saw patch):\n");
    struct ChordCase { const char *name; int notes[4]; uint8_t velocity; };
    const ChordCase chords[] = {
        {"low  C2maj", {36, 40, 43, 48}, 110},
        {"mid  C4maj", {60, 64, 67, 72}, 110},
        {"high C6maj", {84, 88, 91, 96}, 110},
        {"mid  C4maj ff", {60, 64, 67, 72}, 127},
    };

    const auto renderChord = [](const ChordCase &chord, float gain,
                                float &headroom) {
        r50::R50Engine engine;
        engine.setParameter(R50ParamMasterGain, gain);
        engine.setSampleRate(kSR);   // snaps the gain smoother to the store
        for (int note : chord.notes) {
            engine.noteOn(static_cast<uint8_t>(note), chord.velocity);
        }
        const int total = kSettle + kFFT;
        std::vector<float> left(total), right(total);
        for (int offset = 0; offset < total; offset += 137) {
            const int frames = std::min(137, total - offset);
            engine.render(left.data() + offset, right.data() + offset, frames);
        }
        headroom = engine.readHeadroomPeak();
        std::vector<double> capture(kFFT);
        for (int i = 0; i < kFFT; ++i) capture[i] = left[kSettle + i];
        return capture;
    };

    // Intermodulation: everything off the union of the four notes' harmonic
    // grids. The gain rider moving with the chord's beat only puts sidebands
    // within a few Hz of each partial — inside the guard band — so this
    // measures genuine distortion products even while the rider works.
    const auto chordImdDb = [](const std::vector<double> &capture,
                               const int (&notes)[4]) {
        std::vector<std::complex<double>> x(kFFT);
        for (int i = 0; i < kFFT; ++i) x[i] = capture[i] * window()[i];
        fft(x);
        const int half = kFFT / 2;
        const double df = kSR / kFFT;
        const int guard = 6;
        std::vector<bool> onGrid(half, false);
        for (int i = 0; i <= guard; ++i) onGrid[i] = true;
        for (int note : notes) {
            const double f0 = midiHz(note);
            for (int k = 1; k * f0 < kSR * 0.5 - df; ++k) {
                const int bin = static_cast<int>(std::lround(k * f0 / df));
                for (int i = std::max(0, bin - guard);
                     i <= std::min(half - 1, bin + guard); ++i) {
                    onGrid[i] = true;
                }
            }
        }
        double reference = 0.0, worstOff = 0.0;
        for (int i = 0; i < half; ++i) {
            const double magnitude = std::abs(x[i]);
            if (onGrid[i]) reference = std::max(reference, magnitude);
            else worstOff = std::max(worstOff, magnitude);
        }
        return dB(worstOff / std::max(reference, 1e-12));
    };

    for (const ChordCase &chord : chords) {
        for (float gain : {0.74f, 1.0f}) {
            float headroomLoud = 0.0f, headroomQuiet = 0.0f;
            const std::vector<double> loud =
                renderChord(chord, gain, headroomLoud);
            const std::vector<double> quiet =
                renderChord(chord, gain * 0.1f, headroomQuiet);

            double residual = 0.0, power = 0.0, peak = 0.0;
            for (int i = 0; i < kFFT; ++i) {
                const double e = loud[i] - 10.0 * quiet[i];
                residual += e * e;
                power += loud[i] * loud[i];
                peak = std::max(peak, std::fabs(loud[i]));
            }
            const double residualDb = dB(std::sqrt(residual / std::max(power, 1e-30)));
            const double imdDb = chordImdDb(loud, chord.notes);
            const double wobble = wobbleDb(loud, 0.0);
            const bool engaged = headroomLoud > r50::kLimiterCeiling;
            printf("  %-14s gain %.2f: peak %.3f, pre-limiter %.3f%s, "
                   "gain-linearity %7.1f dB, IMD %6.1f dB, wobble %.3f dB\n",
                   chord.name, gain, peak, headroomLoud,
                   engaged ? " (rider engaged)" : "",
                   residualDb, imdDb, wobble);
            // With the output stage untouched the whole engine must be linear
            // to the float floor. When the rider is working, the loud render
            // is deliberately gain-reduced, so distortion is gated through
            // the IMD measurement instead: off-grid products must stay below
            // the audible-alias budget relative to the strongest partial.
            if (!engaged) {
                check(residualDb < -80.0,
                      "%s at gain %.2f: engine chain is linear (%.1f dB)",
                      chord.name, gain, residualDb);
            }
            check(imdDb < -46.0,
                  "%s at gain %.2f: chord intermodulation %.1f dB (< -46 dB)",
                  chord.name, gain, imdDb);
            // Chord wobble is reported but not gated: equal-tempered thirds
            // and fifths beat against each other by construction, and that
            // beating is what the RMS variation measures on a chord.
        }
    }

    // A single steady note through the whole engine, measured with
    // period-aligned windows: any wobble here would be a real engine defect
    // (the chord numbers above include musical beating; this cannot).
    printf("\nFull-engine single-note steadiness:\n");
    for (int note : {36, 60, 84}) {
        r50::R50Engine engine;
        engine.setParameter(R50ParamMasterGain, 0.74f);
        engine.setSampleRate(kSR);
        engine.noteOn(static_cast<uint8_t>(note), 110);
        const int total = kSettle + kFFT;
        std::vector<float> left(total), right(total);
        for (int offset = 0; offset < total; offset += 137) {
            const int frames = std::min(137, total - offset);
            engine.render(left.data() + offset, right.data() + offset, frames);
        }
        std::vector<double> capture(kFFT);
        for (int i = 0; i < kFFT; ++i) capture[i] = left[kSettle + i];
        const double wobble = wobbleDb(capture, midiHz(note));
        check(wobble < 0.2,
              "note %d: single note through the engine is steady (%.3f dB)",
              note, wobble);
    }

    printf("\n%s (%d failure%s). Full data: %s\n",
           g_failures == 0 ? "ALL CLEAR" : "PROBLEMS FOUND",
           g_failures, g_failures == 1 ? "" : "s",
           (outDir / "results.csv").c_str());
    return g_failures == 0 ? 0 : 1;
}
