//
//  gen_audio.cpp
//  Renders the "TZ-FM Bells" patch played staccato (fast enough to force voice
//  stealing), writes a WAV, and reports click-like sample discontinuities and
//  whether they line up with note-on events.
//
//  Build: clang++ -std=c++17 -O2 Tests/gen_audio.cpp -o /tmp/gen && /tmp/gen
//

#include "../Extension/DSP/SynthEngine.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace synth;

static void writeWav(const char* path, const std::vector<float>& x, double sr) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t n = (uint32_t)x.size();
    uint32_t dataBytes = n * 2;
    uint32_t rate = (uint32_t)sr;
    uint32_t byteRate = rate * 2;
    auto w32 = [&](uint32_t v){ fwrite(&v,4,1,f); };
    auto w16 = [&](uint16_t v){ fwrite(&v,2,1,f); };
    fwrite("RIFF",1,4,f); w32(36 + dataBytes); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); w32(16); w16(1); w16(1); w32(rate); w32(byteRate); w16(2); w16(16);
    fwrite("data",1,4,f); w32(dataBytes);
    for (float s : x) {
        int v = (int)std::lround(std::max(-1.f, std::min(1.f, s)) * 32767.f);
        w16((uint16_t)(int16_t)v);
    }
    fclose(f);
}

int main() {
    const double sr = 48000.0;
    SynthEngine e; e.setSampleRate(sr);

    // ---- "TZ-FM Bells" ----
    e.setParameter(SynthParamOscWaveform, 0);
    e.setParameter(SynthParamOsc2Waveform, 0);
    e.setParameter(SynthParamOscCrossMod, 0.4f);
    e.setParameter(SynthParamOscCrossModTZ, 1.0f);
    e.setParameter(SynthParamOsc2Semitone, 7.0f);
    e.setParameter(SynthParamLFOToCrossMod, 0.25f);
    e.setParameter(SynthParamLFORate, 3.0f);
    e.setParameter(SynthParamOscMix, 0.6f);
    e.setParameter(SynthParamFilterCutoff, 8000.0f);
    e.setParameter(SynthParamFilterResonance, 0.1f);
    e.setParameter(SynthParamFilterEnvAmount, 0.35f);
    e.setParameter(SynthParamFilterDecay, normFromTime(0.7f));
    e.setParameter(SynthParamFilterSustain, 0.15f);
    e.setParameter(SynthParamAmpAttack, normFromTime(0.001f));
    e.setParameter(SynthParamAmpDecay, normFromTime(0.9f));
    e.setParameter(SynthParamAmpSustain, 0.15f);
    e.setParameter(SynthParamAmpRelease, normFromTime(0.7f));
    e.setParameter(SynthParamAnalogAmount, 0.3f);

    std::vector<float> out;
    std::vector<long>  noteOnAt;      // sample index of each note-on
    std::vector<float> dummyR;

    auto renderMs = [&](double ms){
        int frames = (int)(sr * ms / 1000.0);
        std::vector<float> L(frames), R(frames);
        e.render(L.data(), R.data(), frames);
        for (float s : L) out.push_back(s);
    };

    // Staccato run: notes every 80 ms (60 ms on + 20 ms off), 24 notes.
    // Tails (~1.5 s) far exceed 8 voices, so stealing kicks in mid-run.
    const int scale[] = {60, 64, 67, 72, 64, 67, 60, 72};
    for (int i = 0; i < 24; ++i) {
        int note = scale[i % 8] + (i / 8) * 0; // stay in range
        noteOnAt.push_back((long)out.size());
        e.noteOn(note, 110);
        renderMs(60);
        e.noteOff(note);
        renderMs(20);
    }
    renderMs(1500); // let tails ring out

    writeWav("/tmp/tzbells.wav", out, sr);

    printf("Rendered %zu samples (%.2f s) to /tmp/tzbells.wav\n\n", out.size(), out.size()/sr);

    // Baseline: typical sample-to-sample jump away from note-ons (median-ish
    // via a coarse histogram of the largest 1% is overkill; use mean abs diff).
    double meanDiff = 0; for (size_t n = 1; n < out.size(); ++n) meanDiff += std::fabs(out[n]-out[n-1]);
    meanDiff /= (out.size()-1);
    printf("Mean |x[n]-x[n-1]| = %.4f (baseline signal steepness)\n\n", meanDiff);

    // Per note-on: jump exactly at the boundary and the max jump in the 6 ms
    // right after. Fresh voices start silent (small jump); a stolen, still-
    // ringing voice that gets hard-reset shows a big anomalous jump.
    printf("note#  voice     jump@on   maxjump[+6ms]   (active voices before)\n");
    int activeApprox = 0;
    for (size_t i = 0; i < noteOnAt.size(); ++i) {
        long s = noteOnAt[i];
        float jAt = (s > 0) ? std::fabs(out[s] - out[s-1]) : 0.f;
        float jMax = 0; long win = (long)(sr*0.006);
        for (long n = s+1; n < s+win && n < (long)out.size(); ++n)
            jMax = std::max(jMax, std::fabs(out[n]-out[n-1]));
        // notes ring ~1.6 s; with 80 ms spacing, ~ (1600/80)=20 could overlap,
        // capped at 8 -> stealing begins ~ note 9.
        const char* kind = (i >= 8) ? "STEAL" : "fresh";
        printf("  %2zu   %-6s   %7.4f   %10.4f\n", i+1, kind, jAt, jMax);
    }
    (void)activeApprox;

    // Dump the onset waveform of note 1 and the region around the first big jump.
    printf("\nNote 1 onset (samples 0..47):\n");
    for (int n = 0; n < 48; ++n) printf("%+.4f%s", out[n], (n%8==7)?"\n":" ");
    printf("\nAround first big jump (samples 178..200):\n");
    for (int n = 178; n < 200; ++n) printf("%+.4f%s", out[n], (n%8==7)?"\n":" ");
    printf("\n");
    return 0;
}
