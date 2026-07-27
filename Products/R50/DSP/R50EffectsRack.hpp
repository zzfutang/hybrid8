//
//  R50EffectsRack.hpp
//  Stereo compressor, chorus, ping-pong delay and FDN reverb, applied to the
//  voice sum.
//
//  Effects matter disproportionately for this instrument. A dry PCM
//  multisample sounds thin, and much of what makes a workstation patch is the
//  processing rather than the source material — the architecture document says
//  as much, and R50's generated pads are the driest thing in it.
//
//  This is a copy of Products/Hybrid8/DSP/Hybrid8EffectsRack.hpp, namespaced to
//  r50. Copying rather than promoting to Shared/ keeps Hybrid 8's build inputs
//  untouched; the two are now candidates for a shared home, which is a change
//  worth making deliberately rather than as a side effect of adding effects
//  here.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Utils.hpp"
#include "R50Oversampling.hpp"

namespace r50 {

// The rack was written against Shared/DSPCore's helpers as unqualified names.
// Pulling in just those keeps the copy a faithful one, and avoids dragging the
// whole of namespace synth in behind it.
using synth::clampf;
using synth::kPi;
using synth::kTwoPi;


struct StereoSample {
    float l = 0.0f;
    float r = 0.0f;
};

inline StereoSample equalPowerMix(float dryL, float dryR,
                                  float wetL, float wetR, float mix) {
    const float amount = clampf(mix, 0.0f, 1.0f);
    const float dryGain = std::cos(amount * static_cast<float>(kPi * 0.5));
    const float wetGain = std::sin(amount * static_cast<float>(kPi * 0.5));
    return {dryL * dryGain + wetL * wetGain,
            dryR * dryGain + wetR * wetGain};
}

// Feed-forward peak compressor with a 6 dB soft knee. A single linked gain
// computer drives both channels, preserving stereo position under compression.
class StereoCompressor {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        paramCoef_ = static_cast<float>(std::exp(-1.0 / (0.020 * sampleRate)));
        bypassCoef_ = static_cast<float>(std::exp(-1.0 / (0.008 * sampleRate)));
        reset();
    }

    void reset() {
        gain_ = 1.0f;
        bypassMix_ = enabledTarget_;
        threshold_ = thresholdTarget_;
        ratio_ = ratioTarget_;
        attack_ = attackTarget_;
        release_ = releaseTarget_;
        makeup_ = makeupTarget_;
    }

    void setParams(float enabled, float thresholdDb, float ratio,
                   float attackSeconds, float releaseSeconds, float makeupDb) {
        enabledTarget_ = enabled >= 0.5f ? 1.0f : 0.0f;
        thresholdTarget_ = clampf(thresholdDb, -36.0f, 0.0f);
        ratioTarget_ = clampf(ratio, 1.0f, 20.0f);
        attackTarget_ = clampf(attackSeconds, 0.001f, 0.1f);
        releaseTarget_ = clampf(releaseSeconds, 0.02f, 1.0f);
        makeupTarget_ = clampf(makeupDb, 0.0f, 18.0f);
    }

    inline StereoSample process(float inputL, float inputR) {
        smoothParams();
        const float detector = std::max(std::fabs(inputL), std::fabs(inputR));
        const float levelDb = 20.0f * std::log10(std::max(detector, 1.0e-9f));
        const float over = levelDb - threshold_;
        constexpr float halfKnee = 3.0f;
        float reductionDb = 0.0f;
        const float slope = 1.0f - 1.0f / ratio_;
        if (over > halfKnee) {
            reductionDb = slope * over;
        } else if (over > -halfKnee) {
            const float kneePosition = over + halfKnee;
            reductionDb = slope * kneePosition * kneePosition / 12.0f;
        }

        const float targetGain = std::pow(10.0f,
            (makeup_ - reductionDb) / 20.0f);
        const float time = targetGain < gain_ ? attack_ : release_;
        const float coef = static_cast<float>(
            std::exp(-1.0 / (time * sampleRate_)));
        gain_ = targetGain + (gain_ - targetGain) * coef;

        // Click-free selectable bypass. There is no lookahead/delay, so this
        // interpolation is phase coherent with the dry signal.
        bypassMix_ = enabledTarget_
                   + (bypassMix_ - enabledTarget_) * bypassCoef_;
        const float appliedGain = 1.0f + (gain_ - 1.0f) * bypassMix_;
        return {inputL * appliedGain, inputR * appliedGain};
    }

