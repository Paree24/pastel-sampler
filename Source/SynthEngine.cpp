#include "SynthEngine.h"

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

float dbToGain(float db) { return db <= -47.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }

// cents offset per scale degree (semitones above the tonic) relative to 12-TET
const float kJust[12] { 0, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -9.78f, 1.96f, 13.69f, -15.64f, 17.6f, -11.73f };
const float kPyth[12] { 0, -9.78f, 3.91f, -5.87f, 7.82f, -1.96f, -11.73f, 1.96f, -7.82f, 5.87f, -3.91f, 9.78f };

// Blackman-windowed sinc, 8 zero crossings each side
struct SincTable
{
    static constexpr int Z = 8, RES = 512;
    std::vector<float> t;
    SincTable()
    {
        t.resize((size_t) (Z * RES + 2));
        for (int i = 0; i < (int) t.size(); ++i)
        {
            double u = (double) i / RES;
            double s = u == 0.0 ? 1.0 : std::sin(juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
            double w = u >= Z ? 0.0 : 0.42 + 0.5 * std::cos(juce::MathConstants<double>::pi * u / Z)
                                    + 0.08 * std::cos(2.0 * juce::MathConstants<double>::pi * u / Z);
            t[(size_t) i] = (float) (s * w);
        }
    }
    float at(float u) const
    {
        u = std::abs(u);
        if (u >= (float) Z) return 0.0f;
        float f = u * RES;
        int i = (int) f;
        float fr = f - (float) i;
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * fr;
    }
};
const SincTable& sincTable()
{
    static SincTable s;
    return s;
}

// Cytomic TPT state-variable filter
struct SVF
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    float process(float v0, float a1, float a2, float a3, float k, int type)
    {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return type == 0 ? v2 : (type == 1 ? v0 - k * v1 - v2 : v1);
    }
};

inline float readInterp(const float* d, int len, double pos, int interp, double inc)
{
    auto at = [&](int j) { return (j >= 0 && j < len) ? d[j] : 0.0f; };
    int i = (int) std::floor(pos);
    float f = (float) (pos - i);
    if (interp == 0) return at(i) + f * (at(i + 1) - at(i));
    if (interp == 1)
    {
        float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
        float c1 = 0.5f * (x1 - xm1);
        float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    const auto& st = sincTable();
    float fc = inc > 1.0 ? (float) (1.0 / inc) : 1.0f;
    int H = juce::jmin(64, (int) std::ceil(SincTable::Z / fc));
    float sum = 0;
    for (int j = i - H + 1; j <= i + H; ++j)
    {
        if (j < 0 || j >= len) continue;
        sum += d[j] * st.at((float) (pos - j) * fc);
    }
    return sum * fc;
}

bool loopActive(int loopMode, bool held) { return loopMode == 1 || loopMode == 2 || (loopMode == 3 && held); }
} // namespace

// ---------------------------------------------------------------------------
namespace Blend
{
float noteHz(const EngineParams& p, double note)
{
    int pc = (((int) std::lround(note) - p.tonic) % 12 + 12) % 12;
    float cents = p.tuning == 1 ? kJust[pc] : (p.tuning == 2 ? kPyth[pc] : 0.0f);
    return (float) (p.a4 * std::pow(2.0, (note - 69.0 + cents / 100.0) / 12.0));
}

void weights(const EngineParams& p, float& ws, float& wa, float& wp, float& wg)
{
    const bool blend = p.engine == 4;
    // the mixer levels only apply in Blend; a single engine always plays at unity
    ws = blend ? dbToGain(p.sLevel) : (p.engine == 0 ? 1.0f : 0.0f);
    wa = blend ? dbToGain(p.aLevel) : (p.engine == 1 ? 1.0f : 0.0f);
    wp = blend ? dbToGain(p.pLevel) : (p.engine == 2 ? 1.0f : 0.0f);
    wg = blend ? dbToGain(p.gLevel) : (p.engine == 3 ? 1.0f : 0.0f);
}
}

// ---------------------------------------------------------------------------
struct SamplerPlayer
{
    double pos = 0;
    int dir = 1;
    bool done = true;

    void start(double p) { pos = p; dir = 1; done = false; }

    void render(const Instrument& in, float* L, float* R, int n, double inc, int interp, int loopMode, bool held)
    {
        const bool loopNow = loopActive(loopMode, held) && in.loopEnd - in.loopStart > 16;
        const bool baked = loopNow && loopMode != 2 && in.loopBaked;
        const auto& buf = baked ? in.audio : in.raw;
        const int len = buf.getNumSamples();
        const float* l = buf.getReadPointer(0);
        const float* r = buf.getReadPointer(juce::jmin(1, buf.getNumChannels() - 1));
        const double ls = in.loopStart, le = in.loopEnd;
        for (int i = 0; i < n; ++i)
        {
            if (done) { L[i] = R[i] = 0; continue; }
            L[i] = readInterp(l, len, pos, interp, inc);
            R[i] = readInterp(r, len, pos, interp, inc);
            pos += inc * dir;
            if (loopNow)
            {
                if (loopMode == 2)
                {
                    if (dir > 0 && pos >= le) { pos = le - (pos - le); dir = -1; }
                    else if (dir < 0 && pos <= ls) { pos = ls + (ls - pos); dir = 1; }
                }
                else if (pos >= le) pos -= (le - ls);
            }
            else
            {
                dir = 1;
                if (pos >= in.end) done = true;
            }
        }
    }
};

struct AdditivePlayer
{
    double frame = 0;
    bool done = true;
    std::array<float, 128> re {}, im {}, amp {};
    uint32_t seed = 22222;
    float lp = 0;

    // Start every partial at the sample's own phase so additive sums coherently with
    // the sampler / spectral engines in Blend (no comb filtering between engines).
    void start(const AdditiveData& d, double startSample, juce::Random&)
    {
        frame = startSample / d.hop;
        done = false;
        // phase reference: the nearest frame whose window lies fully inside the sample
        // (edge frames are zero-padded and skew the phase), projected back to the start
        const int firstFull = (d.window / 2 + d.hop - 1) / d.hop;
        const int f0i = juce::jlimit(0, juce::jmax(0, d.numFrames - 1), juce::jmax(firstFull, (int) std::lround(frame)));
        const double dt = startSample - (double) f0i * d.hop;
        for (int k = 0; k < 128; ++k)
        {
            float ph = 0.0f;
            if (k < d.numPartials && d.numFrames > 0)
            {
                const double w = juce::MathConstants<double>::twoPi * d.f0 * d.ratio[(size_t) k] / d.sr;
                ph = d.phase[(size_t) f0i * (size_t) d.numPartials + (size_t) k] + (float) (w * dt);
            }
            // the oscillator outputs the sine component: sin(x + pi/2) = cos(x)
            ph += juce::MathConstants<float>::halfPi;
            re[(size_t) k] = std::cos(ph);
            im[(size_t) k] = std::sin(ph);
        }
        amp.fill(0.0f);
        lp = 0;
    }

