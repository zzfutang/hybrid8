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

/// 32-bit float mono, and the format is not a preference. R50 matches its
/// generated content by RMS with a peak ceiling of 1.30, so 108 of the 129
/// factory zones legitimately peak above full scale — every integer format
/// clips them. Written as 24-bit these files came back 3.2% quiet with their
/// loudest moments flattened, which is not a rounding error but a different
/// sound. Float also means what you see in an editor is what the synth has,
/// with no gain compensation hidden in a manifest to remember.
///
/// `loopEnd` is exclusive here, as it is everywhere else in R50; the `smpl`
/// chunk wants the last *played* frame, so it goes out as loopEnd - 1. Getting
/// that wrong adds one sample to every loop, which is inaudible on a long
/// sustain and a click on a short one.
inline std::vector<uint8_t> encodeWav(const float *samples, int count,
                                      double sampleRate, int rootKey,
                                      uint32_t loopStart, uint32_t loopEnd,
                                      bool looped, bool pingPong = false) {
    std::vector<uint8_t> out;
    if (samples == nullptr || count <= 0) return out;

    const uint32_t rate = static_cast<uint32_t>(std::lround(sampleRate));
    const uint32_t dataBytes = static_cast<uint32_t>(count) * 4u;
    const bool writeLoop = looped && loopEnd > loopStart + 1
                        && loopEnd <= static_cast<uint32_t>(count);
    // The chunk goes out even with no loop in it, because it is also the only
    // place the root key travels. Writing it only for looped audio meant every
    // one-shot exported with no pitch at all, and a sample re-imported from the
    // drop-in directory had its root guessed by the pitch detector — on a 40 ms
    // transient, which is where detection is least able to help.
    const uint32_t smplBytes = writeLoop ? 60u : 36u;

    out.reserve(dataBytes + smplBytes + 64);
    detail::appendTag(out, "RIFF");
    // 4 ("WAVE") + fmt (8 + 16) + data (8 + n) + smpl (8 + 60 when present)
    // 4 bytes per sample, so the data chunk is always even and needs no pad.
    detail::appendU32(out, 4 + 24 + 8 + dataBytes + 8 + smplBytes);
    detail::appendTag(out, "WAVE");

    detail::appendTag(out, "fmt ");
    detail::appendU32(out, 16);
    detail::appendU16(out, 3);                   // IEEE float
    detail::appendU16(out, 1);                   // mono
    detail::appendU32(out, rate);
    detail::appendU32(out, rate * 4);            // byte rate
    detail::appendU16(out, 4);                   // block align
    detail::appendU16(out, 32);                  // bits per sample

    detail::appendTag(out, "data");
    detail::appendU32(out, dataBytes);
    for (int n = 0; n < count; ++n) {
        // No clamping: content above full scale is the reason this is float.
        uint32_t bits;
        std::memcpy(&bits, &samples[n], sizeof(bits));
        detail::appendU32(out, bits);
    }

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
    detail::appendU32(out, writeLoop ? 1u : 0u);   // loop count
    detail::appendU32(out, 0);               // sampler-specific bytes
    if (writeLoop) {
        detail::appendU32(out, 0);                       // cue id
        detail::appendU32(out, pingPong ? 1u : 0u);      // 0 fwd, 1 alternating
        detail::appendU32(out, loopStart);
        detail::appendU32(out, loopEnd - 1);             // inclusive, see above
        detail::appendU32(out, 0);                       // fraction
        detail::appendU32(out, 0);                       // play count: infinite
    }
    return out;
}

} // namespace r50
