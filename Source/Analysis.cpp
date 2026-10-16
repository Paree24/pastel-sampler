#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace Analysis
{
namespace
{
constexpr float kFMin = 27.5f, kFMax = 4200.0f;

int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

int log2i(int v)
{
    int o = 0;
    while ((1 << o) < v) ++o;
    return o;
}

// Simple owner of a real FFT + scratch buffer (2N floats, JUCE layout).
struct RealFFT
{
    explicit RealFFT(int size) : N(size), fft(log2i(size)), buf((size_t) size * 2, 0.0f) {}
    int N;
    juce::dsp::FFT fft;
    std::vector<float> buf;
    void forward() { fft.performRealOnlyForwardTransform(buf.data()); }
    void inverse() { fft.performRealOnlyInverseTransform(buf.data()); }
};

std::vector<float> hann(int n)
{
    std::vector<float> w((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) n);
    return w;
}

// ---------------- YIN (difference function via FFT autocorrelation) ----------------
struct Yin
{
    Yin(double sampleRate)
        : sr(sampleRate),
          W(nextPow2((int) (sampleRate * 0.04))),
          tauMin(juce::jmax(2, (int) (sampleRate / kFMax))),
          tauMax((int) (sampleRate / kFMin)),
          frameLen(W + tauMax),
          fA(nextPow2(W + tauMax)), fB(nextPow2(W + tauMax)),
          d((size_t) tauMax + 2), prefix((size_t) frameLen + 1)
    {}

    struct Frame { float f0 = 0, conf = 0; };

    Frame analyse(const float* x)
    {
        const int M = fA.N;
        std::fill(fA.buf.begin(), fA.buf.end(), 0.0f);
        std::fill(fB.buf.begin(), fB.buf.end(), 0.0f);
        prefix[0] = 0.0;
        for (int i = 0; i < frameLen; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) x[i] * x[i];
        for (int i = 0; i < W; ++i) fA.buf[(size_t) i] = x[i];
        for (int i = 0; i < frameLen; ++i) fB.buf[(size_t) i] = x[i];
        fA.forward();
        fB.forward();
        // conj(A) * B, stored back in B
        for (int k = 0; k < M; ++k)
        {
            float ar = fA.buf[(size_t) 2 * k], ai = fA.buf[(size_t) 2 * k + 1];
            float br = fB.buf[(size_t) 2 * k], bi = fB.buf[(size_t) 2 * k + 1];
            fB.buf[(size_t) 2 * k] = ar * br + ai * bi;
            fB.buf[(size_t) 2 * k + 1] = ar * bi - ai * br;
        }
        fB.inverse();
        const double e0 = prefix[(size_t) W];
        if (e0 < 1e-10) return {};
        // normalise whatever scaling the FFT backend applies, using r(0) == e0
        const double c0 = fB.buf[0];
        const double scale = std::abs(c0) > 1e-20 ? e0 / c0 : 1.0;

        d[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double et = prefix[(size_t) (tau + W)] - prefix[(size_t) tau];
            double r = fB.buf[(size_t) tau] * scale;
            double diff = juce::jmax(0.0, e0 + et - 2.0 * r);
            running += diff;
            d[(size_t) tau] = running > 0 ? (float) (diff * tau / running) : 1.0f;
        }

        const float thr = 0.15f;
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[(size_t) tau] < thr)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        }
        float tauF = (float) best;
        if (best > 1 && best < tauMax)
        {
            float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            float den = a - 2.0f * b + c;
            if (std::abs(den) > 1e-9f) tauF += 0.5f * (a - c) / den;
        }
        Frame f;
        f.f0 = (float) (sr / tauF);
        f.conf = juce::jlimit(0.0f, 1.0f, 1.0f - d[(size_t) best]);
        return f;
    }

    double sr;
    int W, tauMin, tauMax, frameLen;
    RealFFT fA, fB;
    std::vector<float> d;
    std::vector<double> prefix;
};

// ---------------- long-window magnitude spectrum ----------------
struct Spectrum
{
    std::vector<float> mag;
    double binHz = 1.0;
    int bins() const { return (int) mag.size(); }
};