    float gainReductionDb() const {
        // Remove makeup from the displayed gain so the meter reports only
        // attenuation performed by the compressor. The detector keeps running
        // even while bypassed, so scale by bypassMix_ — the same factor that
        // gates the audio — so the meter reads 0 when the compressor is off.
        const float reduction = std::max(0.0f,
            -20.0f * std::log10(std::max(gain_, 1.0e-9f)) + makeup_);
        return reduction * bypassMix_;
    }

private:
    inline void smoothParams() {
        threshold_ = thresholdTarget_
                   + (threshold_ - thresholdTarget_) * paramCoef_;
        ratio_ = ratioTarget_ + (ratio_ - ratioTarget_) * paramCoef_;
        attack_ = attackTarget_ + (attack_ - attackTarget_) * paramCoef_;
        release_ = releaseTarget_ + (release_ - releaseTarget_) * paramCoef_;
        makeup_ = makeupTarget_ + (makeup_ - makeupTarget_) * paramCoef_;
    }

    double sampleRate_ = 44100.0;
    float gain_ = 1.0f, bypassMix_ = 0.0f;
    float threshold_ = -18.0f, ratio_ = 4.0f;
    float attack_ = 0.010f, release_ = 0.120f, makeup_ = 0.0f;
    float enabledTarget_ = 0.0f, thresholdTarget_ = -18.0f;
    float ratioTarget_ = 4.0f, attackTarget_ = 0.010f;
    float releaseTarget_ = 0.120f, makeupTarget_ = 0.0f;
    float paramCoef_ = 0.0f, bypassCoef_ = 0.0f;
};

// Circular delay line with four-point cubic Hermite interpolation.
class FractionalDelayLine {
public:
    void setup(int samples) {
        buffer_.assign(std::max(8, samples), 0.0f);
        write_ = 0;
    }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    inline void write(float x) {
        buffer_[write_] = x;
        if (++write_ == static_cast<int>(buffer_.size())) write_ = 0;
    }

    inline float read(double delaySamples) const {
        const int size = static_cast<int>(buffer_.size());
        double pos = static_cast<double>(write_) - delaySamples;
        while (pos < 0.0) pos += size;
        while (pos >= size) pos -= size;

        int i1 = static_cast<int>(std::floor(pos));
        float t = static_cast<float>(pos - i1);
        int i0 = (i1 + size - 1) % size;
        int i2 = (i1 + 1) % size;
        int i3 = (i1 + 2) % size;
        float y0 = buffer_[i0], y1 = buffer_[i1];
        float y2 = buffer_[i2], y3 = buffer_[i3];

        // Catmull-Rom form of cubic Hermite interpolation.
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

private:
    std::vector<float> buffer_;
    int write_ = 0;
};

class StereoChorus {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        left_.setup(static_cast<int>(sampleRate * 0.060) + 8);
        right_.setup(static_cast<int>(sampleRate * 0.060) + 8);
        smoothCoef_ = std::exp(-1.0 / (0.010 * sampleRate));
        reset();
    }

    void reset() {
        left_.reset();
        right_.reset();
        phase_ = 0.0;
        driftPhase_ = 0.173;
        mix_ = mixTarget_;
        rate_ = rateTarget_;
        depth_ = depthTarget_;
    }

    void setParams(float mix, float rateHz, float depth) {
        mixTarget_ = clampf(mix, 0.0f, 1.0f);
        rateTarget_ = clampf(rateHz, 0.03f, 15.0f);
        depthTarget_ = clampf(depth, 0.0f, 1.0f);
    }

    inline StereoSample process(float inputL, float inputR) {
        const StereoSample wet = processWet(inputL, inputR);
        return equalPowerMix(inputL, inputR, wet.l, wet.r, mix_);
    }

    inline StereoSample processWet(float inputL, float inputR) {
        smooth();
        left_.write(inputL);
        right_.write(inputR);

        // Four independently moving voices. The secondary very-slow oscillator
        // prevents the main sweep from sounding perfectly repetitive.
        static constexpr double phases[4] = {0.0, 0.25, 0.5, 0.75};
        static constexpr double driftOffsets[4] = {0.11, 0.43, 0.71, 0.89};
        float tapsL[2], tapsR[2];
        const double base = 0.017 * sampleRate_;
        const double span = (0.0005 + 0.0045 * depth_) * sampleRate_;
        for (int i = 0; i < 2; ++i) {
            const int leftVoice = i * 2;
            const int rightVoice = leftVoice + 1;
            double mod = std::sin(kTwoPi * (phase_ + phases[leftVoice]));
            double drift = 0.22 * std::sin(
                kTwoPi * (driftPhase_ + driftOffsets[leftVoice]));
            tapsL[i] = left_.read(base + span * (mod + drift));

            mod = std::sin(kTwoPi * (phase_ + phases[rightVoice]));
            drift = 0.22 * std::sin(
                kTwoPi * (driftPhase_ + driftOffsets[rightVoice]));
            tapsR[i] = right_.read(base + span * (mod + drift));
        }

        phase_ += rate_ / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        driftPhase_ += 0.071 / sampleRate_;
        if (driftPhase_ >= 1.0) driftPhase_ -= 1.0;

        // Cross-distribute the voices for width without polarity tricks.
        float wetL = 0.5f * (tapsL[0] + tapsL[1]);
        float wetR = 0.5f * (tapsR[0] + tapsR[1]);
        return {wetL, wetR};
    }

private:
    inline void smooth() {
        mix_ = mixTarget_ + (mix_ - mixTarget_) * smoothCoef_;
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
    }

