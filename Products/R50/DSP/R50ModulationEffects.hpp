// Shared modulation primitives and the Phase 4 global effects.
// Included by R50EffectsRack.hpp after StereoSample and FractionalDelayLine.

class EffectLFO {
public:
    void setup(double sampleRate) { sampleRate_ = sampleRate; }
    void reset(double phase = 0.0) { phase_ = phase - std::floor(phase); }
    void setRate(float hz) { rate_ = clampf(hz, 0.01f, 20.0f); }
    float value(float offset = 0.0f, bool triangle = false) const {
        const double p = phase_ + offset - std::floor(phase_ + offset);
        return triangle
            ? static_cast<float>(1.0 - 4.0 * std::fabs(p - 0.5))
            : static_cast<float>(std::sin(kTwoPi * p));
    }
    void advance() {
        phase_ += rate_ / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float rate_ = 1.0f;
};

class EffectOnePoleLowpass {
public:
    void setup(double sampleRate) { sampleRate_ = sampleRate; setCutoff(cutoff_); }
    void reset() { state_ = 0.0f; }
    void setCutoff(float hz) {
        cutoff_ = clampf(hz, 20.0f,
                         static_cast<float>(sampleRate_ * 0.45));
        coefficient_ = static_cast<float>(
            std::exp(-kTwoPi * cutoff_ / sampleRate_));
    }
    float process(float input) {
        state_ = input + (state_ - input) * coefficient_;
        return state_;
    }
private:
    double sampleRate_ = 44100.0;
    float cutoff_ = 12000.0f, coefficient_ = 0.0f, state_ = 0.0f;
};

class SymphonicEnsemble {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        left_.setup(static_cast<int>(sampleRate * 0.050) + 8);
        right_.setup(static_cast<int>(sampleRate * 0.050) + 8);
        for (auto &lfo : lfo_) lfo.setup(sampleRate);
        toneL_.setup(sampleRate); toneR_.setup(sampleRate);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.020 * sampleRate)));
        reset();
    }
    void reset() {
        left_.reset(); right_.reset();
        lfo_[0].reset(0.07); lfo_[1].reset(0.39); lfo_[2].reset(0.73);
        rate_ = rateTarget_; depth_ = depthTarget_;
        spread_ = spreadTarget_; drift_ = driftTarget_;
        toneL_.reset(); toneR_.reset(); hpL_ = hpR_ = 0.0f;
    }
    void setParams(float rate, float depth, float spread, float drift,
                   float tone, float lowCut) {
        rateTarget_ = clampf(rate, 0.03f, 3.0f);
        depthTarget_ = clampf(depth, 0.0f, 1.0f);
        spreadTarget_ = clampf(spread, 0.0f, 1.0f);
        driftTarget_ = clampf(drift, 0.0f, 1.0f);
        tone_ = clampf(tone, 1000.0f, 20000.0f);
        toneL_.setCutoff(tone_); toneR_.setCutoff(tone_);
        lowCut_ = clampf(lowCut, 20.0f, 500.0f);
    }
    StereoSample processWet(float inputL, float inputR) {
        smooth();
        left_.write(inputL); right_.write(inputR);
        float outL = 0.0f, outR = 0.0f;
        static constexpr float ratio[3] = {1.0f, 1.311f, 1.703f};
        static constexpr float baseMs[3] = {12.0f, 19.0f, 28.0f};
        for (int i = 0; i < 3; ++i) {
            lfo_[i].setRate(rate_ * (1.0f + (ratio[i] - 1.0f) * spread_));
            const float wander = 0.23f * drift_
                * std::sin(static_cast<float>(kTwoPi)
                           * (0.071f * phaseSeconds_ + i * 0.271f));
            const float modL = lfo_[i].value(i * 0.17f) + wander;
            const float modR = lfo_[i].value(0.5f + i * 0.19f) - wander;
            const float excursion = (0.3f + 3.7f * depth_) * 0.001f
                                  * static_cast<float>(sampleRate_);
            const float base = baseMs[i] * 0.001f * static_cast<float>(sampleRate_);
            outL += left_.read(base + excursion * modL);
            outR += right_.read(base + excursion * modR);
            lfo_[i].advance();
        }
        phaseSeconds_ += 1.0 / sampleRate_;
        outL *= 0.3333333f; outR *= 0.3333333f;
        const float hpCoef = std::exp(-kTwoPi * lowCut_ / sampleRate_);
        hpL_ = outL + (hpL_ - outL) * hpCoef;
        hpR_ = outR + (hpR_ - outR) * hpCoef;
        outL -= hpL_; outR -= hpR_;
        return {toneL_.process(outL), toneR_.process(outR)};
    }
