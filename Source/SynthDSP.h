#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <random>

// =====================================================================
//  Vega Productions Multisynth — DSP building blocks
// =====================================================================
namespace vms
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f); }

// ---------------------------------------------------------------- band‑limited wavetables
class WavetableBank
{
public:
    static constexpr int kSize = 2048, kFrames = 16, kMips = 9;   // mip m keeps harmonics up to 512 >> m ... 2
    static constexpr int kTables = 6;

    static const WavetableBank& get() { static WavetableBank bank; return bank; }

    /** frame position 0..1, phase 0..1, frequency normalised (f / fs) */
    float sample (int table, float position, float phase, float normFreq) const
    {
        const int maxHarm = (int) (0.45f / std::max (normFreq, 1.0e-6f));
        int mip = 0; while (mip < kMips - 1 && (512 >> mip) > maxHarm) ++mip;
        const float fp = juce::jlimit (0.0f, 1.0f, position) * (kFrames - 1);
        const int f0 = (int) fp, f1 = std::min (f0 + 1, kFrames - 1);
        const float ff = fp - (float) f0;
        const float ip = phase * kSize; const int i0 = (int) ip & (kSize - 1), i1 = (i0 + 1) & (kSize - 1); const float fr = ip - std::floor (ip);
        const float* a = frame (table, mip, f0); const float* b = frame (table, mip, f1);
        const float va = a[i0] + (a[i1] - a[i0]) * fr, vb = b[i0] + (b[i1] - b[i0]) * fr;
        return va + (vb - va) * ff;
    }

private:
    const float* frame (int t, int mip, int f) const { return data.data() + (((size_t) t * kMips + (size_t) mip) * kFrames + (size_t) f) * kSize; }

    WavetableBank() : data ((size_t) kTables * kMips * kFrames * kSize)
    {
        juce::dsp::FFT fft (11);   // 2048
        std::vector<float> buf (kSize * 2);
        std::array<float, 513> amp {}, ph {};
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        std::array<float, 513> rndA {}, rndB {};
        for (int h = 1; h <= 512; ++h) { rndA[(size_t) h] = uni (rng) / (float) std::sqrt ((float) h); rndB[(size_t) h] = uni (rng) / (float) h; }

        for (int t = 0; t < kTables; ++t)
            for (int f = 0; f < kFrames; ++f)
            {
                const float x = (float) f / (kFrames - 1);
                amp.fill (0); ph.fill (0);
                for (int h = 1; h <= 512; ++h)
                {
                    const float n = (float) h; float a = 0;
                    switch (t)
                    {
                        case 0: // sine -> triangle -> saw -> square
                        {
                            const float sine = h == 1 ? 1.0f : 0.0f;
                            const float tri = (h % 2) ? 8.0f / (juce::MathConstants<float>::pi * juce::MathConstants<float>::pi * n * n) : 0.0f;
                            const float saw = 2.0f / (juce::MathConstants<float>::pi * n);
                            const float sq = (h % 2) ? 4.0f / (juce::MathConstants<float>::pi * n) : 0.0f;
                            const float s = x * 3.0f; const int seg = std::min (2, (int) s); const float u = s - seg;
                            const float A[] { sine, tri, saw, sq };
                            a = A[seg] + (A[seg + 1] - A[seg]) * u;
                            break;
                        }
                        case 1: { const float c = 1.0f + x * 40.0f; a = std::exp (-0.5f * std::pow ((n - c) / (1.5f + x * 6.0f), 2.0f)) + (h == 1 ? 0.6f : 0.0f); break; }
                        case 2: { const float d = 0.5f - x * 0.47f; a = 2.0f / (juce::MathConstants<float>::pi * n) * std::sin (juce::MathConstants<float>::pi * n * d); break; }
                        case 3:
                        {
                            static const float F[5][3] { { 7, 11, 25 }, { 5, 17, 24 }, { 3, 21, 28 }, { 5, 8, 24 }, { 3, 7, 22 } };
                            const float s = x * 4.0f; const int v = std::min (3, (int) s); const float u = s - v;
                            a = 0.25f / std::sqrt (n);
                            for (int k = 0; k < 3; ++k)
                            {
                                const float c = F[v][k] + (F[v + 1][k] - F[v][k]) * u;
                                a += (k == 0 ? 1.0f : 0.6f) * std::exp (-0.5f * std::pow ((n - c) / 1.6f, 2.0f));
                            }
                            break;
                        }
                        case 4: { a = (h == 1 ? 1.0f : 0.0f) + ((h == 3 || h == 5 || h == 7 || h == 11) ? (0.15f + x * 0.6f) / std::sqrt (n) : 0.0f) + (h > 11 ? x * 0.3f * rndB[(size_t) h] : 0.0f); break; }
                        default: { a = rndA[(size_t) h] * (1.0f - x) + rndB[(size_t) h] * x * 2.0f; if (h % 2 == 0) a *= 0.4f + 0.6f * x; break; }
                    }
                    amp[(size_t) h] = a;
                    ph[(size_t) h] = (t == 5) ? uni (rng) * kTwoPi : 0.0f;
                }
                for (int m = 0; m < kMips; ++m)
                {
                    const int limit = 512 >> m;
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    for (int h = 1; h <= limit && h < kSize / 2; ++h)
                    {   // sine series: X[h] = -j * A/2 * N  (performRealOnlyInverseTransform scaling calibrated below)
                        buf[(size_t) (2 * h)]     = -amp[(size_t) h] * std::sin (ph[(size_t) h]);
                        buf[(size_t) (2 * h + 1)] = -amp[(size_t) h] * std::cos (ph[(size_t) h]);
                    }
                    fft.performRealOnlyInverseTransform (buf.data());
                    float pk = 0; for (int i = 0; i < kSize; ++i) pk = std::max (pk, std::abs (buf[(size_t) i]));
                    const float norm = pk > 1.0e-9f ? 0.9f / pk : 0.0f;    // normalise every frame (stable loudness)
                    float* dst = const_cast<float*> (frame (t, m, f));
                    for (int i = 0; i < kSize; ++i) dst[i] = buf[(size_t) i] * norm;
                }
            }
    }
    std::vector<float> data;
};

