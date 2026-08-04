// Hybrid8 DSP stage audit: tap the signal chain at five points — oscillator,
// post-filter, post-mixer, post-softclip, post-volume — sweep the saw from A0
// to C8 at several levels, and measure the spectrum at every tap.
//
// The stages are the engine's own components (PolyBLEP Oscillator at 2x with
// the half-band Decimator2x, LadderFilter, softClip) wired in the engine's
// per-sample order with envelopes held steady, so a spectral measurement sees
// the DSP alone. Hybrid8's output stage differs from R50's: softClip acts on
// the voice-sum bus BEFORE the master gain, so the master volume does not
// change how hard the bus clips.
//
// A second pass renders chords through the full SynthEngine and measures
// intermodulation (energy off the union of the notes' harmonic grids), with
// the Analog control both at 0 and at its 0.3 default — Analog blends real
// per-voice pitch drift and filter saturation in, and the audit separates
// that intended colour from unintended distortion.
//
// Build & run: ./scripts/audit-hybrid8-dsp-stages.sh

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdarg>
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
    "oscillator", "post-filter", "post-mixer", "post-volume", "post-softclip"
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
/// aliasing, intermodulation, or modulation sidebands. A clean chain fed one
/// band-limited saw should have nothing there — harmonic distortion from the
/// nonlinear stages lands ON the grid and is measured by the per-stage
/// residuals instead.
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

/// Level stability across the capture: RMS of eight consecutive windows, each
/// an exact whole number of periods so a steady tone measures steady even at
/// A0. Pass f0 = 0 for signals with no single period (chords).
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
/// memoryless linear stage this is the float rounding floor; for softClip it
/// IS the total harmonic distortion at that drive level.
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

struct WorstCase {
    double value = -1e18;
    int note = 0;
    double level = 0.0, gain = 0.0;
    double detail = 0.0;      // spur frequency, or drive level — per use
};

static void keepWorst(WorstCase &worst, double value, int note, double level,
                      double gain, double detail) {
    if (value > worst.value) worst = {value, note, level, gain, detail};
}