private:
    void smooth() {
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
        spread_ = spreadTarget_ + (spread_ - spreadTarget_) * smoothCoef_;
        drift_ = driftTarget_ + (drift_ - driftTarget_) * smoothCoef_;
    }
    FractionalDelayLine left_, right_;
    std::array<EffectLFO, 3> lfo_;
    EffectOnePoleLowpass toneL_, toneR_;
    double sampleRate_ = 44100.0, phaseSeconds_ = 0.0;
    float rate_ = 0.22f, depth_ = 0.55f, spread_ = 0.7f, drift_ = 0.45f;
    float rateTarget_ = 0.22f, depthTarget_ = 0.55f;
    float spreadTarget_ = 0.7f, driftTarget_ = 0.45f;
    float tone_ = 12000.0f, lowCut_ = 100.0f, smoothCoef_ = 0.0f;
    float hpL_ = 0.0f, hpR_ = 0.0f;
};

class StereoFlanger {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        left_.setup(static_cast<int>(sampleRate * 0.020) + 8);
        right_.setup(static_cast<int>(sampleRate * 0.020) + 8);
        lfo_.setup(sampleRate);
        toneFilterL_.setup(sampleRate); toneFilterR_.setup(sampleRate);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate)));
        reset();
    }
    void reset() {
        left_.reset(); right_.reset(); lfo_.reset();
        feedbackL_ = feedbackR_ = 0.0f;
        toneFilterL_.reset(); toneFilterR_.reset();
        rate_ = rateTarget_; depth_ = depthTarget_;
        manual_ = manualTarget_; feedback_ = feedbackTarget_;
    }
    void setParams(float rate, float depth, float manualMs, float feedback,
                   bool cross, bool triangle, float phase, float tone) {
        rateTarget_ = clampf(rate, 0.03f, 10.0f);
        depthTarget_ = clampf(depth, 0.0f, 1.0f);
        manualTarget_ = clampf(manualMs, 0.1f, 15.0f);
        feedbackTarget_ = clampf(feedback, -0.95f, 0.95f);
        cross_ = cross; triangle_ = triangle;
        stereoPhase_ = clampf(phase, 0.0f, 0.5f);
        tone_ = clampf(tone, 1000.0f, 20000.0f);
        toneFilterL_.setCutoff(tone_); toneFilterR_.setCutoff(tone_);
    }
    StereoSample processWet(float inputL, float inputR) {
        smooth(); lfo_.setRate(rate_);
        const float modL = lfo_.value(0.0f, triangle_);
        const float modR = lfo_.value(stereoPhase_, triangle_);
        const float centre = manual_ * 0.001f * static_cast<float>(sampleRate_);
        const float span = depth_ * std::min(centre - 4.0f,
            7.0e-3f * static_cast<float>(sampleRate_));
        const float delayedL = left_.read(std::max(4.0f, centre + span * modL));
        const float delayedR = right_.read(std::max(4.0f, centre + span * modR));
        const float toneL = toneFilterL_.process(delayedL);
        const float toneR = toneFilterR_.process(delayedR);
        const float sourceL = cross_ ? toneR : toneL;
        const float sourceR = cross_ ? toneL : toneR;
        left_.write(inputL + sourceL * feedback_);
        right_.write(inputR + sourceR * feedback_);
        lfo_.advance();
        // The feed-forward dry component is intrinsic to a flanger's wet
        // result; the rack's common Mix still owns insert dry-path blending.
        return {0.5f * (inputL + delayedL), 0.5f * (inputR + delayedR)};
    }