    // pitchRatio = played pitch / root pitch; partials follow the sample's own f0 so the
    // additive engine stays in tune with the sampler even when the root is overridden
    void render(const AdditiveData& d, float* out, int n, double pitchRatio, double outSR, double framesPerSample,
                const EngineParams& p, bool loopNow, double lsF, double leF)
    {
        std::fill(out, out + n, 0.0f);
        if (done || d.numFrames == 0) return;
        const int K = juce::jmin(p.partials, d.numPartials);
        int a = juce::jlimit(0, d.numFrames - 1, (int) frame);
        int b = juce::jmin(a + 1, d.numFrames - 1);
        float t = juce::jlimit(0.0f, 1.0f, (float) (frame - a));
        const float nyq = (float) (0.48 * outSR);
        const float invN = 1.0f / (float) n;
        for (int k = 0; k < d.numPartials; ++k)
        {
            float f = (float) (d.f0 * pitchRatio) * d.ratio[(size_t) k];
            float target = 0;
            if (k < K && f < nyq)
            {
                float tilt = p.tilt == 0.0f ? 1.0f : std::pow(10.0f, p.tilt * std::log2((float) k + 1.0f) / 20.0f);
                target = (d.amp(a, k) * (1 - t) + d.amp(b, k) * t) * p.partialGains[(size_t) k] * tilt;
            }
            float g = amp[(size_t) k];
            if (target == 0.0f && g == 0.0f) continue;
            float w = kTwoPi * f / (float) outSR;
            float c = std::cos(w), s = std::sin(w);
            float ar = re[(size_t) k], ai = im[(size_t) k];
            float dg = (target - g) * invN;
            for (int i = 0; i < n; ++i)
            {
                float nr = ar * c - ai * s;
                ai = ar * s + ai * c;
                ar = nr;
                g += dg;
                out[i] += g * ai;
            }
            float corr = (3.0f - (ar * ar + ai * ai)) * 0.5f;
            re[(size_t) k] = ar * corr;
            im[(size_t) k] = ai * corr;
            amp[(size_t) k] = target;
        }
        if (p.noise > 0.0f)
        {
            float lvl = (d.rms[(size_t) a] * (1 - t) + d.rms[(size_t) b] * t) * p.noise * 1.4f;
            float fc = juce::jmin(16000.0f, (float) (d.f0 * pitchRatio) * (float) juce::jmax(1, K));
            float coef = 1.0f - std::exp(-kTwoPi * fc / (float) outSR);
            for (int i = 0; i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                float white = (float) (int32_t) seed * (1.0f / 2147483648.0f);
                lp += coef * (white - lp);
                out[i] += lp * lvl;
            }
        }
        frame += framesPerSample * n;
        if (loopNow && leF - lsF >= 2.0)
        {
            while (frame >= leF) frame -= (leF - lsF);
        }
        else if (frame >= d.numFrames - 1) done = true;
    }
};

struct SpectralCtx
{
    double ratio = 1, srcPerOut = 1, timeRate = 1;
    float shiftRatio = 1, blur = 0;
    bool formant = true, loopNow = false;
    double lsF = 0, leF = 0;
};

struct SpectralPlayer
{
    int N = 0, hop = 0, bins = 0;
    double frame = 0, rp = 0;
    bool done = true, primed = false, fresh = true;
    int tail = 0, skip = 0, lastNear = 0;
    double harmD = 0; // shared harmonic phase offset (radians per harmonic number)
    float invScale = 1;
    // windowed-sinc resampler over the (already band-limited) synthesis stream
    std::array<float, 512> ring {};
    long long written = 0;
    double rpos = 0;
    const float* win = nullptr;
    juce::dsp::FFT* fft = nullptr;
    // psi: output phase of every bin in the previous synthesis frame
    std::vector<float> psi, outMag, outPh, dom, prevMag, mag, ola, buf, queue;
    std::vector<int> peaks, bounds;
    // output phase each SOURCE bin received last frame (Laroche-Dolson peak phase propagation:
    // stays exact at unity and consistent when a region's integer shift changes)
    std::vector<float> srcPsiPrev, srcPsiCur;
    int qr = 0, qn = 0;

    void allocate(int maxN)
    {
        size_t b = (size_t) maxN / 2 + 1;
        psi.assign(b, 0); outMag.assign(b, 0); outPh.assign(b, 0); dom.assign(b, 0); prevMag.assign(b, 0);
        mag.assign(b, 0);
        peaks.reserve(b); bounds.reserve(b + 1);
        srcPsiPrev.assign(b, 0.0f); srcPsiCur.assign(b, 0.0f);
        ola.assign((size_t) maxN, 0); buf.assign((size_t) maxN * 2, 0); queue.assign((size_t) maxN, 0);
    }

    void start(const SpectralData& d, double startSample, juce::dsp::FFT* f, float inv, const float* window, double advance)
    {
        N = d.N; hop = d.hop; bins = d.bins;
        fft = f; invScale = inv; win = window;
        std::fill(psi.begin(), psi.end(), 0.0f);
        std::fill(prevMag.begin(), prevMag.end(), 0.0f);
        std::fill(ola.begin(), ola.end(), 0.0f);
        qr = qn = 0; rp = 0; primed = false; tail = 0; fresh = true;
        ring.fill(0.0f); written = 0; rpos = 0;
        harmD = 0; lastNear = 0;
        std::fill(srcPsiPrev.begin(), srcPsiPrev.end(), 0.0f);
        // Pre-roll: synthesise from frames centred before the note start and drop the first
        // N - hop output samples, so the very first sample already has full overlap-add
        // support (otherwise the attack is faded in by the window and lost).
        skip = N - hop;
        // stream sample `skip` (the first one emitted) must line up with startSample
        frame = (startSample - (skip - N / 2) * advance) / (double) hop;
        done = false;
    }

