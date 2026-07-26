//
//  R50WaveOscillator.hpp
//  Plays a band-limited single-cycle wave from the shared library in
//  R50Wave.hpp. Real-time safe: no allocation, no locking, and the table set it
//  reads is immutable.
//

#pragma once

#include "R50Wave.hpp"
#include "Utils.hpp"

namespace r50 {

class WaveOscillator {
public:
    WaveOscillator() : library_(&waveLibrary()) {}

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        setFrequency(frequency_);
    }

    void reset(float phase = 0.0f) {
        phase_ = phase;
        wrapped_ = false;
    }

    void setWave(int index) {
        waveIndex_ = index < 0 ? 0 : (index >= kWaveCount ? kWaveCount - 1 : index);
    }

    void setWidth(float width) {
        width_ = synth::clampf(width, 0.02f, 0.98f);
    }

    void setFrequency(double hz) {
        frequency_ = hz;
        phaseInc_  = hz / sampleRate_;
        if (phaseInc_ >  0.49) phaseInc_ =  0.49;
        if (phaseInc_ < -0.49) phaseInc_ = -0.49;
        // Mip selection follows the fundamental, not the phase increment, so a
        // negative increment (reverse playback) still picks the right band.
        levelF_ = waveLevelForFreq(hz < 0.0 ? -hz : hz);
    }

    /// One sample, nominally in [-1, 1].
    inline float process() {
        const WaveDescriptor &descriptor = waveDescriptors()[waveIndex_];
        const WavePyramid &pyramid = library_->pyramids[descriptor.pyramid];

        float value;
        if (descriptor.read == WaveRead::Single) {
            value = waveSample(pyramid, levelF_, phase_);
        } else {
            const float width = descriptor.fixedWidth >= 0.0f
                              ? descriptor.fixedWidth : width_;
            double offsetPhase = phase_ - width;
            if (offsetPhase < 0.0) offsetPhase += 1.0;
            // saw(t) - saw(t - w) is a pulse of width w. Its mean is already
            // zero for any w, so no DC removal is needed; the gain restores unit
            // peak, which would otherwise approach 2 as the pulse narrows.
            const float gain = 0.5f / std::max(width, 1.0f - width);
            value = (waveSample(pyramid, levelF_, phase_)
                   - waveSample(pyramid, levelF_, offsetPhase)) * gain;
        }

        phase_ += phaseInc_;
        wrapped_ = false;
        if (phase_ >= 1.0)     { phase_ -= 1.0; wrapped_ = true; }
        else if (phase_ < 0.0) { phase_ += 1.0; wrapped_ = true; }
        return value;
    }

    /// True if the last process() crossed the cycle boundary. Unused today;
    /// needed by the PhaseSync tone structure.
    inline bool justWrapped() const { return wrapped_; }

    double frequency() const { return frequency_; }

private:
    const WaveLibrary *library_;
    double sampleRate_ = 44100.0;
    double frequency_  = 440.0;
    double phaseInc_   = 0.0;
    double phase_      = 0.0;
    float  levelF_     = 0.0f;
    float  width_      = 0.5f;
    int    waveIndex_  = 0;
    bool   wrapped_    = false;
};

} // namespace r50