// ---------------------------------------------------------------- PolyBLEP analog oscillator
struct VaOsc
{
    static inline float blep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    /** wave: 0 saw, 1 square/pulse, 2 triangle, 3 sine */
    static inline float sample (int wave, float phase, float dt, float pw)
    {
        switch (wave)
        {
            case 0: return 2.0f * phase - 1.0f - blep (phase, dt);
            case 1:
            {
                float v = phase < pw ? 1.0f : -1.0f;
                v += blep (phase, dt);
                float t2 = phase - pw; if (t2 < 0.0f) t2 += 1.0f;
                v -= blep (t2, dt);
                return v;
            }
            case 2: return 1.0f - 4.0f * std::abs (phase - 0.5f);
            default: return std::sin (phase * kTwoPi);
        }
    }
};

// ---------------------------------------------------------------- ZDF state‑variable filter (Simper / TPT)
struct ZdfSvf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void reset() { ic1 = ic2 = 0; }
    void set (float cutoff, float res, float fs)
    {
        g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (16.0f, fs * 0.47f, cutoff) / fs);
        k = 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, res);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    /** mode: 0 LP, 1 HP, 2 BP, 3 Notch */
    inline float process (float v0, int mode)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        switch (mode)
        {
            case 0: return v2;
            case 1: return v0 - k * v1 - v2;
            case 2: return v1 * k;              // unity‑peak band pass
            default: return v0 - k * v1;
        }
    }
};

/** One filter slot: LP12/LP24/HP12/HP24/BP/Notch/Off with pre‑drive. Keeps punch at high resonance
    by adding the "loss" back (resonance gain compensation) and soft‑clipping the input only. */
struct FilterSlot
{
    ZdfSvf a, b;
    void reset() { a.reset(); b.reset(); }
    inline float process (float x, int type, float drive)
    {
        if (type == 6) return x;
        if (drive > 0.001f) { const float d = 1.0f + drive * 6.0f; x = std::tanh (x * d) / std::sqrt (d); }
        switch (type)
        {
            case 0: return a.process (x, 0);
            case 1: return b.process (a.process (x, 0), 0);
            case 2: return a.process (x, 1);
            case 3: return b.process (a.process (x, 1), 1);
            case 4: return a.process (x, 2);
            default: return a.process (x, 3);
        }
    }
    void set (float cutoff, float res, float fs, int type)
    {
        if (type == 1 || type == 3) { a.set (cutoff, res * 0.35f, fs); b.set (cutoff, res, fs); }   // 24 dB: resonance on the 2nd stage
        else a.set (cutoff, res, fs);
    }
};

