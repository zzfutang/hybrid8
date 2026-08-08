//
//  R50Envelope.hpp
//  The workstation EG: four timed segments and three levels.
//
//        A
//        /\                    A = attack level
//       /  \__B                B = break point
//      /      \____S           S = sustain level
//     /            \
//  __/              \____      release falls to zero
//    attack decay slope release
//
//  A plain ADSR cannot express a two-stage decay, which is what a piano or a
//  bell actually does — a fast initial fall to a break point, then a slower
//  settle. That second stage is the whole reason this exists.
//
//  Segment shapes come from the same exponential maths as Shared/DSPCore/ADSR,
//  so the instrument keeps the curve it already had rather than gaining an
//  invented one.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace r50 {

/// A slope time at or below this disables the break stage entirely: decay then
/// runs straight from the attack level to sustain, which is exactly what ADSR
/// does. The sentinel is what lets every existing preset keep its sound — a
/// default break point could not, because sustain varies per patch and the two
/// would have to be kept in agreement.
static constexpr float kEnvelopeSlopeOff = 0.0015f;

class R50Envelope {
public:
    enum class Stage { Idle, Attack, Decay, Slope, Sustain, Release };

    void setSampleRate(double sr) {
        sampleRate_ = sr;
        refresh();
    }

    void setAttack(float seconds)      { attackTime_ = seconds;  refresh(); }
    void setAttackLevel(float level)   { attackLevel_ = clamp01(level); refresh(); }
    void setDecay(float seconds)       { decayTime_ = seconds;   refresh(); }
    void setBreakPoint(float level)    { breakPoint_ = clamp01(level); refresh(); }
    void setSlope(float seconds)       { slopeTime_ = seconds;   refresh(); }
    void setSustain(float level)       { sustainLevel_ = clamp01(level); refresh(); }
    void setRelease(float seconds)     { releaseTime_ = seconds; refresh(); }

    void gate(bool on) {
        if (on) {
            stage_ = Stage::Attack;
        } else if (stage_ != Stage::Idle) {
            stage_ = Stage::Release;
        }
    }

    void resetHard() { stage_ = Stage::Idle; output_ = 0.0; }

    bool isActive() const { return stage_ != Stage::Idle; }
    Stage stage() const { return stage_; }

    inline float process() {
        switch (stage_) {
            case Stage::Idle:
                break;

            case Stage::Attack:
                output_ = attackBase_ + output_ * attackCoef_;
                if (output_ >= attackLevel_) {
                    output_ = attackLevel_;
                    stage_ = Stage::Decay;
                }
                break;

            case Stage::Decay:
                output_ = decayBase_ + output_ * decayCoef_;
                if (reachedTarget(decayTarget_)) {
                    output_ = decayTarget_;
                    stage_ = breakEnabled() ? Stage::Slope : Stage::Sustain;
                }
                break;

            case Stage::Slope:
                output_ = slopeBase_ + output_ * slopeCoef_;
                if (reachedTarget(sustainLevel_)) {
                    output_ = sustainLevel_;
                    stage_ = Stage::Sustain;
                }
                break;

            case Stage::Sustain:
                output_ = sustainLevel_;
                break;

            case Stage::Release:
                output_ = releaseBase_ + output_ * releaseCoef_;
                if (output_ <= 0.0) { output_ = 0.0; stage_ = Stage::Idle; }
                break;
        }
        return static_cast<float>(output_);
    }

private:
    static float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

    bool breakEnabled() const { return slopeTime_ > kEnvelopeSlopeOff; }

    /// Where the decay segment is heading: the break point when the break stage
    /// is in use, otherwise straight to sustain.
    double decayDestination() const {
        return breakEnabled() ? breakPoint_ : sustainLevel_;
    }

    /// Segments may fall or rise — a break point below sustain gives a dip that
    /// recovers, which is a legitimate and useful shape — so "arrived" depends
    /// on the direction of travel.
    bool reachedTarget(double target) const {
        return descending_ ? (output_ <= target) : (output_ >= target);
    }

    static double calcCoef(double rate, double targetRatio) {
        return std::exp(-std::log((1.0 + targetRatio) / targetRatio) / rate);
    }

    void refresh() {
        const double attackRate  = std::max(1.0, attackTime_ * sampleRate_);
        const double decayRate   = std::max(1.0, decayTime_ * sampleRate_);
        const double slopeRate   = std::max(1.0, slopeTime_ * sampleRate_);
        const double releaseRate = std::max(1.0, releaseTime_ * sampleRate_);

        attackCoef_ = calcCoef(attackRate, targetRatioA_);
        attackBase_ = (attackLevel_ + targetRatioA_) * (1.0 - attackCoef_);

        decayTarget_ = decayDestination();
        decayCoef_ = calcCoef(decayRate, targetRatioDR_);
        decayBase_ = (decayTarget_ - targetRatioDR_) * (1.0 - decayCoef_);

        slopeCoef_ = calcCoef(slopeRate, targetRatioDR_);
        slopeBase_ = (sustainLevel_ - targetRatioDR_) * (1.0 - slopeCoef_);

        releaseCoef_ = calcCoef(releaseRate, targetRatioDR_);
        releaseBase_ = -targetRatioDR_ * (1.0 - releaseCoef_);

        descending_ = decayTarget_ <= attackLevel_;
    }

    double sampleRate_ = 44100.0;
    Stage  stage_ = Stage::Idle;
    double output_ = 0.0;

    float attackTime_   = 0.005f;
    float attackLevel_  = 1.0f;
    float decayTime_    = 0.1f;
    float breakPoint_   = 1.0f;
    float slopeTime_    = 0.0f;     // <= kEnvelopeSlopeOff: break stage skipped
    float sustainLevel_ = 0.8f;
    float releaseTime_  = 0.2f;

    // Segment curvature, matching Shared/DSPCore/ADSR so the default shape is
    // the one the instrument already had.
    double targetRatioA_  = 0.3;
    double targetRatioDR_ = 0.0001;

    double attackCoef_ = 0.0, decayCoef_ = 0.0, slopeCoef_ = 0.0, releaseCoef_ = 0.0;
    double attackBase_ = 0.0, decayBase_ = 0.0, slopeBase_ = 0.0, releaseBase_ = 0.0;
    double decayTarget_ = 0.0;
    bool   descending_  = true;
};

} // namespace r50