    FractionalDelayLine left_, right_;
    double sampleRate_ = 44100.0;
    double phase_ = 0.0, driftPhase_ = 0.0;
    float mix_ = 0.0f, rate_ = 0.35f, depth_ = 0.35f;
    float mixTarget_ = 0.0f, rateTarget_ = 0.35f, depthTarget_ = 0.35f;
    float smoothCoef_ = 0.0f;
};

class StereoDelay {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        const int maxSamples = static_cast<int>(sampleRate * 12.1) + 8;
        left_.setup(maxSamples);
        right_.setup(maxSamples);
        smoothCoef_ = std::exp(-1.0 / (0.012 * sampleRate));
        crossfadeInc_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.030));
        reset();
    }

    void reset() {
        left_.reset();
        right_.reset();
        lpL_ = lpR_ = hpLpL_ = hpLpR_ = 0.0f;
        delayA_ = requestedDelay_;
        delayB_ = delayA_;
        crossfade_ = 1.0f;
        mix_ = mixTarget_;
        feedback_ = feedbackTarget_;
        tone_ = toneTarget_;
        pingPong_ = pingPongTarget_;
    }

    void setParams(float mix, float timeSeconds, float feedback,
                   float tone, float pingPong) {
        mixTarget_ = clampf(mix, 0.0f, 1.0f);
        requestedDelay_ = clampf(timeSeconds, 0.02f, 12.0f)
                        * static_cast<float>(sampleRate_);
        feedbackTarget_ = clampf(feedback, 0.0f, 0.94f);
        toneTarget_ = clampf(tone, 0.0f, 1.0f);
        pingPongTarget_ = clampf(pingPong, 0.0f, 1.0f);
    }

    inline StereoSample process(float inputL, float inputR) {
        const StereoSample wet = processWet(inputL, inputR);
        return equalPowerMix(inputL, inputR, wet.l, wet.r, mix_);
    }

    inline StereoSample processWet(float inputL, float inputR) {
        smooth();
        if (crossfade_ >= 1.0f && std::fabs(requestedDelay_ - delayA_) > 0.5f) {
            delayB_ = requestedDelay_;
            crossfade_ = 0.0f;
        }

        float oldL = left_.read(delayA_);
        float oldR = right_.read(delayA_);
        float delayedL = oldL, delayedR = oldR;
        if (crossfade_ < 1.0f) {
            float newL = left_.read(delayB_);
            float newR = right_.read(delayB_);
            // Equal-power heads avoid a dip when unrelated echo material is mixed.
            float a = std::cos(crossfade_ * static_cast<float>(kPi * 0.5));
            float b = std::sin(crossfade_ * static_cast<float>(kPi * 0.5));
            delayedL = oldL * a + newL * b;
            delayedR = oldR * a + newR * b;
            crossfade_ = std::min(1.0f, crossfade_ + crossfadeInc_);
            if (crossfade_ >= 1.0f) delayA_ = delayB_;
        }

        float filteredL = feedbackFilter(delayedL, lpL_, hpLpL_);
        float filteredR = feedbackFilter(delayedR, lpR_, hpLpR_);
        float returnL = filteredL + (filteredR - filteredL) * pingPong_;
        float returnR = filteredR + (filteredL - filteredR) * pingPong_;
        left_.write(inputL + std::tanh(returnL * 1.15f) * feedback_);
        right_.write(inputR + std::tanh(returnR * 1.15f) * feedback_);

        return {delayedL, delayedR};
    }

private:
    inline float feedbackFilter(float x, float& lp, float& hpLp) {
        const double cutoff = 1200.0 * std::pow(13.333333, tone_);
        const float lpCoef = static_cast<float>(1.0 - std::exp(-kTwoPi * cutoff / sampleRate_));
        const float hpCoef = static_cast<float>(1.0 - std::exp(-kTwoPi * 35.0 / sampleRate_));
        lp += lpCoef * (x - lp);
        hpLp += hpCoef * (lp - hpLp);
        return lp - hpLp;
    }

    inline void smooth() {
        mix_ = mixTarget_ + (mix_ - mixTarget_) * smoothCoef_;
        feedback_ = feedbackTarget_ + (feedback_ - feedbackTarget_) * smoothCoef_;
        tone_ = toneTarget_ + (tone_ - toneTarget_) * smoothCoef_;
        pingPong_ = pingPongTarget_ + (pingPong_ - pingPongTarget_) * smoothCoef_;
    }

    FractionalDelayLine left_, right_;
    double sampleRate_ = 44100.0;
    float delayA_ = 11025.0f, delayB_ = 11025.0f, requestedDelay_ = 11025.0f;
    float crossfade_ = 1.0f, crossfadeInc_ = 0.001f;
    float lpL_ = 0.0f, lpR_ = 0.0f, hpLpL_ = 0.0f, hpLpR_ = 0.0f;
    float mix_ = 0.0f, feedback_ = 0.35f, tone_ = 0.65f, pingPong_ = 1.0f;
    float mixTarget_ = 0.0f, feedbackTarget_ = 0.35f;
    float toneTarget_ = 0.65f, pingPongTarget_ = 1.0f;
    float smoothCoef_ = 0.0f;
};