Spectrum longSpectrum(const float* x, int n, double sr)
{
    int L = juce::jlimit(8192, 65536, nextPow2(n));
    int use = juce::jmin(n, L);
    int off = (n - use) / 2;
    RealFFT f(L);
    auto w = hann(use);
    for (int i = 0; i < use; ++i) f.buf[(size_t) i] = x[off + i] * w[(size_t) i];
    f.forward();
    Spectrum s;
    s.binHz = sr / L;
    s.mag.resize((size_t) L / 2 + 1);
    for (int k = 0; k <= L / 2; ++k)
        s.mag[(size_t) k] = std::hypot(f.buf[(size_t) 2 * k], f.buf[(size_t) 2 * k + 1]);
    return s;
}

// fraction of energy sitting on the harmonics of f (0..1)
float harmonicity(const Spectrum& s, float f)
{
    if (f <= 0) return 0.0f;
    double on = 0, total = 0;
    int lo = (int) (f * 0.5 / s.binHz), hi = juce::jmin(s.bins() - 1, (int) (f * 10.5 / s.binHz));
    for (int k = juce::jmax(1, lo); k <= hi; ++k)
    {
        double e = (double) s.mag[(size_t) k] * s.mag[(size_t) k];
        total += e;
        double h = (k * s.binHz) / f;
        double frac = std::abs(h - std::round(h));
        if (std::round(h) >= 1 && frac * f < juce::jmax(3.0 * s.binHz, 0.03 * f)) on += e;
    }
    return total > 0 ? (float) (on / total) : 0.0f;
}

float refineWithHarmonics(const Spectrum& s, float f0)
{
    double wsum = 0, fsum = 0;
    for (int h = 1; h <= 10; ++h)
    {
        double target = f0 * h;
        if (target > s.binHz * (s.bins() - 2)) break;
        int c = (int) std::round(target / s.binHz);
        int r = juce::jmax(2, (int) std::round(target * 0.015 / s.binHz)); // ~25 cents
        int bestK = c;
        for (int k = juce::jmax(1, c - r); k <= juce::jmin(s.bins() - 2, c + r); ++k)
            if (s.mag[(size_t) k] > s.mag[(size_t) bestK]) bestK = k;
        if (bestK <= 0 || bestK >= s.bins() - 1) continue;
        float a = std::log(s.mag[(size_t) bestK - 1] + 1e-12f), b = std::log(s.mag[(size_t) bestK] + 1e-12f),
              cc = std::log(s.mag[(size_t) bestK + 1] + 1e-12f);
        float den = a - 2 * b + cc;
        float p = std::abs(den) > 1e-9f ? 0.5f * (a - cc) / den : 0.0f;
        double fk = (bestK + p) * s.binHz / h;
        double w = s.mag[(size_t) bestK] / h; // favour low harmonics
        wsum += w;
        fsum += w * fk;
    }
    if (wsum <= 0) return f0;
    float refined = (float) (fsum / wsum);
    return std::abs(1200.0f * std::log2(refined / f0)) < 30.0f ? refined : f0;
}

PitchResult makeResult(float f0, float conf)
{
    PitchResult r;
    if (f0 > 0)
    {
        r.f0 = f0;
        r.midi = hzToMidi(f0);
    }
    r.confidence = juce::jlimit(0.0f, 1.0f, conf);
    return r;
}

