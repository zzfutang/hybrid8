// Isolated R50 oscillator audit. Renders every analytic waveform with no voice
// filter, envelope, samples, noise, modulation or effects and reports the
// actual harmonic bandwidth retained by the mip pyramid.

#include "R50WaveOscillator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static constexpr double kSR = 48000.0;
static constexpr int kFFT = 16384;
static const char *kNames[] = {
    "Saw", "Triangle", "Square", "Pulse10", "Pulse",
    "Organ", "Tine", "Clarinet", "Strings", "VocalAh", "Bell", "Sine",
    "VocalE", "VocalI", "VocalO", "VocalU", "VocalNasal"
};

static double midiHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

static double magnitude(const std::vector<float> &x, double hz) {
    const double w = synth::kTwoPi * hz / kSR;
    const double cw = std::cos(w), sw = std::sin(w);
    double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0;
    for (int i = 0; i < kFFT; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            synth::kTwoPi * i / (kFFT - 1));
        re += x[i] * window * pr;
        im -= x[i] * window * pi;
        const double nr = pr * cw - pi * sw;
        pi = pr * sw + pi * cw;
        pr = nr;
    }
    return std::sqrt(re * re + im * im);
}

static r50::WaveSpectrum spectrumFor(int wave) {
    switch (r50::waveDescriptors()[wave].pyramid) {
        case r50::kPyramidSaw:      return r50::waveSpectrumSaw();
        case r50::kPyramidTriangle: return r50::waveSpectrumTriangle();
        case r50::kPyramidOrgan:    return r50::waveSpectrumOrgan();
        case r50::kPyramidTine:     return r50::waveSpectrumTine();
        case r50::kPyramidClarinet: return r50::waveSpectrumClarinet();
        case r50::kPyramidStrings:  return r50::waveSpectrumStrings();
        case r50::kPyramidVocalAh:  return r50::waveSpectrumVocalAh();
        case r50::kPyramidBell:     return r50::waveSpectrumBell();
        case r50::kPyramidSine:     return r50::waveSpectrumSine();
        case r50::kPyramidVocalE:   return r50::waveSpectrumVocalE();
        case r50::kPyramidVocalI:   return r50::waveSpectrumVocalI();
        case r50::kPyramidVocalO:   return r50::waveSpectrumVocalO();
        case r50::kPyramidVocalU:   return r50::waveSpectrumVocalU();
        case r50::kPyramidVocalNasal: return r50::waveSpectrumVocalNasal();
        default:                    return r50::waveSpectrumSaw();
    }
}

static double designedMagnitude(int wave, int harmonic) {
    const auto spectrum = spectrumFor(wave);
    double magnitude = spectrum.mag[harmonic];
    const auto &descriptor = r50::waveDescriptors()[wave];
    if (descriptor.read == r50::WaveRead::Difference) {
        const double width = descriptor.fixedWidth >= 0.0f
                           ? descriptor.fixedWidth : 0.5;
        magnitude *= std::fabs(2.0 * std::sin(synth::kPi * harmonic * width));
    }
    return magnitude;
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

int main(int argc, char **argv) {
    const std::string output = argc > 1 ? argv[1] : "build/waveform-audit";
    std::filesystem::create_directories(output);
    FILE *csv = std::fopen((output + "/report.csv").c_str(), "w");
    std::fprintf(csv, "wave,note,fundamental_hz,peak,rms,dc,max_step,"
                      "expected_harmonics,measured_harmonics,bandwidth_hz,"
                      "bandwidth_coverage_percent\n");

    const int notes[] = {36, 60, 84, 108}; // C2, C4, C6, C8
    double worstCoverage = 100.0;
    for (int wave = 0; wave < r50::kWaveCount; ++wave) {
        for (int note : notes) {
            const double f0 = midiHz(note);
            r50::WaveOscillator osc;
            osc.setSampleRate(kSR);
            osc.setWave(wave);
            osc.setWidth(0.5f);
            osc.setFrequency(f0);
            osc.reset();

            std::vector<float> audio(kFFT);
            double squares = 0.0, sum = 0.0, peak = 0.0, step = 0.0;
            for (int i = 0; i < kFFT; ++i) {
                audio[i] = osc.process();
                peak = std::max(peak, std::fabs(static_cast<double>(audio[i])));
                squares += audio[i] * audio[i];
                sum += audio[i];
                if (i) step = std::max(step, std::fabs(
                    static_cast<double>(audio[i] - audio[i - 1])));
            }

            const double fundamental = magnitude(audio, f0);
            const int nyquistHarmonics = std::min(
                r50::kWaveMaxHarm,
                static_cast<int>(std::floor(r50::kWaveAudibleMax / f0)));
            const double designedFundamental = designedMagnitude(wave, 1);
            const float level = r50::waveLevelForFreq(f0);
            const int lowerLevel = std::min(
                static_cast<int>(level), r50::kWaveNumLevels - 1);
            const int upperLevel = std::min(
                lowerLevel + 1, r50::kWaveNumLevels - 1);
            const double lowerWeight = 1.0 - (level - lowerLevel);
            const int lowerLimit = r50::waveMaxHarmonic(lowerLevel);
            const int upperLimit = r50::waveMaxHarmonic(upperLevel);
            int expected = 1, retained = 1;
            for (int harmonic = 2; harmonic <= nyquistHarmonics; ++harmonic) {
                const double designed = designedMagnitude(wave, harmonic);
                if (designed >= designedFundamental * 0.001) expected = harmonic;
                const double mipWeight = harmonic <= upperLimit ? 1.0
                    : (harmonic <= lowerLimit ? lowerWeight : 0.0);
                if (designed * mipWeight >= designedFundamental * 0.001)
                    retained = harmonic;
            }
            const double coverage = 100.0 * retained / std::max(expected, 1);
            worstCoverage = std::min(worstCoverage, coverage);
            const std::string stem = std::to_string(wave) + "_" + kNames[wave]
                                   + "_C" + std::to_string(note / 12 - 1);
            writeWav(output + "/" + stem + ".wav", audio);
            std::fprintf(csv, "%s,%d,%.3f,%.6f,%.6f,%.7f,%.6f,%d,%d,%.1f,%.1f\n",
                         kNames[wave], note, f0, peak,
                         std::sqrt(squares / audio.size()), sum / audio.size(),
                         step, expected, retained, retained * f0, coverage);
            std::printf("%-9s C%d  expected %3d  measured %3d  coverage %5.1f%%\n",
                        kNames[wave], note / 12 - 1, expected, retained, coverage);
        }
    }
    std::fclose(csv);
    std::printf("\n%d isolated WAVs and report written to %s\n",
                r50::kWaveCount * static_cast<int>(std::size(notes)),
                output.c_str());
    std::printf("Worst measured harmonic-bandwidth coverage: %.1f%%\n", worstCoverage);
    return 0;
}
