//
//  R50FactoryFiles.hpp
//  Mirrors the generated factory content into a directory of WAV files, and
//  loads whatever is there back over the top on the next launch.
//
//  The generator stays the source of truth for a first run — nothing is
//  shipped, so the files have to come from somewhere — but once they exist on
//  disk they win. That is what makes a factory sample workable: open the file,
//  change it, relaunch, hear it. Importing could never do that, because an
//  import appends a new instrument and the generated one is still sitting
//  where it was.
//
//  A bad or unreadable file falls back to the generated audio rather than
//  failing. Losing an instrument because a file was half-written is a much
//  worse outcome than ignoring an edit.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "R50Sample.hpp"
#include "R50WavWriter.hpp"

namespace r50 {

/// What a WAV gave us. `ok` false means the file was missing, truncated or in a
/// format we do not read, and the caller should keep what it already had.
struct LoadedWav {
    bool               ok = false;
    std::vector<float> samples;
    double             sampleRate = 44100.0;
    int                rootKey    = 60;
    bool               hasLoop    = false;
    uint32_t           loopStart  = 0;
    uint32_t           loopEnd    = 0;   // exclusive, as everywhere else in R50
};

namespace detail {

inline uint32_t readU32(const std::vector<uint8_t> &b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8)
         | (static_cast<uint32_t>(b[at + 2]) << 16)
         | (static_cast<uint32_t>(b[at + 3]) << 24);
}

inline uint32_t readU16(const std::vector<uint8_t> &b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
}

inline bool tagAt(const std::vector<uint8_t> &b, size_t at, const char *tag) {
    return at + 4 <= b.size()
        && b[at] == static_cast<uint8_t>(tag[0]) && b[at + 1] == static_cast<uint8_t>(tag[1])
        && b[at + 2] == static_cast<uint8_t>(tag[2]) && b[at + 3] == static_cast<uint8_t>(tag[3]);
}

} // namespace detail

/// Decodes 16- and 24-bit integer and 32-bit float mono/stereo PCM, which
/// covers what an audio editor writes when handed one of our exports back.
/// Stereo is summed, matching the importer — the engine is mono per sample.
inline LoadedWav decodeWav(const std::vector<uint8_t> &bytes) {
    LoadedWav out;
    if (bytes.size() < 44 || !detail::tagAt(bytes, 0, "RIFF")
     || !detail::tagAt(bytes, 8, "WAVE")) {
        return out;
    }

    uint32_t format = 1, channels = 1, bits = 16;
    bool haveFormat = false, haveData = false;
    size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const uint32_t size = detail::readU32(bytes, at + 4);
        const size_t body = at + 8;
        if (body + size > bytes.size()) break;

        if (detail::tagAt(bytes, at, "fmt ") && size >= 16) {
            format         = detail::readU16(bytes, body);
            channels       = detail::readU16(bytes, body + 2);
            out.sampleRate = detail::readU32(bytes, body + 4);
            bits           = detail::readU16(bytes, body + 14);
            haveFormat = true;
        } else if (detail::tagAt(bytes, at, "data")) {
            if (!haveFormat || channels == 0) return out;
            const uint32_t bytesPerSample = bits / 8;
            if (bytesPerSample == 0) return out;
            const uint32_t frames = size / (bytesPerSample * channels);
            out.samples.resize(frames, 0.0f);
            for (uint32_t f = 0; f < frames; ++f) {
                double sum = 0.0;
                for (uint32_t c = 0; c < channels; ++c) {
                    const size_t at2 = body + (f * channels + c) * bytesPerSample;
                    if (format == 3 && bits == 32) {
                        float value;
                        std::memcpy(&value, &bytes[at2], sizeof(float));
                        sum += value;
                    } else if (bits == 24) {
                        int32_t v = static_cast<int32_t>(bytes[at2])
                                  | (static_cast<int32_t>(bytes[at2 + 1]) << 8)
                                  | (static_cast<int32_t>(bytes[at2 + 2]) << 16);
                        if (v & 0x800000) v |= ~0xFFFFFF;
                        sum += v / 8388607.0;
                    } else if (bits == 16) {
                        int16_t v = static_cast<int16_t>(
                            detail::readU16(bytes, at2));
                        sum += v / 32767.0;
                    } else if (bits == 32) {
                        const int32_t v = static_cast<int32_t>(detail::readU32(bytes, at2));
                        sum += v / 2147483647.0;
                    } else {
                        return out;   // 8-bit and exotic depths are not read
                    }
                }
                out.samples[f] = static_cast<float>(sum / channels);
            }
            haveData = true;
        } else if (detail::tagAt(bytes, at, "smpl") && size >= 60) {
            out.rootKey = static_cast<int>(detail::readU32(bytes, body + 12));
            if (detail::readU32(bytes, body + 28) >= 1) {
                out.hasLoop   = true;
                out.loopStart = detail::readU32(bytes, body + 44);
                // smpl stores the last played frame; R50's loopEnd is exclusive.
                out.loopEnd   = detail::readU32(bytes, body + 48) + 1;
            }
        }
        at = body + size + (size & 1u);
    }

    // No `haveFormat` here: the data chunk refuses to decode without one, so
    // haveData already implies it. Carrying both meant neither could be tested
    // — each masked the other, and a mutation of either still passed.
    out.ok = haveData && !out.samples.empty() && out.sampleRate > 0.0;
    return out;
}

