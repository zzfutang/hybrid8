// Audit the material that R50's sample oscillator actually sees. Generated
// factory zones and loose bundled WAVs are inspected after decoding to the
// engine's mono float representation, then pitched zones are played through
// the real interpolating SamplePlayer at their root and key-range edges.

#include "R50SampleFactory.hpp"
#include "R50SamplePlayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static constexpr double kOutputRate = 48000.0;

static double midiHz(int key) {
    return 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

static double cents(double actual, double expected) {
    return actual > 0.0 && expected > 0.0
        ? 1200.0 * std::log2(actual / expected) : 0.0;
}

static const char *loopName(r50::LoopMode mode) {
    switch (mode) {
        case r50::LoopMode::Forward:  return "forward";
        case r50::LoopMode::PingPong: return "pingpong";
        default:                      return "none";
    }
}

static r50::DetectedPitch playAndDetect(const r50::SampleData *data,
                                        const r50::SampleRegion &region,
                                        int note) {
    r50::SamplePlayer player;
    player.start(data, &region, 0.0f);
    const double semitones = note - region.rootKey + region.tuneCents / 100.0;
    player.setPlaybackRatio(std::pow(2.0, semitones / 12.0), kOutputRate);
    std::vector<float> rendered(static_cast<size_t>(kOutputRate * 0.35));
    for (float &sample : rendered) sample = player.process();
    return r50::detectPitch(rendered.data(), static_cast<int>(rendered.size()),
                            kOutputRate);
}

int main(int argc, char **argv) {
    const std::string output = argc > 1 ? argv[1] : "build/sample-audit";
    const std::string loose = argc > 2 ? argv[2] : "";
    std::filesystem::create_directories(output);

    r50::SampleLibrary library;
    const int generatedCount = library.instrumentCount();
    const int looseCount = loose.empty() ? 0 : r50::loadSampleDirectory(library, loose);

    FILE *zones = std::fopen((output + "/zones.csv").c_str(), "w");
    FILE *plays = std::fopen((output + "/playback.csv").c_str(), "w");
    if (!zones || !plays) return 2;
    std::fprintf(zones, "origin,instrument,index,zone,low_key,high_key,"
                        "declared_root,source_rate,internal_channels,frames,"
                        "duration_s,loop_mode,loop_start,loop_end,detected_hz,"
                        "detected_key,cents_from_declared,confidence,status\n");
    std::fprintf(plays, "origin,instrument,index,zone,note,expected_hz,"
                        "detected_hz,cents_error,confidence,status\n");

    int pitchedZones = 0, sourceFailures = 0, playbackFailures = 0;
    int rates = 0;
    std::vector<int> seenRates;
    for (int i = 0; i < library.instrumentCount(); ++i) {
        const r50::Multisample *instrument = library.instrument(i);
        // Generated one-shots are attacks. Loose WAVs may be pitched despite
        // having one region (both files currently bundled with R50 are).
        const bool pitched = instrument->regionCount > 1 || i >= generatedCount;
        const char *origin = i < generatedCount ? "generated" : "bundled";
        for (int z = 0; z < instrument->regionCount; ++z) {
            const r50::SampleRegion &region = instrument->regions[z];
            const r50::SampleData *data = library.sample(region.slot);
            if (!data) continue;
            const int roundedRate = static_cast<int>(std::lround(data->sourceSampleRate));
            if (std::find(seenRates.begin(), seenRates.end(), roundedRate)
                == seenRates.end()) {
                seenRates.push_back(roundedRate);
                ++rates;
            }
            const r50::DetectedPitch found = r50::detectPitch(
                data->samples.data(), data->length(), data->sourceSampleRate);
            const double sourceError = found.valid
                ? cents(found.hertz, midiHz(region.rootKey)) : 0.0;
            const double expectedSource = midiHz(region.rootKey);
            const bool sourceMeasurable =
                expectedSource >= 40.0 && expectedSource <= 2400.0;
            const bool sourceOK = !pitched || !sourceMeasurable
                || (found.valid && found.confidence >= 0.5f
                    && std::fabs(sourceError) <= 25.0);
            if (pitched && sourceMeasurable) {
                ++pitchedZones;
                if (!sourceOK) ++sourceFailures;
            }
            std::fprintf(zones,
                "%s,\"%s\",%d,%d,%d,%d,%d,%.0f,1,%d,%.6f,%s,%u,%u,"
                "%.4f,%d,%.2f,%.3f,%s\n",
                origin, instrument->name, i, z, region.lowKey, region.highKey,
                region.rootKey, data->sourceSampleRate, data->length(),
                data->length() / data->sourceSampleRate, loopName(data->loopMode),
                data->loopStart, data->loopEnd, found.hertz, found.rootKey,
                sourceError, found.confidence,
                !pitched ? "unpitched"
                    : (!sourceMeasurable ? "outside_detector_range"
                                        : (sourceOK ? "pass" : "FAIL")));

            if (!pitched) continue;
            int notes[3] = {region.lowKey, region.rootKey, region.highKey};
            const int noteCount = instrument->regionCount > 1 ? 3 : 1;
            if (noteCount == 1) notes[0] = region.rootKey;
            for (int n = 0; n < noteCount; ++n) {
                if (n > 0 && notes[n] == notes[n - 1]) continue;
                const double expected = midiHz(notes[n]);
                if (expected < 40.0 || expected > 2400.0) {
                    std::fprintf(plays,
                        "%s,\"%s\",%d,%d,%d,%.4f,0,0,0,outside_detector_range\n",
                        origin, instrument->name, i, z, notes[n], expected);
                    continue;
                }
                const r50::DetectedPitch played =
                    playAndDetect(data, region, notes[n]);
                const double error = played.valid
                    ? cents(played.hertz, expected) : 0.0;
                const bool ok = played.valid && played.confidence >= 0.5f
                             && std::fabs(error) <= 35.0;
                if (!ok) ++playbackFailures;
                std::fprintf(plays,
                    "%s,\"%s\",%d,%d,%d,%.4f,%.4f,%.2f,%.3f,%s\n",
                    origin, instrument->name, i, z, notes[n],
                    expected, played.hertz, error, played.confidence,
                    ok ? "pass" : "FAIL");
            }
        }
    }
    std::fclose(zones);
    std::fclose(plays);

    std::printf("R50 sample audit: %d instruments (%d generated, %d bundled)\n",
                library.instrumentCount(), generatedCount, looseCount);
    std::printf("Internal format: mono float; %d source sample rate(s):", rates);
    for (int rate : seenRates) std::printf(" %d", rate);
    std::printf(" Hz\nPitched zones: %d; source failures: %d; playback failures: %d\n",
                pitchedZones, sourceFailures, playbackFailures);
    std::printf("Reports: %s/zones.csv and %s/playback.csv\n",
                output.c_str(), output.c_str());
    return sourceFailures || playbackFailures ? 1 : 0;
}