PitchResult yinDetect(const float* x, int n, double sr)
{
    Yin yin(sr);
    std::vector<float> padded;
    if (n < yin.frameLen)
    {
        padded.assign((size_t) yin.frameLen, 0.0f);
        std::copy(x, x + n, padded.begin());
        x = padded.data();
        n = yin.frameLen;
    }
    int hop = yin.W / 2;
    int frames = 1 + (n - yin.frameLen) / hop;
    int stride = juce::jmax(1, frames / 120);
    std::vector<std::pair<float, float>> voiced; // midi, conf
    int total = 0;
    Yin::Frame bestFrame;
    for (int f = 0; f < frames; f += stride)
    {
        auto fr = yin.analyse(x + (size_t) f * (size_t) hop);
        ++total;
        if (fr.conf > bestFrame.conf) bestFrame = fr;
        if (fr.conf > 0.6f && fr.f0 > 0) voiced.push_back({ hzToMidi(fr.f0), fr.conf });
    }
    if (voiced.empty())
        return makeResult(bestFrame.f0, bestFrame.conf * 0.5f);
    std::vector<float> m;
    for (auto& v : voiced) m.push_back(v.first);
    std::nth_element(m.begin(), m.begin() + (long) m.size() / 2, m.end());
    float med = m[m.size() / 2];
    double cs = 0;
    int agree = 0;
    for (auto& v : voiced)
        if (std::abs(v.first - med) < 0.5f) { cs += v.second; ++agree; }
    float conf = agree > 0 ? (float) (cs / agree) * ((float) agree / (float) total) : 0.0f;
    return makeResult(midiToHz(med), juce::jlimit(0.0f, 1.0f, conf * 1.1f));
}

PitchResult hpsDetect(const Spectrum& s)
{
    const int R = 5;
    int kmin = juce::jmax(1, (int) (kFMin / s.binHz)), kmax = juce::jmin((int) (kFMax / s.binHz), (s.bins() - 1) / R);
    if (kmax <= kmin + 2) return {};
    std::vector<float> h((size_t) kmax + 2, -1e30f);
    int best = kmin;
    for (int k = kmin; k <= kmax; ++k)
    {
        double acc = 0;
        for (int r = 1; r <= R; ++r) acc += std::log(s.mag[(size_t) k * r] + 1e-9f);
        h[(size_t) k] = (float) acc;
        if (h[(size_t) k] > h[(size_t) best]) best = k;
    }
    float kf = (float) best;
    if (best > kmin && best < kmax)
    {
        float a = h[(size_t) best - 1], b = h[(size_t) best], c = h[(size_t) best + 1];
        float den = a - 2 * b + c;
        if (std::abs(den) > 1e-9f) kf += 0.5f * (a - c) / den;
    }
    float f0 = (float) (kf * s.binHz);
    return makeResult(f0, harmonicity(s, f0));
}
} // namespace

std::vector<float> mixToMono(const juce::AudioBuffer<float>& b, int start, int end)
{
    start = juce::jlimit(0, b.getNumSamples(), start);
    end = juce::jlimit(start, b.getNumSamples(), end);
    std::vector<float> m((size_t) (end - start), 0.0f);
    const int nc = juce::jmax(1, b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* src = b.getReadPointer(c);
        for (int i = start; i < end; ++i) m[(size_t) (i - start)] += src[i] / (float) nc;
    }
    return m;
}

PitchResult detectPitch(const float* x, int n, double sr, Algo algo)
{
    if (n < 64) return {};
    auto spec = longSpectrum(x, n, sr);
    PitchResult y, h;
    if (algo != Algo::Hps) y = yinDetect(x, n, sr);
    if (algo != Algo::Yin) h = hpsDetect(spec);

    PitchResult r;
    if (algo == Algo::Yin) r = y;
    else if (algo == Algo::Hps) r = h;
    else
    {
        if (y.f0 > 0 && h.f0 > 0)
        {
            float diff = std::abs(y.midi - h.midi);
            if (diff < 0.6f) r = makeResult(y.f0, juce::jmax(y.confidence, h.confidence));
            else r = y.confidence >= 0.5f ? makeResult(y.f0, y.confidence * 0.85f) : h;
        }
        else r = y.f0 > 0 ? y : h;
    }
    if (r.f0 > 0)
    {
        float refined = refineWithHarmonics(spec, r.f0);
        float harm = harmonicity(spec, refined);
        r = makeResult(refined, juce::jmin(r.confidence, 0.35f + 0.65f * harm));
    }
    return r;
}

PitchTrack trackPitch(const float* x, int n, double sr, int maxFrames)
{
    PitchTrack t;
    Yin yin(sr);
    if (n < yin.frameLen) return t;
    t.hop = juce::jmax(yin.W / 2, (n - yin.frameLen) / juce::jmax(1, maxFrames));
    t.firstSample = yin.frameLen / 2;
    for (int s = 0; s + yin.frameLen <= n; s += t.hop)
    {
        auto f = yin.analyse(x + s);
        t.hz.push_back(f.conf > 0.6f ? f.f0 : 0.0f);
    }
    return t;
}