// ---------------------------------------------------------------- ADSR
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float value = 0, a = 0.01f, d = 0.1f, s = 1, r = 0.1f, fs = 44100, relCoef = 0.999f, decCoef = 0.999f;
    void setParams (float A, float D, float S, float R, float sampleRate)
    {
        fs = sampleRate; a = std::max (0.0005f, A); d = std::max (0.0005f, D); s = S; r = std::max (0.0005f, R);
        decCoef = std::exp (-4.6f / (d * fs)); relCoef = std::exp (-4.6f / (r * fs));
    }
    void noteOn() { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }
    void kill() { stage = Idle; value = 0; }
    bool active() const { return stage != Idle; }
    inline float next()
    {
        switch (stage)
        {
            case Attack:  value += 1.0f / (a * fs); if (value >= 1.0f) { value = 1.0f; stage = Decay; } break;
            case Decay:   value = s + (value - s) * decCoef; if (value - s < 1.0e-4f) { value = s; stage = Sustain; } break;
            case Sustain: value = s; break;
            case Release: value *= relCoef; if (value < 1.0e-5f) { value = 0; stage = Idle; } break;
            default: break;
        }
        return value;
    }
};

// ---------------------------------------------------------------- effects
struct Chorus
{
    std::vector<float> buf[2]; int pos = 0, len = 1; float fs = 44100, ph = 0;
    void prepare (double rate) { fs = (float) rate; len = juce::nextPowerOfTwo ((int) (0.06 * rate)); buf[0].assign ((size_t) len, 0); buf[1].assign ((size_t) len, 0); }
    void reset() { std::fill (buf[0].begin(), buf[0].end(), 0.0f); std::fill (buf[1].begin(), buf[1].end(), 0.0f); }
    inline float tap (int c, float delaySamples) const
    {
        const float rp = (float) pos - delaySamples; const float fl = std::floor (rp); const int i0 = (int) fl & (len - 1), i1 = (i0 + 1) & (len - 1);
        const float fr = rp - fl; return buf[c][(size_t) i0] + (buf[c][(size_t) i1] - buf[c][(size_t) i0]) * fr;
    }
    void process (float* l, float* r, int n, float rate, float depth, float mix)
    {
        if (mix <= 0.0f || len <= 1) return;
        const float inc = rate / fs;
        for (int i = 0; i < n; ++i)
        {
            buf[0][(size_t) pos] = l[i]; buf[1][(size_t) pos] = r[i];
            ph += inc; if (ph >= 1.0f) ph -= 1.0f;
            const float base = 0.012f * fs, dep = 0.006f * fs * depth;
            const float dl = base + dep * std::sin (ph * kTwoPi), dr = base + dep * std::sin ((ph + 0.25f) * kTwoPi);
            const float wl = 0.5f * (tap (0, dl) + tap (1, dr * 1.13f)), wr = 0.5f * (tap (1, dr) + tap (0, dl * 1.07f));
            l[i] += mix * (wl - l[i]); r[i] += mix * (wr - r[i]);
            pos = (pos + 1) & (len - 1);
        }
    }
};

struct PingPongDelay
{
    std::vector<float> buf[2]; int pos = 0, len = 1; float fs = 44100, lpL = 0, lpR = 0;
    void prepare (double rate) { fs = (float) rate; len = juce::nextPowerOfTwo ((int) (4.1 * rate)); buf[0].assign ((size_t) len, 0); buf[1].assign ((size_t) len, 0); }
    void reset() { std::fill (buf[0].begin(), buf[0].end(), 0.0f); std::fill (buf[1].begin(), buf[1].end(), 0.0f); lpL = lpR = 0; }
    /** in: send signal; out: wet only (added to outL/outR) */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, float seconds, float fb, float tone, bool ping, float mix)
    {
        const int d = juce::jlimit (1, len - 1, (int) (seconds * fs));
        const float a = 0.05f + 0.9f * tone;
        for (int i = 0; i < n; ++i)
        {
            const int rp = (pos - d) & (len - 1);
            float yl = buf[0][(size_t) rp], yr = buf[1][(size_t) rp];
            lpL += a * (yl - lpL); lpR += a * (yr - lpR); yl = lpL; yr = lpR;
            if (ping) { buf[0][(size_t) pos] = 0.5f * (inL[i] + inR[i]) + fb * yr; buf[1][(size_t) pos] = fb * yl; }
            else      { buf[0][(size_t) pos] = inL[i] + fb * yl; buf[1][(size_t) pos] = inR[i] + fb * yr; }
            outL[i] += mix * yl; outR[i] += mix * yr;
            pos = (pos + 1) & (len - 1);
        }
    }
};

