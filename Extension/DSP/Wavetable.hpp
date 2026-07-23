//
//  Wavetable.hpp
//  2D wavetable oscillator for the synth.
//
//    Axis A (frame)   — morph between waveforms of different harmonic content.
//    Axis B (variant) — slow phase-domain drift ("liveness") so held notes
//                        breathe instead of looping a static cycle.
//
//  Tables are band-limited by construction (additive synthesis of exactly the
//  harmonics that fit below 20 kHz for each mip level), so there is no aliasing
//  and no FFT. The whole library is generated once, off the audio thread, and
//  is immutable afterwards. The audio thread only reads samples and interpolates
//  — no allocation, no locks, no trig.
//

#pragma once
#include "Utils.hpp"
#include <vector>
#include <array>

namespace synth {

// ---- Sizes ---------------------------------------------------------------
static constexpr int    WT_BASE_LEN     = 1024;   // richest table length (<=511 harmonics)
static constexpr int    WT_MIN_LEN      = 64;
static constexpr int    WT_NUM_FRAMES   = 32;     // timbre axis resolution
static constexpr int    WT_NUM_VARIANTS = 2;      // liveness axis resolution
static constexpr int    WT_NUM_LEVELS   = 10;     // mip levels
static constexpr int    WT_NUM_SETS     = 4;      // Harmonic / FM / Choir / Metallic
static constexpr double WT_F_MIN        = 20.0;
static constexpr double WT_AUDIBLE_MAX  = 20000.0; // band-limit ceiling (Hz)

// ---- Data structures -----------------------------------------------------
struct WTMip {
    std::vector<float> samples;   // length+1 (last is a guard copy of [0])
    int length = 0;
};
struct WTPyramid { std::array<WTMip, WT_NUM_LEVELS> levels; };
struct WavetableSet {
    std::vector<WTPyramid> pyramids;  // WT_NUM_FRAMES * WT_NUM_VARIANTS
    const WTPyramid& at(int frame, int variant) const {
        return pyramids[frame * WT_NUM_VARIANTS + variant];
    }
};
struct WavetableLibrary { std::array<WavetableSet, WT_NUM_SETS> sets; };

// ---- Mip helpers ---------------------------------------------------------
inline int wtMaxHarmonic(int level) {
    double fTop = WT_F_MIN * std::pow(2.0, level + 1);
    int k = (int)std::floor(WT_AUDIBLE_MAX / fTop);
    return std::max(1, std::min(k, WT_BASE_LEN / 2 - 1));
}
inline int wtLevelForFreq(double f0) {
    if (f0 <= WT_F_MIN) return 0;
    int L = (int)std::floor(std::log2(f0 / WT_F_MIN));
    return std::max(0, std::min(L, WT_NUM_LEVELS - 1));
}
inline int wtNextPow2(int x) { int p = WT_MIN_LEN; while (p < x) p <<= 1; return p; }

// Deterministic pseudo-random phase in [0, 2pi) for a (harmonic, seed) pair.
inline float wtRandPhase(int k, int seed) {
    uint32_t h = (uint32_t)(k * 2654435761u) ^ (uint32_t)(seed * 40503u + 12345u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)((h & 0xffffffu) / (double)0x1000000 * kTwoPi);
}

// ---- Spectrum ------------------------------------------------------------
struct WTSpectrum { std::vector<float> mag, phase; }; // index k = harmonic

// Additive synthesis of one mip level from a spectrum, using a per-harmonic
// phasor recurrence (no trig in the inner loop). normFactor scales the result
// (0 = leave unnormalised, used for level 0 to measure the peak first).
inline WTMip wtBuildMip(const WTSpectrum& s, int level, float normFactor) {
    int maxH = wtMaxHarmonic(level);
    int len  = std::min(WT_BASE_LEN, std::max(WT_MIN_LEN, wtNextPow2(2 * maxH)));
    WTMip t; t.length = len; t.samples.assign(len + 1, 0.0f);
    int H = std::min(maxH, (int)s.mag.size() - 1);
    for (int k = 1; k <= H; ++k) {
        float m = s.mag[k];
        if (m == 0.0f) continue;
        double w  = kTwoPi * k / len;
        double cr = std::cos(s.phase[k]), ci = std::sin(s.phase[k]);
        double cw = std::cos(w), sw = std::sin(w);
        for (int n = 0; n < len; ++n) {
            t.samples[n] += (float)(m * cr);
            double ncr = cr * cw - ci * sw;
            double nci = cr * sw + ci * cw;
            cr = ncr; ci = nci;
        }
    }
    if (normFactor > 0.0f) for (auto& x : t.samples) x /= normFactor;
    t.samples[len] = t.samples[0]; // guard sample (no wrap branch when reading)
    return t;
}

// Build the full pyramid, normalising every level by the level-0 peak so
// loudness does not jump as the mip level changes with pitch.
inline WTPyramid wtBuildPyramid(const WTSpectrum& s) {
    WTPyramid p;
    p.levels[0] = wtBuildMip(s, 0, 0.0f);
    float peak = 1e-9f;
    for (float x : p.levels[0].samples) peak = std::max(peak, std::fabs(x));
    for (float& x : p.levels[0].samples) x /= peak;
    for (int L = 1; L < WT_NUM_LEVELS; ++L) p.levels[L] = wtBuildMip(s, L, peak);
    return p;
}

// Slow deterministic phase perturbation for a liveness variant (magnitudes
// untouched -> same timbre, animated phase). Upper partials move more.
inline WTSpectrum wtVariant(const WTSpectrum& base, int variant, int seed) {
    if (variant == 0) return base;
    WTSpectrum s = base;
    const float depth = 0.5f;
    for (int k = 1; k < (int)s.phase.size(); ++k) {
        float noise = std::sin(k * 1.7f + variant * 3.1f + seed) * 0.6f
                    + std::sin(k * 0.53f + variant * 5.9f) * 0.4f;
        float scale = std::min(std::sqrt((float)k), 8.0f);
        s.phase[k] += depth * noise * scale;
    }
    return s;
}

// ---- Spectrum generators (one Spectrum per frame) ------------------------
inline WTSpectrum wtSpectrumHarmonic(int frame) {
    int maxH = wtMaxHarmonic(0);
    WTSpectrum s; s.mag.assign(maxH + 1, 0.0f); s.phase.assign(maxH + 1, 0.0f);
    double t = (double)frame / (WT_NUM_FRAMES - 1);   // 0..1

    if (t < 0.5) {
        // First half: classic harmonic sweep, sine -> saw.
        double u = t * 2.0;
        int Hf = std::max(1, (int)std::round(std::pow((double)maxH, u)));
        for (int k = 1; k <= Hf; ++k) { s.mag[k] = 1.0f / k; s.phase[k] = -1.5707963f; }
    } else {
        // Second half: the saw morphs into a plucked-string / guitar tone.
        // A pluck at position p introduces a comb (nulls harmonics at 1/p), and
        // the top rolls off from bright (near saw) to a mellow nylon-string tone.
        double u    = (t - 0.5) * 2.0;                    // 0..1
        double comb = u;                                   // comb depth
        double p    = 0.22;                                // pluck position
        double kCut = maxH * std::pow(0.03, u);            // bright -> mellow
        for (int k = 1; k <= maxH; ++k) {
            double src    = 1.0 / k;
            double combf  = (1.0 - comb) + comb * std::fabs(std::sin(kPi * k * p));
            double bright = 1.0 / (1.0 + (double)(k * k) / (kCut * kCut));
            s.mag[k]   = (float)(src * combf * bright);
            s.phase[k] = -1.5707963f;
        }
    }
    return s;
}

inline WTSpectrum wtSpectrumFM(int frame) {
    int maxH = wtMaxHarmonic(0);
    WTSpectrum s; s.mag.assign(maxH + 1, 0.0f); s.phase.assign(maxH + 1, 0.0f);
    double I = (double)frame / (WT_NUM_FRAMES - 1) * 8.0;   // modulation index 0..8
    const int L = 1024;
    std::vector<double> x(L);
    for (int n = 0; n < L; ++n) {
        double tt = (double)n / L;
        x[n] = std::sin(kTwoPi * tt + I * std::sin(kTwoPi * tt)); // c:m = 1:1
    }
    for (int k = 1; k <= maxH; ++k) {
        double w = kTwoPi * k / L, cr = 1.0, ci = 0.0;
        double cw = std::cos(w), sw = std::sin(w), re = 0.0, im = 0.0;
        for (int n = 0; n < L; ++n) {
            re += x[n] * cr; im -= x[n] * ci;
            double ncr = cr * cw - ci * sw, nci = cr * sw + ci * cw; cr = ncr; ci = nci;
        }
        s.mag[k]   = (float)(std::sqrt(re * re + im * im) * 2.0 / L);
        s.phase[k] = (float)std::atan2(im, re);
    }
    return s;
}

inline WTSpectrum wtSpectrumChoir(int frame) {
    int maxH = wtMaxHarmonic(0);
    WTSpectrum s; s.mag.assign(maxH + 1, 0.0f); s.phase.assign(maxH + 1, 0.0f);
    // Five formants (F1..F5, Hz) per vowel: oo -> oh -> ah -> eh -> ee.
    static const double vowelF[5][5] = {
        {325,  700, 2530, 3500, 4500},   // oo /u/
        {500,  800, 2830, 3500, 4500},   // oh /o/
        {700, 1150, 2600, 3300, 4500},   // ah /a/
        {530, 1680, 2500, 3500, 4500},   // eh /e/
        {270, 2140, 2950, 3900, 4500},   // ee /i/
    };
    // Per-formant peak gain (dB) and bandwidth (Hz); bandwidths widen with
    // formant number, as in a real vocal tract.
    static const double gainDB[5] = {  0.0, -7.0, -13.0, -18.0, -24.0 };
    static const double bwHz[5]   = { 55.0, 85.0, 120.0, 160.0, 220.0 };

    double pos = (double)frame / (WT_NUM_FRAMES - 1) * 4.0;
    int vi = std::min((int)pos, 4), vj = std::min(vi + 1, 4);
    double vt = pos - vi;
    double F[5], G[5];
    for (int i = 0; i < 5; ++i) {
        F[i] = vowelF[vi][i] + vt * (vowelF[vj][i] - vowelF[vi][i]);
        G[i] = std::pow(10.0, gainDB[i] / 20.0);
    }

    // Low reference fundamental so the harmonics sample each formant densely
    // (smooth, vowel-like peaks rather than a single spike per formant).
    const double fRef = 110.0;
    for (int k = 1; k <= maxH; ++k) {
        double freq = k * fRef;
        // Voiced glottal source: ~ -12 dB/oct — warm, with the low harmonics
        // dominant, so the formant peaks (not raw harmonics) define the tone.
        double src = 1.0 / std::pow((double)k, 1.9);

        // Resonant formants (Lorentzian peaks). A very low inter-formant floor
        // keeps deep valleys between the formants -> clear vowel, not a smooth
        // organ-like harmonic series.
        double form = 0.012;
        for (int i = 0; i < 5; ++i) {
            double d = (freq - F[i]) / (bwHz[i] * 0.5);
            form += G[i] / (1.0 + d * d);
        }
        // "Singer's formant": the ~2.8-3.2 kHz ring that gives choral/operatic
        // voices their carrying presence.
        double dsf = (freq - 3000.0) / 350.0;
        form += 0.45 / (1.0 + dsf * dsf);

        // Gentle high-frequency roll-off so the top stays smooth, not harsh.
        double hf = 1.0 / (1.0 + std::pow(freq / 7000.0, 2.2));

        s.mag[k]   = (float)(src * form * hf);
        s.phase[k] = wtRandPhase(k, 700 + frame);    // random phase -> diffuse
    }
    return s;
}

inline WTSpectrum wtSpectrumMetallic(int frame) {
    int maxH = wtMaxHarmonic(0);
    WTSpectrum s; s.mag.assign(maxH + 1, 0.0f); s.phase.assign(maxH + 1, 0.0f);
    static const int sparse[] = {1, 3, 5, 9, 11, 17, 23, 29};
    static const float amps[]  = {1.0f, 0.5f, 0.7f, 0.35f, 0.5f, 0.28f, 0.4f, 0.22f};
    for (int i = 0; i < 8; ++i) {
        int k = sparse[i];
        if (k <= maxH) { s.mag[k] = amps[i]; s.phase[k] = wtRandPhase(k, 200 + frame); }
    }
    // A cluster of adjacent partials that beat against each other; it opens up
    // (moves higher / widens) across the frame axis.
    double t = (double)frame / (WT_NUM_FRAMES - 1);
    int center = (int)(16 + t * 30), half = 5 + (int)(t * 6);
    for (int k = center - half; k <= center + half; ++k) {
        if (k >= 1 && k <= maxH) {
            float a = 0.12f * (0.5f + 0.5f * std::sin(wtRandPhase(k, 900 + frame)));
            s.mag[k] += a;
            s.phase[k] = wtRandPhase(k, 950 + frame);
        }
    }
    return s;
}

// Build the whole immutable library. Call once, off the audio thread.
inline WavetableLibrary wtBuildLibrary() {
    WavetableLibrary lib;
    for (int set = 0; set < WT_NUM_SETS; ++set) {
        lib.sets[set].pyramids.resize(WT_NUM_FRAMES * WT_NUM_VARIANTS);
        for (int f = 0; f < WT_NUM_FRAMES; ++f) {
            WTSpectrum base;
            switch (set) {
                case 0: base = wtSpectrumHarmonic(f); break;
                case 1: base = wtSpectrumFM(f);       break;
                case 2: base = wtSpectrumChoir(f);    break;
                default: base = wtSpectrumMetallic(f); break;
            }
            for (int v = 0; v < WT_NUM_VARIANTS; ++v) {
                WTSpectrum sv = wtVariant(base, v, set * 31 + f);
                lib.sets[set].pyramids[f * WT_NUM_VARIANTS + v] = wtBuildPyramid(sv);
            }
        }
    }
    return lib;
}

// Process-wide singleton, built lazily on first access (never on the audio
// thread — the engine forces it during construction / prepare).
inline const WavetableLibrary& wtLibrary() {
    static const WavetableLibrary lib = wtBuildLibrary();
    return lib;
}

// ---- Runtime oscillator --------------------------------------------------
class WavetableOscillator {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        livenessInc_ = 0.2 / sr;   // ~0.2 Hz base drift
    }
    void reset(float startPhase, float livenessStart) {
        phase_ = startPhase;
        livenessPhase_ = livenessStart;
    }
    void setTable(const WavetableSet* s) { set_ = s; }
    inline void setFrequency(double hz) {
        double f = std::fabs(hz);
        phaseInc_ = f / sampleRate_;
        if (phaseInc_ > 0.5) phaseInc_ = 0.5;
        level_ = wtLevelForFreq(f);
    }
    inline void setFrame(float frame01) {
        framePos_ = clampf(frame01, 0.0f, 1.0f) * (WT_NUM_FRAMES - 1);
    }
    inline void setLiveness(float depth) { liveness_ = clampf(depth, 0.0f, 1.0f); }

