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
#include <cmath>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <set>
#include <vector>

#include "R50Json.hpp"
#include "R50PitchDetect.hpp"
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
    float              tuneCents  = 0.0f;
    /// Whether the root above was stated by the file or is just the default.
    /// A one-shot now carries a `smpl` chunk with no loop in it, so "has a
    /// root" and "has a loop" are no longer the same question.
    bool               hasRoot    = false;
    bool               hasLoop    = false;
    bool               pingPong   = false;
    bool               unsupportedLoop = false;
    bool               validSmpl  = true;
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

inline bool isWavName(const std::string &name) {
    if (name.size() < 5) return false;
    const std::string extension = name.substr(name.size() - 4);
    return extension == ".wav" || extension == ".WAV";
}

inline bool isSafeLeafName(const std::string &name) {
    return !name.empty() && name != "." && name != ".."
        && name.find('/') == std::string::npos
        && name.find('\\') == std::string::npos;
}

inline bool validExplicitId(const std::string &id) {
    if (id.empty()) return false;
    for (const unsigned char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
         || c == '.' || c == '-' || c == '_') continue;
        return false;
    }
    return true;
}

inline std::string canonicalIdPart(const std::string &text) {
    std::string out;
    bool dash = false;
    for (const unsigned char c : text) {
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (alpha || digit || c == '.' || c == '_' || c == '-') {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            dash = false;
        } else if (!dash && !out.empty()) {
            out += '-';
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "unnamed" : out;
}

inline std::string fileStem(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
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
        } else if (detail::tagAt(bytes, at, "smpl") && size >= 36) {
            // 36 is the chunk without any loop record, which is how a one-shot
            // states its root key. Requiring 60 threw those away entirely.
            const uint32_t root = detail::readU32(bytes, body + 12);
            if (root > 127) {
                out.validSmpl = false;
            } else {
                out.rootKey = static_cast<int>(root);
                out.hasRoot = true;
                const double fraction = detail::readU32(bytes, body + 16)
                                      / 4294967296.0;
                // `smpl` describes the recording above its integer root. The
                // engine field is the correction applied during playback.
                out.tuneCents = static_cast<float>(-100.0 * fraction);
            }
            const uint32_t loopCount = detail::readU32(bytes, body + 28);
            if (loopCount > 0) {
                const uint64_t recordsEnd = static_cast<uint64_t>(36)
                                          + static_cast<uint64_t>(loopCount) * 24u;
                if (recordsEnd > size) {
                    out.validSmpl = false;
                } else {
                    for (uint32_t loop = 0; loop < loopCount; ++loop) {
                        const size_t record = body + 36 + loop * 24;
                        const uint32_t type = detail::readU32(bytes, record + 4);
                        if (type > 1) continue;
                        const uint32_t start = detail::readU32(bytes, record + 8);
                        const uint32_t inclusiveEnd =
                            detail::readU32(bytes, record + 12);
                        if (inclusiveEnd == UINT32_MAX || inclusiveEnd <= start) {
                            continue;
                        }
                        out.hasLoop   = true;
                        out.pingPong  = type == 1;
                        out.loopStart = start;
                        out.loopEnd   = inclusiveEnd + 1;
                        break;
                    }
                    out.unsupportedLoop = !out.hasLoop;
                }
            }
        }
        at = body + size + (size & 1u);
    }

    // No `haveFormat` here: the data chunk refuses to decode without one, so
    // haveData already implies it. Carrying both meant neither could be tested
    // — each masked the other, and a mutation of either still passed.
    out.ok = haveData && !out.samples.empty() && out.sampleRate > 0.0
          && out.validSmpl;
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

/// Turn every WAV in a directory into an instrument. This is the drop-in path:
/// no manifest, no import step, no registration — the directory *is* the list.
///
/// Loaded in filename order, and that ordering is load-bearing. A preset stores
/// an instrument index, so a stable order is the only thing that keeps presets
/// pointing at the sample they were built on; scanning in whatever order the
/// filesystem hands back would reshuffle them between machines.
///
/// Root key and loop come from the file's `smpl` chunk when it has one, since
/// that is what an editor writes and it is more trustworthy than any guess.
/// Without one the pitch is detected, and the duration decides the loop the
/// same way the importer does.
inline int loadSampleDirectory(SampleLibrary &library, const std::string &directory) {
    DIR *handle = ::opendir(directory.c_str());
    if (handle == nullptr) return 0;

    std::vector<std::string> names;
    while (dirent *entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name[0] == '.' || !detail::isWavName(name)) continue;
        names.push_back(name);
    }
    ::closedir(handle);
    std::sort(names.begin(), names.end());

    int loaded = 0;
    for (const std::string &name : names) {
        std::vector<uint8_t> bytes;
        if (!readWholeFile(directory + "/" + name, bytes)) continue;
        const LoadedWav wav = decodeWav(bytes);
        if (!wav.ok) continue;

        SampleData data;
        data.samples          = wav.samples;
        data.sourceSampleRate = wav.sampleRate;

        if (wav.hasLoop && wav.loopEnd > wav.loopStart + 1
         && wav.loopEnd <= static_cast<uint32_t>(data.length())) {
            data.rootKey   = wav.rootKey;
            data.loopStart = wav.loopStart;
            data.loopEnd   = wav.loopEnd;
            data.loopMode  = wav.pingPong ? LoopMode::PingPong
                                          : LoopMode::Forward;
        } else {
            // A stated root beats a detected one even when there is no loop:
            // the file is telling us what it is, and detection on a short
            // transient is a guess dressed as an answer.
            if (wav.hasRoot) {
                data.rootKey = wav.rootKey;
            } else {
                const DetectedPitch found = detectPitch(
                    data.samples.data(), data.length(), data.sourceSampleRate);
                data.rootKey = (found.valid && found.confidence > 0.5f)
                    ? found.rootKey : 60;
            }
            const double seconds = data.length() / data.sourceSampleRate;
            data.loopStart = 0;
            data.loopEnd   = static_cast<uint32_t>(data.length());
            data.loopMode  = seconds < 0.5 ? LoopMode::None : LoopMode::Forward;
        }

        const int rootKey = data.rootKey;
        const int slot = library.addSample(std::move(data));
        if (slot < 0) break;      // library full; stop rather than skip silently

        Multisample instrument;
        instrument.setName(name.substr(0, name.size() - 4).c_str());
        const std::string instrumentId =
            "factory.loose." + detail::canonicalIdPart(detail::fileStem(name));
        instrument.setId(instrumentId.c_str());
        SampleRegion region;
        region.rootKey = rootKey;
        region.tuneCents = wav.tuneCents;
        region.setId(detail::canonicalIdPart(detail::fileStem(name)).c_str());
        region.slot    = slot;
        instrument.regions[0] = region;
        instrument.regionCount = 1;
        if (library.addInstrument(instrument) >= 0) ++loaded;
    }
    return loaded;
}

