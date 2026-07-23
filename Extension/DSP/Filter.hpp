//
//  Filter.hpp
//  Analogue-modelled low-pass filter. A TPT state-variable stage (Cytomic /
//  Zavalishin topology) provides a resonant 12 dB/oct 2-pole low-pass. The
//  24 dB mode is a four-stage trapezoidal ladder with nonlinear zero-delay
//  feedback and stable self-oscillation. Both models are always computed and
//  cross-faded so the slope control never clicks.
//

#pragma once
#include "Utils.hpp"

namespace synth {

// Nonlinear 2-pole TPT state-variable low-pass stage. The band-pass damping
// feedback is saturated and solved without a unit delay; at small signal the
// tanh normalisation reduces exactly to the familiar linear TPT SVF.
class SVFStage {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset() { ic1eq_ = 0.0; ic2eq_ = 0.0; }

    inline void setCoefficients(double cutoffHz, double q,
                                float drive, float analog) {
        double fc = clampf(static_cast<float>(cutoffHz), 20.0f,
                           static_cast<float>(sampleRate_ * 0.45));
        g_ = std::tan(kPi * fc / sampleRate_);
        k_ = 1.0 / q;               // damping
        // Drive increases the OTA-like feedback compression. A small baseline
        // keeps the clean model gently bounded while remaining virtually
        // identical to the linear response at normal signal levels.
        saturation_ = 0.25 + static_cast<double>(drive) * 4.0
                           + static_cast<double>(analog) * 0.5;
    }

    inline float processLP(float input) {
        const double u = static_cast<double>(input);

        // Linear closed-form solution is the Newton starting point.
        double hp = (u - (k_ + g_) * ic1eq_ - ic2eq_)
                  / (1.0 + k_ * g_ + g_ * g_);
        for (int iteration = 0; iteration < 3; ++iteration) {
            const double bp = g_ * hp + ic1eq_;
            const double lp = g_ * bp + ic2eq_;
            const double t = std::tanh(saturation_ * bp);
            const double saturatedBP = t / saturation_;
            const double f = hp + k_ * saturatedBP + lp - u;
            const double derivative = 1.0 + g_ * g_
                                    + k_ * g_ * (1.0 - t * t);
            hp -= f / derivative;
        }

        const double bp = g_ * hp + ic1eq_;
        const double lp = g_ * bp + ic2eq_;
        ic1eq_ = 2.0 * bp - ic1eq_;
        ic2eq_ = 2.0 * lp - ic2eq_;
        return static_cast<float>(lp);
    }

private:
    double sampleRate_ = 44100.0;
    double g_ = 0.0, k_ = 1.0;
    double saturation_ = 0.25;
    double ic1eq_ = 0.0, ic2eq_ = 0.0;
};

// Four cascaded TPT one-poles with feedback around the entire cascade.
//
// For fixed states, the cascade output is affine in its input:
//     y4 = gamma * x + sigma
// The nonlinear feedback equation
//     x = tanh(input - k * y4)
// is therefore solved cheaply with Newton iterations and no unit delay in the
// resonance loop. At k ~= 4 the four-pole loop reaches self-oscillation; tanh
// limits its amplitude like the differential pairs in an analogue ladder.
class FourPoleLadder {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset() {
        z1_ = 1.0e-6; // analogue-style seed; inaudible unless resonance sustains it
        z2_ = z3_ = z4_ = 0.0;
    }

    inline void setCoefficients(double cutoffHz, float resonance) {
        const double fc = clampf(static_cast<float>(cutoffHz), 20.0f,
                                 static_cast<float>(sampleRate_ * 0.45));
        const double g = std::tan(kPi * fc / sampleRate_);
        G_ = g / (1.0 + g);
        // Slightly exceed the theoretical k=4 onset at the top of the control
        // so maximum resonance produces a musically stable sine oscillator.
        k_ = 4.35 * static_cast<double>(clampf(resonance, 0.0f, 1.0f));
    }

