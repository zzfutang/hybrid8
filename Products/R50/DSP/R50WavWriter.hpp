//
//  R50WavWriter.hpp
//  Encodes one zone as a WAV file so generated content can be opened in an
//  editor. Everything R50 ships is synthesised into memory and has never
//  existed as a file, so without this there is no way to work on a factory
//  sample at all.
//
//  Written by hand rather than through AVAudioFile for one reason: a `smpl`
//  chunk. R50 chooses a loop length so that the loop is seamless, and that
//  choice is the most valuable thing about a generated sustain — exporting the
//  audio without it hands over a file whose whole point has been discarded.
//  Nearly every sampler and editor reads `smpl`, and AVAudioFile will not write
//  one. It is also plain C++, which means the offline suite can round-trip it.
//

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace r50 {

namespace detail {

inline void appendU32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

inline void appendU16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

inline void appendTag(std::vector<uint8_t> &out, const char *tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
}

} // namespace detail

/// 24-bit mono PCM. 24-bit rather than float because every editor opens it, and
/// the generated content sits well inside +/-1 so nothing is lost.
///
/// `loopEnd` is exclusive here, as it is everywhere else in R50; the `smpl`
/// chunk wants the last *played* frame, so it goes out as loopEnd - 1. Getting
/// that wrong adds one sample to every loop, which is inaudible on a long
/// sustain and a click on a short one.
inline std::vector<uint8_t> encodeWav(const float *samples, int count,
                                      double sampleRate, int rootKey,
                                      uint32_t loopStart, uint32_t loopEnd,
                                      bool looped) {
    std::vector<uint8_t> out;
    if (samples == nullptr || count <= 0) return out;

    const uint32_t rate = static_cast<uint32_t>(std::lround(sampleRate));
    const uint32_t dataBytes = static_cast<uint32_t>(count) * 3u;
    const bool writeLoop = looped && loopEnd > loopStart + 1
                        && loopEnd <= static_cast<uint32_t>(count);
    const uint32_t smplBytes = writeLoop ? 60u : 0u;

    out.reserve(dataBytes + smplBytes + 64);
    detail::appendTag(out, "RIFF");
    // 4 ("WAVE") + fmt (8 + 16) + data (8 + n) + smpl (8 + 60 when present)
    detail::appendU32(out, 4 + 24 + 8 + dataBytes + (writeLoop ? 8 + smplBytes : 0)
                           + (dataBytes & 1u));
    detail::appendTag(out, "WAVE");

    detail::appendTag(out, "fmt ");
    detail::appendU32(out, 16);
    detail::appendU16(out, 1);                   // PCM
    detail::appendU16(out, 1);                   // mono
    detail::appendU32(out, rate);
    detail::appendU32(out, rate * 3);            // byte rate
    detail::appendU16(out, 3);                   // block align
    detail::appendU16(out, 24);                  // bits per sample

    detail::appendTag(out, "data");
    detail::appendU32(out, dataBytes);
    for (int n = 0; n < count; ++n) {
        float value = samples[n];
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        // 8388607 rather than 8388608: scaling by the latter makes +1.0 wrap to
        // the most negative value instead of the most positive.
        const int32_t quantised =
            static_cast<int32_t>(std::lround(value * 8388607.0f));
        out.push_back(static_cast<uint8_t>(quantised & 0xFF));
        out.push_back(static_cast<uint8_t>((quantised >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((quantised >> 16) & 0xFF));
    }
    if (dataBytes & 1u) out.push_back(0);        // chunks are word aligned

    if (writeLoop) {
        detail::appendTag(out, "smpl");
        detail::appendU32(out, smplBytes);
        detail::appendU32(out, 0);               // manufacturer
        detail::appendU32(out, 0);               // product
        detail::appendU32(out, static_cast<uint32_t>(
            std::lround(1.0e9 / std::max(1.0, sampleRate))));  // ns per frame
        detail::appendU32(out, static_cast<uint32_t>(rootKey));
        detail::appendU32(out, 0);               // pitch fraction
        detail::appendU32(out, 0);               // SMPTE format
        detail::appendU32(out, 0);               // SMPTE offset
        detail::appendU32(out, 1);               // one loop
        detail::appendU32(out, 0);               // sampler-specific bytes
        detail::appendU32(out, 0);               // cue id
        detail::appendU32(out, 0);               // forward
        detail::appendU32(out, loopStart);
        detail::appendU32(out, loopEnd - 1);     // inclusive, see above
        detail::appendU32(out, 0);               // fraction
        detail::appendU32(out, 0);               // play count: infinite
    }
    return out;
}

} // namespace r50