    // Pitch shifting = phase-vocoder time stretch by the pitch ratio, then resampling by the
    // same ratio (render()). Nothing is moved between bins, so there is no bin-rounding
    // error: every partial comes out at exactly ratio x its frequency, like the sampler.
    //  - identity phase locking: bins of a peak's region keep their analysed offsets
    //  - harmonic phase locking (pitched samples): harmonic h = analysed phase + h * harmD,
    //    which keeps the waveform shape (vertical coherence)
    //  - inharmonic peaks: Laroche-Dolson propagation from the same source bin last frame
    //  - phase resets at the note start and on onsets keep attacks sharp
    void synth(const SpectralData& d, const SpectralCtx& c)
    {
        std::fill(outMag.begin(), outMag.begin() + bins, 0.0f);
        std::fill(dom.begin(), dom.begin() + bins, 0.0f);
        const float adv = kTwoPi * (float) hop / (float) N;
        int fi = (int) std::floor(frame);
        const bool before = fi < 0;
        const bool beyond = fi >= d.numFrames - 1 || before;
        if (!beyond)
        {
            const float ft = (float) (frame - fi);
            const float* m0 = d.magAt(fi);
            const float* m1 = d.magAt(fi + 1);
            const int near = ft < 0.5f ? fi : fi + 1;
            const float* fr = d.freqAt(near);
            const float* ph = d.phaseAt(near);
            const float* env = d.envAt(near);
            float mx = 0;
            for (int k = 0; k < bins; ++k)
            {
                mag[(size_t) k] = m0[k] + (m1[k] - m0[k]) * ft;
                mx = juce::jmax(mx, mag[(size_t) k]);
            }
            const float thr = mx * 1e-4f;
            peaks.clear();
            for (int k = 2; k < bins - 2; ++k)
            {
                const float m = mag[(size_t) k];
                if (m > thr && m > 0.03f * env[k] && m > mag[(size_t) k - 1] && m >= mag[(size_t) k + 1]
                    && m >= mag[(size_t) k - 2] && m >= mag[(size_t) k + 2])
                    peaks.push_back(k);
            }
            bounds.clear();
            bounds.push_back(0);
            for (size_t j = 1; j < peaks.size(); ++j)
            {
                int lo = peaks[j - 1], best = lo;
                for (int k = lo; k <= peaks[j]; ++k) if (mag[(size_t) k] < mag[(size_t) best]) best = k;
                bounds.push_back(best);
            }
            bounds.push_back(bins);

            const bool reset = fresh || d.transient[(size_t) near] != 0;
            const float f0b = d.f0 > 0.0f ? (float) (d.f0 * N / d.sr) : 0.0f;
            const bool harmonic = f0b > 1.5f;
            if (harmonic)
            {
                const int dn = near - lastNear;
                if (reset) harmD = 0;
                else
                {
                    // synthesis advances one hop; the analysis advanced dn hops
                    harmD += (double) adv * f0b * (1.0 - (double) dn);
                    // timeline jump (loop wrap): re-synchronise on the strongest low harmonic
                    if (dn < 0 || dn > 3)
                    {
                        int bestP = -1;
                        float bestM = 0, bestH = 1;
                        for (int pk : peaks)
                        {
                            const float h = std::round(fr[pk] / f0b);
                            if (h < 1.0f || h > 6.0f || std::abs(fr[pk] / f0b - h) > 0.2f) continue;
                            if (mag[(size_t) pk] > bestM) { bestM = mag[(size_t) pk]; bestP = pk; bestH = h; }
                        }
                        if (bestP >= 0)
                        {
                            const double want = srcPsiPrev[(size_t) bestP] + (double) adv * fr[bestP];
                            const double got = ph[bestP] + bestH * harmD;
                            harmD += std::remainder(want - got, juce::MathConstants<double>::twoPi) / bestH;
                        }
                    }
                }
                harmD = std::fmod(harmD, juce::MathConstants<double>::twoPi * 1.0e6);
            }
            lastNear = near;

            // formant handling: after resampling, bin k sounds at k * ratio. Formant preservation
            // keeps the envelope where it was (env(k * ratio)); otherwise it moves with the pitch.
            const float envPosScale = (c.formant ? (float) c.ratio : 1.0f) / c.shiftRatio;
            const bool unityGain = !c.formant && std::abs(c.shiftRatio - 1.0f) < 1e-4f;
            auto envAtPos = [&](float pos)
            {
                if (pos >= (float) (bins - 1)) return env[bins - 1];
                int ei = juce::jmax(0, (int) pos);
                float ef = pos - (float) ei;
                return env[ei] + (env[ei + 1] - env[ei]) * ef;
            };
            // anti-alias: bins that would land above Nyquist after resampling up are dropped
            const int maxBin = c.ratio > 1.0 ? juce::jmin(bins - 1, (int) ((bins - 1) * 0.98 / c.ratio)) : bins - 1;

            for (size_t j = 0; j < peaks.size(); ++j)
            {
                const int p = peaks[j];
                float psiT;
                const float hReal = harmonic ? fr[p] / f0b : 0.0f;
                const float hInt = std::round(hReal);
                if (harmonic && hInt >= 1.0f && std::abs(hReal - hInt) < 0.3f)
                    psiT = ph[p] + (float) std::fmod((double) hInt * harmD, (double) juce::MathConstants<double>::twoPi);
                else if (reset)
                    psiT = ph[p];
                else
                    psiT = srcPsiPrev[(size_t) p] + adv * fr[p];
                const float g = unityGain ? 1.0f : juce::jmin(16.0f, envAtPos((float) p * envPosScale) / env[p]);
                for (int k = bounds[j]; k < bounds[j + 1]; ++k)
                {
                    const float phk = psiT + (ph[k] - ph[p]);
                    srcPsiCur[(size_t) k] = phk;
                    if (k < 1 || k > maxBin) continue;
                    outMag[(size_t) k] = mag[(size_t) k] * g;
                    dom[(size_t) k] = 1.0f;
                    outPh[(size_t) k] = phk;
                }
            }
        }
        else if (!before) ++tail;
        if (!before) fresh = false;
        if (!beyond && !before) std::swap(srcPsiPrev, srcPsiCur);

        for (int b = 0; b < bins; ++b)
        {
            float ph = dom[(size_t) b] > 0.0f ? outPh[(size_t) b] : psi[(size_t) b] + adv * (float) b;
            ph -= kTwoPi * std::floor(ph / kTwoPi);
            psi[(size_t) b] = ph;
            float m = outMag[(size_t) b];
            if (c.blur > 0.0f)
            {
                prevMag[(size_t) b] = c.blur * prevMag[(size_t) b] + (1.0f - c.blur) * m;
                m = prevMag[(size_t) b];
            }
            buf[(size_t) 2 * b] = m * std::cos(ph);
            buf[(size_t) 2 * b + 1] = m * std::sin(ph);
        }
        buf[1] = 0.0f;
        buf[(size_t) 2 * (bins - 1) + 1] = 0.0f;
        for (int b = bins; b < N; ++b)
        {
            buf[(size_t) 2 * b] = buf[(size_t) 2 * (N - b)];
            buf[(size_t) 2 * b + 1] = -buf[(size_t) 2 * (N - b) + 1];
        }
        fft->performRealOnlyInverseTransform(buf.data());
        // analysis + synthesis Hann at 75% overlap sums to 1.5
        const float g = invScale / 1.5f;
        for (int i = 0; i < N; ++i) ola[(size_t) i] += buf[(size_t) i] * win[i] * g;
        std::copy(ola.begin(), ola.begin() + hop, queue.begin());
        qr = 0; qn = hop;
        std::copy(ola.begin() + hop, ola.begin() + N, ola.begin());
        std::fill(ola.begin() + (N - hop), ola.begin() + N, 0.0f);

        // analysis advance per synthesis hop: the stream is later resampled by `ratio`
        frame += c.timeRate / juce::jmax(1.0e-3, c.ratio);
        if (c.loopNow && c.leF - c.lsF >= 2.0)
        {
            while (frame >= c.leF) frame -= (c.leF - c.lsF);
        }
        if (beyond && tail > N / hop + 1) done = true;
    }

