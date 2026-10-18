// Headless checks for Pastel Sampler: detection accuracy, per-engine output pitch,
// key blend, loop / keep-length behaviour, state round trip and CPU cost.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdlib>
#include <iostream>

// setenv is POSIX only; MSVC has _putenv_s
static void setEnvVar(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static int passed = 0, failed = 0;
#define CHECK(cond, msg)                                                              \
    do {                                                                              \
        if (cond) { ++passed; std::cout << "  ok   " << msg << "\n"; }                \
        else { ++failed; std::cout << "  FAIL " << msg << "  (" #cond ")\n"; }         \
    } while (0)

static float centsBetween(float a, float b) { return 1200.0f * std::log2(a / b); }

// harmonic tone with 1/k partials and an exponential decay
static juce::AudioBuffer<float> makeTone(double sr, float f0, double secs, float tau = 1.5f, int harmonics = 20, float noise = 0.0f)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b(2, n);
    juce::Random rng(1234);
    for (int i = 0; i < n; ++i)
    {
        double t = i / sr;
        double env = juce::jmin(1.0, t / 0.005) * std::exp(-t / tau);
        double v = 0;
        for (int k = 1; k <= harmonics && k * f0 < sr * 0.45; ++k)
            v += std::sin(juce::MathConstants<double>::twoPi * k * f0 * t) / k;
        v = v * 0.3 * env + noise * (rng.nextFloat() * 2 - 1);
        b.setSample(0, i, (float) v);
        b.setSample(1, i, (float) v);
    }
    return b;
}

static juce::File writeFile(const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& b, double sr,
                            juce::AudioFormat& fmt, int bits = 24)
{
    auto f = dir.getChildFile(name);
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
    juce::StringPairArray meta;
    int quality = 0;
    if (fmt.getQualityOptions().size() > 0) quality = fmt.getQualityOptions().size() - 1;
    std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), sr, 2, bits, meta, quality));
    if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    return f;
}

static void setParam(PastelProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

// engine loop mode 0 off / 1 forward / 2 ping-pong / 3 sustain via the LOOP toggle + type
static void setLoop(PastelProcessor& p, float mode)
{
    const int m = (int) std::lround(mode);
    setParam(p, PP::LOOP_ON, m > 0 ? 1.0f : 0.0f);
    if (m > 0) setParam(p, PP::LOOP_TYPE, (float) (m - 1));
}

// play a note for `holdSecs`, return rendered stereo audio (holdSecs + tail)
static juce::AudioBuffer<float> playNote(PastelProcessor& p, int note, double sr, double holdSecs, double tailSecs = 0.2,
                                         float vel = 1.0f, const std::vector<int>& extra = {})
{
    const int block = 256;
    const int total = (int) ((holdSecs + tailSecs) * sr);
    const int offAt = (int) (holdSecs * sr);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    juce::AudioBuffer<float> buf(2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin(block, total - pos);
        buf.setSize(2, n, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
            for (int e : extra) midi.addEvent(juce::MidiMessage::noteOn(1, e, vel), 0);
        }
        if (offAt >= pos && offAt < pos + n)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, note), offAt - pos);
            for (int e : extra) midi.addEvent(juce::MidiMessage::noteOff(1, e), offAt - pos);
        }
        p.renderOffline(buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    return out;
}

static float measurePitch(const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    auto mono = Analysis::mixToMono(b, (int) (t0 * sr), (int) (t1 * sr));
    auto r = Analysis::detectPitch(mono.data(), (int) mono.size(), sr, Analysis::Algo::Yin);
    return r.f0;
}

static float rms(const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    int s0 = juce::jlimit(0, b.getNumSamples(), (int) (t0 * sr)), s1 = juce::jlimit(s0, b.getNumSamples(), (int) (t1 * sr));
    if (s1 <= s0) return 0;
    return b.getRMSLevel(0, s0, s1 - s0);
}


// fraction of energy on the harmonics of f0 (dB of harmonic / non-harmonic)
static float harmonicToNoiseDb(const juce::AudioBuffer<float>& b, double sr, double t0, double t1, float f0)
{
    auto mono = Analysis::mixToMono(b, (int) (t0 * sr), (int) (t1 * sr));
    const int order = 15, N = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> d((size_t) N * 2, 0.0f);
    const int n = juce::jmin(N, (int) mono.size());
    for (int i = 0; i < n; ++i)
        d[(size_t) i] = mono[(size_t) i] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / n));
    fft.performFrequencyOnlyForwardTransform(d.data(), true);
    const double binHz = sr / N;
    double on = 0, off = 0;
    for (int k = (int) (f0 * 0.5 / binHz); k < (int) (juce::jmin(16000.0, sr * 0.45) / binHz); ++k)
    {
        double e = (double) d[(size_t) k] * d[(size_t) k];
        double h = k * binHz / f0;
        if (std::abs(h - std::round(h)) * f0 < juce::jmax(4.0 * binHz, 0.01 * f0 * std::round(h))) on += e; else off += e;
    }
    return (float) (10.0 * std::log10((on + 1e-20) / (off + 1e-20)));
}

static float correlation(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i)
    {
        double x = a.getSample(0, i), y = b.getSample(0, i);
        xy += x * y; xx += x * x; yy += y * y;
    }
    return (float) (xy / std::sqrt(xx * yy + 1e-30));
}

static bool finite(const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(c, i))) return false;
    return true;
}

static void onlyEngine(PastelProcessor& p, int which) // 0 sampler, 1 additive, 2 spectral
{
    setParam(p, PP::ENGINE, (float) which);
}


static void writeWav(const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return;
    os.release();
    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

// waveform-shape similarity: mean over 40 ms windows of the max normalised cross-correlation
// (lag within +-maxLag) between x and a reference
static float shapeSimilarity(const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& x, double sr, double t0, double t1, int maxLag)
{
    const int W = (int) (0.04 * sr);
    double sum = 0; int cnt = 0;
    for (int s = (int) (t0 * sr); s + W + maxLag < (int) (t1 * sr) && s + W + maxLag < x.getNumSamples() && s + W < ref.getNumSamples(); s += W)
    {
        double best = -1;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            if (s + lag < 0) continue;
            double xy = 0, xx = 0, yy = 0;
            for (int i = 0; i < W; ++i)
            {
                double a = ref.getSample(0, s + i) + ref.getSample(1, s + i), b = x.getSample(0, s + lag + i) + x.getSample(1, s + lag + i);
                xy += a * b; xx += a * a; yy += b * b;
            }
            best = juce::jmax(best, xy / std::sqrt(xx * yy + 1e-30));
        }
        sum += best; ++cnt;
    }
    return cnt ? (float) (sum / cnt) : 0.0f;
}

// amplitude flutter: std-dev (dB) of the 5 ms envelope around its 60 ms smoothed trend
static float flutterDb(const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    const int h = (int) (0.005 * sr);
    std::vector<float> env;
    for (int s = (int) (t0 * sr); s + h < (int) (t1 * sr) && s + h < b.getNumSamples(); s += h)
        env.push_back(juce::Decibels::gainToDecibels(b.getRMSLevel(0, s, h) + b.getRMSLevel(1, s, h) + 1e-9f));
    if (env.size() < 30) return 0;
    double acc = 0; int n = 0;
    for (size_t i = 6; i + 6 < env.size(); ++i)
    {
        double m = 0;
        for (size_t j = i - 6; j <= i + 6; ++j) m += env[j];
        m /= 13.0;
        acc += (env[i] - m) * (env[i] - m); ++n;
    }
    return (float) std::sqrt(acc / juce::jmax(1, n));
}