inline bool writeWholeFile(const std::string &path, const std::vector<uint8_t> &bytes) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

inline bool readWholeFile(const std::string &path, std::vector<uint8_t> &bytes) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) { std::fclose(file); return false; }
    bytes.resize(static_cast<size_t>(size));
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return read == bytes.size();
}

/// Stable, human-readable, and above all predictable: the loader builds the
/// name it expects rather than scanning the directory, so a renamed file is
/// simply not picked up instead of being applied to the wrong instrument.
inline std::string factoryFileName(int instrument, const char *name, int zone) {
    std::string safe;
    for (const char *c = name; *c != '\0'; ++c) {
        safe += (*c == '/' || *c == ':') ? '-' : *c;
    }
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "%02d ", instrument);
    char suffix[16];
    std::snprintf(suffix, sizeof suffix, " z%d.wav", zone);
    return std::string(prefix) + safe + suffix;
}

/// Writes anything missing, then loads everything present back over the top.
/// Returns how many zones were replaced by a file, which is the interesting
/// number: zero on a first run, and non-zero once anything has been edited.
inline int syncFactoryDirectory(SampleLibrary &library, const std::string &directory) {
    ::mkdir(directory.c_str(), 0755);

    int replaced = 0;
    for (int index = 0; index < library.instrumentCount(); ++index) {
        const Multisample *instrument = library.instrument(index);
        if (instrument == nullptr) continue;

        for (int zone = 0; zone < instrument->regionCount; ++zone) {
            const SampleRegion &region = instrument->regions[zone];
            const SampleData *current = library.sample(region.slot);
            if (current == nullptr) continue;

            const std::string path = directory + "/"
                + factoryFileName(index, instrument->name, zone);

            std::vector<uint8_t> bytes;
            if (!readWholeFile(path, bytes)) {
                writeWholeFile(path, encodeWav(
                    current->samples.data(), current->length(),
                    current->sourceSampleRate, region.rootKey,
                    current->loopStart, current->loopEnd,
                    current->loopMode != LoopMode::None));
                continue;
            }

            const LoadedWav loaded = decodeWav(bytes);
            if (!loaded.ok) continue;   // keep the generated audio

            SampleData replacement;
            replacement.samples          = loaded.samples;
            replacement.sourceSampleRate = loaded.sampleRate;
            replacement.rootKey          = loaded.rootKey;
            if (loaded.hasLoop && loaded.loopEnd > loaded.loopStart + 1
             && loaded.loopEnd <= static_cast<uint32_t>(replacement.length())) {
                replacement.loopStart = loaded.loopStart;
                replacement.loopEnd   = loaded.loopEnd;
                replacement.loopMode  = LoopMode::Forward;
            } else {
                replacement.loopStart = 0;
                replacement.loopEnd   = static_cast<uint32_t>(replacement.length());
                replacement.loopMode  = LoopMode::None;
            }
            if (library.replaceSample(region.slot, std::move(replacement))) {
                ++replaced;
            }
        }
    }
    return replaced;
}

} // namespace r50
