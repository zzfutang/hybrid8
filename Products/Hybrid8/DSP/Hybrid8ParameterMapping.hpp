#pragma once

#include "../../../Extension/SynthParameters.h"
#include "../../../Shared/DSPCore/Utils.hpp"

namespace synth {

// Hybrid8's public parameter ABI stores envelope times normalized to 0...1.
// This conversion belongs to the product adapter, not to the reusable DSP core.
inline float timeFromNorm(float normalized) {
    normalized = clampf(normalized, 0.0f, 1.0f);
    const float exponent = std::pow(normalized, static_cast<float>(SYNTH_TIME_SKEW));
    return static_cast<float>(SYNTH_TIME_MIN) *
        std::pow(static_cast<float>(SYNTH_TIME_MAX / SYNTH_TIME_MIN), exponent);
}

inline float normFromTime(float seconds) {
    seconds = clampf(seconds, static_cast<float>(SYNTH_TIME_MIN),
                     static_cast<float>(SYNTH_TIME_MAX));
    const float exponent =
        std::log(seconds / static_cast<float>(SYNTH_TIME_MIN)) /
        std::log(static_cast<float>(SYNTH_TIME_MAX / SYNTH_TIME_MIN));
    return std::pow(std::max(0.0f, exponent),
                    1.0f / static_cast<float>(SYNTH_TIME_SKEW));
}

} // namespace synth
