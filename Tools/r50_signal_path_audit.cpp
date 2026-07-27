// R50 basic signal-path audit: analytic oscillator -> filter -> amp envelope.
// No PCM source, noise, waveshaping, modulation, or effects are involved.

#include "R50Engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static constexpr double kSR = 48000.0;
static constexpr int kNoteFrames = 57600;    // 1.2 s
static constexpr int kTailFrames = 57600;    // 1.2 s
static constexpr int kFFT = 4096;

struct Scenario {
    const char *name;
    float cutoff, resonance, slope, envAmount;
    float filterAttack, filterDecay, filterSustain;
    float ampAttack, ampRelease;
};

static const Scenario kScenarios[] = {
    {"Open",     18000, 0.05f, 0, 0.00f, 0.004f, 0.30f, 1.00f, 0.005f, 0.20f},
    {"Warm12",    2500, 0.20f, 0, 0.00f, 0.004f, 0.30f, 1.00f, 0.005f, 0.25f},
    {"Deep24",    1200, 0.35f, 1, 0.00f, 0.004f, 0.30f, 1.00f, 0.008f, 0.30f},
    {"EnvSweep",   450, 0.28f, 0, 0.72f, 0.16f, 0.65f, 0.22f, 0.08f, 0.55f}
};

static const char *kWaveNames[] = {"Saw", "Triangle", "Square", "Pulse10"};

static double midiHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

static double rmsRange(const std::vector<float> &x, int from, int to) {
    from = std::max(from, 0); to = std::min(to, static_cast<int>(x.size()));
    double sum = 0.0;
    for (int i = from; i < to; ++i) sum += x[i] * x[i];
    return std::sqrt(sum / std::max(1, to - from));
}

static double magAt(const std::vector<float> &x, double hz, int from) {
    const double w = synth::kTwoPi * hz / kSR;
    const double cw = std::cos(w), sw = std::sin(w);
    double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0;
    for (int i = 0; i < kFFT; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            synth::kTwoPi * i / (kFFT - 1));
        const double value = x[from + i] * window;
        re += value * pr; im -= value * pi;
        const double nr = pr * cw - pi * sw;
        pi = pr * sw + pi * cw; pr = nr;
    }
    return std::sqrt(re * re + im * im);
}

static void writeWav(const std::string &path, const std::vector<float> &x) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t dataBytes = static_cast<uint32_t>(x.size() * 2);
    const uint32_t riffBytes = 36 + dataBytes, rate = static_cast<uint32_t>(kSR);
    const uint32_t byteRate = rate * 2, fmtSize = 16;
    const uint16_t format = 1, channels = 1, blockAlign = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riffBytes, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmtSize, 4, 1, f);
    std::fwrite(&format, 2, 1, f); std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&blockAlign, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
    for (float sample : x) {
        const int16_t q = static_cast<int16_t>(std::lround(
            std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&q, 2, 1, f);
    }
    std::fclose(f);
}

static std::vector<float> render(int wave, int note, const Scenario &s) {
    r50::R50Engine engine;
    engine.setSampleRate(kSR);
    engine.setParameter(R50ParamMasterGain, 0.70f);
    engine.setParameter(R50ParamOscWave, static_cast<float>(wave));
    engine.setParameter(R50ParamSourceType, 0);
    engine.setParameter(R50ParamNoiseMix, 0);
    engine.setParameter(R50ParamCutoff, s.cutoff);
    engine.setParameter(R50ParamResonance, s.resonance);
    engine.setParameter(R50ParamSlope, s.slope);
    engine.setParameter(R50ParamKeyTrack, 0);
    engine.setParameter(R50ParamFilterEnvAmount, s.envAmount);
    engine.setParameter(R50ParamFilterAttack, s.filterAttack);
    engine.setParameter(R50ParamFilterDecay, s.filterDecay);
    engine.setParameter(R50ParamFilterSustain, s.filterSustain);
    engine.setParameter(R50ParamAmpAttack, s.ampAttack);
    engine.setParameter(R50ParamAmpDecay, 0.30f);
    engine.setParameter(R50ParamAmpSustain, 0.82f);
    engine.setParameter(R50ParamAmpRelease, s.ampRelease);
    engine.setParameter(R50ParamP1DryLevel, 1);
    engine.setParameter(R50ParamP1Send1, 0);
    engine.setParameter(R50ParamP1Send2, 0);
    engine.setParameter(R50ParamP1Send3, 0);

    std::vector<float> left(kNoteFrames + kTailFrames), right(left.size());
    engine.noteOn(static_cast<uint8_t>(note), 105);
    for (int offset = 0; offset < kNoteFrames; offset += 137) {
        const int frames = std::min(137, kNoteFrames - offset);
        engine.render(left.data() + offset, right.data() + offset, frames);
    }
    engine.noteOff(static_cast<uint8_t>(note));
    for (int offset = kNoteFrames; offset < static_cast<int>(left.size());
         offset += 137) {
        const int frames = std::min(137, static_cast<int>(left.size()) - offset);
        engine.render(left.data() + offset, right.data() + offset, frames);
    }
    return left;
}

