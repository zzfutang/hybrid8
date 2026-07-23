//
//  ADSR.hpp
//  Analogue-style ADSR envelope with exponential segments (Nigel Redmond /
//  earlevel design). Times are specified in seconds; the curve shape mimics
//  the capacitor charge/discharge of a real analogue envelope.
//

#pragma once
#include "Utils.hpp"

namespace synth {

class ADSR {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        // Recompute coefficients for the currently stored times.
        setAttack(attackTime_);
        setDecay(decayTime_);
        setSustain(sustainLevel_);
        setRelease(releaseTime_);
    }

    void setAttack(float seconds) {
        attackTime_ = seconds;
        const double rate = std::max(1.0, seconds * sampleRate_);
        attackCoef_ = calcCoef(rate, targetRatioA_);
        attackBase_ = (1.0 + targetRatioA_) * (1.0 - attackCoef_);
    }
    void setDecay(float seconds) {
        decayTime_ = seconds;
        const double rate = std::max(1.0, seconds * sampleRate_);
        decayCoef_ = calcCoef(rate, targetRatioDR_);
        decayBase_ = (sustainLevel_ - targetRatioDR_) * (1.0 - decayCoef_);
    }
    void setSustain(float level) {
        sustainLevel_ = level;
        decayBase_ = (sustainLevel_ - targetRatioDR_) * (1.0 - decayCoef_);
    }
    void setRelease(float seconds) {
        releaseTime_ = seconds;
        const double rate = std::max(1.0, seconds * sampleRate_);
        releaseCoef_ = calcCoef(rate, targetRatioDR_);
        releaseBase_ = -targetRatioDR_ * (1.0 - releaseCoef_);
    }

    void gate(bool on) {
        if (on) {
            state_ = State::Attack;
        } else if (state_ != State::Idle) {
            state_ = State::Release;
        }
    }

    void resetHard() { state_ = State::Idle; output_ = 0.0; }

    inline bool isActive() const { return state_ != State::Idle; }

    inline float process() {
        switch (state_) {
            case State::Idle: break;
            case State::Attack:
                output_ = attackBase_ + output_ * attackCoef_;
                if (output_ >= 1.0) { output_ = 1.0; state_ = State::Decay; }
                break;
            case State::Decay:
                output_ = decayBase_ + output_ * decayCoef_;
                if (output_ <= sustainLevel_) { output_ = sustainLevel_; state_ = State::Sustain; }
                break;
            case State::Sustain:
                output_ = sustainLevel_;
                break;
            case State::Release:
                output_ = releaseBase_ + output_ * releaseCoef_;
                if (output_ <= 0.0) { output_ = 0.0; state_ = State::Idle; }
                break;
        }
        return static_cast<float>(output_);
    }

    State state() const { return state_; }

private:
    static double calcCoef(double rate, double targetRatio) {
        return std::exp(-std::log((1.0 + targetRatio) / targetRatio) / rate);
    }

    double sampleRate_ = 44100.0;
    State  state_ = State::Idle;
    double output_ = 0.0;

    // Stored times / level so we can recompute on sample-rate change.
    float attackTime_ = 0.005f, decayTime_ = 0.1f, releaseTime_ = 0.2f;
    float sustainLevel_ = 0.8f;

    // Shape of the exponential approach; smaller = more curved.
    double targetRatioA_  = 0.3;
    double targetRatioDR_ = 0.0001;

    double attackCoef_ = 0.0, decayCoef_ = 0.0, releaseCoef_ = 0.0;
    double attackBase_ = 0.0, decayBase_ = 0.0, releaseBase_ = 0.0;
};

} // namespace synth
