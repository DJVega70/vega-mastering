#pragma once
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>
#include <atomic>
#include <cstring>

// =====================================================================
//  Vega Productions — DSP building blocks
// =====================================================================
namespace vega
{
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// ---------------------------------------------------------------------
//  Biquad (RBJ cookbook, transposed direct form II, double precision)
// ---------------------------------------------------------------------
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
    static double clampF (double f, double fs) { return juce::jlimit (5.0, fs * 0.49, f); }

    static BiquadCoeffs peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * clampF (f, fs) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return normalise (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * clampF (f, fs) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q), sA = 2.0 * std::sqrt (A) * al;
        return normalise (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                          (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * clampF (f, fs) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q), sA = 2.0 * std::sqrt (A) * al;
        return normalise (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                          (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    static BiquadCoeffs highPass (double fs, double f, double q = 0.7071)
    {
        const double w = juce::MathConstants<double>::twoPi * clampF (f, fs) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    double magnitude (double f, double fs) const
    {
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }
};

struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float xf)
    {
        const double x = xf, y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

// ---------------------------------------------------------------------
//  Three‑band mastering EQ (low shelf / bell / air shelf)
// ---------------------------------------------------------------------
struct EqSettings
{
    float lowF = 100, lowG = 0, midF = 1000, midG = 0, midQ = 0.7f, highF = 12000, highG = 0;
    bool operator== (const EqSettings& o) const
    {
        return lowF == o.lowF && lowG == o.lowG && midF == o.midF && midG == o.midG
            && midQ == o.midQ && highF == o.highF && highG == o.highG;
    }
    bool operator!= (const EqSettings& o) const { return ! (*this == o); }

    std::array<BiquadCoeffs, 3> coeffs (double fs) const
    {
        return { BiquadCoeffs::lowShelf (fs, lowF, 0.7071, lowG),
                 BiquadCoeffs::peak (fs, midF, midQ, midG),
                 BiquadCoeffs::highShelf (fs, highF, 0.7071, highG) };
    }
    double magnitude (double f, double fs) const
    {
        double m = 1.0;
        for (auto& c : coeffs (fs)) m *= c.magnitude (f, fs);
        return m;
    }
};

struct ThreeBandEq
{
    std::array<Biquad, 3> bands;
    EqSettings current;
    bool initialised = false;

    void reset() { for (auto& b : bands) b.reset(); }
    void update (const EqSettings& s, double fs)
    {
        if (initialised && s == current) return;
        current = s; initialised = true;
        auto c = s.coeffs (fs);
        for (int i = 0; i < 3; ++i) bands[(size_t) i].c = c[(size_t) i];
    }
    inline float process (float x)
    {
        // skip bands sitting at 0 dB (exactly unity) to save CPU
        if (current.lowG  != 0.0f) x = bands[0].process (x);
        if (current.midG  != 0.0f) x = bands[1].process (x);
        if (current.highG != 0.0f) x = bands[2].process (x);
        return x;
    }
};

// ---------------------------------------------------------------------
//  Linear‑phase FIR design from a magnitude response
// ---------------------------------------------------------------------
inline void designLinearPhaseFir (const std::function<double (double)>& magnitudeAtHz,
                                  double fs, int fftOrder, float* out)
{
    const int N = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> data ((size_t) N * 2, 0.0f);

    // zero‑phase (real, symmetric) spectrum
    for (int k = 0; k <= N / 2; ++k)
    {
        data[(size_t) (2 * k)]     = (float) magnitudeAtHz ((double) k * fs / N);
        data[(size_t) (2 * k + 1)] = 0.0f;
    }
    const float dc = data[0];
    fft.performRealOnlyInverseTransform (data.data());

    // normalise so that sum(h) == H(0) regardless of the FFT backend's scaling
    double sum = 0.0;
    for (int n = 0; n < N; ++n) sum += data[(size_t) n];
    const float scale = std::abs (sum) > 1.0e-12 ? (float) (dc / sum) : 1.0f;

    // rotate by N/2 -> causal, symmetric about N/2, then window (Blackman)
    for (int n = 0; n < N; ++n)
    {
        const int src = (n + N / 2) % N;
        const double w = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / N)
                              + 0.08 * std::cos (2.0 * juce::MathConstants<double>::twoPi * n / N);
        out[n] = data[(size_t) src] * scale * (float) w;
    }
}

// ---------------------------------------------------------------------
//  Glue compressor (feed‑forward, soft knee, sidechain HPF, Stereo or M/S)
// ---------------------------------------------------------------------
class GlueCompressor
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { envA = envB = 0.0f; for (auto& f : hpf) f.reset(); lastHpf = -1.0f; }

    void setParams (float threshDb, float ratio, float attackMs, float releaseMs,
                    float makeupDb, float mixPct, float scHpfHz, bool midSide)
    {
        thresh = threshDb; slope = 1.0f / std::max (1.0f, ratio) - 1.0f;
        att = std::exp (-1.0f / (attackMs * 0.001f * (float) fs));
        rel = std::exp (-1.0f / (releaseMs * 0.001f * (float) fs));
        makeup = dbToGain (makeupDb); mix = mixPct * 0.01f; ms = midSide;
        if (scHpfHz != lastHpf)
        {
            lastHpf = scHpfHz;
            auto c = BiquadCoeffs::highPass (fs, scHpfHz);
            for (auto& f : hpf) f.c = c;
        }
    }

    /** returns the deepest gain reduction (dB, positive) in this block */
    float process (float* L, float* R, int n)
    {
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float a = L[i], b = R[i];
            if (ms) { const float m = (a + b) * 0.5f, s = (a - b) * 0.5f; a = m; b = s; }

            const float sa = std::abs (hpf[0].process (a)), sb = std::abs (hpf[1].process (b));
            float ga, gb;
            if (ms)
            {
                ga = smooth (envA, computeGr (gainToDb (sa)));
                gb = smooth (envB, computeGr (gainToDb (sb)));
            }
            else
            {
                ga = gb = smooth (envA, computeGr (gainToDb (std::max (sa, sb))));
            }
            maxGr = std::max (maxGr, -std::min (ga, gb));

            float wa = a * dbToGain (ga) * makeup, wb = b * dbToGain (gb) * makeup;
            if (ms) { const float l = wa + wb, r = wa - wb; wa = l; wb = r; }
            L[i] += mix * (wa - L[i]);
            R[i] += mix * (wb - R[i]);
        }
        return maxGr;
    }

private:
    inline float computeGr (float x) const
    {
        constexpr float knee = 6.0f;
        const float over = x - thresh;
        if (2.0f * over < -knee) return 0.0f;
        if (2.0f * std::abs (over) <= knee)
        {
            const float t = over + knee * 0.5f;
            return slope * t * t / (2.0f * knee);
        }
        return slope * over;
    }
    inline float smooth (float& env, float target)
    {
        const float c = target < env ? att : rel;   // target is <= 0; more negative = attack
        env = c * env + (1.0f - c) * target;
        return env;
    }

    double fs = 44100.0;
    float thresh = -12, slope = -0.5f, att = 0.9f, rel = 0.99f, makeup = 1, mix = 1, lastHpf = -1;
    bool ms = false;
    float envA = 0, envB = 0;
    std::array<Biquad, 2> hpf;
};

// ---------------------------------------------------------------------
//  Oversampled non‑linear stage: saturation -> clipper -> true‑peak limiter
// ---------------------------------------------------------------------
// 8x windowed‑sinc interpolator (BS.1770 style) used for true‑peak detection
struct TruePeakInterpolator
{
    static constexpr int half = 16, taps = 2 * half, phases = 7;  // 8x: t = 1/8 .. 7/8
    float coef[phases][taps];
    float hist[2][taps] {};
    int pos = 0;