private:
    void smooth() {
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
        manual_ = manualTarget_ + (manual_ - manualTarget_) * smoothCoef_;
        feedback_ = feedbackTarget_ + (feedback_ - feedbackTarget_) * smoothCoef_;
    }
    FractionalDelayLine left_, right_;
    EffectLFO lfo_;
    EffectOnePoleLowpass toneFilterL_, toneFilterR_;
    double sampleRate_ = 44100.0;
    float rate_ = 0.2f, depth_ = 0.65f, manual_ = 2.5f, feedback_ = 0.35f;
    float rateTarget_ = 0.2f, depthTarget_ = 0.65f;
    float manualTarget_ = 2.5f, feedbackTarget_ = 0.35f;
    float stereoPhase_ = 0.5f, tone_ = 12000.0f, smoothCoef_ = 0.0f;
    float feedbackL_ = 0.0f, feedbackR_ = 0.0f;
    bool cross_ = false, triangle_ = false;
};

class StereoPhaser {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate; lfo_.setup(sampleRate);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.012 * sampleRate)));
        reset();
    }
    void reset() {
        stateL_.fill(0.0f); stateR_.fill(0.0f);
        feedbackL_ = feedbackR_ = 0.0f; lfo_.reset();
        rate_ = rateTarget_; depth_ = depthTarget_;
        center_ = centerTarget_; spread_ = spreadTarget_;
        feedback_ = feedbackTarget_;
    }
    void setParams(float rate, float depth, float center, float spread,
                   float feedback, int stages, bool triangle, float phase) {
        rateTarget_ = clampf(rate, 0.03f, 10.0f);
        depthTarget_ = clampf(depth, 0.0f, 1.0f);
        centerTarget_ = clampf(center, 80.0f, 4000.0f);
        spreadTarget_ = clampf(spread, 0.5f, 6.0f);
        feedbackTarget_ = clampf(feedback, -0.95f, 0.95f);
        stages_ = stages >= 12 ? 12 : 6;
        triangle_ = triangle; stereoPhase_ = clampf(phase, 0.0f, 0.5f);
    }
    StereoSample processWet(float inputL, float inputR) {
        smooth(); lfo_.setRate(rate_);
        const float sweepL = lfo_.value(0.0f, triangle_);
        const float sweepR = lfo_.value(stereoPhase_, triangle_);
        float left = inputL + feedbackL_ * feedback_;
        float right = inputR + feedbackR_ * feedback_;
        for (int i = 0; i < stages_; ++i) {
            const float stage = (static_cast<float>(i) / std::max(1, stages_ - 1) - 0.5f);
            const float freqL = clampf(center_ * std::pow(2.0f,
                stage * spread_ + sweepL * depth_ * 1.5f), 20.0f,
                static_cast<float>(sampleRate_ * 0.45));
            const float freqR = clampf(center_ * std::pow(2.0f,
                stage * spread_ + sweepR * depth_ * 1.5f), 20.0f,
                static_cast<float>(sampleRate_ * 0.45));
            left = allpass(left, stateL_[i], freqL);
            right = allpass(right, stateR_[i], freqR);
        }
        feedbackL_ = left; feedbackR_ = right; lfo_.advance();
        return {0.5f * (inputL + left), 0.5f * (inputR + right)};
    }