// Eight mutually-prime delay lines connected by an energy-preserving Hadamard
// matrix. Per-line decay compensation gives a consistent RT60 as room size
// changes; slow, decorrelated modulation suppresses static metallic modes.
class StereoReverb {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        preDelay_.setup(static_cast<int>(sampleRate * 0.205) + 8);
        for (auto& line : lines_)
            line.setup(static_cast<int>(sampleRate * 0.18) + 8);
        smoothCoef_ = std::exp(-1.0 / (0.030 * sampleRate));
        reset();
    }

    void reset() {
        preDelay_.reset();
        for (auto& line : lines_) line.reset();
        damping_.fill(0.0f);
        phase_ = 0.0;
        mix_ = mixTarget_;
        size_ = sizeTarget_;
        decay_ = decayTarget_;
        tone_ = toneTarget_;
        preDelaySamples_ = preDelayTarget_ * static_cast<float>(sampleRate_);
    }

    void setParams(float mix, float size, float decaySeconds,
                   float tone, float preDelaySeconds) {
        mixTarget_ = clampf(mix, 0.0f, 1.0f);
        sizeTarget_ = clampf(size, 0.0f, 1.0f);
        decayTarget_ = clampf(decaySeconds, 0.2f, 12.0f);
        toneTarget_ = clampf(tone, 0.0f, 1.0f);
        preDelayTarget_ = clampf(preDelaySeconds, 0.0f, 0.2f);
    }

    inline StereoSample process(float inputL, float inputR) {
        const StereoSample wet = processWet(inputL, inputR);
        return equalPowerMix(inputL, inputR, wet.l, wet.r, mix_);
    }

    inline StereoSample processWet(float inputL, float inputR) {
        smooth();

        // Mono-compatible injection, with a little side information retained.
        const float mid = 0.5f * (inputL + inputR);
        const float side = 0.5f * (inputL - inputR);
        preDelay_.write(mid);
        const float pred = preDelay_.read(std::max(1.0f, preDelaySamples_));

        static constexpr float baseMs[8] =
            {31.13f, 37.11f, 41.73f, 47.17f, 53.09f, 59.33f, 67.07f, 73.21f};
        static constexpr float phaseOffset[8] =
            {0.03f, 0.19f, 0.31f, 0.47f, 0.58f, 0.71f, 0.83f, 0.94f};
        float tap[8];
        const float scale = 0.62f + 1.15f * size_;
        for (int i = 0; i < 8; ++i) {
            const float modulation = static_cast<float>(
                std::sin(kTwoPi * (phase_ + phaseOffset[i])));
            const float samples = (baseMs[i] * 0.001f * scale
                                  + modulation * 0.00037f) *
                                  static_cast<float>(sampleRate_);
            tap[i] = lines_[i].read(samples);
        }

        // In-place normalized Walsh-Hadamard transform: orthogonal feedback
        // redistributes energy without changing its total gain.
        float feedback[8];
        for (int i = 0; i < 8; ++i) feedback[i] = tap[i];
        for (int span = 1; span < 8; span <<= 1) {
            for (int base = 0; base < 8; base += span << 1) {
                for (int j = 0; j < span; ++j) {
                    const float a = feedback[base + j];
                    const float b = feedback[base + j + span];
                    feedback[base + j] = a + b;
                    feedback[base + j + span] = a - b;
                }
            }
        }

        const float dampingHz = 1200.0f * std::pow(13.333333f, tone_);
        const float dampingCoef = static_cast<float>(
            1.0 - std::exp(-kTwoPi * dampingHz / sampleRate_));
        static constexpr float injectionSign[8] =
            {1, -1, 1, 1, -1, 1, -1, -1};
        for (int i = 0; i < 8; ++i) {
            damping_[i] += dampingCoef * (feedback[i] * 0.35355339f
                                         - damping_[i]);
            const float delaySeconds = baseMs[i] * 0.001f * scale;
            const float rt60Gain = std::pow(10.0f,
                                            -3.0f * delaySeconds / decay_);
            const float injection = pred * injectionSign[i] * 0.22f
                                  + side * (i < 4 ? 0.08f : -0.08f);
            lines_[i].write(injection + damping_[i] * rt60Gain);
        }

        phase_ += 0.083 / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        // Different orthogonal projections create a stable, decorrelated
        // stereo return without phase-inverting the dry signal.
        const float wetL = (tap[0] + tap[1] - tap[2] + tap[3]
                          - tap[4] - tap[5] + tap[6] - tap[7]) * 0.35355339f;
        const float wetR = (-tap[0] + tap[1] + tap[2] + tap[3]
                          + tap[4] - tap[5] - tap[6] - tap[7]) * 0.35355339f;
        return {wetL, wetR};
    }

