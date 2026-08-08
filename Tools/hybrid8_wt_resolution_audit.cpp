//
// hybrid8_wt_resolution_audit.cpp
// Offline null-test for Hybrid8's wavetable resolution modes. Renders matched
// signals through both the raw WavetableOscillator and the complete SynthEngine,
// writes 24-bit WAVs, and reports residual distortion relative to Clean.
//
// Build:
//   clang++ -std=c++17 -O2 Tools/hybrid8_wt_resolution_audit.cpp \
//     -o /tmp/hybrid8_wt_resolution_audit
// Run:
//   /tmp/hybrid8_wt_resolution_audit [/tmp/hybrid8-wt-audit]
//

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace synth;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kMidiNote = 57; // A3 = 220 Hz
constexpr double kFundamental = 220.0;
constexpr int kRenderFrames = static_cast<int>(kSampleRate * 2.0);
constexpr int kAnalysisStart = static_cast<int>(kSampleRate * 0.25);
constexpr int kAnalysisFrames = static_cast<int>(kSampleRate * 1.0);

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

static void write16(FILE* file, uint16_t value) {
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)
    };
    std::fwrite(bytes, 1, sizeof(bytes), file);
}

static void write32(FILE* file, uint32_t value) {
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)
    };
    std::fwrite(bytes, 1, sizeof(bytes), file);
}

static bool writeWav24(const std::string& path, const Render& render) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const uint32_t frames = static_cast<uint32_t>(render.left.size());
    const uint32_t dataBytes = frames * 2u * 3u;
    std::fwrite("RIFF", 1, 4, file); write32(file, 36u + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, file); write32(file, 16);
    write16(file, 1); write16(file, 2); write32(file, 48000);
    write32(file, 48000 * 2 * 3); write16(file, 2 * 3); write16(file, 24);
    std::fwrite("data", 1, 4, file); write32(file, dataBytes);
    auto sample = [&](float value) {
        const double clipped = std::max(-1.0, std::min(1.0, static_cast<double>(value)));
        int32_t pcm = static_cast<int32_t>(std::llround(clipped * 8388607.0));
        const uint8_t bytes[3] = {
            static_cast<uint8_t>(pcm), static_cast<uint8_t>(pcm >> 8),
            static_cast<uint8_t>(pcm >> 16)
        };
        std::fwrite(bytes, 1, sizeof(bytes), file);
    };
    for (uint32_t frame = 0; frame < frames; ++frame) {
        sample(render.left[frame]);
        sample(render.right[frame]);
    }
    std::fclose(file);
    return true;
}

static Render renderEngine(int resolution, float smooth) {
    SynthEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setParameter(SynthParamOscWaveform, 3.0f);
    engine.setParameter(SynthParamOsc1Level, 1.0f);
    engine.setParameter(SynthParamOsc2Level, 0.0f);
    engine.setParameter(SynthParamNoiseLevel, 0.0f);
    engine.setParameter(SynthParamWavetable, 1.0f); // harmonically rich FM table
    engine.setParameter(SynthParamWTFrame, 0.43f);
    engine.setParameter(SynthParamWTLiveness, 0.0f);
    engine.setParameter(SynthParamWTResolution, static_cast<float>(resolution));
    engine.setParameter(SynthParamWTSmooth, smooth);
    engine.setParameter(SynthParamFilterCutoff, 20000.0f);
    engine.setParameter(SynthParamFilterResonance, 0.0f);
    engine.setParameter(SynthParamFilterEnvAmount, 0.0f);
    engine.setParameter(SynthParamFilterDrive, 0.0f);
    engine.setParameter(SynthParamFilterSlope, 0.0f);
    engine.setParameter(SynthParamFilterMode, 0.0f);
    engine.setParameter(SynthParamAmpAttack, normFromTime(0.0005f));
    engine.setParameter(SynthParamAmpDecay, normFromTime(0.01f));
    engine.setParameter(SynthParamAmpSustain, 1.0f);
    engine.setParameter(SynthParamAmpRelease, normFromTime(0.01f));
    engine.setParameter(SynthParamVelToVolume, 0.0f);
    engine.setParameter(SynthParamAnalogAmount, 0.0f);
    engine.setParameter(SynthParamOscPhaseSpread, 0.0f);
    engine.setParameter(SynthParamStereoSpread, 0.0f);
    engine.setParameter(SynthParamMasterGain, 0.85f);

    Render render{std::vector<float>(kRenderFrames),
                  std::vector<float>(kRenderFrames)};
    engine.noteOn(kMidiNote, 127);
    engine.render(render.left.data(), render.right.data(), kRenderFrames);
    return render;
}

static std::vector<float> renderRaw(int resolution, float smooth) {
    // Match the actual per-voice oscillator rate: Hybrid8 evaluates the table
    // at 4x before its nonlinear filter and two decimation stages.
    constexpr double rate = kSampleRate * Voice::kOversample;
    constexpr int warmup = static_cast<int>(rate * 0.25);
    constexpr int frames = static_cast<int>(rate * 1.0);
    WavetableOscillator osc;
    osc.setSampleRate(rate);
    osc.reset(0.0f, 0.0f);
    osc.setTable(wtTableAt(1));
    osc.setFrame(0.43f);
    osc.setLiveness(0.0f);
    osc.setResolution(resolution);
    osc.setSmooth(smooth);
    osc.setFrequency(kFundamental);
    for (int frame = 0; frame < warmup; ++frame) osc.process();
    std::vector<float> output(frames);
    for (float& value : output) value = osc.process();
    return output;
}

