//
//  Filter.hpp
//  Analogue-modelled low-pass filter. A TPT state-variable stage (Cytomic /
//  Zavalishin topology) provides a resonant 12 dB/oct 2-pole low-pass; a second
//  Butterworth stage adds another 12 dB/oct for the 24 dB mode. The two slopes
//  are always both computed and cross-faded so the slope control never clicks,
//  and the resonance lives in a single stage so peaks don't compound.
//

#pragma once
#include "Utils.hpp"

namespace synth {

// One 2-pole TPT state-variable low-pass stage (parameterised by Q).
class SVFStage {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset() { ic1eq_ = 0.0; ic2eq_ = 0.0; }

    inline void setCoefficients(double cutoffHz, double q) {
        double fc = clampf(static_cast<float>(cutoffHz), 20.0f,
                           static_cast<float>(sampleRate_ * 0.45));
        double g = std::tan(kPi * fc / sampleRate_);
        double k = 1.0 / q;               // damping
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    inline float processLP(float input) {
        double v3 = input - ic2eq_;
        double v1 = a1_ * ic1eq_ + a2_ * v3;
        double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0 * v1 - ic1eq_;
        ic2eq_ = 2.0 * v2 - ic2eq_;
        return static_cast<float>(v2);
    }

private:
    double sampleRate_ = 44100.0;
    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double ic1eq_ = 0.0, ic2eq_ = 0.0;
};

class LadderFilter {
public:
    void setSampleRate(double sr) {
        s1_.setSampleRate(sr);
        s2_.setSampleRate(sr);
    }
    void reset() { s1_.reset(); s2_.reset(); }

    // slopeMix: 0 = 12 dB/oct .. 1 = 24 dB/oct (cross-faded, click-free).
    inline void setParams(double cutoffHz, float resonance, float slopeMix,
                          float analog, float drive) {
        // Resonance drives ONE stage only (Q up to ~10) so the 24 dB mode does
        // not multiply peaks; the 2nd stage is a fixed Butterworth (Q=0.707).
        double q = 0.5 + static_cast<double>(resonance) * resonance * 9.5;
        s1_.setCoefficients(cutoffHz, q);
        s2_.setCoefficients(cutoffHz, 0.70710678);
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

        // Both slopes always run (no stale state), then cross-fade.
        float y2p = s1_.processLP(x);          // 2-pole / 12 dB
        float y4p = s2_.processLP(y2p);        // 4-pole / 24 dB
        return y2p + (y4p - y2p) * slopeMix_;
    }

private:
    SVFStage s1_, s2_;
    float slopeMix_ = 0.0f;
    float analog_ = 0.0f;
    float drive_  = 0.0f;
    float driveGain_ = 1.0f;
    float makeup_ = 1.0f;
    float analogSat_ = 1.0f;
};

} // namespace synth