private:
    inline void smooth() {
        mix_ = mixTarget_ + (mix_ - mixTarget_) * smoothCoef_;
        size_ = sizeTarget_ + (size_ - sizeTarget_) * smoothCoef_;
        decay_ = decayTarget_ + (decay_ - decayTarget_) * smoothCoef_;
        tone_ = toneTarget_ + (tone_ - toneTarget_) * smoothCoef_;
        const float target = preDelayTarget_ * static_cast<float>(sampleRate_);
        preDelaySamples_ = target + (preDelaySamples_ - target) * smoothCoef_;
    }

    FractionalDelayLine preDelay_;
    std::array<FractionalDelayLine, 8> lines_;
    std::array<float, 8> damping_{};
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float mix_ = 0.0f, size_ = 0.55f, decay_ = 2.4f, tone_ = 0.55f;
    float mixTarget_ = 0.0f, sizeTarget_ = 0.55f;
    float decayTarget_ = 2.4f, toneTarget_ = 0.55f;
    float preDelaySamples_ = 0.0f, preDelayTarget_ = 0.015f;
    float smoothCoef_ = 0.0f;
};

#include "R50ModulationEffects.hpp"

enum class EffectAlgorithm {
    Off = 0,
    HallReverb,
    RoomReverb,
    PlateStageReverb,
    EarlyReflections,
    StereoDelay,
    CrossDelay,
    Chorus,
    Ensemble,
    Flanger,
    Phaser,
    TremoloAutoPan,
    RotarySpeaker,
    Equalizer,
    Overdrive,
    Distortion,
    Exciter
};

static constexpr int kEffectAlgorithmCount = 17;
static constexpr int kEffectSlotCount = 3;

enum class EffectTopology {
    Serial = 0,
    Parallel,
    SerialPairParallel,
    ParallelPairMaster
};

static constexpr int kEffectTopologyCount = 4;