    float pull(const SpectralData& d, const SpectralCtx& c)
    {
        for (;;)
        {
            if (qr >= qn) synth(d, c);
            float v = queue[(size_t) qr++];
            if (skip > 0) { --skip; continue; }
            return v;
        }
    }

    void render(const SpectralData& d, float* out, int n, const SpectralCtx& c)
    {
        if (done) { std::fill(out, out + n, 0.0f); return; }
        // the stream is a time-stretched copy; reading it `ratio` times faster restores the
        // duration and transposes every frequency exactly. Content was band-limited in the
        // spectral domain, so a fixed 8-zero-crossing sinc is alias-free here.
        const auto& st = sincTable();
        constexpr int H = SincTable::Z;
        const double inc = c.srcPerOut * c.ratio;
        for (int i = 0; i < n; ++i)
        {
            const long long i0 = (long long) std::floor(rpos);
            while (written <= i0 + H) { ring[(size_t) (written & 511)] = pull(d, c); ++written; }
            float sum = 0;
            for (long long j = i0 - H + 1; j <= i0 + H; ++j)
                if (j >= 0) sum += ring[(size_t) (j & 511)] * st.at((float) (rpos - (double) j));
            out[i] = sum;
            rpos += inc;
        }
    }
};

// ---------------------------------------------------------------------------
// Granular: windowed grains read from the sample around a moving playhead. Each grain is
// resampled to the played key (pitch follows the keyboard, duration does not), sprayed in
// time, optionally pitch-jittered and panned within the stereo width.
struct GranularPlayer
{
    struct Grain { double pos = 0, inc = 1; int age = 0, len = 0; float gL = 1, gR = 1; bool on = false; };
    static constexpr int kMaxGrains = 64;
    std::array<Grain, kMaxGrains> g {};
    double head = 0, untilNext = 0;
    bool first = true;
    uint32_t seed = 1;

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); }

    void start(const Instrument& in, double startSample, const EngineParams& p, uint32_t s)
    {
        for (auto& x : g) x.on = false;
        const double span = juce::jmax(1, in.end - in.start);
        head = p.gPos > 0.0f ? in.start + p.gPos * span : startSample;
        untilNext = 0;
        first = true;
        seed = s | 1u;
    }

    static float window(float x, float alpha)
    {
        if (x < 0.0f || x > 1.0f) return 0.0f;
        const float h = 0.5f * alpha;
        if (x < h) return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * x / h);
        if (x > 1.0f - h) return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * (1.0f - x) / h);
        return 1.0f;
    }

    static float cubicAt(const float* d, int len, double pos)
    {
        const int i = (int) pos;
        const float f = (float) (pos - i);
        auto at = [&](int j) { return d[juce::jlimit(0, len - 1, j)]; };
        const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2,
                    c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void render(const Instrument& in, float* L, float* R, int n, double incBase, double srcPerOut, double outSR,
                const EngineParams& p, bool loopNow)
    {
        const auto& buf = in.raw;
        const int len = buf.getNumSamples();
        const float* dl = buf.getReadPointer(0);
        const float* dr = buf.getReadPointer(juce::jmin(1, buf.getNumChannels() - 1));
        const int glen = juce::jmax(16, (int) (p.gSizeMs * 0.001 * outSR));
        const double interval = outSR / juce::jmax(0.5f, p.gDensity);
        const float alpha = 1.0f - 0.95f * p.gShape;
        const double spray = p.gSprayMs * 0.001 * in.sr;
        // level normalisation for overlapping grains: period-aligned grains add coherently
        // (amplitude ~ overlap * mean window); pitch-jittered grains add incoherently (power)
        const float overlap = (float) glen / (float) interval;
        const bool coherent = in.pitch.pitched() && p.gJitter < 5.0f;
        const float wMean = 1.0f - 0.5f * alpha, wPow = 1.0f - 0.625f * alpha;
        const float gain = coherent ? 1.0f / juce::jmax(1.0f, overlap * wMean)
                                    : 1.0f / std::sqrt(juce::jmax(1.0f, overlap * wPow));
        // pitch-synchronous grain placement: new grains start a whole number of periods away
        // from the newest sounding grain, so overlapping grains add in phase (clean pitch,
        // no comb filtering). Spray still randomises, in steps of a period.
        const double period = in.pitch.pitched() ? in.sr / in.pitch.f0 : 0.0;
        const bool loopOk = loopNow && in.loopEnd - in.loopStart > glen;

        for (int i = 0; i < n; ++i)
        {
            if (untilNext <= 0.0)
            {
                for (auto& gr : g)
                {
                    if (gr.on) continue;
                    const double jit = first ? 0.0 : (rnd() * 2.0f - 1.0f) * p.gJitter;
                    gr.inc = incBase * std::pow(2.0, jit / 1200.0);
                    double pos = head + p.gPosMod * (double) juce::jmax(1, in.end - in.start)
                               + (first ? 0.0 : (rnd() * 2.0f - 1.0f) * spray);
                    const double need = gr.inc * glen;
                    const double lo = in.start, hi = juce::jmax((double) in.start, in.end - need - 2.0);
                    pos = juce::jlimit(lo, hi, pos);
                    if (period > 1.0 && !first)
                    {
                        const Grain* ref = nullptr;
                        for (auto& o : g)
                            if (o.on && &o != &gr && (ref == nullptr || o.age < ref->age)) ref = &o;
                        if (ref != nullptr)
                        {
                            double k = std::round((pos - ref->pos) / period);
                            double aligned = ref->pos + k * period;
                            while (aligned > hi && aligned - period >= lo) aligned -= period;
                            while (aligned < lo && aligned + period <= hi) aligned += period;
                            pos = aligned;
                        }
                    }
                    gr.pos = pos;
                    gr.age = 0;
                    gr.len = glen;
                    const float pan = first ? 0.0f : (rnd() * 2.0f - 1.0f) * p.gWidth;
                    const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    gr.gL = std::cos(a) * juce::MathConstants<float>::sqrt2;
                    gr.gR = std::sin(a) * juce::MathConstants<float>::sqrt2;
                    gr.on = true;
                    break;
                }
                untilNext += interval * (first ? 1.0 : 0.75 + 0.5 * rnd());
                first = false;
            }
            untilNext -= 1.0;

            float l = 0, r = 0;
            for (auto& gr : g)
            {
                if (!gr.on) continue;
                const float w = window((float) gr.age / (float) gr.len, alpha);
                l += cubicAt(dl, len, gr.pos) * w * gr.gL;
                r += cubicAt(dr, len, gr.pos) * w * gr.gR;
                gr.pos += gr.inc;
                if (++gr.age >= gr.len || gr.pos >= len - 2) gr.on = false;
            }
            L[i] = l * gain;
            R[i] = r * gain;

            // playhead: Scan = speed through the sample (1 = real time, 0 = freeze)
            head += p.gScan * srcPerOut;
            if (loopOk)
            {
                const double ls = in.loopStart, le = in.loopEnd, span = le - ls;
                while (head >= le) head -= span;
                while (head < ls) head += span;
            }
            else
                head = juce::jlimit((double) in.start, juce::jmax((double) in.start, in.end - incBase * glen - 2.0), head);
        }
    }

    int activePositions(float* out, int maxN, double total) const
    {
        int c = 0;
        for (auto& gr : g)
            if (gr.on && c < maxN) out[c++] = (float) (gr.pos / total);
        return c;
    }
};