private:
    float allpass(float x, float &state, float frequency) {
        const float t = std::tan(static_cast<float>(kPi) * frequency
                                 / static_cast<float>(sampleRate_));
        const float a = clampf((1.0f - t) / (1.0f + t), -0.9995f, 0.9995f);
        const float y = a * x + state;
        state = x - a * y;
        return y;
    }
    void smooth() {
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
        center_ = centerTarget_ + (center_ - centerTarget_) * smoothCoef_;
        spread_ = spreadTarget_ + (spread_ - spreadTarget_) * smoothCoef_;
        feedback_ = feedbackTarget_ + (feedback_ - feedbackTarget_) * smoothCoef_;
    }
    EffectLFO lfo_;
    std::array<float, 12> stateL_{}, stateR_{};
    double sampleRate_ = 44100.0;
    float rate_ = 0.3f, depth_ = 0.7f, center_ = 800.0f;
    float spread_ = 3.0f, feedback_ = 0.3f;
    float rateTarget_ = 0.3f, depthTarget_ = 0.7f, centerTarget_ = 800.0f;
    float spreadTarget_ = 3.0f, feedbackTarget_ = 0.3f;
    float stereoPhase_ = 0.25f, smoothCoef_ = 0.0f;
    float feedbackL_ = 0.0f, feedbackR_ = 0.0f;
    int stages_ = 6; bool triangle_ = false;
};

class TremoloAutoPan {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate; lfo_.setup(sampleRate);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.008 * sampleRate)));
        reset();
    }
    void reset() {
        lfo_.reset(); rate_ = rateTarget_; depth_ = depthTarget_;
        gainL_ = gainR_ = 1.0f;
    }
    void setParams(float rate, float depth, float shape, float phase,
                   float bias, int mode) {
        rateTarget_ = clampf(rate, 0.03f, 20.0f);
        depthTarget_ = clampf(depth, 0.0f, 1.0f);
        shape_ = clampf(shape, -1.0f, 1.0f);
        stereoPhase_ = clampf(phase, 0.0f, 0.5f);
        bias_ = clampf(bias, -1.0f, 1.0f); mode_ = std::min(2, std::max(0, mode));
    }
    StereoSample processWet(float inputL, float inputR) {
        rate_ = rateTarget_ + (rate_ - rateTarget_) * smoothCoef_;
        depth_ = depthTarget_ + (depth_ - depthTarget_) * smoothCoef_;
        lfo_.setRate(rate_);
        const float waveL = shaped(lfo_.value());
        const float waveR = shaped(lfo_.value(mode_ == 0 ? 0.0f : stereoPhase_));
        float targetL, targetR;
        if (mode_ == 2) {
            const float pan = clampf(waveL + bias_, -1.0f, 1.0f) * depth_;
            targetL = std::cos((pan + 1.0f) * static_cast<float>(kPi * 0.25));
            targetR = std::sin((pan + 1.0f) * static_cast<float>(kPi * 0.25));
        } else {
            targetL = 1.0f - depth_ * 0.5f * (1.0f - waveL);
            targetR = 1.0f - depth_ * 0.5f * (1.0f - waveR);
        }
        gainL_ = targetL + (gainL_ - targetL) * smoothCoef_;
        gainR_ = targetR + (gainR_ - targetR) * smoothCoef_;
        lfo_.advance();
        return {inputL * gainL_, inputR * gainR_};
    }
private:
    float shaped(float sine) const {
        const float triangle = 2.0f / static_cast<float>(kPi) * std::asin(sine);
        const float rounded = std::tanh(2.5f * (sine + 0.35f * bias_))
                            / std::tanh(2.5f);
        return shape_ < 0.0f ? triangle + (sine - triangle) * -shape_
                             : triangle + (rounded - triangle) * shape_;
    }
    EffectLFO lfo_;
    double sampleRate_ = 44100.0;
    float rate_ = 4.0f, depth_ = 0.6f, rateTarget_ = 4.0f, depthTarget_ = 0.6f;
    float shape_ = -1.0f, stereoPhase_ = 0.5f, bias_ = 0.0f;
    float gainL_ = 1.0f, gainR_ = 1.0f, smoothCoef_ = 0.0f;
    int mode_ = 0;
};