struct EffectSlotDescriptor {
    EffectAlgorithm algorithm = EffectAlgorithm::Off;
    bool bypass = false;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    float mix = 1.0f;
    float width = 1.0f;
    float control[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    int mode[2] = {0, 0};
};

struct EffectRackInput {
    StereoSample dry{};
    StereoSample send[kEffectSlotCount]{};
};

class EffectSlot {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate)));
        transitionStep_ = 1.0f / static_cast<float>(
            std::max(1.0, sampleRate * 0.020));
        chorus_.setup(sampleRate);
        delay_.setup(sampleRate);
        reverb_.setup(sampleRate);
        ensemble_.setup(sampleRate);
        flanger_.setup(sampleRate);
        phaser_.setup(sampleRate);
        tremolo_.setup(sampleRate);
        rotary_.setup(sampleRate);
        early_.setup(sampleRate);
        modernDelay_.setup(sampleRate);
        toneEffect_.setup(sampleRate);
        reset();
    }

    void reset() {
        chorus_.reset();
        delay_.reset();
        reverb_.reset();
        ensemble_.reset();
        flanger_.reset();
        phaser_.reset();
        tremolo_.reset();
        rotary_.reset();
        early_.reset();
        modernDelay_.reset();
        toneEffect_.reset();
        inputGain_ = descriptor_.inputGain;
        outputGain_ = descriptor_.outputGain;
        mix_ = descriptor_.mix;
        width_ = descriptor_.width;
        enabled_ = isEnabled(descriptor_) ? 1.0f : 0.0f;
        transition_ = 1.0f;
        lastSerial_ = {};
        lastParallel_ = {};
    }

    void setDescriptor(const EffectSlotDescriptor &descriptor) {
        if (descriptor.algorithm != descriptor_.algorithm) {
            transition_ = 0.0f;
            serialAnchor_ = lastSerial_;
            parallelAnchor_ = lastParallel_;
        }
        descriptor_ = descriptor;
        const float c1 = clampf(descriptor.control[0], 0.0f, 1.0f);
        const float c2 = clampf(descriptor.control[1], 0.0f, 1.0f);
        const float c3 = clampf(descriptor.control[2], 0.0f, 1.0f);
        const float c4 = clampf(descriptor.control[3], 0.0f, 1.0f);
        const float c5 = clampf(descriptor.control[4], 0.0f, 1.0f);
        const float c6 = clampf(descriptor.control[5], 0.0f, 1.0f);
        const float c7 = clampf(descriptor.control[6], 0.0f, 1.0f);
        const float c8 = clampf(descriptor.control[7], 0.0f, 1.0f);
        chorus_.setParams(0.0f, 0.05f * std::pow(160.0f, c1), c2);
        delay_.setParams(0.0f, 0.02f * std::pow(100.0f, c1),
                         c2 * 0.94f, c3,
                         descriptor.algorithm == EffectAlgorithm::CrossDelay
                             ? 1.0f : static_cast<float>(descriptor.mode[0] != 0));
        if (descriptor.algorithm == EffectAlgorithm::RoomReverb)
            reverb_.setParams(0.0f, c3, 0.15f * std::pow(26.6667f, c2),
                              c4, c1 * 0.08f);
        else if (descriptor.algorithm == EffectAlgorithm::PlateStageReverb)
            reverb_.setParams(0.0f,
                              descriptor.mode[0] == 0 ? c3 : c3 * 0.72f,
                              0.3f * std::pow(40.0f, c2),
                              descriptor.mode[0] == 0
                                  ? clampf(c4 + 0.18f, 0.0f, 1.0f)
                                  : clampf(c4 - 0.12f, 0.0f, 1.0f),
                              c1 * 0.2f);
        else
            reverb_.setParams(0.0f, c3, 0.4f * std::pow(30.0f, c2),
                              c4, c1 * 0.2f);
        ensemble_.setParams(0.03f * std::pow(100.0f, c1), c2, c3, c4,
                            1000.0f * std::pow(20.0f, c5),
                            20.0f * std::pow(25.0f, c6));
        flanger_.setParams(0.03f * std::pow(333.333f, c1), c2,
                           0.1f * std::pow(150.0f, c3), c4 * 1.9f - 0.95f,
                           descriptor.mode[0] != 0, descriptor.mode[1] != 0,
                           c5 * 0.5f, 1000.0f * std::pow(20.0f, c6));
        phaser_.setParams(0.03f * std::pow(333.333f, c1), c2,
                          80.0f * std::pow(50.0f, c3), 0.5f + 5.5f * c4,
                          c5 * 1.9f - 0.95f,
                          descriptor.mode[0] != 0 ? 12 : 6,
                          descriptor.mode[1] != 0, c8 * 0.5f);
        tremolo_.setParams(0.03f * std::pow(666.667f, c1), c2,
                           c3 * 2.0f - 1.0f, c4 * 0.5f,
                           c5 * 2.0f - 1.0f, descriptor.mode[0]);
        rotary_.setParams(descriptor.mode[0],
                          0.2f * std::pow(7.5f, c1),
                          3.0f * std::pow(3.333333f, c2),
                          0.5f * std::pow(4.0f, c3),
                          0.2f * std::pow(40.0f, c4), c5, c6,
                          400.0f * std::pow(5.0f, c7));
        early_.setParams(c1 * 0.2f, 0.03f * std::pow(26.6667f, c2), c3,
                         c4 * 2.0f - 1.0f,
                         500.0f * std::pow(40.0f, c5), c6 * 2.0f,
                         descriptor.mode[0]);
        modernDelay_.setParams(0.001f * std::pow(2000.0f, c1),
                               0.001f * std::pow(2000.0f, c2),
                               c3 * 1.9f - 0.95f,
                               descriptor.algorithm == EffectAlgorithm::CrossDelay
                                   ? c4 : 0.0f,
                               20.0f * std::pow(100.0f, c5),
                               500.0f * std::pow(40.0f, c6), c7);
        if (descriptor.algorithm >= EffectAlgorithm::Equalizer
            && descriptor.algorithm <= EffectAlgorithm::Exciter) {
            const auto kind = static_cast<ToneAndNonlinearEffect::Kind>(
                static_cast<int>(descriptor.algorithm)
                - static_cast<int>(EffectAlgorithm::Equalizer));
            toneEffect_.setParams(kind, descriptor.control, descriptor.mode[0]);
        }
    }

    inline StereoSample processSerial(StereoSample input) {
        // A zero return is an exact bypass contract, including the first
        // render quantum after selecting an algorithm. Do not let the mix or
        // algorithm-transition smoothers briefly colour that dry signal.
        if (descriptor_.mix <= 0.0f) {
            smooth();
            (void)processWet(input);
            transition_ = 1.0f;
            lastSerial_ = input;
            return input;
        }
        smooth();
        StereoSample wet = processWet(input);
        const float dryGain = std::cos(mix_ * 0.5f * static_cast<float>(synth::kPi));
        const float wetGain = std::sin(mix_ * 0.5f * static_cast<float>(synth::kPi));
        StereoSample effected = {input.l * dryGain + wet.l * wetGain,
                                 input.r * dryGain + wet.r * wetGain};
        StereoSample output = {input.l + (effected.l - input.l) * enabled_,
                               input.r + (effected.r - input.r) * enabled_};
        output = applyTransition(output, serialAnchor_);
        lastSerial_ = output;
        return output;
    }

    inline StereoSample processParallel(StereoSample input) {
        if (descriptor_.mix <= 0.0f) {
            smooth();
            (void)processWet(input);
            transition_ = 1.0f;
            lastParallel_ = {};
            return {};
        }
        smooth();
        StereoSample wet = processWet(input);
        StereoSample output = {wet.l * mix_ * enabled_, wet.r * mix_ * enabled_};
        output = applyTransition(output, parallelAnchor_);
        lastParallel_ = output;
        return output;
    }

    float tailSeconds() const {
        switch (descriptor_.algorithm) {
            case EffectAlgorithm::StereoDelay:
            case EffectAlgorithm::CrossDelay: return 12.0f;
            case EffectAlgorithm::HallReverb: return 12.0f;
            case EffectAlgorithm::RoomReverb: return 4.0f;
            case EffectAlgorithm::PlateStageReverb: return 12.0f;
            case EffectAlgorithm::EarlyReflections: return 1.0f;
            case EffectAlgorithm::Chorus: return 0.060f;
            case EffectAlgorithm::Ensemble: return 0.060f;
            case EffectAlgorithm::Flanger: return 0.020f;
            case EffectAlgorithm::RotarySpeaker: return 0.010f;
            default: return 0.0f;
        }
    }
    int latencySamples() const { return 0; }