struct SynthEngine::Voice
{
    bool active = false, held = false;
    int note = -1;
    float vel = 0;
    uint64_t age = 0;
    double pitch = 60, target = 60, glideStep = 0;
    float pan = 0, detune = 0, gain = 1;
    bool runS = false, runA = false, runP = false;
    juce::ADSR env;
    SVF filt[2][2];
    SamplerPlayer sp;
    GranularPlayer gp;
    bool runG = false;
    bool blendMode = false; // started in Blend: levels follow the mixer
    // transient handoff: the attack of a resynthesised note comes from the sample itself
    SamplerPlayer att;
    bool attOn = false;
    int attPos = 0, attFade0 = 0, attFade1 = 0;
    AdditivePlayer ap;
    SpectralPlayer pp, ppR; // left / right (ppR unused for mono files)
    // attack timeline: 1 = follow the sampler (no stretch) during the attack, ramps to 0 after
    double tl = 0.0;
    bool stereoP = false;
    const Instrument* src = nullptr; // the instrument this voice started on
    float modRand = 0; // Random mod source: drawn per note, -1..1
};

// ---------------------------------------------------------------------------
SynthEngine::SynthEngine() = default;
SynthEngine::~SynthEngine() = default;

static std::vector<float> s_windows[SynthEngine::kNumFft];

void SynthEngine::prepare(double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& b : lfoBuf) b.assign((size_t) (juce::jmax(512, maxBlock) / kSub + 2), 0.0f);
    sincTable();
    for (int i = 0; i < kNumFft; ++i)
    {
        int order = 10 + i, N = 1 << order;
        ffts[(size_t) i] = std::make_unique<juce::dsp::FFT>(order);
        // measure the backend's inverse scaling with an impulse round trip
        std::vector<float> b((size_t) N * 2, 0.0f);
        b[0] = 1.0f;
        ffts[(size_t) i]->performRealOnlyForwardTransform(b.data());
        ffts[(size_t) i]->performRealOnlyInverseTransform(b.data());
        fftInvScale[(size_t) i] = std::abs(b[0]) > 1e-12f ? 1.0f / b[0] : 1.0f;
        if (s_windows[i].size() != (size_t) N)
        {
            s_windows[i].resize((size_t) N);
            for (int k = 0; k < N; ++k)
                s_windows[i][(size_t) k] = 0.5f - 0.5f * std::cos(kTwoPi * (float) k / (float) N);
        }
    }
    voices.clear();
    for (auto& l : lingering) l.reset();
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto v = std::make_unique<Voice>();
        v->env.setSampleRate(sr);
        v->pp.allocate(16384);
        v->ppR.allocate(16384);

        voices.push_back(std::move(v));
    }
    heldStack.reserve(128);
}

std::shared_ptr<const Instrument> SynthEngine::setInstrument(std::shared_ptr<const Instrument> i, bool keepVoices)
{
    if (!keepVoices || inst == nullptr)
    {
        allNotesOff(true);
        std::swap(inst, i);
        return i;
    }
    // sounding voices keep playing the instrument they started on; it lingers until they end
    std::shared_ptr<const Instrument> out;
    if (usedByVoice(inst.get()))
    {
        std::shared_ptr<const Instrument>* slot = nullptr;
        for (auto& l : lingering) if (l == nullptr) { slot = &l; break; }
        if (slot == nullptr)
        {
            // no room: the oldest lingering instrument's voices stop
            slot = &lingering[(size_t) lingerNext];
            lingerNext = (lingerNext + 1) % (int) lingering.size();
            for (auto& v : voices)
                if (v->src == slot->get()) { v->active = false; v->env.reset(); }
            out = std::move(*slot);
        }
        *slot = std::move(inst);
    }
    else out = std::move(inst);
    inst = std::move(i);
    return out;
}

bool SynthEngine::usedByVoice(const Instrument* in) const
{
    for (auto& v : voices) if (v->active && v->src == in) return true;
    return false;
}

std::shared_ptr<const Instrument> SynthEngine::takeRetired()
{
    for (auto& l : lingering)
        if (l != nullptr && !usedByVoice(l.get())) return std::move(l);
    return nullptr;
}

float SynthEngine::rootHzOf(const Instrument& in)
{
    return Analysis::midiToHz(in.pitch.f0 > 0 ? in.pitch.midi : 60.0f);
}

int SynthEngine::numActiveVoices() const
{
    int n = 0;
    for (auto& v : voices) n += v->active ? 1 : 0;
    return n;
}

static juce::ADSR::Parameters envParams(const EngineParams& p)
{
    if (!p.eOn) return { 0.001f, 0.001f, 1.0f, 0.012f };
    return { p.A * 0.001f, p.D * 0.001f, p.S, p.R * 0.001f };
}

