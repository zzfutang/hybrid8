//  osc_test.cpp — is the per-cycle overshoot from PolyBLEP or from the decimator?
//  clang++ -std=c++17 -O2 Tests/osc_test.cpp -o /tmp/osc && /tmp/osc
#include "../Extension/DSP/Oscillator.hpp"
#include "../Extension/DSP/Decimator.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
using namespace synth;

static void report(const char* label, const std::vector<float>& x, double sr) {
    float mx = 0; int at = 0;
    for (size_t n = 1; n < x.size(); ++n) { float d = std::fabs(x[n]-x[n-1]); if (d > mx) { mx = d; at = (int)n; } }
    printf("%-28s maxJump=%.4f at %d\n   window:", label, mx, at);
    for (int n = at-3; n <= at+4 && n < (int)x.size(); ++n) printf(" %+.4f", x[n]);
    printf("\n");
}

int main() {
    const double sr = 48000.0;
    const double f = 261.6; // middle C, period ~183.5 samples

    // A) pure PolyBLEP saw at 1x (what we had before oversampling)
    {
        Oscillator o; o.setSampleRate(sr); o.setWave(OscWave::Saw); o.setFrequency(f);
        std::vector<float> x(1000);
        for (auto& s : x) s = o.process();
        report("A) pure polyBLEP saw 1x", x, sr);
    }

    // B) saw at 2x through the Butterworth decimator (current path)
    {
        Oscillator o; o.setSampleRate(sr * 2); o.setWave(OscWave::Saw); o.setFrequency(f);
        Decimator2x d; d.setup(sr);
        std::vector<float> x(1000);
        for (auto& s : x) { float a = d.process(o.process()); s = d.process(o.process()); }
        report("B) saw 2x + Butterworth decim", x, sr);
    }
    return 0;
}