private:
    static bool isEnabled(const EffectSlotDescriptor &descriptor) {
        return !descriptor.bypass
            && descriptor.algorithm != EffectAlgorithm::Off;
    }

    inline void smooth() {
        inputGain_ = descriptor_.inputGain
                   + (inputGain_ - descriptor_.inputGain) * smoothCoef_;
        outputGain_ = descriptor_.outputGain
                    + (outputGain_ - descriptor_.outputGain) * smoothCoef_;
        mix_ = descriptor_.mix + (mix_ - descriptor_.mix) * smoothCoef_;
        width_ = descriptor_.width + (width_ - descriptor_.width) * smoothCoef_;
        const float enabledTarget = isEnabled(descriptor_) ? 1.0f : 0.0f;
        enabled_ = enabledTarget + (enabled_ - enabledTarget) * smoothCoef_;
    }

    inline StereoSample processWet(StereoSample input) {
        // Hostile/non-finite host buffers must not poison recursive delay,
        // filter, or oversampling state for all subsequent renders.
        const float safeL = std::isfinite(input.l) ? input.l : 0.0f;
        const float safeR = std::isfinite(input.r) ? input.r : 0.0f;
        float left = safeL * inputGain_;
        float right = safeR * inputGain_;
        StereoSample wet;
        switch (descriptor_.algorithm) {
            case EffectAlgorithm::Chorus:
                wet = chorus_.processWet(left, right);
                break;
            case EffectAlgorithm::StereoDelay:
            case EffectAlgorithm::CrossDelay:
                wet = modernDelay_.processWet(left, right);
                break;
            case EffectAlgorithm::HallReverb:
                wet = reverb_.processWet(left, right);
                break;
            case EffectAlgorithm::RoomReverb:
            case EffectAlgorithm::PlateStageReverb:
                wet = reverb_.processWet(left, right);
                break;
            case EffectAlgorithm::EarlyReflections:
                wet = early_.processWet(left, right);
                break;
            case EffectAlgorithm::Ensemble:
                wet = ensemble_.processWet(left, right);
                break;
            case EffectAlgorithm::Flanger:
                wet = flanger_.processWet(left, right);
                break;
            case EffectAlgorithm::Phaser:
                wet = phaser_.processWet(left, right);
                break;
            case EffectAlgorithm::TremoloAutoPan:
                wet = tremolo_.processWet(left, right);
                break;
            case EffectAlgorithm::RotarySpeaker:
                wet = rotary_.processWet(left, right);
                break;
            case EffectAlgorithm::Equalizer:
            case EffectAlgorithm::Overdrive:
            case EffectAlgorithm::Distortion:
            case EffectAlgorithm::Exciter:
                wet = toneEffect_.processWet(left, right);
                break;
            case EffectAlgorithm::Off:
                wet = input;
                break;
            default:
                // Algorithms introduced in later phases remain transparent in
                // serial paths and silent in parallel through enabled_.
                wet = {left, right};
                break;
        }
        const float wetMid = 0.5f * (wet.l + wet.r);
        const float wetSide = 0.5f * (wet.l - wet.r) * width_;
        return {(wetMid + wetSide) * outputGain_,
                (wetMid - wetSide) * outputGain_};
    }

    inline StereoSample applyTransition(StereoSample output,
                                        const StereoSample &anchor) {
        if (transition_ >= 1.0f) return output;
        const float x = transition_;
        output = {anchor.l + (output.l - anchor.l) * x,
                  anchor.r + (output.r - anchor.r) * x};
        transition_ = std::min(1.0f, transition_ + transitionStep_);
        return output;
    }

    EffectSlotDescriptor descriptor_{};
    StereoChorus chorus_;
    StereoDelay delay_;
    StereoReverb reverb_;
    SymphonicEnsemble ensemble_;
    StereoFlanger flanger_;
    StereoPhaser phaser_;
    TremoloAutoPan tremolo_;
    RotarySpeaker rotary_;
    EarlyReflections early_;
    ModernStereoDelay modernDelay_;
    ToneAndNonlinearEffect toneEffect_;
    double sampleRate_ = 44100.0;
    float smoothCoef_ = 0.0f;
    float transitionStep_ = 1.0f, transition_ = 1.0f;
    float inputGain_ = 1.0f, outputGain_ = 1.0f;
    float mix_ = 1.0f, width_ = 1.0f, enabled_ = 0.0f;
    StereoSample lastSerial_{}, lastParallel_{};
    StereoSample serialAnchor_{}, parallelAnchor_{};
};