    inline float process(float input) {
        const double oneMinusG = 1.0 - G_;
        const double g2 = G_ * G_;
        const double gamma = g2 * g2;
        const double sigma = oneMinusG
                           * (G_ * g2 * z1_ + g2 * z2_ + G_ * z3_ + z4_);

        // Linear closed-loop solution is an excellent Newton starting point.
        const double u = static_cast<double>(input);
        double x = (u - k_ * sigma) / (1.0 + k_ * gamma);
        for (int iteration = 0; iteration < 3; ++iteration) {
            const double arg = u - k_ * (gamma * x + sigma);
            const double t = std::tanh(arg);
            const double f = x - t;
            const double derivative = 1.0 + k_ * gamma * (1.0 - t * t);
            x -= f / derivative;
        }
        const double y1 = G_ * x  + oneMinusG * z1_;
        const double y2 = G_ * y1 + oneMinusG * z2_;
        const double y3 = G_ * y2 + oneMinusG * z3_;
        const double y4 = G_ * y3 + oneMinusG * z4_;

        // Trapezoidal-integrator state updates.
        z1_ = 2.0 * y1 - z1_;
        z2_ = 2.0 * y2 - z2_;
        z3_ = 2.0 * y3 - z3_;
        z4_ = 2.0 * y4 - z4_;
        return static_cast<float>(y4);
    }

private:
    double sampleRate_ = 44100.0;
    double G_ = 0.0;
    double k_ = 0.0;
    double z1_ = 1.0e-6, z2_ = 0.0, z3_ = 0.0, z4_ = 0.0;
};

class LadderFilter {
public:
    void setSampleRate(double sr) {
        s1_.setSampleRate(sr);
        ladder_.setSampleRate(sr);
    }
    void reset() { s1_.reset(); ladder_.reset(); }

    // slopeMix: 0 = 12 dB/oct .. 1 = 24 dB/oct (cross-faded, click-free).
    inline void setParams(double cutoffHz, float resonance, float slopeMix,
                          float analog, float drive) {
        // The 12 dB SVF uses Q; the 24 dB ladder maps the same control to its
        // four-pole feedback coefficient.
        double q = 0.5 + static_cast<double>(resonance) * resonance * 9.5;
        s1_.setCoefficients(cutoffHz, q, drive, analog);
        ladder_.setCoefficients(cutoffHz, resonance);
        slopeMix_ = clampf(slopeMix, 0.0f, 1.0f);
        analog_ = analog;
        drive_  = drive;
        // Overdrive gain grows exponentially for a musical taper; makeup gain
        // tames the extra level so "Drive" adds grit rather than just volume.
        driveGain_ = 1.0f + drive * drive * 22.0f;
        makeup_    = 1.0f / (1.0f + drive * 1.6f);
        analogSat_ = 1.0f + analog * 2.0f;
    }

    inline float process(float input) {
        float x = input;

        // Dedicated overdrive / distortion stage (pre-filter).
        if (drive_ > 0.0001f) {
            x = std::tanh(x * driveGain_) * makeup_;
        }

        // Subtle analogue saturation, gain-normalised so it colours the tone
        // without changing small-signal level; blended by the analog amount so
        // analog == 0 stays perfectly clean.
        if (analog_ > 0.0f) {
            float dirty = softClip(x * analogSat_) / analogSat_;
            x = x + (dirty - x) * analog_;
        }

        // Both topologies always run (no stale state), then cross-fade.
        float y2p = s1_.processLP(x);          // 2-pole / 12 dB
        float y4p = ladder_.process(x);         // nonlinear 4-pole / 24 dB
        return y2p + (y4p - y2p) * slopeMix_;
    }

private:
    SVFStage s1_;
    FourPoleLadder ladder_;
    float slopeMix_ = 0.0f;
    float analog_ = 0.0f;
    float drive_  = 0.0f;
    float driveGain_ = 1.0f;
    float makeup_ = 1.0f;
    float analogSat_ = 1.0f;
};

} // namespace synth