void SynthEngine::startVoice(Voice& v, int note, float vel, const EngineParams& p, double fromPitch, bool glide)
{
    v.active = true;
    v.held = true;
    v.note = note;
    v.vel = vel;
    v.age = ++ageCounter;
    v.target = note;
    v.pitch = (glide && fromPitch >= 0 && p.glideMs > 0) ? fromPitch : (double) note;
    v.glideStep = p.glideMs > 0 ? std::abs(v.target - v.pitch) / (p.glideMs * 0.001 * sr) : 0.0;

    float startJitter = 0;
    v.detune = 0;
    v.gain = 1;
    v.pan = 0;
    if (p.rr)
    {
        v.detune += (rng.nextFloat() * 2 - 1) * 4.0f;
        v.gain = dbToGain((rng.nextFloat() * 2 - 1) * 0.5f);
        startJitter = rng.nextFloat() * 0.004f;
    }
    if (p.spread > 0)
    {
        float side = (v.age & 1) ? 1.0f : -1.0f;
        v.pan = side * p.spread * (0.5f + 0.5f * rng.nextFloat());
        v.detune += side * p.spread * 8.0f * rng.nextFloat();
    }

    v.modRand = rng.nextFloat() * 2.0f - 1.0f;
    v.env.setParameters(envParams(p));
    v.env.reset();
    v.env.noteOn();
    for (auto& ch : v.filt) for (auto& f : ch) f.reset();

    // the voice keeps the engine it started with, so switching engines lets ringing notes finish
    v.runS = v.runA = v.runP = v.runG = false;
    v.blendMode = p.engine == 4;
    v.src = inst.get();
    if (inst == nullptr) return;
    const double region = (p.loopMode != 0 ? inst->loopStart : inst->end) - inst->start;
    double startSample = inst->start + juce::jmax(0.0, region) * p.start * 0.999 + startJitter * inst->sr;
    // Blend runs every engine whose mixer level is above -inf
    float ws, wa, wp;
    float wg;
    Blend::weights(p, ws, wa, wp, wg);
    if (ws > 0) { v.sp.start(startSample); v.runS = true; }
    if (wg > 0) { v.gp.start(*inst, startSample, p, (uint32_t) (v.age * 2654435761u)); v.runG = true; }
    v.attOn = false;
    if (wa > 0 && inst->additive) { v.ap.start(*inst->additive, startSample, rng); v.runA = true; }
    if (wp > 0 && inst->spectral)
    {
        int idx = juce::jlimit(0, kNumFft - 1, (int) std::lround(std::log2((double) inst->spectral->N)) - 10);
        const double adv0 = 1.0 - p.freeze; // attack follows the sampler timeline (see handoff)
        v.pp.start(*inst->spectral, startSample, ffts[(size_t) idx].get(), fftInvScale[(size_t) idx], s_windows[idx].data(), adv0);
        v.stereoP = inst->spectralR != nullptr && inst->spectralR != inst->spectral;
        if (v.stereoP)
            v.ppR.start(*inst->spectralR, startSample, ffts[(size_t) idx].get(), fftInvScale[(size_t) idx], s_windows[idx].data(), adv0);
        v.runP = true;
    }
    v.tl = 0.0;
    if (v.runA || v.runP)
    {
        // Transient handoff: analysis windows smear the onset over ~their length, so the real
        // attack plays from the sample and crossfades (linearly: the engines start
        // phase-aligned with it) into the resynthesis. During the attack the engines follow
        // the sampler's timeline (no time-stretch, like a transient-preserving stretcher), so
        // both sides of the crossfade match; the stretch ramps in afterwards.
        int win = 0;
        if (v.runA) win = juce::jmax(win, inst->additive->window);
        if (v.runP) win = juce::jmax(win, inst->spectral->N);
        const double winSec = win / inst->sr;
        v.tl = 1.0;
        v.att.start(startSample);
        v.attOn = true;
        v.attPos = 0;
        v.attFade0 = (int) (0.08 * winSec * sr);
        v.attFade1 = juce::jmax(v.attFade0 + 32, (int) (0.30 * winSec * sr));
    }
}

void SynthEngine::releaseVoice(Voice& v)
{
    v.held = false;
    v.env.noteOff();
}

void SynthEngine::noteOn(int note, float velocity, const EngineParams& p)
{
    if (note < p.loKey || note > p.hiKey || inst == nullptr) return;
    sustained[(size_t) note] = false;
    if (p.mode == 0)
    {
        Voice* pick = nullptr;
        const int limit = juce::jlimit(1, kMaxVoices, p.voices);
        for (int i = 0; i < limit && pick == nullptr; ++i)
            if (!voices[(size_t) i]->active) pick = voices[(size_t) i].get();
        if (pick == nullptr)
        {
            // steal: oldest released voice first, otherwise oldest
            for (int pass = 0; pass < 2 && pick == nullptr; ++pass)
            {
                uint64_t best = UINT64_MAX;
                for (int i = 0; i < limit; ++i)
                {
                    auto& v = *voices[(size_t) i];
                    if (pass == 0 && v.held) continue;
                    if (v.age < best) { best = v.age; pick = &v; }
                }
            }
        }
        startVoice(*pick, note, velocity, p, -1, false);
    }
    else
    {
        heldStack.erase(std::remove(heldStack.begin(), heldStack.end(), note), heldStack.end());
        heldStack.push_back(note);
        Voice& v = *voices[0];
        if (p.mode == 2 && v.active && v.held)
        {
            v.target = note;
            v.note = note;
            v.glideStep = p.glideMs > 0 ? std::abs(v.target - v.pitch) / (p.glideMs * 0.001 * sr) : 0.0;
            if (p.glideMs <= 0) v.pitch = note;
        }
        else
            startVoice(v, note, velocity, p, v.active ? v.pitch : lastPitch, p.mode == 1);
    }
    lastPitch = note;
}

void SynthEngine::noteOff(int note, const EngineParams& p)
{
    if (note < 0 || note > 127) return;
    if (sustainDown) { sustained[(size_t) note] = true; return; }
    if (p.mode == 0)
    {
        for (auto& v : voices)
            if (v->active && v->held && v->note == note) releaseVoice(*v);
        return;
    }
    heldStack.erase(std::remove(heldStack.begin(), heldStack.end(), note), heldStack.end());
    Voice& v = *voices[0];
    if (v.active && v.held && v.note == note)
    {
        if (!heldStack.empty())
        {
            v.target = heldStack.back();
            v.note = heldStack.back();
            v.glideStep = p.glideMs > 0 ? std::abs(v.target - v.pitch) / (p.glideMs * 0.001 * sr) : 0.0;
            if (p.glideMs <= 0) v.pitch = v.target;
        }
        else releaseVoice(v);
    }
}

void SynthEngine::setSustain(bool down, const EngineParams& p)
{
    sustainDown = down;
    if (!down)
        for (int n = 0; n < 128; ++n)
            if (sustained[(size_t) n]) { sustained[(size_t) n] = false; noteOff(n, p); }
}

void SynthEngine::allNotesOff(bool hard)
{
    for (auto& v : voices)
    {
        if (hard) { v->active = false; v->env.reset(); }
        else if (v->active) releaseVoice(*v);
    }
    heldStack.clear();
    sustained.fill(false);
    publishActive();
}

void SynthEngine::publishActive()
{
    uint64_t lo = 0, hi = 0;
    for (auto& v : voices)
        if (v->active && v->note >= 0)
        {
            if (v->note < 64) lo |= (1ull << v->note);
            else hi |= (1ull << (v->note - 64));
        }
    activeLo.store(lo);
    activeHi.store(hi);
}

// one value per kSub block of this render call (shapes as the DSPEngine filter LFO)
void SynthEngine::advanceLfos(int num, const EngineParams& p)
{
    const int blocks = (num + kSub - 1) / kSub;
    for (int k = 0; k < 2; ++k)
    {
        auto& buf = lfoBuf[k];
        if ((int) buf.size() < blocks) continue; // prepare() sizes it for the host block
        const double inc = (double) p.lfoHz[k] / sr;
        for (int b = 0; b < blocks; ++b)
        {
            const double ph = lfoPhase[k];
            float v = 0.0f;
            switch (p.lfoWave[k])
            {
                case 0: v = std::sin((float) (kTwoPi * ph)); break;
                case 1: v = ph < 0.5 ? (float) (4.0 * ph - 1.0) : (float) (3.0 - 4.0 * ph); break;
                case 2: v = (float) (2.0 * ph - 1.0); break;
                case 3: v = ph < 0.5 ? 1.0f : -1.0f; break;
                default: v = lfoHold[k]; break;
            }
            buf[(size_t) b] = v;
            const int n = juce::jmin(kSub, num - b * kSub);
            lfoPhase[k] += inc * n;
            if (lfoPhase[k] >= 1.0)
            {
                lfoPhase[k] -= std::floor(lfoPhase[k]);
                lfoHold[k] = rng.nextFloat() * 2.0f - 1.0f; // S&H: a new step every cycle
            }
        }
        if (blocks > 0) lfoViz[(size_t) k].store(buf[0]);
    }
}