class ThreeSlotEffectsRack {
public:
    void setup(double sampleRate) {
        topologyStep_ = 1.0f / static_cast<float>(
            std::max(1.0, sampleRate * 0.020));
        for (EffectSlot &slot : slot_) slot.setup(sampleRate);
        reset();
    }
    void reset() {
        for (EffectSlot &slot : slot_) slot.reset();
        topologyTransition_ = 1.0f;
        lastOutput_ = {};
        topologyAnchor_ = {};
    }
    void setTopology(EffectTopology topology) {
        if (topology != topology_) {
            topology_ = topology;
            topologyAnchor_ = lastOutput_;
            topologyTransition_ = 0.0f;
        }
    }
    void setSlot(int index, const EffectSlotDescriptor &descriptor) {
        if (index >= 0 && index < kEffectSlotCount)
            slot_[index].setDescriptor(descriptor);
    }

    inline StereoSample process(const EffectRackInput &input) {
        StereoSample result = input.dry;
        switch (topology_) {
            case EffectTopology::Serial: {
                StereoSample chain = slot_[0].processSerial(input.send[0]);
                chain = slot_[1].processSerial(add(chain, input.send[1]));
                chain = slot_[2].processSerial(add(chain, input.send[2]));
                result = add(result, chain);
                break;
            }
            case EffectTopology::Parallel:
                for (int i = 0; i < kEffectSlotCount; ++i)
                    result = add(result, slot_[i].processParallel(input.send[i]));
                break;
            case EffectTopology::SerialPairParallel: {
                StereoSample pair = slot_[0].processSerial(input.send[0]);
                pair = slot_[1].processSerial(add(pair, input.send[1]));
                result = add(result, pair);
                result = add(result, slot_[2].processParallel(input.send[2]));
                break;
            }
            case EffectTopology::ParallelPairMaster: {
                StereoSample pair = add(slot_[0].processParallel(input.send[0]),
                                        slot_[1].processParallel(input.send[1]));
                result = add(result, slot_[2].processSerial(add(pair, input.send[2])));
                break;
            }
        }
        if (topologyTransition_ < 1.0f) {
            result = {topologyAnchor_.l
                        + (result.l - topologyAnchor_.l) * topologyTransition_,
                      topologyAnchor_.r
                        + (result.r - topologyAnchor_.r) * topologyTransition_};
            topologyTransition_ =
                std::min(1.0f, topologyTransition_ + topologyStep_);
        }
        lastOutput_ = result;
        return result;
    }

private:
    static inline StereoSample add(StereoSample a, StereoSample b) {
        return {a.l + b.l, a.r + b.r};
    }

    EffectTopology topology_ = EffectTopology::Serial;
    EffectSlot slot_[kEffectSlotCount];
    float topologyStep_ = 1.0f, topologyTransition_ = 1.0f;
    StereoSample topologyAnchor_{}, lastOutput_{};
};

class GlobalEffects {
public:
    void setup(double sampleRate) {
        compressor_.setup(sampleRate);
        rack_.setup(sampleRate);
    }
    void reset() {
        compressor_.reset();
        rack_.reset();
    }

    // The compressor is the sole processor outside the three-slot rack.
    // Keeping its configuration separate prevents the retired fixed
    // Chorus -> Delay -> Reverb chain from becoming a second effects path.
    void setCompressorParams(float compressorOn,
                             float compressorThreshold,
                             float compressorRatio,
                             float compressorAttack,
                             float compressorRelease,
                             float compressorMakeup) {
        compressor_.setParams(compressorOn, compressorThreshold,
                              compressorRatio, compressorAttack,
                              compressorRelease, compressorMakeup);
    }

    float compressorGainReductionDb() const {
        return compressor_.gainReductionDb();
    }

    void setRackTopology(EffectTopology topology) { rack_.setTopology(topology); }
    void setRackSlot(int index, const EffectSlotDescriptor &descriptor) {
        rack_.setSlot(index, descriptor);
    }
    inline StereoSample processRack(const EffectRackInput &input) {
        const StereoSample routed = rack_.process(input);
        return compressor_.process(routed.l, routed.r);
    }

private:
    StereoCompressor compressor_;
    ThreeSlotEffectsRack rack_;
};

} // namespace r50
