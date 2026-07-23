//
//  Effects.hpp
//  Global stereo effects: a four-voice fractional-delay chorus followed by a
//  filtered, softly saturated stereo feedback delay.
//

#pragma once
#include "Utils.hpp"
#include <vector>

namespace synth {

struct StereoSample {
    float l = 0.0f;
    float r = 0.0f;
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
        line_.setup(static_cast<int>(sampleRate * 0.060) + 8);
        smoothCoef_ = std::exp(-1.0 / (0.010 * sampleRate));
        reset();
    }

    void reset() {
        line_.reset();
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

    inline StereoSample process(float input) {
        smooth();
        line_.write(input);

        // Four independently moving voices. The secondary very-slow oscillator
        // prevents the main sweep from sounding perfectly repetitive.
        static constexpr double phases[4] = {0.0, 0.25, 0.5, 0.75};
        static constexpr double driftOffsets[4] = {0.11, 0.43, 0.71, 0.89};
        float taps[4];
        const double base = 0.017 * sampleRate_;
        const double span = (0.0005 + 0.0045 * depth_) * sampleRate_;
        for (int i = 0; i < 4; ++i) {
            double mod = std::sin(kTwoPi * (phase_ + phases[i]));
            double drift = 0.22 * std::sin(kTwoPi * (driftPhase_ + driftOffsets[i]));
            taps[i] = line_.read(base + span * (mod + drift));
        }

        phase_ += rate_ / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        driftPhase_ += 0.071 / sampleRate_;
        if (driftPhase_ >= 1.0) driftPhase_ -= 1.0;

        // Cross-distribute the voices for width without polarity tricks.
        float wetL = 0.5f * (taps[0] + taps[2]);
        float wetR = 0.5f * (taps[1] + taps[3]);
        return {input + (wetL - input) * mix_,
                input + (wetR - input) * mix_};
    }

private:
    inline void smooth() {
        mix_ = mixTarget_ + (mix_ - mixTarget_) * smoothCoef_;
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
    }

    FractionalDelayLine line_;
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

        return {inputL + (delayedL - inputL) * mix_,
                inputR + (delayedR - inputR) * mix_};
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

class GlobalEffects {
public:
    void setup(double sampleRate) {
        chorus_.setup(sampleRate);
        delay_.setup(sampleRate);
    }
    void reset() { chorus_.reset(); delay_.reset(); }

    void setParams(float chorusMix, float chorusRate, float chorusDepth,
                   float delayMix, float delayTime, float delayFeedback,
                   float delayTone, float delayPingPong) {
        chorus_.setParams(chorusMix, chorusRate, chorusDepth);
        delay_.setParams(delayMix, delayTime, delayFeedback, delayTone, delayPingPong);
    }

    inline StereoSample process(float mono) {
        StereoSample c = chorus_.process(mono);
        return delay_.process(c.l, c.r);
    }

private:
    StereoChorus chorus_;
    StereoDelay delay_;
};

} // namespace synth