void SynthEngine::render(juce::AudioBuffer<float>& out, int start, int num, const EngineParams& p)
{
    if (num > 0) advanceLfos(num, p);
    if (inst == nullptr) { publishActive(); return; }
    // matrix: which destinations are routed (the param-copy path only runs when needed)
    bool anyRoute = false, copyDst = false, panRouted = false;
    for (auto& r : p.routes)
        if (r.src != MS_NONE && r.dst != MD_NONE && r.amt != 0.0f)
        {
            anyRoute = true;
            if (r.dst >= MD_GPOS) copyDst = true;
            if (r.dst == MD_PAN) panRouted = true;
        }
    EngineParams q;
    if (copyDst) q = p;
    float* outL = out.getWritePointer(0, start);
    float* outR = out.getWritePointer(juce::jmin(1, out.getNumChannels() - 1), start);
    const float curveExp = std::pow(2.0f, (p.curve - 0.5f) * 3.0f);
    const auto envP = envParams(p);
    uint64_t newestGranular = 0;
    Voice* vizVoice = nullptr;

    for (auto& vp : voices)
    {
        Voice& v = *vp;
        if (!v.active || v.src == nullptr) continue;
        // a voice plays the instrument it started on (a rebuild may have swapped in a new one)
        const Instrument& in = *v.src;
        const double srcPerOut = in.sr / sr;
        const float rootHz = (v.src == inst.get() || p.rootOverride) ? p.rootHz : rootHzOf(in);
        v.env.setParameters(envP);
        const float velGain = v.vel * v.vel;
        const float panA = (v.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        float gL = std::cos(panA) * juce::MathConstants<float>::sqrt2;
        float gR = std::sin(panA) * juce::MathConstants<float>::sqrt2;

        for (int off = 0; off < num && v.active; off += kSub)
        {
            const int n = juce::jmin(kSub, num - off);
            // mod matrix for this voice + kSub block
            float md[MD_COUNT] {};
            const EngineParams* pv = &p;
            if (anyRoute)
            {
                const int bi = juce::jmin(off / kSub, (int) lfoBuf[0].size() - 1);
                for (auto& r : p.routes)
                {
                    if (r.src == MS_NONE || r.dst <= MD_NONE || r.dst >= MD_COUNT || r.amt == 0.0f) continue;
                    float sv = 0.0f;
                    switch (r.src)
                    {
                        case MS_LFO1: sv = lfoBuf[0][(size_t) bi]; break;
                        case MS_LFO2: sv = lfoBuf[1][(size_t) bi]; break;
                        case MS_WHEEL: sv = modWheel; break;
                        case MS_AT: sv = aftertouch; break;
                        case MS_VEL: sv = v.vel; break;
                        case MS_KEY: sv = juce::jlimit(-1.0f, 1.0f, (float) (v.note - 60) / 48.0f); break;
                        case MS_RAND: sv = v.modRand; break;
                        default: break;
                    }
                    md[r.dst] += sv * r.amt;
                }
                if (panRouted)
                {
                    const float pa = (juce::jlimit(-1.0f, 1.0f, v.pan + md[MD_PAN]) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    gL = std::cos(pa) * juce::MathConstants<float>::sqrt2;
                    gR = std::sin(pa) * juce::MathConstants<float>::sqrt2;
                }
                if (copyDst)
                {
                    q.gPosMod = p.gPosMod + md[MD_GPOS];
                    q.gSizeMs = juce::jlimit(5.0f, 1000.0f, p.gSizeMs * std::pow(4.0f, md[MD_GSIZE]));
                    q.gDensity = juce::jlimit(0.5f, 400.0f, p.gDensity * std::pow(4.0f, md[MD_GDENS]));
                    q.gSprayMs = juce::jlimit(0.0f, 2000.0f, p.gSprayMs + 200.0f * md[MD_GSPRAY]);
                    q.shift = juce::jlimit(-24.0f, 24.0f, p.shift + 12.0f * md[MD_SHIFT]);
                    q.blur = juce::jlimit(0.0f, 1.0f, p.blur + md[MD_BLUR]);
                    q.freeze = juce::jlimit(0.0f, 1.0f, p.freeze + md[MD_FREEZE]);
                    q.tilt = juce::jlimit(-24.0f, 24.0f, p.tilt + 12.0f * md[MD_TILT]);
                    pv = &q;
                }
            }
            if (v.pitch != v.target)
            {
                double step = v.glideStep * n;
                if (step <= 0 || std::abs(v.target - v.pitch) <= step) v.pitch = v.target;
                else v.pitch += v.target > v.pitch ? step : -step;
            }
            const double pn = v.pitch + bend * p.bendRange + v.detune / 100.0 + 12.0 * md[MD_PITCH];
            const float hz = Blend::noteHz(p, pn);
            const double ratio = hz / juce::jmax(1.0f, rootHz);
            // a voice keeps the mode it started in: blend voices follow the mixer, single-engine
            // voices play at unity (switching modes never changes the level of ringing notes)
            const float ws = v.runS ? (v.blendMode ? dbToGain(p.sLevel) : 1.0f) : 0.0f;
            const float wa = v.runA ? (v.blendMode ? dbToGain(p.aLevel) : 1.0f) : 0.0f;
            const float wp = v.runP ? (v.blendMode ? dbToGain(p.pLevel) : 1.0f) : 0.0f;
            const bool loopNow = loopActive(p.loopMode, v.held) && in.loopEnd - in.loopStart > 16;
            // keep length: 1 (stretch) ... but during the attack follow the sampler (ratio)
            const double timeRate = p.keepLen ? 1.0 + (ratio - 1.0) * v.tl : ratio;

            float L[kSub] {}, R[kSub] {}, tL[kSub], tR[kSub], M[kSub];
            if (v.runS)
            {
                double inc = ratio * std::pow(2.0, p.sTune / 1200.0) * srcPerOut;
                v.sp.render(in, tL, tR, n, inc, p.interp, p.loopMode, v.held);
                for (int i = 0; i < n; ++i) { L[i] += ws * tL[i]; R[i] += ws * tR[i]; }
            }
            if (v.runG)
            {
                const float wg = v.blendMode ? dbToGain(p.gLevel) : 1.0f;
                float gL[kSub], gR[kSub];
                v.gp.render(in, gL, gR, n, ratio * srcPerOut, srcPerOut, sr, *pv, loopNow);
                for (int i = 0; i < n; ++i) { L[i] += wg * gL[i]; R[i] += wg * gR[i]; }
                if (v.age >= newestGranular) { newestGranular = v.age; vizVoice = &v; }
            }
            float RL[kSub] {}, RR[kSub] {};   // spectral layer
            float ADL[kSub] {}, ADR[kSub] {}; // additive layer (sampler-attack handoff applies here)
            if (v.runA)
            {
                const auto& ad = *in.additive;
                v.ap.render(ad, M, n, ratio, sr, timeRate * srcPerOut / ad.hop, *pv, loopNow,
                            (double) in.loopStart / ad.hop, (double) in.loopEnd / ad.hop);
                for (int i = 0; i < n; ++i) { ADL[i] = wa * M[i]; ADR[i] = wa * M[i]; }
            }
            if (v.runP)
            {
                const auto& sd = *in.spectral;
                SpectralCtx c;
                c.ratio = ratio;
                c.srcPerOut = srcPerOut;
                // Freeze slows the analysis read-out down to a standstill, but only after the
                // attack (tl fades out), so a frozen note holds its tone rather than the onset
                c.timeRate = timeRate * (1.0 - pv->freeze * (1.0 - v.tl));
                c.shiftRatio = std::pow(2.0f, pv->shift / 12.0f);
                // Smear: one-pole smoothing of the spectrum with a time constant of 0..2 s,
                // independent of the FFT size (frames per second differ per sample / quality)
                {
                    const double tau = 2.0 * (double) pv->blur * pv->blur;
                    c.blur = tau > 1.0e-4 ? (float) std::exp(-((double) sd.hop / sd.sr) / tau) : 0.0f;
                }
                c.formant = p.formant;
                c.loopNow = loopNow;
                c.lsF = (double) in.loopStart / sd.hop;
                c.leF = (double) in.loopEnd / sd.hop;
                float MR[kSub];
                v.pp.render(sd, M, n, c);
                if (v.stereoP) v.ppR.render(*in.spectralR, MR, n, c);
                else std::copy(M, M + n, MR);
                for (int i = 0; i < n; ++i) { RL[i] += wp * M[i]; RR[i] += wp * MR[i]; }
            }
            if (v.runA || v.runP)
            {
                if (v.attOn)
                {
                    float aL[kSub], aR[kSub];
                    v.att.render(in, aL, aR, n, ratio * srcPerOut, p.interp, 0, true);
                    const float gA = v.runA ? wa : 0.0f, gP = v.runP ? wp : 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        const int t = v.attPos + i;
                        float w = 1.0f;
                        if (t >= v.attFade1) w = 0.0f;
                        else if (t > v.attFade0)
                            w = 0.5f + 0.5f * std::cos(juce::MathConstants<float>::pi * (float) (t - v.attFade0) / (float) (v.attFade1 - v.attFade0));
                        ADL[i] = w * gA * aL[i] + (1.0f - w) * ADL[i];
                        ADR[i] = w * gA * aR[i] + (1.0f - w) * ADR[i];
                        RL[i] = w * gP * aL[i] + (1.0f - w) * RL[i];
                        RR[i] = w * gP * aR[i] + (1.0f - w) * RR[i];
                    }
                    v.attPos += n;
                    if (v.attPos >= v.attFade1) v.attOn = false;
                }
                else if (v.tl > 0.0)
                    v.tl = juce::jmax(0.0, v.tl - (double) n / (0.06 * sr)); // stretch ramps in over 60 ms
                for (int i = 0; i < n; ++i) { L[i] += RL[i] + ADL[i]; R[i] += RR[i] + ADR[i]; }
            }

            if (p.fOn)
            {
                float fc = p.cutoff * std::pow(2.0f, p.keytrack * (float) (v.pitch - 60.0) / 12.0f + 5.0f * md[MD_CUTOFF]);
                fc = juce::jlimit(20.0f, (float) (sr * 0.45), fc);
                const float g = juce::jmin(8.0f, std::tan(juce::MathConstants<float>::pi * fc / (float) sr));
                // Resonance: Q = 0.5 .. 25, linear so the peak stays sharp (knob centre = Q 6.6).
                // Only the last stage resonates in 24 dB mode (the first is Butterworth). Level is
                // held by a Q-scaled make-down plus a soft limiter that only acts above unity.
                const float r = juce::jlimit(0.0f, 1.0f, p.reso + md[MD_RESO]);
                const float Q = 0.5f + r * r * 24.5f;
                const float kRes = 1.0f / Q, kFlat = 1.41421356f;
                const float qComp = Q > 0.7071f ? std::pow(Q / 0.7071f, -0.4f) : 1.0f;
                auto coeffs = [g](float k, float& a1, float& a2, float& a3)
                {
                    a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
                };
                float r1, r2, r3, f1, f2, f3;
                coeffs(kRes, r1, r2, r3);
                coeffs(kFlat, f1, f2, f3);
                const float pre = 1.0f + 9.0f * p.drive;
                const float post = 1.0f / std::sqrt(pre);
                const bool twoStage = p.slope == 1;
                // transparent below 0.7, rounds peaks towards 1.0 above it
                auto softLimit = [](float x)
                {
                    const float ax = std::abs(x);
                    if (ax <= 0.7f) return x;
                    const float over = (ax - 0.7f) / 0.3f;
                    return std::copysign(0.7f + 0.3f * over / (1.0f + over), x);
                };
                for (int i = 0; i < n; ++i)
                {
                    float xl = L[i], xr = R[i];
                    if (p.drive > 0) { xl = std::tanh(xl * pre) * post; xr = std::tanh(xr * pre) * post; }
                    if (twoStage)
                    {
                        xl = v.filt[0][0].process(xl, f1, f2, f3, kFlat, p.ftype);
                        xr = v.filt[1][0].process(xr, f1, f2, f3, kFlat, p.ftype);
                    }
                    xl = v.filt[0][1].process(xl, r1, r2, r3, kRes, p.ftype);
                    xr = v.filt[1][1].process(xr, r1, r2, r3, kRes, p.ftype);
                    xl = softLimit(xl * qComp);
                    xr = softLimit(xr * qComp);
                    L[i] = xl; R[i] = xr;
                }
            }

            for (int i = 0; i < n; ++i)
            {
                float e = v.env.getNextSample();
                if (curveExp != 1.0f && e > 0.0f) e = std::pow(e, curveExp);
                const float g = e * velGain * v.gain * juce::jlimit(0.0f, 2.0f, 1.0f + md[MD_LEVEL]);
                outL[off + i] += L[i] * g * gL;
                outR[off + i] += R[i] * g * gR;
            }

            // granular sustains (the playhead holds at the end) until the envelope releases
            const bool allDone = !v.runG && (!v.runS || v.sp.done) && (!v.runA || v.ap.done) && (!v.runP || v.pp.done);
            if (!v.env.isActive() || allDone) v.active = false;
        }
    }
    // grain positions of the newest granular voice, for the waveform display
    if (vizVoice != nullptr && vizVoice->active)
    {
        float tmp[64];
        const int c = vizVoice->gp.activePositions(tmp, 64, (double) juce::jmax(1, vizVoice->src->raw.getNumSamples()));
        for (int i = 0; i < c; ++i) grainViz[(size_t) i].store(tmp[i]);
        grainVizCount.store(c);
    }
    else grainVizCount.store(0);
    publishActive();
}