    TruePeakInterpolator()
    {
        for (int p = 0; p < phases; ++p)
        {
            const double t = (p + 1) / 8.0;
            for (int k = 0; k < taps; ++k)
            {
                const double d = (k - half + 1) - t;   // tap offsets -half+1 .. half
                const double pi = juce::MathConstants<double>::pi;
                const double sinc = std::sin (pi * d) / (pi * d);
                const double w = 0.5 + 0.5 * std::cos (pi * d / (half + 0.5));
                coef[p][k] = (float) (sinc * w);
            }
        }
    }
    void reset() { std::memset (hist, 0, sizeof (hist)); pos = 0; }

    /** push one stereo sample; returns the inter‑sample peak (abs, both channels) in the interval
        between the samples that are `half` and `half-1` samples old. */
    inline float push (float l, float r)
    {
        hist[0][pos] = l; hist[1][pos] = r;
        pos = (pos + 1) % taps;              // pos now points at the oldest sample
        float pk = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int p = 0; p < phases; ++p)
            {
                float acc = 0.0f;
                for (int k = 0; k < taps; ++k) acc += hist[c][(pos + k) % taps] * coef[p][k];
                pk = std::max (pk, std::abs (acc));
            }
        return pk;
    }
    inline float sampleAt (int c, int age) const { return hist[c][(pos - 1 - age + 2 * taps) % taps]; }
};