/** 8‑line feedback delay network reverb with damping and gentle modulation. */
struct FdnReverb
{
    static constexpr int N = 8;
    std::array<std::vector<float>, N> lines; std::array<int, N> lens {}, pos {}; std::array<float, N> lp {};
    std::vector<float> pre; int prePos = 0; float fs = 44100, ph = 0;
    void prepare (double rate)
    {
        fs = (float) rate;
        static const float ms[N] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 79.3f };
        for (int i = 0; i < N; ++i) { lens[(size_t) i] = (int) (ms[i] * 0.001f * fs * 2.0f) + 8; lines[(size_t) i].assign ((size_t) lens[(size_t) i] + 16, 0.0f); pos[(size_t) i] = 0; lp[(size_t) i] = 0; }
        pre.assign ((size_t) (0.05f * fs) + 1, 0.0f);
    }
    void reset() { for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f); lp.fill (0); std::fill (pre.begin(), pre.end(), 0.0f); }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, float size, float damp, float width, float mix)
    {
        if (mix <= 0.0f) return;
        const float scale = 0.45f + 0.55f * size;
        const float fb = 0.70f + 0.27f * size;
        const float dampA = 0.15f + 0.8f * (1.0f - damp);
        for (int i = 0; i < n; ++i)
        {
            const float in = 0.5f * (inL[i] + inR[i]);
            const float pd = pre[(size_t) prePos]; pre[(size_t) prePos] = in; prePos = (prePos + 1) % (int) pre.size();
            std::array<float, N> o {};
            ph += 0.3f / fs; if (ph > 1.0f) ph -= 1.0f;
            for (int k = 0; k < N; ++k)
            {
                const int L = (int) (lens[(size_t) k] * scale);
                const float mod = (k & 1 ? 1.0f : -1.0f) * 3.0f * std::sin ((ph + k * 0.125f) * kTwoPi);
                const float rp = (float) pos[(size_t) k] - (float) L - mod;
                const int sz = (int) lines[(size_t) k].size();
                float fl = std::floor (rp); const float fr = rp - fl;
                int i0 = ((int) fl % sz + sz) % sz, i1 = (i0 + 1) % sz;
                float v = lines[(size_t) k][(size_t) i0] + (lines[(size_t) k][(size_t) i1] - lines[(size_t) k][(size_t) i0]) * fr;
                lp[(size_t) k] += dampA * (v - lp[(size_t) k]);
                o[(size_t) k] = lp[(size_t) k];
            }
            // Hadamard mix
            std::array<float, N> h = o;
            for (int len = 1; len < N; len <<= 1)
                for (int s = 0; s < N; s += len << 1)
                    for (int j = s; j < s + len; ++j) { const float a = h[(size_t) j], b = h[(size_t) (j + len)]; h[(size_t) j] = a + b; h[(size_t) (j + len)] = a - b; }
            const float norm = 1.0f / std::sqrt ((float) N);
            for (int k = 0; k < N; ++k)
            {
                const int sz = (int) lines[(size_t) k].size();
                lines[(size_t) k][(size_t) pos[(size_t) k]] = pd * 0.35f + h[(size_t) k] * norm * fb;
                pos[(size_t) k] = (pos[(size_t) k] + 1) % sz;
            }
            const float l = o[0] + o[2] + o[4] + o[6], r = o[1] + o[3] + o[5] + o[7];
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * width;
            outL[i] += mix * 0.5f * (m + sd); outR[i] += mix * 0.5f * (m - sd);
        }
    }
};

struct Shelf { juce::dsp::IIR::Filter<float> f[2]; };

/** small biquad helper (RBJ) used for EQs */
struct Bq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float xf) { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
    void set (int type, double fs, double f, double q, double g) // 0 low shelf, 1 peak, 2 high shelf
    {
        const double A = std::pow (10.0, g / 40.0), w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        double B0, B1, B2, A0, A1, A2;
        if (type == 1) { B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; }
        else if (type == 0) { B0 = A * ((A + 1) - (A - 1) * c + s); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - s); A0 = (A + 1) + (A - 1) * c + s; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - s; }
        else { B0 = A * ((A + 1) + (A - 1) * c + s); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - s); A0 = (A + 1) - (A - 1) * c + s; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - s; }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};
} // namespace vms
