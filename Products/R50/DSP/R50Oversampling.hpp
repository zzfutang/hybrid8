//
//  R50Oversampling.hpp
//  High-quality, allocation-free 2x oversampling for bandwidth-expanding DSP.
//
//  The 129-tap Kaiser-windowed half-band FIR keeps the 20 kHz passband flat at
//  44.1 kHz and reaches approximately -86 dB by the far edge of the transition
//  band (-100 dB shortly after it). Its even taps are zero apart from the
//  centre, and symmetry reduces each high-rate sample to 33 multiplies. The
//  64-sample high-rate group delay also gives exact integer base-rate latency
//  for both the 2x and cascaded 4x configurations.
//

#pragma once

#include <algorithm>

namespace r50 {

class HalfBandFIR2x {
public:
    static constexpr int kTaps = 129;
    static constexpr int kHighRateGroupDelay = (kTaps - 1) / 2;

    void reset() {
        std::fill(history_, history_ + kTaps, 0.0f);
        position_ = 0;
    }

    inline float process(float input) {
        history_[position_] = input;

        float sum = coefficient_[64] * delayed(64);
        // Every even coefficient other than the centre is mathematically zero.
        // Pair symmetric taps to halve the remaining multiplies.
        for (int tap = 1; tap < 64; tap += 2) {
            sum += coefficient_[tap]
                 * (delayed(tap) + delayed(kTaps - 1 - tap));
        }

        if (++position_ == kTaps) position_ = 0;
        return sum;
    }

private:
    inline float delayed(int samples) const {
        int index = position_ - samples;
        if (index < 0) index += kTaps;
        return history_[index];
    }

    // 129-tap half-band sinc, Kaiser beta 8.6, normalized to unity DC gain.
    static constexpr float coefficient_[kTaps] = {
        0.0f, -0.0000111822f, 0.0f, 0.0000247486f,
        0.0f, -0.0000461030f, 0.0f, 0.0000778752f,
        0.0f, -0.0001232086f, 0.0f, 0.0001857989f,
        0.0f, -0.0002699314f, 0.0f, 0.0003805157f,
        0.0f, -0.0005231209f, 0.0f, 0.0007040148f,
        0.0f, -0.0009302135f, 0.0f, 0.0012095470f,
        0.0f, -0.0015507563f, 0.0f, 0.0019636350f,
        0.0f, -0.0024592428f, 0.0f, 0.0030502244f,
        0.0f, -0.0037512876f, 0.0f, 0.0045799210f,
        0.0f, -0.0055574784f, 0.0f, 0.0067108359f,
        0.0f, -0.0080749695f, 0.0f, 0.0096970625f,
        0.0f, -0.0116432623f, 0.0f, 0.0140102585f,
        0.0f, -0.0169461659f, 0.0f, 0.0206907150f,
        0.0f, -0.0256592935f, 0.0f, 0.0326390248f,
        0.0f, -0.0433209067f, 0.0f, 0.0621089335f,
        0.0f, -0.1051653583f, 0.0f, 0.3179978580f,
        0.5000030240f,
        0.3179978580f, 0.0f, -0.1051653583f, 0.0f,
        0.0621089335f, 0.0f, -0.0433209067f, 0.0f,
        0.0326390248f, 0.0f, -0.0256592935f, 0.0f,
        0.0206907150f, 0.0f, -0.0169461659f, 0.0f,
        0.0140102585f, 0.0f, -0.0116432623f, 0.0f,
        0.0096970625f, 0.0f, -0.0080749695f, 0.0f,
        0.0067108359f, 0.0f, -0.0055574784f, 0.0f,
        0.0045799210f, 0.0f, -0.0037512876f, 0.0f,
        0.0030502244f, 0.0f, -0.0024592428f, 0.0f,
        0.0019636350f, 0.0f, -0.0015507563f, 0.0f,
        0.0012095470f, 0.0f, -0.0009302135f, 0.0f,
        0.0007040148f, 0.0f, -0.0005231209f, 0.0f,
        0.0003805157f, 0.0f, -0.0002699314f, 0.0f,
        0.0001857989f, 0.0f, -0.0001232086f, 0.0f,
        0.0000778752f, 0.0f, -0.0000461030f, 0.0f,
        0.0000247486f, 0.0f, -0.0000111822f, 0.0f
    };

    float history_[kTaps] = {};
    int position_ = 0;
};

class Oversampler2x {
public:
    static constexpr int latencySamples() {
        // Interpolation and decimation each contribute 64 high-rate samples.
        return HalfBandFIR2x::kHighRateGroupDelay;
    }

    void reset() {
        interpolation_.reset();
        decimation_.reset();
    }

    template<typename Processor>
    inline float process(float input, Processor&& processor) {
        const float first = 2.0f * interpolation_.process(input);
        const float second = 2.0f * interpolation_.process(0.0f);
        const float output = decimation_.process(processor(first));
        decimation_.process(processor(second));
        return output;
    }

private:
    HalfBandFIR2x interpolation_;
    HalfBandFIR2x decimation_;
};

class Oversampler4x {
public:
    static constexpr int latencySamples() {
        // Both interpolation/decimation stages contribute 64 samples in each
        // direction: 128 / 2 + 128 / 4 = 96 base-rate samples.
        return 96;
    }

    void reset() {
        interpolation2x_.reset();
        interpolation4x_.reset();
        decimation4x_.reset();
        decimation2x_.reset();
    }

    template<typename Processor>
    inline float process(float input, Processor&& processor) {
        const float at2x0 = 2.0f * interpolation2x_.process(input);
        const float at2x1 = 2.0f * interpolation2x_.process(0.0f);

        const float at4x0 = 2.0f * interpolation4x_.process(at2x0);
        const float at4x1 = 2.0f * interpolation4x_.process(0.0f);
        const float at4x2 = 2.0f * interpolation4x_.process(at2x1);
        const float at4x3 = 2.0f * interpolation4x_.process(0.0f);

        const float back2x0 = decimation4x_.process(processor(at4x0));
        decimation4x_.process(processor(at4x1));
        const float back2x1 = decimation4x_.process(processor(at4x2));
        decimation4x_.process(processor(at4x3));

        const float output = decimation2x_.process(back2x0);
        decimation2x_.process(back2x1);
        return output;
    }

private:
    HalfBandFIR2x interpolation2x_;
    HalfBandFIR2x interpolation4x_;
    HalfBandFIR2x decimation4x_;
    HalfBandFIR2x decimation2x_;
};

} // namespace r50