    inline float process() {
        if (set_ == nullptr) return 0.0f;

        // Liveness: variant position sweeps 0..V and back (raised cosine loop),
        // scaled by depth, so depth 0 = the exact designed waveform.
        livenessPhase_ += livenessInc_;
        if (livenessPhase_ >= 1.0) livenessPhase_ -= 1.0;
        double vp = liveness_ * WT_NUM_VARIANTS * 0.5 *
                    (1.0 - std::cos(kTwoPi * livenessPhase_));

        float out = read(framePos_, vp, level_, phase_);

        phase_ += phaseInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return out;
    }

private:
    static inline float sampleMip(const WTMip& t, double phase) {
        double fp = phase * t.length;
        int i = (int)fp;
        float fr = (float)(fp - i);
        return t.samples[i] + fr * (t.samples[i + 1] - t.samples[i]);
    }
    inline float read(float framePos, double variantPos, int L, double phase) const {
        int f0 = (int)framePos, f1 = std::min(f0 + 1, WT_NUM_FRAMES - 1);
        float ff = framePos - f0;
        int vint = (int)variantPos;
        int v0 = vint % WT_NUM_VARIANTS, v1 = (v0 + 1) % WT_NUM_VARIANTS;
        float vf = (float)(variantPos - vint);
        float a = sampleMip(set_->at(f0, v0).levels[L], phase);
        float b = sampleMip(set_->at(f1, v0).levels[L], phase);
        float c = sampleMip(set_->at(f0, v1).levels[L], phase);
        float d = sampleMip(set_->at(f1, v1).levels[L], phase);
        float lo = a + ff * (b - a);
        float hi = c + ff * (d - c);
        return lo + vf * (hi - lo);
    }

    const WavetableSet* set_ = nullptr;
    double sampleRate_ = 96000.0;
    double phase_ = 0.0, phaseInc_ = 0.0;
    double livenessPhase_ = 0.0, livenessInc_ = 0.0;
    int    level_ = 0;
    float  framePos_ = 0.0f, liveness_ = 0.0f;
};

} // namespace synth
