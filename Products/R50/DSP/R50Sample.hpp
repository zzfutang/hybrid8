//
//  R50Sample.hpp
//  Sample assets, key/velocity region mapping, and the process-wide library
//  that owns them.
//
//  This is the third PCM category, after phase 1's single-cycle tables: audio
//  that is longer than one cycle, and therefore able to beat, chorus, breathe
//  and decay in ways a periodic table cannot.
//
//  Threading. Assets are built or decoded on a loader thread and published
//  here; voices read them on the render thread. Publication is one-way and
//  append-only: an entry is filled completely, then the count is released, so
//  the render thread either does not see an entry at all or sees it finished.
//  Published entries are never mutated.
//
//  Lifetime. Assets are retained until the process exits. This mirrors the
//  choice already documented in Hybrid 8's WavetableStore ("DSP memory is
//  intentionally retained until process exit so an audio callback can never
//  observe freed table data"). Deferred-free needs voice reference counting,
//  which is not worth its risk while the library is bounded and small.
//
//  Deviation from the architecture document: loop points live on the asset
//  rather than on the region. Region-level loop overrides need per-region
//  editing UI that does not exist yet.
//

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace r50 {

enum class LoopMode { None = 0, Forward, PingPong };

// Sized well above what the factory content needs, because running out is
// silent: addSample returns -1, the region is skipped, and an instrument simply
// does not appear. Adding the nine Spectrum waves took the count to 129 against
// a limit of 128, and the casualty was the last attack in the list rather than
// anything to do with what had just been added.
static constexpr int kMaxSampleSlots = 256;
static constexpr int kMaxRegions     = 16;
static constexpr int kMaxInstruments = 96;
static constexpr int kInstrumentNameLength = 32;

/// One immutable block of audio. Mono: the voice is mono until the pan stage.
struct SampleData {
    std::vector<float> samples;
    double   sourceSampleRate = 44100.0;
    int      rootKey   = 60;
    uint32_t loopStart = 0;
    uint32_t loopEnd   = 0;    // exclusive
    LoopMode loopMode  = LoopMode::None;

    int length() const { return static_cast<int>(samples.size()); }
};

/// A key/velocity zone pointing at one asset.
struct SampleRegion {
    int   lowKey      = 0,   highKey      = 127;
    int   lowVelocity = 1,   highVelocity = 127;
    int   rootKey     = 60;      // overrides the asset's root
    float tuneCents   = 0.0f;
    float gainDb      = 0.0f;
    int   slot        = -1;
};

/// A playable instrument: a bounded set of regions. Fixed capacity — nothing
/// the render thread walks may allocate.
struct Multisample {
    SampleRegion regions[kMaxRegions];
    int          regionCount = 0;
    char         name[kInstrumentNameLength] = {0};

    /// First region covering this key and velocity, or null. A linear scan is
    /// bounded by kMaxRegions and runs once per note-on, not per sample.
    const SampleRegion *find(int key, int velocity) const {
        for (int i = 0; i < regionCount; ++i) {
            const SampleRegion &region = regions[i];
            if (key >= region.lowKey && key <= region.highKey
             && velocity >= region.lowVelocity && velocity <= region.highVelocity) {
                return &regions[i];
            }
        }
        return nullptr;
    }

    void setName(const char *text) {
        std::strncpy(name, text, kInstrumentNameLength - 1);
        name[kInstrumentNameLength - 1] = '\0';
    }
};

/// Process-wide store of assets and instruments, shared by every voice and
/// every engine instance so a sample is decoded and held once.
class SampleLibrary {
public:
    static SampleLibrary &shared();

    // ---- Loader thread only ----------------------------------------------

    /// Take ownership of an asset and publish it. Returns its slot, or -1 when
    /// the library is full.
    int addSample(SampleData &&data) {
        const int slot = slotCount_.load(std::memory_order_relaxed);
        if (slot >= kMaxSampleSlots || data.samples.empty()) return -1;

        // Heap-allocated and never freed; see the lifetime note above.
        SampleData *published = new SampleData(std::move(data));
        if (published->loopEnd == 0
         || published->loopEnd > static_cast<uint32_t>(published->length())) {
            published->loopEnd = static_cast<uint32_t>(published->length());
        }
        slots_[slot].store(published, std::memory_order_release);
        slotCount_.store(slot + 1, std::memory_order_release);
        return slot;
    }

    /// Publish an instrument. Fill it completely before calling: the release
    /// of the count is what makes it visible to the render thread.
    int addInstrument(const Multisample &instrument) {
        const int index = instrumentCount_.load(std::memory_order_relaxed);
        if (index >= kMaxInstruments || instrument.regionCount <= 0) return -1;
        instruments_[index] = instrument;
        instrumentCount_.store(index + 1, std::memory_order_release);
        return index;
    }

    // ---- Any thread -------------------------------------------------------

    const SampleData *sample(int slot) const {
        if (slot < 0 || slot >= kMaxSampleSlots) return nullptr;
        return slots_[slot].load(std::memory_order_acquire);
    }

    const Multisample *instrument(int index) const {
        if (index < 0 || index >= instrumentCount_.load(std::memory_order_acquire))
            return nullptr;
        return &instruments_[index];
    }

    int instrumentCount() const {
        return instrumentCount_.load(std::memory_order_acquire);
    }

private:
    SampleLibrary();   // builds the generated factory content

    std::atomic<const SampleData *> slots_[kMaxSampleSlots] = {};
    std::atomic<int> slotCount_{0};

    Multisample      instruments_[kMaxInstruments];
    std::atomic<int> instrumentCount_{0};
};

} // namespace r50
