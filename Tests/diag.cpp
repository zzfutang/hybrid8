//  diag.cpp — isolate the per-cycle glitch in TZ-FM Bells.
//  clang++ -std=c++17 -O2 Tests/diag.cpp -o /tmp/diag && /tmp/diag <variant>
#include "../Extension/DSP/SynthEngine.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace synth;

int main(int argc, char** argv) {
    const double sr = 48000.0;
    const char* v = argc > 1 ? argv[1] : "base";
    SynthEngine e; e.setSampleRate(sr);
    // base TZ-FM Bells
    e.setParameter(SynthParamOscCrossMod, 0.4f);
    e.setParameter(SynthParamOscCrossModTZ, 1.0f);
    e.setParameter(SynthParamOsc2Semitone, 7.0f);
    e.setParameter(SynthParamLFOToCrossMod, 0.25f);
    e.setParameter(SynthParamLFORate, 3.0f);
    e.setParameter(SynthParamOscMix, 0.6f);
    e.setParameter(SynthParamFilterCutoff, 8000.0f);
    e.setParameter(SynthParamFilterEnvAmount, 0.35f);
    e.setParameter(SynthParamAmpSustain, 0.6f);
    e.setParameter(SynthParamAnalogAmount, 0.3f);

    if (!strcmp(v, "nofm"))  e.setParameter(SynthParamOscCrossMod, 0.0f);
    if (!strcmp(v, "exp"))   e.setParameter(SynthParamOscCrossModTZ, 0.0f);
    if (!strcmp(v, "osc1"))  e.setParameter(SynthParamOscMix, 0.0f);
    if (!strcmp(v, "osc2"))  e.setParameter(SynthParamOscMix, 1.0f);
    if (!strcmp(v, "nolfo")) e.setParameter(SynthParamLFOToCrossMod, 0.0f);
    if (!strcmp(v, "noanalog")) e.setParameter(SynthParamAnalogAmount, 0.0f);

    int N = (int)(sr * 0.3);
    std::vector<float> L(N), R(N);
    e.noteOn(60, 110);
    e.render(L.data(), R.data(), N);

    // largest jump after the onset settles (sample > 2400 = 50 ms)
    float mx = 0; int at = 0;
    for (int n = 2400; n < N; ++n) { float d = std::fabs(L[n]-L[n-1]); if (d > mx) { mx = d; at = n; } }
    printf("variant=%-8s  maxJump(after50ms)=%.4f at sample %d (t=%.4fs)\n", v, mx, at, at/sr);
    printf("  window: ");
    for (int n = at-4; n <= at+4; ++n) printf("%+.4f ", L[n]);
    printf("\n");
    return 0;
}
