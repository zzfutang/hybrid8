//
//  R50DigitalFilter.hpp
//  Deliberately digital, linear resonant low-pass filter for R50.
//
//  The instruments that motivate R50 used digital filters as tone controls,
//  not as overdriven analogue circuit models.  This filter therefore has no
//  saturation, drive or "analogue" parameter.  A topology-preserving
//  state-variable section supplies the 12 dB response; a second identical
//  section adds the 24 dB response.  Both paths stay live so slope changes can
//  be cross-faded without stale state or clicks.
//

#pragma once

#include <cmath>

#include "Utils.hpp"

namespace r50 {

class DigitalSVFLowPass {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = std::max(1.0, sampleRate);
    }

    void reset() {
        ic1eq_ = 0.0;
        ic2eq_ = 0.0;
    }

    void setParams(double cutoffHz, double q) {
        const double cutoff = synth::clampf(
            static_cast<float>(cutoffHz), 20.0f,
            static_cast<float>(sampleRate_ * 0.45));
        const double limitedQ = std::max(0.5, std::min(12.0, q));
        const double g = std::tan(synth::kPi * cutoff / sampleRate_);
        const double k = 1.0 / limitedQ;
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    inline float process(float input) {
        const double v3 = static_cast<double>(input) - ic2eq_;
        const double v1 = a1_ * ic1eq_ + a2_ * v3;
        const double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0 * v1 - ic1eq_;
        ic2eq_ = 2.0 * v2 - ic2eq_;
        return static_cast<float>(v2);
    }

private:
    double sampleRate_ = 44100.0;
    double a1_ = 1.0, a2_ = 0.0, a3_ = 0.0;
    double ic1eq_ = 0.0, ic2eq_ = 0.0;
};

class DigitalLowPassFilter {
public:
    void setSampleRate(double sampleRate) {
        first_.setSampleRate(sampleRate);
        second_.setSampleRate(sampleRate);
    }

    void reset() {
        first_.reset();
        second_.reset();
    }

    void setParams(double cutoffHz, float resonance, float slope) {
        const double amount = synth::clampf(resonance, 0.0f, 1.0f);
        // A squared taper leaves most of the control useful for gentle tone
        // shaping but still permits a pronounced, clean digital resonance.
        const double q = 0.7071067811865476 + amount * amount * 11.292893218813452;
        first_.setParams(cutoffHz, q);
        // Keep the extra 12 dB section Butterworth-damped. Resonance belongs to
        // the filter as a whole rather than being multiplied by both cascades.
        second_.setParams(cutoffHz, 0.7071067811865476);
        slope_ = synth::clampf(slope, 0.0f, 1.0f);
    }

    inline float process(float input) {
        const float twelve = first_.process(input);
        const float twentyFour = second_.process(twelve);
        return twelve + (twentyFour - twelve) * slope_;
    }

private:
    DigitalSVFLowPass first_;
    DigitalSVFLowPass second_;
    float slope_ = 0.0f;
};

} // namespace r50