int main() {
    printf("Hybrid8 DSP stage audit — saw sweep, five taps, %g kHz\n\n",
           kSR / 1000.0);

    const std::filesystem::path outDir = "build/hybrid8-stage-audit";
    std::filesystem::create_directories(outDir);
    FILE *csv = fopen((outDir / "results.csv").c_str(), "w");
    if (csv) {
        fprintf(csv, "analog,note,f0,level,gain,tap,peak,fund_db,"
                     "audible_spur_db,audible_spur_hz,ultra_spur_db,"
                     "ultra_spur_hz,spur_power_db,wobble_db\n");
    }

    // Analog = 0 is the engine's "perfectly clean" claim; 0.3 is the default
    // patch. The harness leaves pitch drift out (it is applied by the Voice,
    // not these components) so the analog axis here isolates the filter's
    // saturation colour.
    const float analogSettings[] = {0.0f, 0.3f};
    const double levels[] = {0.25, 0.625, 1.0};   // osc1 mix level
    const double gains[]  = {0.7, 1.0};           // default and full master

    WorstCase worstAudible[kTaps], worstUltra[kTaps];
    WorstCase worstSpurPower[kTaps], worstWobble[kTaps];
    WorstCase worstMixResidual, worstVolResidual;
    WorstCase worstClipThd;       // softClip stage: residual IS the THD

    for (float analog : analogSettings) {
        for (int note = 21; note <= 108; note += 3) {
            const double f0 = midiHz(note);
            for (double level : levels) {
                for (double gain : gains) {
                    synth::Oscillator osc;
                    synth::LadderFilter filter;
                    synth::Decimator2x decimA, oscDecimA;
                    synth::Decimator2xSharp decimB, oscDecimB;
                    const double overRate = kSR * synth::Voice::kOversample;
                    osc.setSampleRate(overRate);
                    filter.setSampleRate(overRate);
                    decimA.setup(kSR * 2.0);    decimB.setup(kSR);
                    oscDecimA.setup(kSR * 2.0); oscDecimB.setup(kSR);
                    osc.setWave(synth::OscWave::Saw);
                    osc.reset(0.0f);
                    osc.setFrequency(f0);
                    // Engine defaults: cutoff 6 kHz, res 0.15, 12 dB slope,
                    // no drive, LP mode.
                    filter.setParams(6000.0, 0.15f, 0.0f, analog, 0.0f, 0.0f);

                    std::vector<std::vector<double>> tap(
                        kTaps, std::vector<double>(kFFT));

                    // The engine's per-sample order: 4x subsamples of
                    // oscillator -> mix level -> filter, decimated back to
                    // host rate through the two half-band stages; then the
                    // voice-sum bus (centre voice: pan cos(45)*sqrt(2) = 1)
                    // scaled by kVoiceSumGain, effects (defaults are
                    // pass-through, verified in the chord phase), the master
                    // gain, and finally softClip.
                    for (int i = -kSettle; i < kFFT; ++i) {
                        double filtOut = 0.0, oscOut = 0.0;
                        for (int os = 0; os < synth::Voice::kOversample; ++os) {
                            const float o1 = osc.process();
                            const float m = static_cast<float>(o1 * level);
                            const float oscHalf = oscDecimA.process(m);
                            const float filtHalf =
                                decimA.process(filter.process(m));
                            if (os & 1) {
                                oscOut  = oscDecimB.process(oscHalf);
                                filtOut = decimB.process(filtHalf);
                            }
                        }
                        const double mixOut = filtOut * synth::kVoiceSumGain;
                        // Stored at float precision — softClip receives a
                        // float, so the stage residual compares what it saw.
                        const float clipIn   = static_cast<float>(mixOut * gain);
                        const double clipOut = synth::softClip(clipIn);
                        if (i < 0) continue;
                        tap[0][i] = oscOut;
                        tap[1][i] = filtOut;
                        tap[2][i] = mixOut;
                        tap[3][i] = clipIn;
                        tap[4][i] = clipOut;
                    }

                    for (int t = 0; t < kTaps; ++t) {
                        const SpectrumReport r = analyse(tap[t], f0);
                        const double wobble = wobbleDb(tap[t], f0);
                        keepWorst(worstAudible[t], r.audibleSpurRel, note,
                                  level, gain, r.audibleSpurHz);
                        keepWorst(worstUltra[t], r.ultraSpurRel, note, level,
                                  gain, r.ultraSpurHz);
                        keepWorst(worstSpurPower[t], r.spurPowerRel, note,
                                  level, gain, 0.0);
                        keepWorst(worstWobble[t], wobble, note, level, gain, 0.0);
                        if (csv) {
                            double peak = 0.0;
                            for (double v : tap[t])
                                peak = std::max(peak, std::fabs(v));
                            fprintf(csv,
                                "%.1f,%d,%.2f,%.3f,%.2f,%s,%.6f,%.1f,"
                                "%.1f,%.0f,%.1f,%.0f,%.1f,%.3f\n",
                                analog, note, f0, level, gain, kTapNames[t],
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
                    double clipPeak = 0.0;
                    for (double v : tap[3])
                        clipPeak = std::max(clipPeak, std::fabs(v));
                    keepWorst(worstClipThd,
                              scalarFitResidualDb(tap[3], tap[4]),
                              note, level, gain, clipPeak);
                }
            }
        }
    }
    if (csv) fclose(csv);

    printf("Worst audible-band spur per tap (< 20 kHz, relative to the fundamental):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstAudible[t];
        printf("  %-14s %7.1f dB   note %3d, level %.3f, gain %.2f, spur at %.0f Hz\n",
               kTapNames[t], dB(w.value), w.note, w.level, w.gain, w.detail);
    }
    printf("\nWorst ultrasonic spur per tap (>= 20 kHz, inaudible, for reference):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstUltra[t];
        printf("  %-14s %7.1f dB   note %3d, level %.3f, gain %.2f, spur at %.0f Hz\n",
               kTapNames[t], dB(w.value), w.note, w.level, w.gain, w.detail);
    }
    printf("\nWorst non-harmonic power (relative to harmonic power):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstSpurPower[t];
        printf("  %-14s %7.1f dB   note %3d, level %.3f, gain %.2f\n",
               kTapNames[t], 10.0 * std::log10(std::max(w.value, 1e-24)),
               w.note, w.level, w.gain);
    }
    printf("\nLevel stability across the capture (wobble, max window-to-window):\n");
    for (int t = 0; t < kTaps; ++t) {
        const WorstCase &w = worstWobble[t];
        printf("  %-14s %7.3f dB  note %3d, level %.3f, gain %.2f\n",
               kTapNames[t], w.value, w.note, w.level, w.gain);
    }
    printf("\nStage linearity (scalar-fit residual, relative to output):\n");
    printf("  filter->mixer   %7.1f dB   note %3d, level %.3f\n",
           worstMixResidual.value, worstMixResidual.note, worstMixResidual.level);
    printf("  mixer->volume   %7.1f dB   note %3d, level %.3f\n",
           worstVolResidual.value, worstVolResidual.note, worstVolResidual.level);
    printf("  softClip stage  %7.1f dB   note %3d, level %.3f "
           "(THD at clip input peak %.3f — one voice)\n\n",
           worstClipThd.value, worstClipThd.note, worstClipThd.level,
           worstClipThd.detail);

    // 0.5% of the fundamental — the same audible-alias budget the R50 audit
    // and its tests use, for comparability across the two engines.
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
    for (int t = 0; t < kTaps; ++t) {
        check(worstWobble[t].value < 0.2,
              "%s level is steady (%.3f dB wobble)", kTapNames[t],
              worstWobble[t].value);
    }

    // ---- Full-engine chord pass -------------------------------------------
    //
    // Chords through the real engine, measured as intermodulation: energy off
    // the union of the notes' harmonic grids. Rendered with Analog at 0
    // (deterministic pitch — a clean grid, so off-grid energy is genuine
    // distortion) and at the 0.3 default (drift widens every partial into a
    // skirt; the difference is the intended analogue movement).
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

    printf("\nFull-engine chord intermodulation (default saw patch):\n");
    struct ChordCase { const char *name; int notes[4]; int velocity; };
    const ChordCase chords[] = {
        {"low  C2maj", {36, 40, 43, 48}, 110},
        {"mid  C4maj", {60, 64, 67, 72}, 110},
        {"high C6maj", {84, 88, 91, 96}, 110},
        {"mid  C4maj ff", {60, 64, 67, 72}, 127},
    };

    const auto renderChord = [](const ChordCase &chord, float analog,
                                float gain, double &busPeak) {
        synth::SynthEngine engine;
        engine.setParameter(SynthParamAnalogAmount, analog);
        engine.setParameter(SynthParamMasterGain, gain);
        // Deterministic phases so Analog=0 renders are bit-repeatable.
        engine.setParameter(SynthParamOscPhaseSpread, 0.0f);
        engine.setSampleRate(kSR);
        for (int note : chord.notes) engine.noteOn(note, chord.velocity);
        const int total = kSettle + kFFT;
        std::vector<float> left(total), right(total);
        for (int offset = 0; offset < total; offset += 137) {
            const int frames = std::min(137, total - offset);
            engine.render(left.data() + offset, right.data() + offset, frames);
        }
        // Reconstruct the clip drive: softClip is invertible on [-1, 1), so
        // the peak entering it (bus * gain) can be recovered from the output
        // peak, then divided by the master gain to give the bus level.
        busPeak = 0.0;
        double outPeak = 0.0;
        for (int i = kSettle; i < total; ++i) {
            outPeak = std::max(outPeak, std::fabs(static_cast<double>(left[i])));
        }
        // Invert y = x(27+x^2)/(27+9x^2) numerically (monotonic).
        double lo = 0.0, hi = 6.0;
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (lo + hi);
            (synth::softClip(static_cast<float>(mid)) < outPeak ? lo : hi) = mid;
        }
        busPeak = 0.5 * (lo + hi) / std::max(gain, 1e-6f);
        std::vector<double> capture(kFFT);
        for (int i = 0; i < kFFT; ++i) capture[i] = left[kSettle + i];
        return capture;
    };

    for (const ChordCase &chord : chords) {
        for (float gain : {0.7f, 1.0f}) {
            double busPeakClean = 0.0, busPeakAnalog = 0.0;
            const std::vector<double> clean =
                renderChord(chord, 0.0f, gain, busPeakClean);
            const std::vector<double> analog =
                renderChord(chord, 0.3f, gain, busPeakAnalog);
            const double imdClean  = chordImdDb(clean, chord.notes);
            const double imdAnalog = chordImdDb(analog, chord.notes);
            printf("  %-14s gain %.2f: bus %.3f, into softClip %.3f, "
                   "IMD analog=0 %6.1f dB, analog=0.3 %6.1f dB, wobble %.3f dB\n",
                   chord.name, gain, busPeakClean, busPeakClean * gain,
                   imdClean, imdAnalog, wobbleDb(clean, 0.0));
            // Only the Analog=0 render is gated: with drift disabled the
            // partial grid is exact, so off-grid energy is real distortion —
            // from the bus softClip and the filter nonlinearities.
            check(imdClean < -46.0,
                  "%s at gain %.2f: chord IMD with Analog off %.1f dB (< -46 dB)",
                  chord.name, gain, imdClean);
        }
    }

    printf("\n%s (%d failure%s). Full data: %s\n",
           g_failures == 0 ? "ALL CLEAR" : "PROBLEMS FOUND",
           g_failures, g_failures == 1 ? "" : "s",
           (outDir / "results.csv").c_str());
    return g_failures == 0 ? 0 : 1;
}