// Look‑ahead brick‑wall limiter with true‑peak (inter‑sample) detection
class TruePeakLimiter
{
public:
    void prepare (int lookaheadSamples)
    {
        L = std::max (1, lookaheadSamples);
        D = TruePeakInterpolator::half;                   // detector delay
        delayLen = L + D;
        delay[0].assign ((size_t) delayLen, 0.0f);
        delay[1].assign ((size_t) delayLen, 0.0f);
        avgRing.assign ((size_t) L + 1, 1.0);
        cap = juce::nextPowerOfTwo (L + 2);
        dqVal.assign ((size_t) cap, 1.0f);
        dqIdx.assign ((size_t) cap, 0);
        reset();
    }
    void reset()
    {
        std::fill (delay[0].begin(), delay[0].end(), 0.0f);
        std::fill (delay[1].begin(), delay[1].end(), 0.0f);
        std::fill (avgRing.begin(), avgRing.end(), 1.0);
        interp.reset(); prevInterval = 0;
        sum = (double) (L + 1); avgPos = 0; dPos = 0; head = tail = 0; counter = 0; env = 1.0f;
    }

    /** returns min gain reached in this block (linear) */
    float process (float* l, float* r, int n, float ceiling, float releaseCoef, bool enabled)
    {
        float minGain = 1.0f;
        const int mask = cap - 1;
        for (int i = 0; i < n; ++i)
        {
            // true‑peak detection for the sample that is D samples old
            const float interval = interp.push (l[i], r[i]);
            const int age = D - 1;
            const float sp = std::max (std::abs (interp.sampleAt (0, age)), std::abs (interp.sampleAt (1, age)));
            const float pk = std::max ({ sp, interval, prevInterval });
            prevInterval = interval;
            const float t = (enabled && pk > ceiling) ? ceiling / pk : 1.0f;

            // sliding minimum over the last L+1 targets (monotonic deque)
            while (head != tail && dqVal[(size_t) ((tail - 1) & mask)] >= t) --tail;
            dqVal[(size_t) (tail & mask)] = t; dqIdx[(size_t) (tail & mask)] = counter; ++tail;
            while (dqIdx[(size_t) (head & mask)] <= counter - (L + 1)) ++head;
            const float hold = dqVal[(size_t) (head & mask)];
            ++counter;

            // moving average of the held minimum -> smooth attack ramp
            sum += hold - avgRing[(size_t) avgPos];
            avgRing[(size_t) avgPos] = hold;
            if (++avgPos > L) avgPos = 0;
            const float avg = (float) (sum / (L + 1));

            env = avg < env ? avg : avg + (env - avg) * releaseCoef;
            minGain = std::min (minGain, env);

            // audio delay = detector delay + look‑ahead
            const float dl = delay[0][(size_t) dPos], dr = delay[1][(size_t) dPos];
            delay[0][(size_t) dPos] = l[i]; delay[1][(size_t) dPos] = r[i];
            if (++dPos >= delayLen) dPos = 0;
            l[i] = dl * env; r[i] = dr * env;
        }
        if (++blocksSinceResum >= 256)
        {
            blocksSinceResum = 0;
            sum = 0; for (auto v : avgRing) sum += v;
        }
        return minGain;
    }
    int getLatency() const { return delayLen; }

private:
    TruePeakInterpolator interp;
    float prevInterval = 0;
    int L = 1, D = 8, delayLen = 9, cap = 4, avgPos = 0, dPos = 0, blocksSinceResum = 0;
    int64_t head = 0, tail = 0, counter = 0;
    double sum = 0;
    float env = 1.0f;
    std::vector<float> delay[2];
    std::vector<double> avgRing;
    std::vector<float> dqVal;
    std::vector<int64_t> dqIdx;
};