std::shared_ptr<AdditiveData> analyseAdditive(const float* x, int n, double sr, float f0, int maxPartials)
{
    auto d = std::make_shared<AdditiveData>();
    if (f0 <= 0) f0 = 261.63f;
    d->sr = sr;
    d->f0 = f0;
    d->hop = 512;
    int Wn = juce::jlimit(1024, 16384, nextPow2((int) (6.0 * sr / f0)));
    int K = juce::jlimit(1, maxPartials, (int) (0.48 * sr / f0));
    d->window = Wn;
    d->numPartials = K;
    d->numFrames = n / d->hop + 1;
    d->amps.assign((size_t) d->numFrames * (size_t) K, 0.0f);
    d->phase.assign((size_t) d->numFrames * (size_t) K, 0.0f);
    d->rms.assign((size_t) d->numFrames, 0.0f);
    d->meanAmps.assign((size_t) K, 0.0f);
    std::vector<double> rsum((size_t) K, 0.0), wsum((size_t) K, 0.0);

    RealFFT f(Wn);
    auto w = hann(Wn);
    const float wnorm = 2.0f / (Wn * 0.5f);
    const double binHz = sr / Wn;
    std::vector<float> mag((size_t) Wn / 2 + 1);

    for (int fr = 0; fr < d->numFrames; ++fr)
    {
        int c = fr * d->hop;
        std::fill(f.buf.begin(), f.buf.end(), 0.0f);
        double e = 0;
        for (int i = 0; i < Wn; ++i)
        {
            int s = c - Wn / 2 + i;
            float v = (s >= 0 && s < n) ? x[s] : 0.0f;
            e += (double) v * v;
            f.buf[(size_t) i] = v * w[(size_t) i];
        }
        d->rms[(size_t) fr] = (float) std::sqrt(e / Wn);
        f.forward();
        for (int k = 0; k <= Wn / 2; ++k) mag[(size_t) k] = std::hypot(f.buf[(size_t) 2 * k], f.buf[(size_t) 2 * k + 1]);

        double stretch = 1.0;
        for (int k = 0; k < K; ++k)
        {
            double expect = (k + 1) * f0 * stretch;
            int cb = (int) std::round(expect / binHz);
            int r = juce::jmax(1, (int) (0.3 * f0 / binHz));
            int best = juce::jlimit(1, Wn / 2 - 1, cb);
            for (int b = juce::jmax(1, cb - r); b <= juce::jmin(Wn / 2 - 1, cb + r); ++b)
                if (mag[(size_t) b] > mag[(size_t) best]) best = b;
            float a = std::log(mag[(size_t) best - 1] + 1e-12f), bb = std::log(mag[(size_t) best] + 1e-12f),
                  cc = std::log(mag[(size_t) best + 1] + 1e-12f);
            float den = a - 2 * bb + cc;
            float p = std::abs(den) > 1e-9f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (a - cc) / den) : 0.0f;
            float amp = std::exp(bb - 0.25f * (a - cc) * p) * wnorm;
            double fk = (best + p) * binHz;
            d->amps[(size_t) fr * (size_t) K + (size_t) k] = amp;
            // centre-referenced phase: the (-1)^bin factor moves the time origin to the
            // window centre, where a symmetric window leaves the peak phase untouched
            float ph = std::atan2(f.buf[(size_t) 2 * best + 1], f.buf[(size_t) 2 * best]);
            if (best & 1) ph += juce::MathConstants<float>::pi;
            d->phase[(size_t) fr * (size_t) K + (size_t) k] = ph;
            rsum[(size_t) k] += amp * (fk / f0);
            wsum[(size_t) k] += amp;
            if (amp > 1e-5f) stretch = juce::jlimit(0.9, 1.1, fk / ((k + 1) * f0));
        }
    }
    d->ratio.resize((size_t) K);
    float maxMean = 0;
    for (int k = 0; k < K; ++k)
    {
        double harm = k + 1;
        double rr = wsum[(size_t) k] > 1e-9 ? rsum[(size_t) k] / wsum[(size_t) k] : harm;
        d->ratio[(size_t) k] = (float) juce::jlimit(harm * 0.9, harm * 1.1, rr);
        d->meanAmps[(size_t) k] = (float) (wsum[(size_t) k] / juce::jmax(1, d->numFrames));
        maxMean = juce::jmax(maxMean, d->meanAmps[(size_t) k]);
    }
    // the refined f0 is more accurate than the per-frame peak estimates: anchor the series
    // on the fundamental (when it is strong enough to have been measured reliably)
    if (K > 0 && d->meanAmps[0] > 0.1f * maxMean && d->ratio[0] > 0.0f)
    {
        const float r0 = d->ratio[0];
        for (auto& r : d->ratio) r /= r0;
    }
    return d;
}