class RotarySpeaker {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        hornL_.setup(static_cast<int>(sampleRate * 0.010) + 8);
        hornR_.setup(static_cast<int>(sampleRate * 0.010) + 8);
        drumL_.setup(static_cast<int>(sampleRate * 0.010) + 8);
        drumR_.setup(static_cast<int>(sampleRate * 0.010) + 8);
        reset();
    }
    void reset() {
        hornL_.reset(); hornR_.reset(); drumL_.reset(); drumR_.reset();
        crossoverL_ = crossoverR_ = cabinetL_ = cabinetR_ = 0.0f;
        hornPhase_ = 0.0; drumPhase_ = 0.31; currentRate_ = 0.0f;
    }
    void setParams(int speed, float slowRate, float fastRate, float ratio,
                   float acceleration, float doppler, float amplitude,
                   float crossover) {
        speed_ = std::min(2, std::max(0, speed));
        slowRate_ = clampf(slowRate, 0.2f, 1.5f);
        fastRate_ = clampf(fastRate, 3.0f, 10.0f);
        ratio_ = clampf(ratio, 0.5f, 2.0f);
        acceleration_ = clampf(acceleration, 0.2f, 8.0f);
        doppler_ = clampf(doppler, 0.0f, 1.0f);
        amplitude_ = clampf(amplitude, 0.0f, 1.0f);
        crossover_ = clampf(crossover, 400.0f, 2000.0f);
    }
    StereoSample processWet(float inputL, float inputR) {
        const float target = speed_ == 0 ? 0.0f
                           : (speed_ == 1 ? slowRate_ : fastRate_);
        const float ramp = 1.0f - std::exp(-1.0f /
            (acceleration_ * static_cast<float>(sampleRate_)));
        currentRate_ += (target - currentRate_) * ramp;
        const float xoverCoef = std::exp(-kTwoPi * crossover_ / sampleRate_);
        crossoverL_ = inputL + (crossoverL_ - inputL) * xoverCoef;
        crossoverR_ = inputR + (crossoverR_ - inputR) * xoverCoef;
        const float drumInL = crossoverL_, drumInR = crossoverR_;
        const float hornInL = inputL - drumInL, hornInR = inputR - drumInR;
        hornL_.write(hornInL); hornR_.write(hornInR);
        drumL_.write(drumInL); drumR_.write(drumInR);
        const float hornSin = std::sin(static_cast<float>(kTwoPi * hornPhase_));
        const float drumSin = std::sin(static_cast<float>(kTwoPi * drumPhase_));
        const float hornDelayL = (1.2f + 0.8f * doppler_ * hornSin)
                               * 0.001f * static_cast<float>(sampleRate_);
        const float hornDelayR = (1.2f - 0.8f * doppler_ * hornSin)
                               * 0.001f * static_cast<float>(sampleRate_);
        const float drumDelayL = (2.0f + 0.45f * doppler_ * drumSin)
                               * 0.001f * static_cast<float>(sampleRate_);
        const float drumDelayR = (2.0f - 0.45f * doppler_ * drumSin)
                               * 0.001f * static_cast<float>(sampleRate_);
        float outL = hornL_.read(hornDelayL) * (1.0f + amplitude_ * 0.45f * hornSin)
                   + drumL_.read(drumDelayL) * (1.0f + amplitude_ * 0.25f * drumSin);
        float outR = hornR_.read(hornDelayR) * (1.0f - amplitude_ * 0.45f * hornSin)
                   + drumR_.read(drumDelayR) * (1.0f - amplitude_ * 0.25f * drumSin);
        hornPhase_ += currentRate_ / sampleRate_;
        drumPhase_ += currentRate_ / (ratio_ * sampleRate_);
        hornPhase_ -= std::floor(hornPhase_); drumPhase_ -= std::floor(drumPhase_);
        const float cabinetCoef = std::exp(-kTwoPi * 14000.0 / sampleRate_);
        cabinetL_ = outL + (cabinetL_ - outL) * cabinetCoef;
        cabinetR_ = outR + (cabinetR_ - outR) * cabinetCoef;
        return {cabinetL_, cabinetR_};
    }