// Wobble: pitch deviation (cents RMS around a 400 ms moving average, YIN per 20 ms) and slow
// level modulation (dB RMS of the 20 ms envelope around a 400 ms trend)
struct Wobble { float pitchCents = 0, levelDb = 0, hiDb = 0; };
static Wobble wobble(const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    auto mono = Analysis::mixToMono(b, (int) (t0 * sr), (int) (t1 * sr));
    const int hop = (int) (0.02 * sr), win = 4096;
    std::vector<float> cents, lvl;
    for (int s = 0; s + win < (int) mono.size(); s += hop)
    {
        auto r = Analysis::detectPitch(mono.data() + s, win, sr, Analysis::Algo::Yin);
        cents.push_back(r.f0 > 0 ? 1200.0f * std::log2(r.f0 / 440.0f) : 0.0f);
        double e = 0;
        for (int i = s; i < s + hop; ++i) e += (double) mono[(size_t) i] * mono[(size_t) i];
        lvl.push_back((float) (10.0 * std::log10(e / hop + 1e-20)));
    }
    // reject octave / tracking errors: keep frames within 150 ct of the median pitch
    {
        std::vector<float> sorted(cents);
        std::sort(sorted.begin(), sorted.end());
        const float med = sorted.empty() ? 0.0f : sorted[sorted.size() / 2];
        for (auto& c : cents) if (std::abs(c - med) > 150.0f) c = med;
    }
    auto dev = [](const std::vector<float>& v)
    {
        const int k = 10; // +-200 ms
        double acc = 0; int n = 0;
        for (int i = k; i + k < (int) v.size(); ++i)
        {
            double m = 0;
            for (int j = i - k; j <= i + k; ++j) m += v[(size_t) j];
            m /= (2 * k + 1);
            acc += (v[(size_t) i] - m) * (v[(size_t) i] - m); ++n;
        }
        return n ? (float) std::sqrt(acc / n) : 0.0f;
    };
    // upper band (> ~1.5 kHz, simple 2nd-order high-pass) level modulation: where garble lives
    std::vector<float> hi;
    {
        const double fc = 1500.0, q = 0.707, w0 = juce::MathConstants<double>::twoPi * fc / sr, al = std::sin(w0) / (2 * q);
        const double b0 = (1 + std::cos(w0)) / 2, b1 = -(1 + std::cos(w0)), b2 = b0, a0 = 1 + al, a1 = -2 * std::cos(w0), a2 = 1 - al;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        std::vector<float> y(mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
        {
            double x = mono[i], o = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x; y2 = y1; y1 = o; y[i] = (float) o;
        }
        for (int s0 = 0; s0 + hop < (int) y.size(); s0 += hop)
        {
            double e = 0;
            for (int i = s0; i < s0 + hop; ++i) e += (double) y[(size_t) i] * y[(size_t) i];
            hi.push_back((float) (10.0 * std::log10(e / hop + 1e-20)));
        }
    }
    if (std::getenv("PASTEL_EVAL_TRACE"))
        for (size_t i = 0; i < hi.size(); ++i) std::cout << "TRACE " << i * 20 << "ms hi " << hi[i] << "\n";
    return { dev(cents), dev(lvl), dev(hi) };
}

// Dev tool: PASTEL_EVAL_FILES="a.wav;b.wav" PASTEL_EVAL_OUT=/dir renders real material through
// the sampler and spectral engines (unity, +7, +12, -12 st) and reports reconstruction quality.
static void evalRealFiles(const juce::String& files, const juce::File& outDir)
{
    const double sr = 48000.0;
    PastelProcessor p;
    p.setAsyncLoads(false);
    p.prepareToPlay(sr, 256);
    outDir.createDirectory();
    for (auto& path : juce::StringArray::fromTokens(files, ";", ""))
    {
        if (!p.loadSampleSync(path)) { std::cout << "EVAL cannot load " << path << "\n"; continue; }
        auto inst = p.getInstrument();
        const int root = inst->pitch.pitched() ? inst->pitch.nearestNote() : 60;
        const double len = juce::jmin(4.0, (inst->end - inst->start) / inst->sr);
        // unity: root override = nearest note +0 ct, so the root key plays the file untransposed
        setParam(p, PP::ROOTOVR, 1); setParam(p, PP::ROOTNOTE, (float) root); setParam(p, PP::ROOTFINE, 0);
        auto stem = juce::File(path).getFileNameWithoutExtension().replaceCharacter(' ', '_');
        for (int st : { 0, 7, 12, -12 })
        {
            setParam(p, PP::ENGINE, 0);
            auto a = playNote(p, root + st, sr, len, 0.3);
            setParam(p, PP::ENGINE, 2);
            auto b = playNote(p, root + st, sr, len, 0.3);
            writeWav(outDir.getChildFile(stem + "_samp_" + juce::String(st) + ".wav"), a, sr);
            writeWav(outDir.getChildFile(stem + "_spec_" + juce::String(st) + ".wav"), b, sr);
            setParam(p, PP::ENGINE, 1);
            writeWav(outDir.getChildFile(stem + "_add_" + juce::String(st) + ".wav"), playNote(p, root + st, sr, len, 0.3), sr);
            setParam(p, PP::ENGINE, (float) PP::kGranular);
            auto gr = playNote(p, root + st, sr, len, 0.3);
            writeWav(outDir.getChildFile(stem + "_gran_" + juce::String(st) + ".wav"), gr, sr);
            {
                const double t1 = juce::jmin(len, 1.0);
                float fg = measurePitch(gr, sr, 0.1, t1), fs = measurePitch(a, sr, 0.1, juce::jmin(t1, len / std::pow(2.0, st / 12.0)));
                std::cout << "GRAN " << stem << " " << (st >= 0 ? "+" : "") << st << "  pitch " << juce::String(fg, 2) << " Hz (sampler "
                          << juce::String(fs, 2) << ")  level vs sampler " << juce::String(juce::Decibels::gainToDecibels(rms(gr, sr, 0.05, 0.3) / (rms(a, sr, 0.05, 0.3) + 1e-9f)), 1)
                          << " dB  finite " << (int) finite(gr) << "\n";
            }
            if (st == 0)
            {
                double err = 0, ref = 0;
                double side = 0;
                for (int i = 0; i < a.getNumSamples(); ++i)
                {
                    double am = 0.5 * (a.getSample(0, i) + a.getSample(1, i)), bm = 0.5 * (b.getSample(0, i) + b.getSample(1, i));
                    double as = 0.5 * (a.getSample(0, i) - a.getSample(1, i));
                    err += (bm - am) * (bm - am); ref += am * am; side += as * as;
                }
                double eL = 0, rL = 0, eR = 0, rR = 0;
                for (int i = 0; i < a.getNumSamples(); ++i)
                {
                    double dl = b.getSample(0, i) - a.getSample(0, i), dr = b.getSample(1, i) - a.getSample(1, i);
                    eL += dl * dl; rL += (double) a.getSample(0, i) * a.getSample(0, i);
                    eR += dr * dr; rR += (double) a.getSample(1, i) * a.getSample(1, i);
                }
                std::cout << "EVAL " << stem << " L/R unity SNR " << juce::String(10.0 * std::log10(rL / (eL + 1e-20)), 1) << " / "
                          << juce::String(10.0 * std::log10(rR / (eR + 1e-20)), 1) << " dB\n";
                std::cout << "EVAL " << stem << " side/mid " << juce::String(10.0 * std::log10((side + 1e-20) / (ref + 1e-20)), 1) << " dB\n";
                std::cout << "EVAL " << stem << " root " << PP::noteName(root) << " conf " << inst->pitch.confidence
                          << "  unity SNR " << juce::String(10.0 * std::log10(ref / (err + 1e-20)), 1) << " dB"
                          << "  corr " << juce::String(correlation(a, b, sr, 0.0, len), 3)
                          << "  level " << juce::String(10.0 * std::log10(b.getRMSLevel(0, 0, b.getNumSamples()) * b.getRMSLevel(0, 0, b.getNumSamples()) / (ref / a.getNumSamples() + 1e-20)), 1) << " dB"
                          << "  N " << inst->spectral->N << "\n";
            }
        }
        if (const char* tr = std::getenv("PASTEL_EVAL_TRACE"))
        {
            // PASTEL_EVAL_TRACE="semitones,loop,engine"
            auto parts = juce::StringArray::fromTokens(tr, ",", "");
            setLoop(p, parts[1].getFloatValue());
            p.rebuildSync(false);
            setParam(p, PP::ENGINE, parts[2].getFloatValue());
            auto o = playNote(p, root + parts[0].getIntValue(), sr, juce::jmin(3.0, len), 0.0);
            writeWav(outDir.getChildFile(stem + "_trace.wav"), o, sr);
            wobble(o, sr, 0.1, juce::jmin(3.0, len));
            return;
        }
        if (std::getenv("PASTEL_EVAL_BLEND"))
        {
            // Blend beating: sampler alone vs sampler + spectral, and sampler + additive (equal levels)
            const double wl = juce::jmin(2.5, len);
            setLoop(p, 1);
            p.rebuildSync(false);
            for (int st : { 3, 7, 12, -5 })
            {
                setParam(p, PP::ENGINE, 0); auto s0 = playNote(p, root + st, sr, wl, 0.0);
                setParam(p, PP::ENGINE, 3); setParam(p, PP::S_LEVEL, 0); setParam(p, PP::A_LEVEL, -48); setParam(p, PP::P_LEVEL, 0);
                auto sp = playNote(p, root + st, sr, wl, 0.0);
                setParam(p, PP::A_LEVEL, 0); setParam(p, PP::P_LEVEL, -48);
                auto sa = playNote(p, root + st, sr, wl, 0.0);
                setParam(p, PP::P_LEVEL, 0);
                auto w0 = wobble(s0, sr, 0.1, wl), wsp = wobble(sp, sr, 0.1, wl), wsa = wobble(sa, sr, 0.1, wl);
                std::cout << "BLEND " << stem << " " << (st > 0 ? "+" : "") << st << "  level wobble dB: sampler " << juce::String(w0.levelDb, 2)
                          << "  samp+spec " << juce::String(wsp.levelDb, 2) << "  samp+add " << juce::String(wsa.levelDb, 2)
                          << "   upper: " << juce::String(w0.hiDb, 2) << " / " << juce::String(wsp.hiDb, 2) << " / " << juce::String(wsa.hiDb, 2) << "\n";
            }
            setParam(p, PP::ENGINE, 0);
            setLoop(p, 0);
            p.rebuildSync(false);
        }
        if (std::getenv("PASTEL_EVAL_WOBBLE"))
        {
            { const char* fm = std::getenv("PASTEL_EVAL_FORMANT"); setParam(p, PP::V_FORMANT, (fm != nullptr && *fm == '1') ? 1.0f : 0.0f); }
            const double wl = juce::jmin(3.0, len);
            for (int loop : { 0, 1 })
            {
                setLoop(p, (float) loop);
                p.rebuildSync(false);
                for (int st : { 1, 5, 7, 12, -7 })
                {
                    setParam(p, PP::ENGINE, 0); auto a2 = playNote(p, root + st, sr, wl, 0.0);
                    setParam(p, PP::ENGINE, 1); auto a3 = playNote(p, root + st, sr, wl, 0.0);
                    setParam(p, PP::ENGINE, 2); auto b2 = playNote(p, root + st, sr, wl, 0.0);
                    const double we = loop ? wl : juce::jmin(wl, 0.45 * len);
                    auto ws = wobble(a2, sr, 0.1, loop ? wl : juce::jmin(wl, len / std::pow(2.0, st / 12.0) * 0.9));
                    auto wa = wobble(a3, sr, 0.1, we), wp = wobble(b2, sr, 0.1, we);
                    std::cout << "WOB " << stem << (loop ? " loop" : "     ") << " " << (st > 0 ? "+" : "") << st
                              << "  pitch ct  samp " << juce::String(ws.pitchCents, 2) << " add " << juce::String(wa.pitchCents, 2) << " spec " << juce::String(wp.pitchCents, 2)
                              << "   level dB  samp " << juce::String(ws.levelDb, 2) << " add " << juce::String(wa.levelDb, 2) << " spec " << juce::String(wp.levelDb, 2)
                              << "   upper-band dB  samp " << juce::String(ws.hiDb, 2) << " add " << juce::String(wa.hiDb, 2) << " spec " << juce::String(wp.hiDb, 2) << "\n";
                }
            }
            setLoop(p, 0);
            p.rebuildSync(false);
        }
        // octave-up comparison (formant preservation off so every engine should match the sampler)
        setParam(p, PP::V_FORMANT, 0);
        const double cmpLen = juce::jmin(1.0, len * 0.45);
        setParam(p, PP::ENGINE, 0); auto so = playNote(p, root + 12, sr, cmpLen, 0.0);
        setParam(p, PP::ENGINE, 1); auto ao = playNote(p, root + 12, sr, cmpLen, 0.0);
        setParam(p, PP::ENGINE, 2); auto po = playNote(p, root + 12, sr, cmpLen, 0.0);
        const int period = (int) (sr / juce::jmax(20.0f, Analysis::midiToHz((float) root + 12)));
        {
            const float fT = Analysis::midiToHz((float) root + 12);
            std::cout << "HNR " << stem << " +12 harmonic/noise dB: sampler " << juce::String(harmonicToNoiseDb(so, sr, 0.05, cmpLen, fT), 1)
                      << "  additive " << juce::String(harmonicToNoiseDb(ao, sr, 0.05, cmpLen, fT), 1)
                      << "  spectral " << juce::String(harmonicToNoiseDb(po, sr, 0.05, cmpLen, fT), 1) << "  (N " << p.getInstrument()->spectral->N << ")\n";
        }
        std::cout << "OCT " << stem << "  shape vs sampler: additive " << juce::String(shapeSimilarity(so, ao, sr, 0.05, cmpLen, period), 3)
                  << "  spectral " << juce::String(shapeSimilarity(so, po, sr, 0.05, cmpLen, period), 3)
                  << "   flutter dB: sampler " << juce::String(flutterDb(so, sr, 0.05, cmpLen), 2)
                  << " additive " << juce::String(flutterDb(ao, sr, 0.05, cmpLen), 2)
                  << " spectral " << juce::String(flutterDb(po, sr, 0.05, cmpLen), 2) << "\n";
        writeWav(outDir.getChildFile(stem + "_spec_12_noformant.wav"), po, sr);
        setParam(p, PP::V_FORMANT, 1);
        writeWav(outDir.getChildFile(stem + "_spec_12_formant.wav"), playNote(p, root + 12, sr, cmpLen, 0.0), sr);
        setParam(p, PP::V_FORMANT, 0);
        setParam(p, PP::V_KEEPLEN, 0); // same timeline as the sampler: exact comparison
        setParam(p, PP::ENGINE, 0); auto so2 = playNote(p, root + 12, sr, cmpLen, 0.0);
        setParam(p, PP::ENGINE, 1); auto ao2 = playNote(p, root + 12, sr, cmpLen, 0.0);
        setParam(p, PP::ENGINE, 2); auto po2 = playNote(p, root + 12, sr, cmpLen, 0.0);
        writeWav(outDir.getChildFile(stem + "_spec_12_nokeeplen.wav"), po2, sr);
        std::cout << "OCT " << stem << "  (keep length off) additive " << juce::String(shapeSimilarity(so2, ao2, sr, 0.05, cmpLen, period), 3)
                  << "  spectral " << juce::String(shapeSimilarity(so2, po2, sr, 0.05, cmpLen, period), 3) << "\n";
        setParam(p, PP::V_KEEPLEN, 1);
        setParam(p, PP::V_FORMANT, 1);
        setParam(p, PP::ROOTOVR, 0);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    if (const char* ef = std::getenv("PASTEL_EVAL_FILES"); ef != nullptr && *ef != 0)
    {
        const char* od = std::getenv("PASTEL_EVAL_OUT");
        evalRealFiles(ef, juce::File(od != nullptr ? od : "/tmp/pastel_eval"));
        return 0;
    }
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("PastelSamplerHarness");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setEnvVar("PASTEL_SETTINGS_FOLDER", tmp.getChildFile("settings").getFullPathName().toRawUTF8());
    const double sr = 48000.0;

    std::cout << "== pitch detection on synthetic tones\n";
    for (float f : { 55.0f, 110.0f, 261.63f * std::pow(2.0f, 12.0f / 1200.0f), 440.0f, 987.77f, 2093.0f })
    {
        auto tone = makeTone(sr, f, 1.5);
        auto mono = Analysis::mixToMono(tone, 0, tone.getNumSamples());
        double a0, a1;
        Analysis::defaultAnalysisWindow(mono.data(), (int) mono.size(), sr, a0, a1);
        for (auto algo : { Analysis::Algo::Yin, Analysis::Algo::Hps, Analysis::Algo::YinHps })
        {
            auto r = Analysis::detectPitch(mono.data() + (int) a0, (int) (a1 - a0), sr, algo);
            float err = r.f0 > 0 ? std::abs(centsBetween(r.f0, f)) : 9999.0f;
            const char* an = algo == Analysis::Algo::Yin ? "YIN" : (algo == Analysis::Algo::Hps ? "HPS" : "YIN+HPS");
            CHECK(err < (algo == Analysis::Algo::Hps ? 10.0f : 3.0f) && r.confidence > 0.6f,
                  juce::String(f, 2) << " Hz " << an << " -> " << juce::String(r.f0, 2) << " Hz (" << juce::String(err, 2)
                                     << " ct, conf " << juce::String(r.confidence, 2) << ")");
        }
    }
    {
        // a noisy but pitched tone still resolves
        auto tone = makeTone(sr, 196.0f, 1.5, 1.5f, 20, 0.05f);
        auto mono = Analysis::mixToMono(tone, 0, tone.getNumSamples());
        auto r = Analysis::detectPitch(mono.data() + 4800, 48000, sr, Analysis::Algo::YinHps);
        CHECK(std::abs(centsBetween(r.f0, 196.0f)) < 5.0f, "noisy G3 detected (" << juce::String(r.f0, 2) << " Hz)");
        juce::AudioBuffer<float> noise(1, 48000);
        juce::Random rng(7);
        for (int i = 0; i < 48000; ++i) noise.setSample(0, i, rng.nextFloat() * 2 - 1);
        auto rn = Analysis::detectPitch(noise.getReadPointer(0), 48000, sr, Analysis::Algo::YinHps);
        CHECK(!rn.pitched() || rn.confidence < 0.5f, "white noise reported as unpitched / low confidence (" << juce::String(rn.confidence, 2) << ")");
    }

    std::cout << "== file formats\n";
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flac;
    juce::OggVorbisAudioFormat ogg;
    const float srcF = 261.63f * std::pow(2.0f, 12.0f / 1200.0f); // C4 +12 ct
    auto tone = makeTone(sr, srcF, 2.0, 1.2f);
    auto wavF = writeFile(tmp, "tone_c4.wav", tone, sr, wav);
    auto flacF = writeFile(tmp, "tone_c4.flac", tone, sr, flac);
    auto oggF = writeFile(tmp, "tone_c4.ogg", tone, sr, ogg, 16);
    auto sustain = makeTone(sr, 220.0f, 1.0, 1000.0f); // non-decaying, for loop tests
    auto susF = writeFile(tmp, "sustain_a3.wav", sustain, sr, wav);

    PastelProcessor proc;
    proc.setAsyncLoads(false);
    proc.prepareToPlay(sr, 256);
    for (auto& f : { wavF, flacF, oggF })
    {
        bool ok = f.existsAsFile() && proc.loadSampleSync(f.getFullPathName());
        auto inst = proc.getInstrument();
        float err = (ok && inst) ? std::abs(centsBetween(inst->pitch.f0, srcF)) : 9999.0f;
        CHECK(ok && err < 3.0f, f.getFileName() << " loads, root " << (inst ? PP::noteName(inst->pitch.nearestNote()) : "?")
                                                << " " << (inst ? juce::String(inst->pitch.cents(), 1) : "") << " ct");
    }

    std::cout << "== engine output pitch (root C4 +12 ct, played G4 -> expect 392.00 Hz)\n";
    proc.loadSampleSync(wavF.getFullPathName());
    const float g4 = 440.0f * std::pow(2.0f, -2.0f / 12.0f);
    const char* engNames[] { "sampler", "additive", "spectral" };
    for (int e = 0; e < 3; ++e)
    {
        onlyEngine(proc, e);
        auto out = playNote(proc, 67, sr, 0.8);
        float f = measurePitch(out, sr, 0.15, 0.7);
        float err = f > 0 ? std::abs(centsBetween(f, g4)) : 9999.0f;
        CHECK(finite(out) && rms(out, sr, 0.1, 0.7) > 1e-3f && err < (e == 2 ? 8.0f : 3.0f),
              engNames[e] << " -> " << juce::String(f, 2) << " Hz (" << juce::String(err, 2) << " ct), rms "
                          << juce::String(rms(out, sr, 0.1, 0.7), 4));
        auto far = playNote(proc, 84, sr, 0.5); // two octaves up
        float ff = measurePitch(far, sr, 0.15, 0.45);
        float expect = 440.0f * std::pow(2.0f, 15.0f / 12.0f);
        CHECK(std::abs(centsBetween(ff, expect)) < (e == 2 ? 10.0f : 3.0f),
              engNames[e] << " at C6 -> " << juce::String(ff, 2) << " Hz (expect " << juce::String(expect, 2) << ")");
    }

    std::cout << "== spectral quality\n";
    {
        // root override C4 +0: the C4 key plays the file untransposed (pitch ratio exactly 1)
        setParam(proc, PP::ROOTOVR, 1);
        setParam(proc, PP::ROOTNOTE, 60);
        setParam(proc, PP::ROOTFINE, 0.0f);
        onlyEngine(proc, 0);
        auto ref = playNote(proc, 60, sr, 1.2);
        onlyEngine(proc, 2);
        auto uni = playNote(proc, 60, sr, 1.2);
        onlyEngine(proc, 1);
        auto add = playNote(proc, 60, sr, 1.2);
        setParam(proc, PP::ROOTOVR, 0);
        setParam(proc, PP::ROOTFINE, 0);
        float corr = correlation(ref, uni, sr, 0.2, 1.0);

        float corrA = correlation(ref, add, sr, 0.03, 0.15);
        CHECK(corrA > 0.9f, "additive starts phase-aligned with the sample (correlation " << juce::String(corrA, 3) << ")");
        CHECK(corr > 0.98f, "spectral at the root matches the sampler (correlation " << juce::String(corr, 3) << ")");
        for (int st : { 7, 19, -12 })
        {
            onlyEngine(proc, 0);
            auto rs = playNote(proc, 60 + st, sr, 1.2);
            onlyEngine(proc, 2);
            auto sp = playNote(proc, 60 + st, sr, 1.2);
            float fT = 261.63f * std::pow(2.0f, st / 12.0f);
            float hS = harmonicToNoiseDb(rs, sr, 0.2, 1.0, fT), hP = harmonicToNoiseDb(sp, sr, 0.2, 1.0, fT);
            CHECK(hP > 25.0f, "spectral " << (st > 0 ? "+" : "") << st << " st harmonic/noise " << juce::String(hP, 1)
                  << " dB (sampler " << juce::String(hS, 1) << " dB)");
        }
    }

    std::cout << "== filter resonance stays bounded\n";
    {
        onlyEngine(proc, 0);
        setParam(proc, PP::F_ON, 0);
        auto dry = playNote(proc, 60, sr, 0.8);
        setParam(proc, PP::F_ON, 1);
        setParam(proc, PP::F_RES, 1.0f);
        for (int slope : { 0, 1 })
            for (int type : { 0, 1, 2 })
                for (float cut : { 263.0f, 526.0f, 2000.0f })
                {
                    setParam(proc, PP::F_SLOPE, (float) slope);
                    setParam(proc, PP::F_TYPE, (float) type);
                    setParam(proc, PP::F_CUT, cut);
                    auto wet = playNote(proc, 60, sr, 0.8);
                    float ratioDb = juce::Decibels::gainToDecibels(wet.getMagnitude(0, 0, wet.getNumSamples()) / dry.getMagnitude(0, 0, dry.getNumSamples()));
                    CHECK(finite(wet) && ratioDb < 6.0f, "max reso " << PP::filterTypeChoices[type] << " " << PP::slopeChoices[slope] << " @ "
                          << (int) cut << " Hz: peak " << juce::String(ratioDb, 1) << " dB vs dry");
                }
        // sharpness: with the cutoff on the 4th harmonic, max resonance must lift it well above
        // the 2nd harmonic compared with the unfiltered sound (quiet note: linear region)
        auto bandDb = [&](const juce::AudioBuffer<float>& b, float hz)
        {
            auto mono = Analysis::mixToMono(b, (int) (0.1 * sr), (int) (0.1 * sr) + 32768);
            juce::dsp::FFT fft(15);
            std::vector<float> d(65536, 0.0f);
            for (size_t i = 0; i < mono.size() && i < 32768; ++i)
                d[i] = mono[i] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / 32768.0f));
            fft.performFrequencyOnlyForwardTransform(d.data(), true);
            int c = (int) std::lround(hz / (sr / 32768.0));
            float m = 0;
            for (int k = c - 3; k <= c + 3; ++k) m = juce::jmax(m, d[(size_t) k]);
            return juce::Decibels::gainToDecibels(m, -200.0f);
        };
        setParam(proc, PP::F_SLOPE, 1); setParam(proc, PP::F_TYPE, 0);
        setParam(proc, PP::F_ON, 0);
        auto q0 = playNote(proc, 60, sr, 1.0, 0.0, 0.3f);
        const float c4 = 261.63f; // the played (in-tune) pitch
        setParam(proc, PP::F_ON, 1); setParam(proc, PP::F_CUT, c4 * 4.0f); setParam(proc, PP::F_RES, 1.0f);
        auto q1 = playNote(proc, 60, sr, 1.0, 0.0, 0.3f);
        float emph = (bandDb(q1, c4 * 4) - bandDb(q1, c4 * 2)) - (bandDb(q0, c4 * 4) - bandDb(q0, c4 * 2));
        CHECK(emph > 15.0f, "max resonance is sharp: +" << juce::String(emph, 1) << " dB emphasis at the cutoff");
        setParam(proc, PP::F_ON, 0);
        setParam(proc, PP::F_RES, 0.1f);
        setParam(proc, PP::F_TYPE, 0);
        setParam(proc, PP::F_SLOPE, 1);
        setParam(proc, PP::F_CUT, 8000.0f);
    }

    std::cout << "== level matching between engines\n";
    {
        float lv[3];
        for (int e = 0; e < 3; ++e)
        {
            onlyEngine(proc, e);
            lv[e] = rms(playNote(proc, 60, sr, 0.6), sr, 0.1, 0.5);
        }
        CHECK(std::abs(juce::Decibels::gainToDecibels(lv[1] / lv[0])) < 3.0f, "additive within 3 dB of sampler at root ("
              << juce::String(juce::Decibels::gainToDecibels(lv[1] / lv[0]), 2) << " dB)");
        CHECK(std::abs(juce::Decibels::gainToDecibels(lv[2] / lv[0])) < 3.0f, "spectral within 3 dB of sampler at root ("
              << juce::String(juce::Decibels::gainToDecibels(lv[2] / lv[0]), 2) << " dB)");
    }

    std::cout << "== root override\n";
    {
        onlyEngine(proc, 0);
        setParam(proc, PP::ROOTOVR, 1);
        setParam(proc, PP::ROOTNOTE, 60);
        setParam(proc, PP::ROOTFINE, 0);
        auto out = playNote(proc, 72, sr, 0.6);
        float f = measurePitch(out, sr, 0.1, 0.5);
        CHECK(std::abs(centsBetween(f, srcF * 2)) < 3.0f, "override C4: C5 plays the raw sample an octave up (" << juce::String(f, 2) << " Hz)");
        setParam(proc, PP::ROOTFINE, 12);
        out = playNote(proc, 72, sr, 0.6);
        f = measurePitch(out, sr, 0.1, 0.5);
        CHECK(std::abs(centsBetween(f, 523.25f)) < 3.0f, "override C4 +12 ct: C5 in tune (" << juce::String(f, 2) << " Hz)");
        setParam(proc, PP::ROOTOVR, 0);
        setParam(proc, PP::ROOTFINE, 0);
        proc.armRootLearn(true);
        playNote(proc, 55, sr, 0.05);
        CHECK(!proc.isRootLearnArmed(), "MIDI learn consumes the next note");
    }

    std::cout << "== engine selector (exactly one engine)\n";
    {
        auto p = proc.snapshotParams();
        for (int e = 0; e < PP::kNumEngines; ++e)
        {
            p.engine = e;
            float w[4];
            Blend::weights(p, w[0], w[1], w[2], w[3]);
            int nonZero = (w[0] > 0) + (w[1] > 0) + (w[2] > 0) + (w[3] > 0);
            CHECK(nonZero == 1 && w[e] > 0, PP::engineChoices[e] << " selected -> only it has gain");
        }
        // a note keeps its engine when the selector changes: switching to spectral mid-note
        // must not silence a ringing sampler note
        onlyEngine(proc, 0);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        proc.renderOffline(buf, on);
        onlyEngine(proc, 2);
        juce::MidiBuffer none;
        float level = 0;
        for (int b = 0; b < 40; ++b) { proc.renderOffline(buf, none); level = buf.getRMSLevel(0, 0, 256); }
        CHECK(level > 1e-3f, "held sampler note keeps sounding after switching engine (" << juce::String(level, 4) << ")");
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        proc.renderOffline(buf, off);
        proc.panic();
        proc.renderOffline(buf, none);
    }

    std::cout << "== blend mode (mixer)\n";
    {
        auto p = proc.snapshotParams();
        p.engine = PP::kBlend;
        p.sLevel = 0; p.aLevel = -6; p.pLevel = -48; p.gLevel = -48;
        float w[4];
        Blend::weights(p, w[0], w[1], w[2], w[3]);
        CHECK(w[0] == 1.0f && std::abs(w[1] - 0.501f) < 0.01f && w[2] == 0.0f && w[3] == 0.0f, "blend gains follow the mixer (-inf mutes)");
        setParam(proc, PP::ENGINE, (float) PP::kBlend);
        setParam(proc, PP::S_LEVEL, 0); setParam(proc, PP::A_LEVEL, -48); setParam(proc, PP::P_LEVEL, -48); setParam(proc, PP::G_LEVEL, -48);
        float sOnly = rms(playNote(proc, 67, sr, 0.6), sr, 0.1, 0.5);
        setParam(proc, PP::A_LEVEL, 0); setParam(proc, PP::P_LEVEL, 0);
        auto all = playNote(proc, 67, sr, 0.6);
        float f = measurePitch(all, sr, 0.15, 0.55);
        CHECK(rms(all, sr, 0.1, 0.5) > sOnly * 2.0f, "all three engines sound together in Blend (" << juce::String(rms(all, sr, 0.1, 0.5) / sOnly, 2) << "x the sampler alone)");
        CHECK(std::abs(centsBetween(f, g4)) < 3.0f, "blended output in tune (" << juce::String(f, 2) << " Hz)");
        setParam(proc, PP::A_LEVEL, 0);
        setParam(proc, PP::G_LEVEL, 0);
        setParam(proc, PP::ENGINE, 0);
    }

    std::cout << "== blend levels only apply in Blend\n";
    {
        onlyEngine(proc, 2);
        const float ref = rms(playNote(proc, 60, sr, 0.5), sr, 0.1, 0.45);
        setParam(proc, PP::P_LEVEL, -20.0f);              // mix setting made in the BLEND tab
        const float single = rms(playNote(proc, 60, sr, 0.5), sr, 0.1, 0.45);
        CHECK(std::abs(juce::Decibels::gainToDecibels(single / ref)) < 0.1f, "single engine ignores its Blend level ("
              << juce::String(juce::Decibels::gainToDecibels(single / ref), 2) << " dB)");
        setParam(proc, PP::ENGINE, (float) PP::kBlend);
        setParam(proc, PP::S_LEVEL, -48); setParam(proc, PP::A_LEVEL, -48); setParam(proc, PP::G_LEVEL, -48);
        const float blended = rms(playNote(proc, 60, sr, 0.5), sr, 0.1, 0.45);
        CHECK(std::abs(juce::Decibels::gainToDecibels(blended / ref) + 20.0f) < 0.5f, "Blend applies the level ("
              << juce::String(juce::Decibels::gainToDecibels(blended / ref), 1) << " dB)");
        for (auto id : { PP::S_LEVEL, PP::A_LEVEL, PP::P_LEVEL, PP::G_LEVEL }) setParam(proc, id, 0.0f);
        onlyEngine(proc, 0);
    }

    std::cout << "== effects + master\n";
    {
        onlyEngine(proc, 0);
        auto dry = playNote(proc, 60, sr, 0.3, 1.2);
        setParam(proc, PP::RVON, 1); setParam(proc, PP::RVMIX, 0.5f);
        auto wet = playNote(proc, 60, sr, 0.3, 1.2);
        CHECK(finite(wet) && rms(wet, sr, 0.9, 1.4) > rms(dry, sr, 0.9, 1.4) * 2.0f, "reverb adds a tail after note-off");
        setParam(proc, PP::RVON, 0);
        setParam(proc, PP::DLON, 1); setParam(proc, PP::DLTIME, 300); setParam(proc, PP::DLMIX, 0.5f);
        auto echo = playNote(proc, 60, sr, 0.3, 1.2);
        CHECK(rms(echo, sr, 0.9, 1.4) > rms(dry, sr, 0.9, 1.4) * 2.0f, "delay repeats after note-off");
        setParam(proc, PP::DLON, 0);
        for (auto id : { PP::CHON, PP::PHON, PP::FLON, PP::DION, PP::SAON })
        {
            setParam(proc, id, 1);
            auto o = playNote(proc, 60, sr, 0.4);
            CHECK(finite(o) && rms(o, sr, 0.1, 0.4) > 1e-3f, id << " runs");
            setParam(proc, id, 0);
        }
        setParam(proc, PP::VOLUME, 6.0f);
        setParam(proc, PP::DION, 1); setParam(proc, PP::DIDRIVE, 1.0f);
        auto hot = playNote(proc, 48, sr, 0.4, 0.0, 1.0f, { 52, 55, 60, 64, 67 });
        CHECK(hot.getMagnitude(0, 0, hot.getNumSamples()) <= 0.9662f, "limiter holds a hot chord at -0.3 dBFS (peak "
              << juce::String(hot.getMagnitude(0, 0, hot.getNumSamples()), 4) << ")");
        // a quiet signal passes unchanged (only delayed by the lookahead)
        setParam(proc, PP::DION, 0); setParam(proc, PP::VOLUME, -6.0f);
        proc.panic();
        playNote(proc, 60, sr, 0.0, 1.5); // let the limiter release fully
        setParam(proc, PP::LIMIT, 0);
        auto q0 = playNote(proc, 60, sr, 0.3, 0.0, 0.5f);
        proc.panic();
        playNote(proc, 60, sr, 0.0, 0.5); // flush the lookahead delay with silence
        setParam(proc, PP::LIMIT, 1);
        auto q1 = playNote(proc, 60, sr, 0.3, 0.0, 0.5f);
        proc.panic();
        double dmax = 0;
        for (int i = 0; i < q0.getNumSamples(); ++i) dmax = juce::jmax(dmax, (double) std::abs(q0.getSample(0, i) - q1.getSample(0, i)));
        int at = 0; for (int i = 0; i < q0.getNumSamples(); ++i) if (std::abs(q0.getSample(0, i) - q1.getSample(0, i)) >= dmax * 0.999) { at = i; break; }
        CHECK(dmax < 1e-6, "limiter is transparent below the ceiling (max diff " << dmax << " at sample " << at << ", peak " << q0.getMagnitude(0, 0, q0.getNumSamples()) << ")");
        CHECK(proc.getLatencySamples() > 0, "lookahead latency reported to the host (" << proc.getLatencySamples() << " samples)");
        setParam(proc, PP::VOLUME, 6.0f); setParam(proc, PP::DION, 1);
        setParam(proc, PP::DION, 0); setParam(proc, PP::DIDRIVE, 0.45f);
        setParam(proc, PP::VOLUME, -6.0f);
        proc.panic();
        playNote(proc, 60, sr, 0.02, 0.0);
    }

    std::cout << "== spectral freeze / smear\n";
    {
        proc.loadSampleSync(wavF.getFullPathName()); // decaying C4 tone
        setLoop(proc, 0);
        proc.rebuildSync(false);
        onlyEngine(proc, 2);
        auto nf = playNote(proc, 60, sr, 1.9, 0.05);
        setParam(proc, PP::P_FREEZE, 1.0f);
        auto fz = playNote(proc, 60, sr, 1.9, 0.05);
        const float dNo = juce::Decibels::gainToDecibels(rms(nf, sr, 1.5, 1.8) / rms(nf, sr, 0.3, 0.6));
        const float dFz = juce::Decibels::gainToDecibels(rms(fz, sr, 1.5, 1.8) / rms(fz, sr, 0.3, 0.6));
        CHECK(std::abs(dFz) < 1.5f && dNo < -3.0f, "freeze holds the tone after the attack (" << juce::String(dFz, 1)
              << " dB vs " << juce::String(dNo, 1) << " dB unfrozen)");
        CHECK(std::abs(centsBetween(measurePitch(fz, sr, 0.5, 1.5), 261.63f)) < 5.0f, "frozen note stays in tune");
        setParam(proc, PP::P_FREEZE, 0.0f);
        // smear: a tremolo tone loses most of its amplitude modulation
        juce::AudioBuffer<float> trem(2, (int) (sr * 2.0));
        for (int i = 0; i < trem.getNumSamples(); ++i)
        {
            double t = i / sr, v = 0;
            for (int k = 1; k <= 10; ++k) v += std::sin(juce::MathConstants<double>::twoPi * 220.0 * k * t) / k;
            v *= 0.25 * (1.0 + 0.8 * std::sin(juce::MathConstants<double>::twoPi * 5.0 * t));
            trem.setSample(0, i, (float) v); trem.setSample(1, i, (float) v);
        }
        auto tf = writeFile(tmp, "trem.wav", trem, sr, wav);
        proc.loadSampleSync(tf.getFullPathName());
        auto amDepth = [&](const juce::AudioBuffer<float>& b)
        {
            float mn = 1e9f, mx = 0;
            for (double t = 0.4; t < 1.6; t += 0.01) { float r = rms(b, sr, t, t + 0.01); mn = juce::jmin(mn, r); mx = juce::jmax(mx, r); }
            return juce::Decibels::gainToDecibels(mx / juce::jmax(1e-9f, mn));
        };
        onlyEngine(proc, 2);
        auto s0 = playNote(proc, 57, sr, 1.8);
        setParam(proc, PP::P_BLUR, 0.7f);
        auto s1 = playNote(proc, 57, sr, 1.8);
        setParam(proc, PP::P_BLUR, 0.0f);
        CHECK(amDepth(s1) < amDepth(s0) * 0.5f, "smear washes out tremolo (" << juce::String(amDepth(s0), 1) << " dB -> "
              << juce::String(amDepth(s1), 1) << " dB)");
        proc.loadSampleSync(wavF.getFullPathName());
        onlyEngine(proc, 0);
    }

    std::cout << "== granular engine\n";
    {
        proc.loadSampleSync(wavF.getFullPathName());
        setLoop(proc, 0);
        proc.rebuildSync(false);
        onlyEngine(proc, PP::kGranular);
        setParam(proc, PP::G_SPRAY, 0); setParam(proc, PP::G_JITTER, 0); setParam(proc, PP::G_SIZE, 80); setParam(proc, PP::G_DENS, 40);
        auto o = playNote(proc, 67, sr, 0.8);
        float f = measurePitch(o, sr, 0.15, 0.7);
        CHECK(finite(o) && std::abs(centsBetween(f, g4)) < 5.0f, "granular plays in tune at G4 (" << juce::String(f, 2) << " Hz)");
        float lvG = rms(o, sr, 0.1, 0.7);
        onlyEngine(proc, 0);
        float lvS = rms(playNote(proc, 67, sr, 0.8), sr, 0.1, 0.7);
        CHECK(std::abs(juce::Decibels::gainToDecibels(lvG / lvS)) < 6.0f, "granular level within 6 dB of the sampler ("
              << juce::String(juce::Decibels::gainToDecibels(lvG / lvS), 1) << " dB)");
        onlyEngine(proc, PP::kGranular);
        // scan 1 keeps the length two octaves up (unlike the sampler)
        auto up = playNote(proc, 84, sr, 1.9, 0.1);
        CHECK(rms(up, sr, 1.0, 1.4) > 1e-3f, "granular two octaves up still sounding at 1.0-1.4 s");
        // freeze: scan 0 holds a slice; the level stays steady instead of decaying
        setParam(proc, PP::G_SCAN, 0); setParam(proc, PP::G_POS, 0.3f);
        auto fz = playNote(proc, 60, sr, 3.0, 0.1);
        float e1 = rms(fz, sr, 0.5, 1.0), e2 = rms(fz, sr, 2.4, 2.9);
        CHECK(e2 > 0.7f * e1, "scan 0 freezes (level " << juce::String(juce::Decibels::gainToDecibels(e2 / e1), 1) << " dB over 2 s)");
        // width spreads, width 0 stays mono
        proc.panic();
        playNote(proc, 60, sr, 0.02, 0.0);
        setParam(proc, PP::G_WIDTH, 0);
        auto mo = playNote(proc, 60, sr, 0.6);
        double side = 0, mid = 1e-12;
        for (int i = 0; i < mo.getNumSamples(); ++i) { side += std::abs(mo.getSample(0, i) - mo.getSample(1, i)); mid += std::abs(mo.getSample(0, i)); }
        CHECK(side / mid < 1e-5, "width 0 is mono (side/mid " << side / mid << ")");
        setParam(proc, PP::G_WIDTH, 1); setParam(proc, PP::G_SPRAY, 80); setParam(proc, PP::G_JITTER, 30);
        auto wd = playNote(proc, 60, sr, 0.6);
        side = 0;
        for (int i = 0; i < wd.getNumSamples(); ++i) side += std::abs(wd.getSample(0, i) - wd.getSample(1, i));
        CHECK(side > 1.0 && finite(wd), "width 1 spreads grains across the stereo field");
        for (auto id : { PP::G_SCAN, PP::G_POS, PP::G_WIDTH, PP::G_SPRAY, PP::G_JITTER, PP::G_SIZE, PP::G_DENS })
            if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->getDefaultValue());
        onlyEngine(proc, 0);
    }

    std::cout << "== keep length (resynthesis keeps duration when transposed up)\n";
    {
        proc.loadSampleSync(wavF.getFullPathName());
        setLoop(proc, 0);
        onlyEngine(proc, 0);
        auto s = playNote(proc, 84, sr, 1.9, 0.1);
        onlyEngine(proc, 2);
        setParam(proc, PP::V_KEEPLEN, 1);
        auto k = playNote(proc, 84, sr, 1.9, 0.1);
        CHECK(rms(s, sr, 1.0, 1.4) < 1e-5f, "sampler two octaves up ends after ~0.5 s");
        CHECK(rms(k, sr, 1.0, 1.4) > 1e-3f, "spectral two octaves up still sounding at 1.0-1.4 s ("
              << juce::String(rms(k, sr, 1.0, 1.4), 4) << ")");
        onlyEngine(proc, 1);
        auto a = playNote(proc, 84, sr, 1.9, 0.1);
        CHECK(rms(a, sr, 1.0, 1.4) > 1e-3f, "additive two octaves up still sounding at 1.0-1.4 s");
    }

    std::cout << "== looping\n";
    {
        proc.loadSampleSync(susF.getFullPathName());
        auto e = proc.getEdits();
        CHECK(e.loopEnd > e.loopStart && e.loopStart > 0 && e.loopEnd <= e.end, "auto loop points found ("
              << e.loopStart << " .. " << e.loopEnd << ")");
        setLoop(proc, 1);
        proc.rebuildSync(false);
        auto inst = proc.getInstrument();
        CHECK(inst->loopBaked, "forward loop has a baked crossfade");
        onlyEngine(proc, 0);
        auto out = playNote(proc, 57, sr, 3.0);
        CHECK(rms(out, sr, 2.5, 2.9) > 0.05f, "1 s sample sustains at 2.5 s with loop on ("
              << juce::String(rms(out, sr, 2.5, 2.9), 3) << ")");
        // discontinuity check: max sample-to-sample jump stays near the waveform's own maximum slope
        float maxJump = 0, maxSlopeRef = 0;
        for (int i = 1; i < out.getNumSamples() - (int) (0.3 * sr); ++i)
            maxJump = juce::jmax(maxJump, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        for (int i = 1; i < (int) (0.9 * sr); ++i)
            maxSlopeRef = juce::jmax(maxSlopeRef, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        CHECK(maxJump < maxSlopeRef * 1.5f, "no click at the loop seam (max step " << juce::String(maxJump, 4)
              << " vs " << juce::String(maxSlopeRef, 4) << ")");
        onlyEngine(proc, 1);
        out = playNote(proc, 57, sr, 3.0);
        CHECK(rms(out, sr, 2.5, 2.9) > 0.05f, "additive sustains via loop");
        onlyEngine(proc, 2);
        out = playNote(proc, 57, sr, 3.0);
        CHECK(rms(out, sr, 2.5, 2.9) > 0.05f && finite(out), "spectral sustains via loop");
        setLoop(proc, 0);
    }

    std::cout << "== edits\n";
    {
        proc.loadSampleSync(wavF.getFullPathName());
        auto before = proc.getInstrument();
        proc.toggleReverse();
        auto after = proc.getInstrument();
        const int n = after->raw.getNumSamples();
        CHECK(after->raw.getSample(0, n - 10) == before->raw.getSample(0, 9), "reverse flips the audio");
        proc.toggleReverse();
        proc.toggleNormalize();
        CHECK(std::abs(proc.getInstrument()->raw.getMagnitude(0, 0, n) - 0.98f) < 1e-3f, "normalize to 0.98");
        proc.toggleNormalize();
        auto sel = proc.getEdits();
        sel.start = 4800; sel.end = 52800; // 0.1 s .. 1.1 s
        proc.setEdits(sel, true, false);
        proc.trimToSelection();
        auto t = proc.getInstrument();
        CHECK(t->raw.getNumSamples() == 48000 && proc.getEdits().start == 0, "TRIM crops the sample to the selection ("
              << t->raw.getNumSamples() << " samples)");
        CHECK(t->raw.getSample(0, 0) == before->raw.getSample(0, 4800), "cropped audio starts at the old selection start");
        proc.toggleReverse();
        auto rs = proc.getEdits();
        rs.start = 0; rs.end = 24000; // first half of the reversed crop = last half of the forward crop
        proc.setEdits(rs, true, false);
        proc.trimToSelection();
        proc.toggleReverse();
        CHECK(proc.getInstrument()->raw.getNumSamples() == 24000 && proc.getInstrument()->raw.getSample(0, 0) == before->raw.getSample(0, 4800 + 24000),
              "TRIM on a reversed sample keeps the right region");
        proc.untrim();
        CHECK(proc.getInstrument()->raw.getNumSamples() == n, "untrim restores the whole file");
    }

    std::cout << "== voice modes\n";
    {
        onlyEngine(proc, 0);
        setParam(proc, PP::V_MODE, 2); // legato
        playNote(proc, 60, sr, 0.1, 0.0, 1.0f, { 64, 67 });
        CHECK(proc.getActiveNotes().size() <= 1, "legato uses one voice");
        setParam(proc, PP::V_MODE, 0);
        proc.panic();
        playNote(proc, 60, sr, 0.05, 0.0);
        setParam(proc, PP::V_VOICES, 2);
        playNote(proc, 60, sr, 0.1, 0.0, 1.0f, { 64, 67, 71 });
        CHECK(proc.getActiveNotes().size() <= 2, "voice limit respected");
        setParam(proc, PP::V_VOICES, 16);
        proc.panic();
        playNote(proc, 60, sr, 0.02, 0.0);
    }

    std::cout << "== state round trip\n";
    {
        setParam(proc, PP::ROOTOVR, 1);
        setParam(proc, PP::ROOTNOTE, 62);
        setParam(proc, PP::A_PARTIALS, 33);
        proc.setPartialGain(3, 0.5f);
        auto e = proc.getEdits();
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        PastelProcessor p2;
        p2.setAsyncLoads(false);
        p2.prepareToPlay(sr, 256);
        p2.setStateInformation(mb.getData(), (int) mb.getSize());
        auto e2 = p2.getEdits();
        CHECK(p2.getInstrument() != nullptr && p2.getInstrument()->path == wavF.getFullPathName(), "sample path restored");
        CHECK((int) p2.apvts.getRawParameterValue(PP::ROOTNOTE)->load() == 62 && p2.apvts.getRawParameterValue(PP::ROOTOVR)->load() > 0.5f,
              "root override restored");
        CHECK((int) p2.apvts.getRawParameterValue(PP::A_PARTIALS)->load() == 33, "parameters restored");
        CHECK(std::abs(p2.getPartialGain(3) - 0.5f) < 1e-3f, "partial gains restored");
        CHECK(e2.loopStart == e.loopStart && e2.loopEnd == e.loopEnd && e2.start == e.start && e2.end == e.end, "edits restored");
        auto pf = tmp.getChildFile("test.pastelsampler");
        CHECK(proc.savePreset(pf) && p2.loadPreset(pf), "preset save / load");
        setParam(proc, PP::ROOTOVR, 0);
    }

    std::cout << "== CPU (16 voices, spectral engine, Ultra quality)\n";
    {
        setParam(proc, PP::QUALITY, 2);
        proc.rebuildSync(false);
        onlyEngine(proc, 2); // spectral is the heaviest engine
        std::vector<int> chord;
        for (int i = 1; i < 16; ++i) chord.push_back(40 + i * 3);
        const double secs = 2.0;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = playNote(proc, 40, sr, secs, 0.0, 0.9f, chord);
        auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        double rt = (secs * 1000.0) / ms;
        std::cout << "       rendered " << secs << " s in " << juce::String(ms, 1) << " ms  (" << juce::String(rt, 1) << "x realtime)\n";
        CHECK(finite(out) && rt > 1.0, "faster than realtime");
    }

    // optional: render the real editor offscreen (PASTEL_SNAPSHOT_DIR=/some/dir)
    if (const char* dir = std::getenv("PASTEL_SNAPSHOT_DIR"); dir != nullptr && *dir != 0)
    {
        proc.loadSampleSync(wavF.getFullPathName());
        setParam(proc, PP::ENGINE, (float) PP::kBlend);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(1400, 800);
        auto save = [&](const juce::String& name)
        {
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File out = juce::File(dir).getChildFile(name);
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            std::cout << "       snapshot " << out.getFullPathName() << "\n";
        };
        save("page_main.png");
        std::function<juce::TextButton*(juce::Component*)> find = [&](juce::Component* c) -> juce::TextButton*
        {
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "EFFECTS") return b;
            for (auto* ch : c->getChildren()) if (auto* r = find(ch)) return r;
            return nullptr;
        };
        std::function<juce::TextButton*(juce::Component*, const juce::String&)> findT = [&](juce::Component* c, const juce::String& t) -> juce::TextButton*
        {
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText().endsWith(t) && b->isVisible()) return b;
            for (auto* ch : c->getChildren()) if (auto* r = findT(ch, t)) return r;
            return nullptr;
        };
        for (auto name : { "SAMPLER", "ADDITIVE", "SPECTRAL", "GRANULAR" })
            if (auto* tb = findT(ed.get(), name)) { tb->onClick(); save(juce::String("tab_") + name + ".png"); }
        if (auto* fxBtn = find(ed.get())) { fxBtn->onClick(); save("page_effects.png"); }
        ed.reset();
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    tmp.deleteRecursively();
    return failed == 0 ? 0 : 1;
}