std::shared_ptr<SpectralData> analyseSpectral(const float* x, int n, double sr, int N, float f0)
{
    auto d = std::make_shared<SpectralData>();
    d->sr = sr;
    d->N = N;
    d->f0 = f0;
    d->hop = N / 4;
    d->bins = N / 2 + 1;
    d->numFrames = n / d->hop + 1;
    const size_t total = (size_t) d->numFrames * (size_t) d->bins;
    d->mag.assign(total, 0.0f);
    d->freq.assign(total, 0.0f);
    d->env.assign(total, 0.0f);
    d->phase.assign(total, 0.0f);
    d->transient.assign((size_t) d->numFrames, 0);
    std::vector<float> fluxHistory;

    RealFFT f(N);
    auto w = hann(N);
    std::vector<float> prevPhase((size_t) d->bins, 0.0f), tmp((size_t) d->bins);
    const float twoPi = juce::MathConstants<float>::twoPi;
    const double binHz = sr / N;
    // cepstral envelope helpers: FFT round trip scale is measured, not assumed
    RealFFT cep(N);
    std::vector<float> target;
    const int lifter = juce::jlimit(8, N / 4, (int) (0.5 * (f0 > 0 ? sr / f0 : sr / 200.0)));
    float cepScale = 1.0f;
    {
        std::fill(cep.buf.begin(), cep.buf.end(), 0.0f);
        cep.buf[0] = 1.0f;
        cep.inverse();
        cep.forward();
        cepScale = std::abs(cep.buf[0]) > 1e-12f ? 1.0f / cep.buf[0] : 1.0f;
    }

    for (int fr = 0; fr < d->numFrames; ++fr)
    {
        int c = fr * d->hop;
        for (int i = 0; i < N; ++i)
        {
            int s = c - N / 2 + i;
            f.buf[(size_t) i] = ((s >= 0 && s < n) ? x[s] : 0.0f) * w[(size_t) i];
        }
        std::fill(f.buf.begin() + N, f.buf.end(), 0.0f);
        f.forward();
        float* mag = d->mag.data() + (size_t) fr * (size_t) d->bins;
        float* frq = d->freq.data() + (size_t) fr * (size_t) d->bins;
        float* env = d->env.data() + (size_t) fr * (size_t) d->bins;
        float* phs = d->phase.data() + (size_t) fr * (size_t) d->bins;
        float mx = 0;
        for (int k = 0; k < d->bins; ++k)
        {
            float re = f.buf[(size_t) 2 * k], im = f.buf[(size_t) 2 * k + 1];
            mag[k] = std::hypot(re, im);
            mx = juce::jmax(mx, mag[k]);
            float ph = std::atan2(im, re);
            phs[k] = ph;
            if (fr == 0) frq[k] = (float) k;
            else
            {
                float dphi = ph - prevPhase[(size_t) k] - twoPi * (float) k * (float) d->hop / (float) N;
                dphi -= twoPi * std::round(dphi / twoPi);
                frq[k] = (float) k + dphi * (float) N / (twoPi * (float) d->hop);
            }
            prevPhase[(size_t) k] = ph;
        }
        // onset detection: positive spectral flux relative to the frame's energy
        if (fr > 0)
        {
            const float* prev = d->mag.data() + (size_t) (fr - 1) * (size_t) d->bins;
            double flux = 0, sum = 1e-12;
            for (int k = 0; k < d->bins; ++k) { flux += juce::jmax(0.0f, mag[k] - prev[k]); sum += mag[k]; }
            float f = (float) (flux / sum);
            float avg = 0;
            for (float h : fluxHistory) avg += h;
            avg = fluxHistory.empty() ? 0.0f : avg / (float) fluxHistory.size();
            d->transient[(size_t) fr] = (f > 0.3f && f > 2.5f * avg) ? 1 : 0;
            fluxHistory.push_back(f);
            if (fluxHistory.size() > 8) fluxHistory.erase(fluxHistory.begin());
        }
        // "True envelope" (Roebel & Rodet): iterative cepstral smoothing that rises to the
        // harmonic peaks without resolving them. Lifter order ~ half a period in quefrency.
        {
            const float floorV = juce::jmax(1e-9f, mx * 1e-5f);
            for (int k = 0; k < d->bins; ++k) tmp[(size_t) k] = std::log(juce::jmax(floorV, mag[k]));
            std::vector<float>& A = target; // working log spectrum
            A.assign(tmp.begin(), tmp.end());
            for (int it = 0; it < 6; ++it)
            {
                // log spectrum -> real cepstrum (inverse of a real, even spectrum)
                std::fill(cep.buf.begin(), cep.buf.end(), 0.0f);
                for (int k = 0; k < d->bins; ++k) cep.buf[(size_t) 2 * k] = A[(size_t) k];
                for (int k = d->bins; k < N; ++k) cep.buf[(size_t) 2 * k] = A[(size_t) (N - k)];
                cep.inverse();
                for (int q = lifter + 1; q < N - lifter; ++q) cep.buf[(size_t) q] = 0.0f;
                std::fill(cep.buf.begin() + N, cep.buf.end(), 0.0f);
                cep.forward();
                const float sc = cepScale;
                bool changed = false;
                for (int k = 0; k < d->bins; ++k)
                {
                    float smooth = cep.buf[(size_t) 2 * k] * sc;
                    env[k] = smooth;
                    float nv = juce::jmax(tmp[(size_t) k], smooth);
                    if (nv - A[(size_t) k] > 0.05f) changed = true;
                    A[(size_t) k] = nv;
                }
                if (!changed) break;
            }
            for (int k = 0; k < d->bins; ++k) env[k] = juce::jmax(floorV, std::exp(env[k]));
        }
    }
    return d;
}