/// Load the factory instruments named by a manifest.
///
/// The manifest exists so that identity is never inferred from a filename.
/// Instrument order is the order of the array, and which file becomes which
/// instrument is stated rather than sorted into place — renaming a file cannot
/// silently repoint a preset, and two files whose names happen to sort
/// differently on another machine cannot swap.
///
/// All or nothing: a manifest that names a file we cannot read leaves the
/// library untouched so the caller can fall back to generating, rather than
/// publishing a half-built factory set that would be much harder to diagnose.
///
///   { "instruments": [ { "name": "Choir",
///                        "zones": [ {"file": "...", "rootKey": 36,
///                                    "lowKey": 0, "highKey": 44,
///                                    "loop": true,
///                                    "loopStart": 0, "loopEnd": 22050} ] } ] }
///
/// `loop` is optional and overrides the file when present: true sustains, false
/// makes a one-shot of audio that carries a loop. Omit it and the WAV's `smpl`
/// chunk decides, which is what every zone did before the key existed.
/// `loopMode` is "forward" (the default) or "pingpong"; an unrecognised value
/// fails the manifest rather than quietly playing the other one.
///
/// The override exists because a WAV dropped in from anywhere else usually has
/// no `smpl` chunk at all, and the strict rule — no chunk, no loop — turned
/// every such file into a one-shot with nothing said about why. Stating it in
/// the manifest is a way to loop audio without having to rewrite the file
/// first. `loopStart`/`loopEnd` (exclusive, as everywhere in R50) are likewise
/// optional; with `loop: true` and no points the whole file loops.
///
/// `tuneCents` is the correction applied on playback, not a description of the
/// recording: a file that sits 15 cents sharp of its declared root wants -15.
/// (Same sign convention as the importer's editable root.) It exists because
/// `rootKey` only resolves to the nearest semitone, and a loop landing a
/// quarter-tone off beats audibly against the other Partial.
///
/// Sample rate is still not repeated here: it lives in the WAV, which remains
/// the source of truth for what the audio *is*.
inline bool loadFactoryManifest(SampleLibrary &library, const std::string &directory) {
    std::vector<uint8_t> raw;
    if (!readWholeFile(directory + "/factory_samples.json", raw)) return false;

    JsonValue root;
    if (!parseJson(std::string(raw.begin(), raw.end()), root)) return false;
    const JsonValue &instruments = root["instruments"];
    if (!instruments.isArray() || instruments.items.empty()) return false;

    // Decode everything before publishing anything.
    struct PendingZone {
        SampleData data;
        std::string id;
        int   rootKey, lowKey, highKey, lowVelocity, highVelocity;
        float tuneCents;
    };
    struct Pending {
        std::string id;
        std::string name;
        std::vector<PendingZone> zones;
    };
    std::vector<Pending> pending;

    int manifestIndex = 0;
    for (const JsonValue &entry : instruments.items) {
        if (!entry.isObject()) return false;
        const JsonValue &zones = entry["zones"];
        if (!zones.isArray() || zones.items.empty()) return false;
        if (zones.items.size() > static_cast<size_t>(kMaxRegions)) return false;

        Pending instrument;
        instrument.name = entry["name"].stringOr("");
        if (instrument.name.empty()) return false;
        instrument.id = entry["id"].stringOr(
            "factory.manifest." + std::to_string(manifestIndex));
        if (!detail::validExplicitId(instrument.id)) return false;

        for (const JsonValue &zone : zones.items) {
            const std::string file = zone["file"].stringOr("");
            if (!detail::isSafeLeafName(file)) return false;

            std::vector<uint8_t> bytes;
            if (!readWholeFile(directory + "/" + file, bytes)) return false;
            const LoadedWav wav = decodeWav(bytes);
            if (!wav.ok) return false;

            PendingZone out;
            out.id = zone["id"].stringOr(
                detail::canonicalIdPart(detail::fileStem(file)));
            if (!detail::validExplicitId(out.id)) return false;
            out.rootKey = zone["rootKey"].intOr(wav.rootKey);
            // Rejected, not clamped — unlike the loop points above. A root key
            // is a stated fact about what the recording *is*, so one outside
            // the keyboard is a typo, and clamping it would publish an
            // instrument that plays at the wrong pitch across every key with
            // nothing said. A loop end past the end of the file is the same
            // manifest describing audio that has since been re-edited, which is
            // worth surviving; this is not.
            if (out.rootKey < 0 || out.rootKey > 127) return false;
            // Clamped to a semitone either way: past that the root key is
            // simply wrong, and a manifest that says so is describing a
            // different note rather than detuning this one.
            out.tuneCents = static_cast<float>(
                std::min(100.0, std::max(-100.0,
                    zone["tuneCents"].doubleOr(0.0))));
            out.lowKey  = zone["lowKey"].intOr(0);
            out.highKey = zone["highKey"].intOr(127);
            out.lowVelocity  = zone["lowVelocity"].intOr(1);
            out.highVelocity = zone["highVelocity"].intOr(127);
            if (out.lowKey < 0 || out.highKey > 127 || out.lowKey > out.highKey
             || out.lowVelocity < 1 || out.highVelocity > 127
             || out.lowVelocity > out.highVelocity) return false;
            if (!zone["tuneCents"].exists()) out.tuneCents = wav.tuneCents;
            out.data.samples          = wav.samples;
            out.data.sourceSampleRate = wav.sampleRate;
            out.data.rootKey          = out.rootKey;

            const uint32_t frames = static_cast<uint32_t>(out.data.length());
            const JsonValue &loopFlag = zone["loop"];
            const bool wavLoops = wav.hasLoop && wav.loopEnd > wav.loopStart + 1
                               && wav.loopEnd <= frames;
            const bool looped = loopFlag.exists() ? loopFlag.boolOr(false)
                                                  : wavLoops;

            // Points: whatever the manifest states, else the file's, else the
            // whole thing. Clamped rather than rejected — a loopEnd past the
            // end of the audio is the manifest describing a file that has since
            // been re-edited shorter, and losing the whole factory set over
            // that is out of proportion to a loop that stops early.
            uint32_t start = zone["loopStart"].intOr(
                wavLoops ? static_cast<int>(wav.loopStart) : 0);
            uint32_t end = zone["loopEnd"].intOr(
                wavLoops ? static_cast<int>(wav.loopEnd) : static_cast<int>(frames));
            if (end > frames) end = frames;
            if (end <= start + 1) { start = 0; end = frames; }

            // "pingpong" reverses at each end instead of jumping back, which
            // doubles the effective period and hides a join that does not
            // quite meet — worth having on the short loops, where a forward
            // jump repeats often enough to be heard as a pitch of its own.
            const std::string mode = zone["loopMode"].stringOr(
                wavLoops && wav.pingPong ? "pingpong" : "forward");
            if (mode != "forward" && mode != "pingpong") return false;

            out.data.loopStart = looped ? start : 0;
            out.data.loopEnd   = looped ? end : frames;
            out.data.loopMode  = !looped ? LoopMode::None
                               : mode == "pingpong" ? LoopMode::PingPong
                                                    : LoopMode::Forward;
            instrument.zones.push_back(std::move(out));
        }
        pending.push_back(std::move(instrument));
        ++manifestIndex;
    }

    if (library.sampleCount() + static_cast<int>([&] {
            size_t count = 0;
            for (const Pending &instrument : pending) count += instrument.zones.size();
            return count;
        }()) > kMaxSampleSlots
     || library.instrumentCount() + static_cast<int>(pending.size()) > kMaxInstruments) {
        return false;
    }
    std::set<std::string> instrumentIds;
    for (const Pending &instrument : pending) {
        if (!instrumentIds.insert(instrument.id).second
         || library.instrumentIndex(instrument.id.c_str()) >= 0) return false;
        std::set<std::string> zoneIds;
        for (const PendingZone &zone : instrument.zones) {
            if (!zoneIds.insert(zone.id).second) return false;
        }
    }

    for (Pending &instrument : pending) {
        Multisample published;
        published.setName(instrument.name.c_str());
        published.setId(instrument.id.c_str());
        for (PendingZone &zone : instrument.zones) {
            const int slot = library.addSample(std::move(zone.data));
            if (slot < 0) return false;
            SampleRegion region;
            region.lowKey  = zone.lowKey;
            region.highKey = zone.highKey;
            region.lowVelocity  = zone.lowVelocity;
            region.highVelocity = zone.highVelocity;
            region.rootKey   = zone.rootKey;
            region.tuneCents = zone.tuneCents;
            region.setId(zone.id.c_str());
            region.slot    = slot;
            published.regions[published.regionCount++] = region;
        }
        if (library.addInstrument(published) < 0) return false;
    }
    return true;
}