private:
    FractionalDelayLine hornL_, hornR_, drumL_, drumR_;
    double sampleRate_ = 44100.0, hornPhase_ = 0.0, drumPhase_ = 0.0;
    float crossoverL_ = 0.0f, crossoverR_ = 0.0f;
    float cabinetL_ = 0.0f, cabinetR_ = 0.0f, currentRate_ = 0.0f;
    float slowRate_ = 0.7f, fastRate_ = 6.5f, ratio_ = 1.25f;
    float acceleration_ = 1.8f, doppler_ = 0.55f, amplitude_ = 0.65f;
    float crossover_ = 800.0f;
    int speed_ = 1;
};

class EarlyReflections {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        lineL_.setup(static_cast<int>(sampleRate * 1.05) + 8);
        lineR_.setup(static_cast<int>(sampleRate * 1.05) + 8);
        reset();
    }
    void reset() {
        lineL_.reset(); lineR_.reset();
        toneL_ = toneR_ = 0.0f;
    }
    void setParams(float preDelay, float length, float density, float shape,
                   float tone, float spread, int pattern) {
        preDelay_ = clampf(preDelay, 0.0f, 0.2f);
        length_ = clampf(length, 0.03f, 0.8f);
        density_ = clampf(density, 0.0f, 1.0f);
        shape_ = clampf(shape, -1.0f, 1.0f);
        tone_ = clampf(tone, 500.0f, 20000.0f);
        spread_ = clampf(spread, 0.0f, 2.0f);
        pattern_ = std::min(3, std::max(0, pattern));
    }
    StereoSample processWet(float l, float r) {
        lineL_.write(l);
        lineR_.write(r);
        float outL = 0, outR = 0;
        constexpr int taps = 24;
        for (int i = 0; i < taps; ++i) {
            const float grid = static_cast<float>(i + 1) / taps;
            // A deterministic low-discrepancy jitter keeps the taps ordered
            // while preventing a pitched FIR comb.
            const float hash = std::fmod((i + 1) * 0.61803398875f
                                       + pattern_ * 0.17320508f, 1.0f);
            const float x = clampf(grid + (hash - 0.5f) * (0.7f / taps),
                                   0.01f, 1.0f);
            float position = x;
            if (pattern_ == 1) position = std::sqrt(x);
            if (pattern_ == 2) position = 0.15f + 0.85f * x;
            if (pattern_ == 3) position = x * x;
            const float delay = (preDelay_ + length_ * position)
                              * static_cast<float>(sampleRate_);
            const float envelope = std::pow(std::max(0.001f,
                pattern_ == 3 ? x : 1.0f - x), 0.3f + 1.7f * std::fabs(shape_));
            const float active = i < 6 ? 1.0f
                : clampf(density_ * 24.0f - static_cast<float>(i - 5), 0.0f, 1.0f);
            // Occasional inversion gives spatial complexity without the
            // wholesale cancellation of an alternating-sign train.
            const float polarity = ((i * 5 + pattern_ * 3) % 11 == 0)
                                 ? -1.0f : 1.0f;
            const float pan = clampf((hash * 2.0f - 1.0f) * spread_, -1.0f, 1.0f);
            const float source = 0.5f * ((1.0f - pan) * lineL_.read(delay)
                                      + (1.0f + pan) * lineR_.read(delay));
            const float tap = source * envelope * active * polarity * 0.24f;
            outL += tap * std::sqrt(0.5f * (1.0f - pan));
            outR += tap * std::sqrt(0.5f * (1.0f + pan));
        }
        const float c = std::exp(-kTwoPi * tone_ / sampleRate_);
        toneL_ = outL + (toneL_ - outL) * c;
        toneR_ = outR + (toneR_ - outR) * c;
        return {toneL_, toneR_};
    }
private:
    FractionalDelayLine lineL_, lineR_;
    double sampleRate_ = 44100.0;
    float preDelay_ = 0.005f, length_ = 0.18f, density_ = 0.65f;
    float shape_ = 0.5f, tone_ = 9000.0f, spread_ = 1.0f;
    float toneL_ = 0, toneR_ = 0;
    int pattern_ = 0;
};