struct NonLinearStage
{
    double fs = 44100.0;
    float dcX[2] {}, dcY[2] {}, dcCoef = 0.9995f;

    void prepare (double rate)
    {
        fs = rate;
        dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 8.0 / fs);
        reset();
    }
    void reset() { dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0; }

    struct Settings
    {
        int satMode = 0; float drive = 1, satMix = 1;
        bool clipOn = false; float clipT = 1;
    };

    static inline float tape (float u) { return std::tanh (u); }
    static inline float tube (float u)
    {
        constexpr float b = 0.2f;
        static const float tb = std::tanh (b), norm = 1.0f / (1.0f - tb * tb);
        return (std::tanh (u + b) - tb) * norm;
    }
    static inline float softClip (float x, float T)
    {
        constexpr float k = 0.85f;
        const float u = x / T, a = std::abs (u);
        if (a <= k) return x;
        return std::copysign (k + (1.0f - k) * std::tanh ((a - k) / (1.0f - k)), u) * T;
    }

    void process (juce::dsp::AudioBlock<float>& block, const Settings& s)
    {
        float* ch[2] = { block.getChannelPointer (0), block.getChannelPointer (1) };
        const int n = (int) block.getNumSamples();

        if (s.satMode > 0 && s.satMix > 0.0f)
        {
            const float g = s.drive, invG = 1.0f / g;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = ch[c][i];
                    float y = (s.satMode == 1 ? tape (x * g) : tube (x * g)) * invG;
                    // DC blocker (tube curve is asymmetric)
                    const float d = y - dcX[c] + dcCoef * dcY[c];
                    dcX[c] = y; dcY[c] = d; y = d;
                    ch[c][i] = x + s.satMix * (y - x);
                }
        }
        if (s.clipOn)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    ch[c][i] = softClip (ch[c][i], s.clipT);

    }
};