/// Load one immediate child directory as one multisampled instrument.
///
/// With `instrument.json`, key/velocity bounds and IDs are explicit. Without
/// it, every WAV must carry a unique `smpl` root and key bounds are derived at
/// the midpoint between adjacent roots. Everything is decoded and validated
/// before any slot is published.
inline bool loadFactoryInstrumentDirectory(SampleLibrary &library,
                                           const std::string &directory,
                                           const std::string &directoryName) {
    struct PendingZone {
        std::string id;
        std::string file;
        SampleData data;
        int rootKey = 60;
        float tuneCents = 0.0f;
        int lowKey = 0, highKey = 127;
        int lowVelocity = 1, highVelocity = 127;
    };

    std::vector<uint8_t> raw;
    const bool hasManifest =
        readWholeFile(directory + "/instrument.json", raw);
    JsonValue manifest;
    if (hasManifest
     && !parseJson(std::string(raw.begin(), raw.end()), manifest)) return false;
    if (hasManifest && (!manifest.isObject()
                     || manifest["schemaVersion"].intOr(0) != 1)) return false;

    std::string name = hasManifest
        ? manifest["name"].stringOr(directoryName) : directoryName;
    std::string instrumentId = hasManifest
        ? manifest["id"].stringOr("")
        : "factory.auto." + detail::canonicalIdPart(directoryName);
    if (name.empty() || !detail::validExplicitId(instrumentId)
     || instrumentId.size() >= kAssetIdLength
     || library.instrumentIndex(instrumentId.c_str()) >= 0) return false;

    std::vector<std::pair<std::string, const JsonValue *>> files;
    if (hasManifest) {
        const JsonValue &zones = manifest["zones"];
        if (!zones.isArray() || zones.items.empty()
         || zones.items.size() > static_cast<size_t>(kMaxRegions)) return false;
        for (const JsonValue &zone : zones.items) {
            if (!zone.isObject()) return false;
            const std::string file = zone["file"].stringOr("");
            if (!detail::isSafeLeafName(file) || !detail::isWavName(file))
                return false;
            files.push_back({file, &zone});
        }
    } else {
        DIR *handle = ::opendir(directory.c_str());
        if (handle == nullptr) return false;
        while (dirent *entry = ::readdir(handle)) {
            const std::string file = entry->d_name;
            if (file.empty() || file[0] == '.' || !detail::isWavName(file))
                continue;
            struct stat info {};
            const std::string path = directory + "/" + file;
            if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
                continue;
            files.push_back({file, nullptr});
        }
        ::closedir(handle);
        if (files.empty() || files.size() > static_cast<size_t>(kMaxRegions))
            return false;
        std::sort(files.begin(), files.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
    }

    std::vector<PendingZone> pending;
    std::set<std::string> zoneIds;
    for (const auto &source : files) {
        std::vector<uint8_t> bytes;
        if (!readWholeFile(directory + "/" + source.first, bytes)) return false;
        const LoadedWav wav = decodeWav(bytes);
        if (!wav.ok) return false;
        if (!hasManifest && wav.unsupportedLoop) return false;

        const JsonValue *zone = source.second;
        PendingZone out;
        out.file = source.first;
        out.id = zone ? (*zone)["id"].stringOr("") :
                        detail::canonicalIdPart(detail::fileStem(source.first));
        if (!detail::validExplicitId(out.id) || out.id.size() >= kZoneIdLength
         || !zoneIds.insert(out.id).second) return false;

        const bool explicitRoot = zone && (*zone)["rootKey"].exists();
        if (!explicitRoot && !wav.hasRoot) return false;
        out.rootKey = explicitRoot ? (*zone)["rootKey"].intOr(-1) : wav.rootKey;
        if (out.rootKey < 0 || out.rootKey > 127) return false;

        out.tuneCents = zone && (*zone)["tuneCents"].exists()
            ? static_cast<float>(std::min(100.0, std::max(-100.0,
                  (*zone)["tuneCents"].doubleOr(0.0))))
            : wav.tuneCents;
        if (zone) {
            out.lowKey = (*zone)["lowKey"].intOr(0);
            out.highKey = (*zone)["highKey"].intOr(127);
            out.lowVelocity = (*zone)["lowVelocity"].intOr(1);
            out.highVelocity = (*zone)["highVelocity"].intOr(127);
        }
        if (out.lowKey < 0 || out.highKey > 127 || out.lowKey > out.highKey
         || out.lowVelocity < 1 || out.highVelocity > 127
         || out.lowVelocity > out.highVelocity) return false;

        out.data.samples = wav.samples;
        out.data.sourceSampleRate = wav.sampleRate;
        out.data.rootKey = out.rootKey;
        const uint32_t frames = static_cast<uint32_t>(out.data.length());
        const bool wavLoops = wav.hasLoop && wav.loopEnd > wav.loopStart + 1
                           && wav.loopEnd <= frames;
        const JsonValue *loopFlag = zone ? &(*zone)["loop"] : nullptr;
        const bool looped = loopFlag && loopFlag->exists()
            ? loopFlag->boolOr(false) : wavLoops;
        uint32_t start = zone ? (*zone)["loopStart"].intOr(
            wavLoops ? static_cast<int>(wav.loopStart) : 0)
            : (wavLoops ? wav.loopStart : 0);
        uint32_t end = zone ? (*zone)["loopEnd"].intOr(
            wavLoops ? static_cast<int>(wav.loopEnd) : static_cast<int>(frames))
            : (wavLoops ? wav.loopEnd : frames);
        if (end > frames) end = frames;
        if (end <= start + 1) {
            if (zone && (looped || (*zone)["loopStart"].exists()
                              || (*zone)["loopEnd"].exists())) return false;
            start = 0;
            end = frames;
        }
        const std::string mode = zone
            ? (*zone)["loopMode"].stringOr(
                  wavLoops && wav.pingPong ? "pingpong" : "forward")
            : (wav.pingPong ? "pingpong" : "forward");
        if (mode != "forward" && mode != "pingpong") return false;
        out.data.loopStart = looped ? start : 0;
        out.data.loopEnd = looped ? end : frames;
        out.data.loopMode = !looped ? LoopMode::None
                          : mode == "pingpong" ? LoopMode::PingPong
                                               : LoopMode::Forward;
        pending.push_back(std::move(out));
    }

    if (!hasManifest) {
        std::sort(pending.begin(), pending.end(),
                  [](const PendingZone &a, const PendingZone &b) {
                      return a.rootKey != b.rootKey ? a.rootKey < b.rootKey
                                                    : a.file < b.file;
                  });
        for (size_t i = 1; i < pending.size(); ++i)
            if (pending[i - 1].rootKey == pending[i].rootKey) return false;
        for (size_t i = 0; i < pending.size(); ++i) {
            pending[i].lowKey = i == 0 ? 0
                : (pending[i - 1].rootKey + pending[i].rootKey) / 2 + 1;
            pending[i].highKey = i + 1 == pending.size() ? 127
                : (pending[i].rootKey + pending[i + 1].rootKey) / 2;
        }
    }

    if (library.sampleCount() + static_cast<int>(pending.size()) > kMaxSampleSlots
     || library.instrumentCount() >= kMaxInstruments) return false;

    Multisample instrument;
    instrument.setName(name.c_str());
    instrument.setId(instrumentId.c_str());
    for (PendingZone &zone : pending) {
        const int slot = library.addSample(std::move(zone.data));
        if (slot < 0) return false; // capacities were checked above
        SampleRegion region;
        region.lowKey = zone.lowKey;
        region.highKey = zone.highKey;
        region.lowVelocity = zone.lowVelocity;
        region.highVelocity = zone.highVelocity;
        region.rootKey = zone.rootKey;
        region.tuneCents = zone.tuneCents;
        region.slot = slot;
        region.setId(zone.id.c_str());
        instrument.regions[instrument.regionCount++] = region;
    }
    return library.addInstrument(instrument) >= 0;
}

/// Discover immediate child directories in deterministic bytewise order.
/// Invalid instruments are isolated: they are skipped without preventing other
/// directories from loading.
inline int loadFactoryDirectories(SampleLibrary &library,
                                  const std::string &rootDirectory) {
    DIR *handle = ::opendir(rootDirectory.c_str());
    if (handle == nullptr) return 0;
    std::vector<std::string> directories;
    while (dirent *entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') continue;
        struct stat info {};
        const std::string path = rootDirectory + "/" + name;
        if (::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
            directories.push_back(name);
    }
    ::closedir(handle);
    std::sort(directories.begin(), directories.end());

    int loaded = 0;
    for (const std::string &name : directories) {
        if (loadFactoryInstrumentDirectory(library, rootDirectory + "/" + name,
                                           name)) ++loaded;
    }
    return loaded;
}

} // namespace r50