void defaultAnalysisWindow(const float* x, int n, double sr, double& a0, double& a1)
{
    if (n <= 0) { a0 = a1 = 0; return; }
    const int hop = 256;
    int frames = n / hop;
    if (frames < 4) { a0 = 0; a1 = n; return; }
    std::vector<float> rms((size_t) frames);
    float peak = 0;
    int peakIdx = 0;
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int i = 0; i < hop; ++i) e += (double) x[f * hop + i] * x[f * hop + i];
        rms[(size_t) f] = (float) std::sqrt(e / hop);
        if (rms[(size_t) f] > peak) { peak = rms[(size_t) f]; peakIdx = f; }
    }
    int last = peakIdx;
    for (int f = frames - 1; f > peakIdx; --f)
        if (rms[(size_t) f] > peak * 0.01f) { last = f; break; }
    double s = peakIdx * hop + 0.06 * sr;
    double e = juce::jmin((double) (last + 1) * hop, s + 1.0 * sr);
    if (e - s < 0.1 * sr)
    {
        s = 0;
        e = n;
    }
    a0 = juce::jlimit(0.0, (double) n, s);
    a1 = juce::jlimit(a0, (double) n, e);
}

void findTrim(const float* x, int n, int& start, int& end)
{
    float peak = 0;
    for (int i = 0; i < n; ++i) peak = juce::jmax(peak, std::abs(x[i]));
    start = 0;
    end = n;
    if (peak <= 0) return;
    for (int i = 0; i < n; ++i)
        if (std::abs(x[i]) > peak * 0.003f) { start = juce::jmax(0, i - 64); break; }
    for (int i = n - 1; i >= 0; --i)
        if (std::abs(x[i]) > peak * 0.001f) { end = juce::jmin(n, i + 256); break; }
    if (end <= start) { start = 0; end = n; }
}