// ---------------------------------------------------------------------
//  ITU‑R BS.1770‑4 / EBU R128 loudness meter
// ---------------------------------------------------------------------
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // K‑weighting stage 1 (high shelf) and stage 2 (RLB high‑pass), from BS.1770
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelfC = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                       2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hpC = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        for (auto& f : stage1) f.c = shelfC;
        for (auto& f : stage2) f.c = hpC;
        blockLen = std::max (1, (int) std::round (0.1 * fs));
        gating.assign (36000 * 2, 0.0f);  // two hours of 100 ms‑hop gating blocks
        hardReset();
    }

    void requestReset() { resetRequested.store (true); }

    void process (const float* L, const float* R, int n)
    {
        if (resetRequested.exchange (false)) hardReset();
        for (int i = 0; i < n; ++i)
        {
            const double l = stage2[0].process (stage1[0].process (L[i]));
            const double r = stage2[1].process (stage1[1].process (R[i]));
            acc += l * l + r * r;
            if (++accCount >= blockLen)
            {
                ring[(size_t) ringPos] = acc / blockLen;
                ringPos = (ringPos + 1) % 30;
                ringFilled = std::min (30, ringFilled + 1);
                acc = 0; accCount = 0;

                const double m = meanOfLast (4), st = meanOfLast (30);
                momentary.store (toLufs (m));
                shortTerm.store (toLufs (st));
                if (ringFilled >= 4)
                {
                    const int idx = numGating.load (std::memory_order_relaxed);
                    if (idx < (int) gating.size())
                    {
                        gating[(size_t) idx] = (float) m;
                        numGating.store (idx + 1, std::memory_order_release);
                    }
                }
            }
        }
    }

    /** Call from the message thread — gated integrated loudness. */
    float computeIntegrated() const
    {
        const int n = numGating.load (std::memory_order_acquire);
        double sum = 0; int cnt = 0;
        const double absGate = fromLufs (-70.0);
        for (int i = 0; i < n; ++i) if (gating[(size_t) i] > absGate) { sum += gating[(size_t) i]; ++cnt; }
        if (cnt == 0) return -100.0f;
        const double relGate = fromLufs (toLufs (sum / cnt) - 10.0);
        sum = 0; cnt = 0;
        for (int i = 0; i < n; ++i)
            if (gating[(size_t) i] > absGate && gating[(size_t) i] > relGate) { sum += gating[(size_t) i]; ++cnt; }
        return cnt ? toLufs (sum / cnt) : -100.0f;
    }

    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f };

private:
    static float toLufs (double p)   { return p > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (p)) : -100.0f; }
    static double fromLufs (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }

    double meanOfLast (int k) const
    {
        k = std::min (k, ringFilled);
        if (k == 0) return 0;
        double s = 0;
        for (int i = 1; i <= k; ++i) s += ring[(size_t) ((ringPos - i + 30) % 30)];
        return s / k;
    }
    void hardReset()
    {
        for (auto& f : stage1) f.reset();
        for (auto& f : stage2) f.reset();
        acc = 0; accCount = 0; ringPos = 0; ringFilled = 0; ring.fill (0);
        numGating.store (0);
        momentary.store (-100.0f); shortTerm.store (-100.0f);
    }

    double fs = 48000;
    BiquadCoeffs shelfC, hpC;
    std::array<Biquad, 2> stage1, stage2;
    int blockLen = 4800, accCount = 0, ringPos = 0, ringFilled = 0;
    double acc = 0;
    std::array<double, 30> ring {};
    std::vector<float> gating;
    std::atomic<int> numGating { 0 };
    std::atomic<bool> resetRequested { false };
};

// ---------------------------------------------------------------------
//  Lock‑free single‑producer / single‑consumer stereo sample FIFO
// ---------------------------------------------------------------------
class StereoFifo
{
public:
    explicit StereoFifo (int size) : fifo (size) { buf[0].resize ((size_t) size); buf[1].resize ((size_t) size); }

    void push (const float* l, const float* r, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { buf[0][(size_t) (s1 + i)] = l[i];      buf[1][(size_t) (s1 + i)] = r[i]; }
        for (int i = 0; i < n2; ++i) { buf[0][(size_t) (s2 + i)] = l[n1 + i]; buf[1][(size_t) (s2 + i)] = r[n1 + i]; }
        fifo.finishedWrite (n1 + n2);
    }
    int pull (float* l, float* r, int maxN)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (maxN, fifo.getNumReady()), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { l[i] = buf[0][(size_t) (s1 + i)];      r[i] = buf[1][(size_t) (s1 + i)]; }
        for (int i = 0; i < n2; ++i) { l[n1 + i] = buf[0][(size_t) (s2 + i)]; r[n1 + i] = buf[1][(size_t) (s2 + i)]; }
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
    int ready() const { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> buf[2];
};
} // namespace vega