class ModernStereoDelay {
public:
    void setup(double sampleRate) {
        sampleRate_ = sampleRate;
        left_.setup(static_cast<int>(sampleRate * 2.05) + 8);
        right_.setup(static_cast<int>(sampleRate * 2.05) + 8);
        smoothCoef_ = std::exp(-1.0 / (0.025 * sampleRate));
        reset();
    }
    void reset() {
        left_.reset(); right_.reset();
        lpL_ = lpR_ = hpL_ = hpR_ = 0;
        timeL_ = targetL_; timeR_ = targetR_;
        feedback_ = feedbackTarget_; cross_ = crossTarget_;
    }
    void setParams(float leftSeconds, float rightSeconds, float feedback,
                   float cross, float lowCut, float highCut, float saturation) {
        targetL_ = clampf(leftSeconds, 0.001f, 2.0f) * sampleRate_;
        targetR_ = clampf(rightSeconds, 0.001f, 2.0f) * sampleRate_;
        feedbackTarget_ = clampf(feedback, -0.95f, 0.95f);
        crossTarget_ = clampf(cross, 0.0f, 1.0f);
        lowCut_ = clampf(lowCut, 20.0f, 2000.0f);
        highCut_ = clampf(highCut, 500.0f, 20000.0f);
        saturation_ = clampf(saturation, 0.0f, 1.0f);
    }
    StereoSample processWet(float inputL, float inputR) {
        timeL_ = targetL_ + (timeL_ - targetL_) * smoothCoef_;
        timeR_ = targetR_ + (timeR_ - targetR_) * smoothCoef_;
        feedback_ = feedbackTarget_ + (feedback_ - feedbackTarget_) * smoothCoef_;
        cross_ = crossTarget_ + (cross_ - crossTarget_) * smoothCoef_;
        const float delayedL = left_.read(timeL_);
        const float delayedR = right_.read(timeR_);
        const float filteredL = filter(delayedL, lpL_, hpL_);
        const float filteredR = filter(delayedR, lpR_, hpR_);
        // Convex matrix has spectral radius <= 1 for every Cross setting.
        const float returnL = filteredL * (1.0f - cross_) + filteredR * cross_;
        const float returnR = filteredR * (1.0f - cross_) + filteredL * cross_;
        left_.write(inputL + saturate(returnL * feedback_));
        right_.write(inputR + saturate(returnR * feedback_));
        return {delayedL, delayedR};
    }
private:
    float filter(float x, float &lp, float &hp) {
        const float lc = 1.0f - std::exp(-kTwoPi * highCut_ / sampleRate_);
        const float hc = 1.0f - std::exp(-kTwoPi * lowCut_ / sampleRate_);
        lp += lc * (x - lp);
        hp += hc * (lp - hp);
        return lp - hp;
    }
    float saturate(float x) const {
        const float drive = 1.0f + 3.0f * saturation_;
        return saturation_ <= 0.0001f ? x : std::tanh(x * drive) / drive;
    }
    FractionalDelayLine left_, right_;
    double sampleRate_ = 44100.0;
    float targetL_ = 11025, targetR_ = 16537, timeL_ = 11025, timeR_ = 16537;
    float feedback_ = 0.35f, feedbackTarget_ = 0.35f;
    float cross_ = 0, crossTarget_ = 0, lowCut_ = 40, highCut_ = 8000;
    float saturation_ = 0.15f, smoothCoef_ = 0;
    float lpL_ = 0, lpR_ = 0, hpL_ = 0, hpR_ = 0;
};