static double rms(const std::vector<float>& signal, int start, int frames) {
    double power = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double value = signal[start + i];
        power += value * value;
    }
    return std::sqrt(power / frames);
}

struct Metrics {
    double signalRMS = 0.0;
    double testRMS = 0.0;
    double residualRMS = 0.0;
    double residualPercent = 0.0;
    double residualDB = 0.0;
    double correlation = 0.0;
};

static Metrics compare(const std::vector<float>& clean,
                       const std::vector<float>& test,
                       int start, int frames) {
    Metrics result;
    result.signalRMS = rms(clean, start, frames);
    result.testRMS = rms(test, start, frames);
    double residualPower = 0.0, cross = 0.0;
    double cleanPower = 0.0, testPower = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double a = clean[start + i];
        const double b = test[start + i];
        const double delta = b - a;
        residualPower += delta * delta;
        cross += a * b;
        cleanPower += a * a;
        testPower += b * b;
    }
    result.residualRMS = std::sqrt(residualPower / frames);
    result.residualPercent = 100.0 * result.residualRMS
                           / std::max(result.signalRMS, 1.0e-15);
    result.residualDB = 20.0 * std::log10(
        std::max(result.residualRMS / std::max(result.signalRMS, 1.0e-15),
                 1.0e-15));
    result.correlation = cross / std::sqrt(cleanPower * testPower);
    return result;
}

static void printMetrics(const char* label, const Metrics& metrics) {
    std::printf("  %-17s residual=%7.4f%%  %7.2f dB  corr=%.8f  RMS %.6f -> %.6f\n",
                label, metrics.residualPercent, metrics.residualDB,
                metrics.correlation, metrics.signalRMS, metrics.testRMS);
}

static Render listeningMontage(const std::vector<Render>& renders) {
    constexpr int segmentFrames = static_cast<int>(kSampleRate * 1.0);
    constexpr int silenceFrames = static_cast<int>(kSampleRate * 0.15);
    const int total = static_cast<int>(renders.size()) * segmentFrames
                    + (static_cast<int>(renders.size()) - 1) * silenceFrames;
    Render montage{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
    int writeAt = 0;
    for (size_t index = 0; index < renders.size(); ++index) {
        for (int frame = 0; frame < segmentFrames; ++frame) {
            montage.left[writeAt + frame] = renders[index].left[kAnalysisStart + frame];
            montage.right[writeAt + frame] = renders[index].right[kAnalysisStart + frame];
        }
        writeAt += segmentFrames;
        if (index + 1 < renders.size()) writeAt += silenceFrames;
    }
    return montage;
}

} // namespace

int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "/tmp/hybrid8-wt-audit";
    mkdir(directory.c_str(), 0755);

    const Render clean = renderEngine(0, 1.0f);
    const Render bit12 = renderEngine(1, 1.0f);
    const Render bit8 = renderEngine(2, 1.0f);
    const Render vintage = renderEngine(3, 1.0f);
    writeWav24(directory + "/wt_clean.wav", clean);
    writeWav24(directory + "/wt_12bit.wav", bit12);
    writeWav24(directory + "/wt_8bit.wav", bit8);
    writeWav24(directory + "/wt_vintage.wav", vintage);
    writeWav24(directory + "/wt_compare_clean_12_8_vintage.wav",
               listeningMontage({clean, bit12, bit8, vintage}));

    std::printf("Hybrid8 wavetable resolution null test\n");
    std::printf("Patch: FM table, frame .43, A3, filter open, no drive/analog/liveness\n\n");
    std::printf("Complete SynthEngine output (reference distortion + noise):\n");
    printMetrics("12-bit vs Clean",
                 compare(clean.left, bit12.left, kAnalysisStart, kAnalysisFrames));
    printMetrics("8-bit vs Clean",
                 compare(clean.left, bit8.left, kAnalysisStart, kAnalysisFrames));
    printMetrics("Vintage vs Clean",
                 compare(clean.left, vintage.left, kAnalysisStart, kAnalysisFrames));

    const auto rawClean = renderRaw(0, 1.0f);
    const auto raw12 = renderRaw(1, 1.0f);
    const auto raw8 = renderRaw(2, 1.0f);
    const auto rawVintage = renderRaw(3, 1.0f);
    std::printf("\nRaw 4x wavetable oscillator output:\n");
    printMetrics("12-bit vs Clean",
                 compare(rawClean, raw12, 0, static_cast<int>(rawClean.size())));
    printMetrics("8-bit vs Clean",
                 compare(rawClean, raw8, 0, static_cast<int>(rawClean.size())));
    printMetrics("Vintage vs Clean",
                 compare(rawClean, rawVintage, 0, static_cast<int>(rawClean.size())));

    std::printf("\nWrote 24-bit WAVs to %s\n", directory.c_str());
    std::printf("Montage order: Clean, 12-bit, 8-bit, Vintage\n");
    return 0;
}