int main(int argc, char **argv) {
    const std::string output = argc > 1 ? argv[1]
                                        : "build/signal-path-audit";
    std::filesystem::create_directories(output);
    FILE *csv = std::fopen((output + "/report.csv").c_str(), "w");
    std::fprintf(csv, "wave,note,scenario,peak,rms,dc,max_step,"
                      "attack10ms_rms,sustain_rms,noteoff_step,tail_rms,"
                      "harmonic_hf_ratio,offgrid_ratio,status\n");

    const int notes[] = {36, 60, 84};
    int failures = 0;
    for (int wave = 0; wave < 4; ++wave) {
        for (int note : notes) {
            const double f0 = midiHz(note);
            for (const auto &scenario : kScenarios) {
                const auto audio = render(wave, note, scenario);
                double peak = 0, squares = 0, sum = 0, maxStep = 0;
                for (size_t i = 0; i < audio.size(); ++i) {
                    peak = std::max(peak, std::fabs(static_cast<double>(audio[i])));
                    squares += audio[i] * audio[i]; sum += audio[i];
                    if (i) maxStep = std::max(maxStep, std::fabs(
                        static_cast<double>(audio[i] - audio[i - 1])));
                }
                const double totalRms = std::sqrt(squares / audio.size());
                const double dc = std::fabs(sum / audio.size());
                const double attackRms = rmsRange(
                    audio, 0, static_cast<int>(0.010 * kSR));
                const double sustainRms = rmsRange(
                    audio, static_cast<int>(0.82 * kSR),
                    static_cast<int>(1.02 * kSR));
                const double noteOffStep = std::fabs(
                    audio[kNoteFrames] - audio[kNoteFrames - 1]);
                const double tailRms = rmsRange(
                    audio, audio.size() - static_cast<int>(0.10 * kSR),
                    audio.size());

                const int from = static_cast<int>(0.82 * kSR);
                const double fundamental = magAt(audio, f0, from);
                double harmonicTotal = 0, harmonicHigh = 0;
                for (int h = 1; h * f0 < 20000; ++h) {
                    const double m = magAt(audio, h * f0, from);
                    harmonicTotal += m;
                    if (h * f0 > 5000) harmonicHigh += m;
                }
                double offgrid = 0;
                if (note == 84) {
                    for (double hz = 80; hz < 20000; hz += 80) {
                        const double nearest = std::round(hz / f0) * f0;
                        if (std::fabs(hz - nearest) < 0.28 * f0) continue;
                        offgrid = std::max(offgrid, magAt(audio, hz, from));
                    }
                }
                const double hfRatio = harmonicHigh / std::max(harmonicTotal, 1e-12);
                const double aliasRatio = offgrid / std::max(fundamental, 1e-12);
                std::string status = "PASS";
                if (!std::isfinite(peak) || peak > 1.0) status = "level";
                else if (dc > 0.01) status = "dc";
                else if (std::string(scenario.name) == "EnvSweep"
                         && attackRms > sustainRms * 0.45)
                    status = "attack";
                else if (noteOffStep > std::max(0.15, totalRms * 5)) status = "noteoff_click";
                else if (tailRms > 0.002) status = "tail";
                else if (note == 84 && aliasRatio > 0.01) status = "offgrid";
                if (status != "PASS") ++failures;

                const std::string stem = std::string(kWaveNames[wave]) + "_C"
                    + std::to_string(note / 12 - 1) + "_" + scenario.name;
                writeWav(output + "/" + stem + ".wav", audio);
                std::fprintf(csv, "%s,%d,%s,%.6f,%.6f,%.7f,%.6f,"
                                  "%.7f,%.7f,%.6f,%.7f,%.6f,%.6f,%s\n",
                             kWaveNames[wave], note, scenario.name, peak, totalRms,
                             dc, maxStep, attackRms, sustainRms,
                             noteOffStep, tailRms, hfRatio,
                             aliasRatio, status.c_str());
                std::printf("%-8s C%d %-8s peak %.3f tail %.6f alias %.4f %s\n",
                            kWaveNames[wave], note / 12 - 1, scenario.name,
                            peak, tailRms, aliasRatio, status.c_str());
            }
        }
    }
    std::fclose(csv);
    std::printf("\n48 WAVs and report written to %s; %d flagged\n",
                output.c_str(), failures);
    return failures == 0 ? 0 : 1;
}
