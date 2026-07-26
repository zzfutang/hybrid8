//
//  R50SamplePlayer.hpp
//  Plays one region of one sample asset. Real-time safe: no allocation, no
//  locking, and the asset it reads is immutable.
//
//  The interesting part is tap wrapping. A 4-point interpolation kernel near
//  the loop end must fetch its trailing taps from just after the loop start,
//  not from whatever raw samples follow the loop — otherwise every loop pass
//  produces a small discontinuity that is easy to misdiagnose as a badly
//  chosen loop point. The taps *before* the loop start are subtler still: on
//  the first pass they should read the real attack audio preceding the loop,
//  but once the loop has wrapped, the true predecessor of loopStart is
//  loopEnd - 1. Both cases are handled below.
//

#pragma once

#include <cmath>

#include "R50Sample.hpp"
#include "Utils.hpp"

namespace r50 {

class SamplePlayer {
public:
    /// Begin playing. `startOffset` (0..1) scrubs into the asset, which for a
    /// looped sustain is a way past the attack and for a one-shot is a way to
    /// clip its front.
    void start(const SampleData *data, const SampleRegion *region,
               float startOffset) {
        data_    = data;
        gain_    = region ? std::pow(10.0f, region->gainDb / 20.0f) : 1.0f;
        wrapped_ = false;
        direction_ = 1;
        finished_  = (data == nullptr);
        if (data_ == nullptr) return;

        const double length = data_->length();
        position_ = synth::clampf(startOffset, 0.0f, 0.999f) * length;
        if (position_ >= length) position_ = 0.0;
    }

    void stop() { data_ = nullptr; finished_ = true; }

    /// Playback rate for a note, set once per control block.
    void setPlaybackRatio(double ratio, double outputSampleRate) {
        if (data_ == nullptr || outputSampleRate <= 0.0) return;
        increment_ = ratio * data_->sourceSampleRate / outputSampleRate;
        if (increment_ > 64.0) increment_ = 64.0;      // keep the reader sane
        if (increment_ < 0.0)  increment_ = 0.0;
    }

    bool isFinished() const { return finished_; }
    bool isActive() const { return data_ != nullptr && !finished_; }

    inline float process() {
        if (data_ == nullptr || finished_) return 0.0f;

        const float value = readInterpolated(position_) * gain_;
        advance();
        return value;
    }

private:
    inline void advance() {
        position_ += increment_ * direction_;

        const double loopStart = data_->loopStart;
        const double loopEnd   = data_->loopEnd;
        const double loopLength = loopEnd - loopStart;
        const double length = data_->length();

        switch (data_->loopMode) {
            case LoopMode::None:
                if (position_ >= length) {
                    position_ = length;
                    finished_ = true;
                }
                break;

            case LoopMode::Forward:
                if (loopLength <= 1.0) { finished_ = true; break; }
                while (position_ >= loopEnd) {
                    position_ -= loopLength;
                    wrapped_ = true;
                }
                break;

            case LoopMode::PingPong:
                if (loopLength <= 1.0) { finished_ = true; break; }
                wrapped_ = true;
                if (position_ >= loopEnd) {
                    position_ = loopEnd - (position_ - loopEnd);
                    direction_ = -1;
                } else if (position_ < loopStart) {
                    position_ = loopStart + (loopStart - position_);
                    direction_ = 1;
                }
                break;
        }
    }

    /// 4-point Catmull-Rom, matching the kernel used for the wave tables.
    inline float readInterpolated(double position) const {
        const int index = static_cast<int>(position);
        const float fraction = static_cast<float>(position - index);

        const float y0 = at(index - 1);
        const float y1 = at(index);
        const float y2 = at(index + 1);
        const float y3 = at(index + 2);

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
    }

    /// Fetch one sample, resolving an index that may fall outside the loop.
    inline float at(int index) const {
        const int length = data_->length();
        if (length == 0) return 0.0f;

        const int loopStart = static_cast<int>(data_->loopStart);
        const int loopEnd   = static_cast<int>(data_->loopEnd);
        const int loopLength = loopEnd - loopStart;

        switch (data_->loopMode) {
            case LoopMode::None:
                break;

            case LoopMode::Forward:
                if (loopLength > 1) {
                    if (index >= loopEnd) {
                        index = loopStart + (index - loopEnd) % loopLength;
                    } else if (index < loopStart && wrapped_) {
                        // Only once the loop has wrapped is loopEnd-1 the true
                        // predecessor of loopStart; before that the attack
                        // audio ahead of the loop is the correct source.
                        index = loopEnd - 1 - (loopStart - 1 - index) % loopLength;
                    }
                }
                break;

            case LoopMode::PingPong:
                if (loopLength > 1) {
                    // Mirror at both bounds; two passes cover an index that
                    // overshoots by more than one loop length.
                    for (int guard = 0; guard < 2; ++guard) {
                        if (index >= loopEnd)  index = (loopEnd - 1) - (index - (loopEnd - 1));
                        if (index < loopStart) index = loopStart + (loopStart - index);
                    }
                }
                break;
        }

        if (index < 0) index = 0;
        if (index >= length) index = length - 1;
        return data_->samples[index];
    }

    const SampleData *data_ = nullptr;
    double position_  = 0.0;
    double increment_ = 1.0;
    int    direction_ = 1;
    float  gain_      = 1.0f;
    bool   finished_  = true;
    /// True once the loop has wrapped at least once — see at().
    bool   wrapped_   = false;
};

} // namespace r50