class ToneAndNonlinearEffect {
public:
    enum Kind { Equalizer, Overdrive, Distortion, Exciter };
    void setup(double sampleRate) { sampleRate_ = sampleRate; reset(); }
    void reset() {
        lowL_ = lowR_ = highLpL_ = highLpR_ = 0;
        dcXL_ = dcXR_ = dcYL_ = dcYR_ = 0;
        osL_.reset(); osR_.reset();
    }
    void setParams(Kind kind, const float *c, int mode) {
        kind_ = kind; mode_ = mode;
        for (int i = 0; i < 8; ++i) c_[i] = clampf(c[i], 0.0f, 1.0f);
    }
    StereoSample processWet(float l, float r) {
        if (kind_ == Equalizer) return {equalize(l, true), equalize(r, false)};
        return {nonlinear(l, true), nonlinear(r, false)};
    }
private:
    float equalize(float x, bool left) {
        float &low = left ? lowL_ : lowR_;
        float &highLp = left ? highLpL_ : highLpR_;
        const float lowHz = 40.0f * std::pow(25.0f, c_[1]);
        const float highHz = 1000.0f * std::pow(16.0f, c_[6]);
        low += (1.0f - std::exp(-kTwoPi * lowHz / sampleRate_)) * (x - low);
        highLp += (1.0f - std::exp(-kTwoPi * highHz / sampleRate_)) * (x - highLp);
        const float high = x - highLp;
        const float mid = x - low - high;
        const float gl = std::pow(10.0f, (c_[0] * 36.0f - 18.0f) / 20.0f);
        const float gm = std::pow(10.0f, (c_[2] * 36.0f - 18.0f) / 20.0f);
        const float gh = std::pow(10.0f, (c_[5] * 36.0f - 18.0f) / 20.0f);
        return low * gl + mid * gm + high * gh;
    }
    float nonlinear(float x, bool left) {
        auto &os = left ? osL_ : osR_;
        float output = 0;
        if (kind_ == Exciter) {
            float &lp = left ? highLpL_ : highLpR_;
            const float hz = 1000.0f * std::pow(10.0f, c_[0]);
            lp += (1.0f - std::exp(-kTwoPi * hz / sampleRate_)) * (x - lp);
            const float side = x - lp;
            const float gain = std::pow(10.0f, c_[1] * 36.0f / 20.0f);
            const float shaped = os.process(side, [=](float s) {
                return std::tanh(s * gain + c_[2] * 0.15f);
            });
            output = x + shaped * c_[3];
        } else {
            const float maxDb = kind_ == Overdrive ? 36.0f : 48.0f;
            const float gain = std::pow(10.0f, c_[0] * maxDb / 20.0f);
            const float bias = kind_ == Distortion ? (c_[2] * 2 - 1) * 0.3f
                                                   : c_[2] * 0.15f;
            output = os.process(x, [=](float s) {
                const float driven = s * gain + bias;
                if (kind_ == Overdrive) {
                    if (mode_ == 0) return std::tanh(driven * 0.72f);
                    if (mode_ == 2)
                        return std::tanh(driven * 1.35f)
                             + 0.08f * std::tanh(driven * driven);
                    return std::tanh(driven);
                }
                if (kind_ == Distortion && mode_ == 0)
                    return clampf(driven, -1.0f, 1.0f);
                if (kind_ == Distortion && mode_ == 1)
                    return clampf(driven - driven * driven * driven / 3.0f,
                                  -1.0f, 1.0f);
                return std::tanh(driven);
            });
            const float levelDb = (kind_ == Overdrive ? -24.0f : -30.0f)
                                + c_[5] * 36.0f;
            output *= std::pow(10.0f, levelDb / 20.0f);
        }
        float &previousX = left ? dcXL_ : dcXR_;
        float &previousY = left ? dcYL_ : dcYR_;
        const float blocked = output - previousX + 0.995f * previousY;
        previousX = output;
        previousY = blocked;
        return blocked;
    }
    Kind kind_ = Equalizer;
    int mode_ = 0;
    double sampleRate_ = 44100.0;
    float c_[8]{};
    float lowL_ = 0, lowR_ = 0, highLpL_ = 0, highLpR_ = 0;
    float dcXL_ = 0, dcXR_ = 0, dcYL_ = 0, dcYR_ = 0;
    Oversampler4x osL_, osR_;
};