bool findLoop(const float* x, int n, double sr, float f0, int start, int end, int& ls, int& le)
{
    end = juce::jmin(end, n);
    const int P = f0 > 0 ? juce::jmax(2, (int) std::round(sr / f0)) : 256;
    const int Wc = juce::jmax(2 * P, 1024);
    const int len = end - start;
    if (len < 2 * Wc + 8 * P) return false;

    int s0 = start + (int) (0.35 * len);
    s0 = juce::jmax(s0, start + Wc);
    double L = juce::jlimit(juce::jmax(0.05 * sr, 4.0 * P), 3.0 * sr, 0.4 * len);
    int lo = s0 + (int) (0.7 * L), hi = juce::jmin(end - Wc - 1, s0 + (int) (1.3 * L));
    if (hi <= lo) return false;

    auto score = [&](int a, int b)
    {
        double xy = 0, xx = 0, yy = 0;
        for (int i = -Wc / 2; i < Wc / 2; ++i)
        {
            float u = x[a + i], v = x[b + i];
            xy += u * v; xx += u * u; yy += v * v;
        }
        if (xx <= 1e-12 || yy <= 1e-12) return -1.0;
        double corr = xy / std::sqrt(xx * yy);
        double lvl = juce::jmin(xx, yy) / juce::jmax(xx, yy);
        return corr * 0.8 + std::sqrt(lvl) * 0.2;
    };
    const int step = juce::jmax(1, P / 8);
    int best = lo;
    double bestS = -2;
    for (int c = lo; c <= hi; c += step)
    {
        double s = score(s0, c);
        if (s > bestS) { bestS = s; best = c; }
    }
    for (int c = juce::jmax(lo, best - step); c <= juce::jmin(hi, best + step); ++c)
    {
        double s = score(s0, c);
        if (s > bestS) { bestS = s; best = c; }
    }
    auto snap = [&](int p)
    {
        for (int r = 0; r < P; ++r)
        {
            for (int q : { p + r, p - r })
                if (q > 0 && q < n && x[q - 1] <= 0.0f && x[q] > 0.0f) return q;
        }
        return p;
    };
    ls = snap(s0);
    le = snap(best);
    if (le - ls < 2 * P) return false;
    return true;
}

int bakeLoopCrossfade(juce::AudioBuffer<float>& b, int loopStart, int loopEnd, int xf)
{
    xf = juce::jmin(xf, loopStart, (loopEnd - loopStart) / 2);
    if (xf < 2) return 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        float* d = b.getWritePointer(c);
        for (int i = 0; i < xf; ++i)
        {
            float t = ((float) i + 0.5f) / (float) xf;
            float gOut = std::cos(t * juce::MathConstants<float>::halfPi);
            float gIn = std::sin(t * juce::MathConstants<float>::halfPi);
            int idx = loopEnd - xf + i;
            d[idx] = d[idx] * gOut + d[loopStart - xf + i] * gIn;
        }
    }
    return xf;
}

void computePeaks(const juce::AudioBuffer<float>& b, int columns, std::vector<float>& mn, std::vector<float>& mx)
{
    mn.assign((size_t) columns, 0.0f);
    mx.assign((size_t) columns, 0.0f);
    const int n = b.getNumSamples();
    if (n == 0 || columns <= 0) return;
    for (int col = 0; col < columns; ++col)
    {
        int s0 = (int) ((int64_t) col * n / columns), s1 = juce::jmax(s0 + 1, (int) ((int64_t) (col + 1) * n / columns));
        float lo = 0, hi = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float* d = b.getReadPointer(c);
            for (int i = s0; i < juce::jmin(s1, n); ++i) { lo = juce::jmin(lo, d[i]); hi = juce::jmax(hi, d[i]); }
        }
        mn[(size_t) col] = lo;
        mx[(size_t) col] = hi;
    }
}
}
