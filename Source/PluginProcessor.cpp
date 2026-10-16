#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

namespace
{
float dbToGain(float db) { return db <= -59.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }

juce::String audioKeyFor(const juce::String& path, const SampleEdits& e)
{
    return path + "|" + (e.reverse ? "r" : "") + (e.normalize ? "n" : "");
}
}

PastelProcessor::PastelProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", PP::createLayout())
{
    formats.registerBasicFormats();
    for (auto& g : partialGains) g.store(1.0f);
    for (auto* id : { &PP::LOOP_ON, &PP::LOOP_TYPE, &PP::S_XFADE, &PP::QUALITY, &PP::ALGO })
        apvts.addParameterListener(*id, this);
    startTimerHz(15);
    // dev hook: preload a sample (used for screenshots / smoke tests)
    if (auto* f = std::getenv("PASTEL_AUTOLOAD"); f != nullptr && *f != 0) loadSample(f);
}

PastelProcessor::~PastelProcessor()
{
    *alive = false;
    stopTimer();
    pool.removeAllJobs(true, 10000);
    previewPool.removeAllJobs(true, 10000);
    for (auto* id : { &PP::LOOP_ON, &PP::LOOP_TYPE, &PP::S_XFADE, &PP::QUALITY, &PP::ALGO })
        apvts.removeParameterListener(*id, this);
}

bool PastelProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PastelProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    auto keep = engine.setInstrument(nullptr);
    engine.prepare(sampleRate, samplesPerBlock);
    engine.setInstrument(keep);
    fx.prepare(sampleRate, samplesPerBlock);
    fx.reset();
    limiter.prepare(sampleRate);
    setLatencySamples(limiter.latency());
}

// ---------------------------------------------------------------------------
// parameters
float PastelProcessor::rootMidiFor(const Instrument* inst) const
{
    if (apvts.getRawParameterValue(PP::ROOTOVR)->load() > 0.5f)
        return apvts.getRawParameterValue(PP::ROOTNOTE)->load() + apvts.getRawParameterValue(PP::ROOTFINE)->load() / 100.0f;
    if (inst != nullptr && inst->pitch.f0 > 0) return inst->pitch.midi;
    return 60.0f;
}

float PastelProcessor::getRootMidi() const { return rootMidiFor(uiInstrument.get()); }
float PastelProcessor::getRootHz() const { return Analysis::midiToHz(getRootMidi()); }

int PastelProcessor::loopMode() const
{
    if (apvts.getRawParameterValue(PP::LOOP_ON)->load() < 0.5f) return 0;
    return 1 + juce::jlimit(0, 2, (int) std::lround(apvts.getRawParameterValue(PP::LOOP_TYPE)->load()));
}

EngineParams PastelProcessor::snapshotParams() const
{
    EngineParams p;
    auto v = [&](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    auto b = [&](const juce::String& id) { return v(id) > 0.5f; };
    auto i = [&](const juce::String& id) { return (int) std::lround(v(id)); };
    p.rootMidi = rootMidiFor(engine.instrument());
    p.rootHz = Analysis::midiToHz(p.rootMidi);
    p.engine = juce::jlimit(0, 4, i(PP::ENGINE));
    p.gPos = v(PP::G_POS); p.gScan = v(PP::G_SCAN); p.gSizeMs = v(PP::G_SIZE); p.gDensity = v(PP::G_DENS);
    p.gSprayMs = v(PP::G_SPRAY); p.gJitter = v(PP::G_JITTER); p.gWidth = v(PP::G_WIDTH); p.gShape = v(PP::G_SHAPE);
    p.gLevel = v(PP::G_LEVEL);
    p.interp = i(PP::S_INTERP); p.loopMode = loopMode();
    p.start = v(PP::S_START); p.sTune = v(PP::S_TUNE); p.sLevel = v(PP::S_LEVEL);
    p.partials = i(PP::A_PARTIALS); p.tilt = v(PP::A_TILT);
    p.noise = v(PP::A_NOISE); p.aLevel = v(PP::A_LEVEL);
    p.shift = v(PP::P_SHIFT); p.blur = v(PP::P_BLUR);
    p.freeze = v(PP::P_FREEZE); p.pLevel = v(PP::P_LEVEL);
    p.eOn = b(PP::E_ON); p.A = v(PP::E_A); p.D = v(PP::E_D); p.S = v(PP::E_S); p.R = v(PP::E_R);
    p.curve = v(PP::E_CURVE);
    p.fOn = b(PP::F_ON); p.cutoff = v(PP::F_CUT); p.reso = v(PP::F_RES); p.drive = v(PP::F_DRIVE);
    p.keytrack = v(PP::F_KEY); p.ftype = i(PP::F_TYPE); p.slope = i(PP::F_SLOPE);
    p.mode = i(PP::V_MODE); p.voices = i(PP::V_VOICES); p.glideMs = v(PP::V_GLIDE);
    p.bendRange = i(PP::V_BEND); p.spread = v(PP::V_SPREAD); p.formant = b(PP::V_FORMANT);
    p.keepLen = b(PP::V_KEEPLEN); p.rr = b(PP::V_RR); p.mono = b(PP::V_MONO);
    p.tuning = i(PP::V_TUNING); p.a4 = PP::a4Values[juce::jlimit(0, 9, i(PP::V_A4))];
    p.loKey = i(PP::V_LOKEY); p.hiKey = i(PP::V_HIKEY);
    p.volumeDb = v(PP::VOLUME);
    for (size_t k = 0; k < 128; ++k) p.partialGains[k] = partialGains[k].load();
    return p;
}

DspParams PastelProcessor::fxParams() const
{
    DspParams d;
    auto v = [&](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };
    auto b = [&](const juce::String& id) { return v(id) > 0.5f; };
    d.fOn = false;   // the filter lives per voice (Filter card)
    d.lfoOn = false;
    d.chOn = b(PP::CHON); d.chMode = (int) v(PP::CHMODE); d.chRate = v(PP::CHRATE); d.chDepth = v(PP::CHDEPTH); d.chMix = v(PP::CHMIX);
    d.phOn = b(PP::PHON); d.phRate = v(PP::PHRATE); d.phDepth = v(PP::PHDEPTH); d.phFb = v(PP::PHFB); d.phMix = v(PP::PHMIX);
    d.flOn = b(PP::FLON); d.flRate = v(PP::FLRATE); d.flDepth = v(PP::FLDEPTH); d.flFb = v(PP::FLFB); d.flMix = v(PP::FLMIX);
    d.diOn = b(PP::DION); d.diDrive = v(PP::DIDRIVE); d.diMix = v(PP::DIMIX);
    d.saOn = b(PP::SAON); d.saAmt = v(PP::SAAMT); d.saTone = v(PP::SATONE); d.saMode = (int) v(PP::SAMODE);
    d.rvOn = b(PP::RVON); d.rvSize = v(PP::RVSIZE); d.rvDamp = v(PP::RVDAMP); d.rvMix = v(PP::RVMIX);
    d.dlOn = b(PP::DLON); d.dlTimeMs = v(PP::DLTIME); d.dlFb = v(PP::DLFB); d.dlMix = v(PP::DLMIX);
    d.dlSync = b(PP::DLSYNC); d.dlDiv = (int) v(PP::DLDIV);
    d.volume = dbToGain(v(PP::VOLUME));
    d.limiter = false; // the master PeakLimiter runs after the DSP chain
    d.mono = b(PP::V_MONO);
    return d;
}

void PastelProcessor::parameterChanged(const juce::String& id, float)
{
    if (id == PP::ALGO) redetectPending = true;
    rebuildPending = true;
}

void PastelProcessor::timerCallback()
{
    // free objects the audio thread handed back
    auto r = retireFifo.read(retireFifo.getNumReady());
    for (int k = 0; k < r.blockSize1; ++k) retireSlots[(size_t) (r.startIndex1 + k)].reset();
    for (int k = 0; k < r.blockSize2; ++k) retireSlots[(size_t) (r.startIndex2 + k)].reset();

    int learned = learnedNote.exchange(-1);
    if (learned >= 0)
    {
        auto set = [&](const juce::String& id, float value)
        {
            auto* prm = apvts.getParameter(id);
            prm->setValueNotifyingHost(prm->convertTo0to1(value));
        };
        set(PP::ROOTOVR, 1.0f);
        set(PP::ROOTNOTE, (float) learned);
        set(PP::ROOTFINE, 0.0f);
        sendChangeMessage();
    }

    if (rebuildPending.exchange(false) && decoded != nullptr)
        rebuild(redetectPending.exchange(false));
}

void PastelProcessor::retire(std::shared_ptr<const void> p)
{
    if (p == nullptr) return;
    auto w = retireFifo.write(1);
    if (w.blockSize1 > 0) retireSlots[(size_t) w.startIndex1] = std::move(p);
}

// ---------------------------------------------------------------------------
// loading
std::shared_ptr<const PastelProcessor::Decoded> PastelProcessor::decode(juce::AudioFormatManager& fm, const juce::String& path,
                                                                     juce::String& error)
{
    juce::File f(path);
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    if (reader == nullptr) { error = "unsupported or unreadable file"; return nullptr; }
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0) { error = "empty file"; return nullptr; }
    const auto maxLen = (juce::int64) (reader->sampleRate * 120.0);
    const int len = (int) juce::jmin(reader->lengthInSamples, maxLen);
    const int nch = (int) juce::jmax(1u, reader->numChannels);
    juce::AudioBuffer<float> tmp(nch, len);
    tmp.clear();
    reader->read(&tmp, 0, len, 0, true, nch > 1);

    auto d = std::make_shared<Decoded>();
    d->path = f.getFullPathName();
    d->sr = reader->sampleRate;
    d->audio.setSize(2, len);
    d->audio.copyFrom(0, 0, tmp, 0, 0, len);
    d->audio.copyFrom(1, 0, tmp, nch > 1 ? 1 : 0, 0, len);
    juce::String chans = nch == 1 ? "Mono" : (nch == 2 ? "Stereo" : juce::String(nch) + " ch");
    d->info = juce::String(reader->sampleRate / 1000.0, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + juce::String(juce::CharPointer_UTF8(" kHz · "))
            + juce::String(reader->bitsPerSample) + juce::String(juce::CharPointer_UTF8("-bit · ")) + chans + juce::String(juce::CharPointer_UTF8(" · "))
            + juce::String(len / reader->sampleRate, 2) + " s"
            + (reader->lengthInSamples > maxLen ? " (first 120 s)" : "");
    return d;
}

PastelProcessor::BuildInput PastelProcessor::makeInput(bool redetect, bool fresh) const
{
    BuildInput in;
    in.decoded = decoded;
    in.edits = edits;
    in.algo = (Analysis::Algo) (int) apvts.getRawParameterValue(PP::ALGO)->load();
    in.quality = (int) apvts.getRawParameterValue(PP::QUALITY)->load();
    in.loopMode = loopMode();
    in.xfadeMs = apvts.getRawParameterValue(PP::S_XFADE)->load();
    in.autoLoop = apvts.getRawParameterValue(PP::S_AUTOLOOP)->load() > 0.5f;
    in.redetect = redetect;
    in.freshLoad = fresh;
    in.previous = uiInstrument;
    return in;
}

PastelProcessor::BuildOutput PastelProcessor::build(const BuildInput& in, std::atomic<float>* progress)
{
    auto setP = [&](float v) { if (progress) progress->store(v); };
    BuildOutput out;
    auto inst = std::make_shared<Instrument>();
    const auto& dec = *in.decoded;
    SampleEdits e = in.edits;
    if (in.freshLoad) e = SampleEdits();
    inst->path = dec.path;
    inst->name = juce::File(dec.path).getFileName();
    inst->formatInfo = dec.info;
    inst->sr = dec.sr;
    const double sr = dec.sr;

    // TRIM crop first (decoded coordinates), then reverse / normalise the kept region
    const int decN = dec.audio.getNumSamples();
    e.cropStart = juce::jlimit(0, juce::jmax(0, decN - 1), e.cropStart);
    if (e.cropEnd <= e.cropStart || e.cropEnd > decN) e.cropEnd = decN;
    juce::AudioBuffer<float> a(2, e.cropEnd - e.cropStart);
    for (int c = 0; c < 2; ++c) a.copyFrom(c, 0, dec.audio, c, e.cropStart, e.cropEnd - e.cropStart);
    const int n = a.getNumSamples();
    if (e.reverse) a.reverse(0, n);
    if (e.normalize)
    {
        float pk = a.getMagnitude(0, n);
        if (pk > 0) a.applyGain(0.98f / pk);
    }
    if (e.end < 0 || e.end > n) e.end = n;
    if (!e.valid(n)) { e.start = 0; e.end = n; }
    auto mono = Analysis::mixToMono(a, 0, n);
    setP(0.3f);

    if (e.anaStart < 0 || e.anaEnd <= e.anaStart || e.anaEnd > n || e.anaStart < e.start)
    {
        double a0, a1;
        Analysis::defaultAnalysisWindow(mono.data() + e.start, e.end - e.start, sr, a0, a1);
        e.anaStart = e.start + a0;
        e.anaEnd = e.start + a1;
    }
    inst->audioKey = audioKeyFor(dec.path, e);
    inst->anaKey = juce::String((int) e.anaStart) + ":" + juce::String((int) e.anaEnd) + ":" + juce::String((int) in.algo)
                 + ":" + juce::String(e.start) + ":" + juce::String(e.end);
    const auto* prev = in.previous.get();
    const bool sameAudio = prev != nullptr && prev->audioKey == inst->audioKey;
    if (!in.redetect && sameAudio && prev->anaKey == inst->anaKey)
    {
        inst->pitch = prev->pitch;
        inst->track = prev->track;
    }
    else
    {
        const int a0 = (int) e.anaStart, a1 = (int) e.anaEnd;
        inst->pitch = Analysis::detectPitch(mono.data() + a0, a1 - a0, sr, in.algo);
        inst->track = Analysis::trackPitch(mono.data() + e.start, e.end - e.start, sr);
        inst->track.firstSample += e.start;
    }
    setP(0.45f);

    if (e.loopStart < 0 || e.loopEnd <= e.loopStart || e.loopEnd > e.end || e.loopStart < e.start)
    {
        int ls = 0, le = 0;
        bool ok = in.autoLoop && Analysis::findLoop(mono.data(), n, sr, inst->pitch.f0, e.start, e.end, ls, le);
        if (!ok)
        {
            ls = e.start + (int) (0.4 * (e.end - e.start));
            le = e.end - (int) juce::jmin(0.01 * sr, 0.05 * (e.end - e.start));
        }
        e.loopStart = ls;
        e.loopEnd = le;
    }
    e.loopStart = juce::jlimit(e.start, juce::jmax(e.start, e.end - 2), e.loopStart);
    e.loopEnd = juce::jlimit(e.loopStart + 1, e.end, e.loopEnd);

    const float f0 = inst->pitch.f0 > 0 ? inst->pitch.f0 : 261.63f;
    const int anaLen = (int) juce::jmin((double) n, sr * 60.0);
    if (sameAudio && prev->additive && std::abs(prev->additive->f0 - f0) < 1e-3f) inst->additive = prev->additive;
    else inst->additive = Analysis::analyseAdditive(mono.data(), anaLen, sr, f0);
    setP(0.7f);
    // harmonic phase locking needs a trustworthy f0; 0 = treat as unpitched
    const float specF0 = inst->pitch.pitched() ? inst->pitch.f0 : 0.0f;
    const int fftSize = PP::spectralFftSize(in.quality, sr, specF0);
    if (sameAudio && prev->spectral && prev->spectral->N == fftSize && prev->spectral->f0 == specF0)
    {
        inst->spectral = prev->spectral;
        inst->spectralR = prev->spectralR;
    }
    else
    {
        // stereo files are resynthesised per channel so the image survives; mono shares one analysis
        bool stereo = false;
        {
            const float* l = a.getReadPointer(0);
            const float* r = a.getReadPointer(1);
            for (int i = 0; i < n && !stereo; ++i) stereo = std::abs(l[i] - r[i]) > 1e-6f;
        }
        if (stereo)
        {
            inst->spectral = Analysis::analyseSpectral(a.getReadPointer(0), anaLen, sr, fftSize, specF0);
            inst->spectralR = Analysis::analyseSpectral(a.getReadPointer(1), anaLen, sr, fftSize, specF0);

        }
        else
        {
            inst->spectral = Analysis::analyseSpectral(mono.data(), anaLen, sr, fftSize, specF0);
            inst->spectralR = inst->spectral;

        }
    }
    setP(0.95f);

    inst->start = e.start;
    inst->end = e.end;
    inst->loopStart = e.loopStart;
    inst->loopEnd = e.loopEnd;
    inst->anaStart = e.anaStart;
    inst->anaEnd = e.anaEnd;
    inst->raw = a;
    inst->audio = std::move(a);
    if (in.loopMode == 1 || in.loopMode == 3)
    {
        int xf = (int) (in.xfadeMs * 0.001 * sr);
        inst->loopBaked = Analysis::bakeLoopCrossfade(inst->audio, e.loopStart, e.loopEnd, xf) > 0;
    }
    Analysis::computePeaks(inst->raw, 2048, inst->peaksMin, inst->peaksMax);
    setP(1.0f);
    out.inst = std::move(inst);
    out.edits = e;
    return out;
}

void PastelProcessor::install(BuildOutput out)
{
    {
        const juce::ScopedLock l(stateLock);
        edits = out.edits;
        samplePath = out.inst->path;
    }
    uiInstrument = out.inst;
    status = out.inst->name + juce::String(juce::CharPointer_UTF8("  ·  ")) + out.inst->formatInfo;
    {
        const juce::SpinLock::ScopedLockType l(pendingLock);
        pendingInst = out.inst;
        pendingFlag = true;
    }
    if (!asyncLoads)
    {
        // no audio callback running in the harness: swap directly
        const juce::SpinLock::ScopedLockType l(pendingLock);
        engine.setInstrument(std::move(pendingInst));
        pendingInst = nullptr;
        pendingFlag = false;
    }
    loadProgress = 1.0f;
    sendChangeMessage();
}

void PastelProcessor::startJob(std::function<void()> job)
{
    ++loadingCount;
    pool.addJob([this, job]
    {
        job();
        --loadingCount;
    });
}

void PastelProcessor::loadInternal(const juce::String& path, const SampleEdits* restore, bool sync)
{
    const int gen = ++generation;
    loadProgress = 0.0f;
    status = "Loading " + juce::File(path).getFileName() + " ...";
    {
        const juce::ScopedLock l(stateLock);
        samplePath = path;
    }
    sendChangeMessage();
    BuildInput in = makeInput(true, restore == nullptr);
    in.previous = nullptr;
    if (restore != nullptr) in.edits = *restore;

    auto work = [this, path, gen, in, sync]() mutable -> std::function<void()>
    {
        juce::String err;
        auto dec = decode(formats, path, err);
        if (dec == nullptr)
            return [this, path, err, gen]
            {
                if (gen != generation) return;
                status = "Could not load " + juce::File(path).getFileName() + ": " + err;
                loadProgress = 1.0f;
                sendChangeMessage();
            };
        in.decoded = dec;
        auto out = build(in, sync ? nullptr : &loadProgress);
        return [this, dec, out, gen, path]
        {
            if (gen != generation) return;
            decoded = dec;
            install(out);
            library.setLastFile(path);
        };
    };

    if (sync)
    {
        work()();
        return;
    }
    std::weak_ptr<bool> token = alive;
    startJob([work, token]() mutable
    {
        auto finish = work();
        juce::MessageManager::callAsync([token, finish]
        {
            if (auto a = token.lock(); a && *a) finish();
        });
    });
}

void PastelProcessor::loadSample(const juce::String& path) { loadInternal(path, nullptr, !asyncLoads); }

bool PastelProcessor::loadSampleSync(const juce::String& path)
{
    loadInternal(path, nullptr, true);
    return uiInstrument != nullptr && uiInstrument->path == juce::File(path).getFullPathName();
}

void PastelProcessor::rebuild(bool redetect)
{
    if (decoded == nullptr) return;
    if (!asyncLoads) { rebuildSync(redetect); return; }
    const int gen = ++generation;
    BuildInput in = makeInput(redetect, false);
    std::weak_ptr<bool> token = alive;
    startJob([this, in, gen, token]
    {
        auto out = build(in, nullptr);
        juce::MessageManager::callAsync([this, out, gen, token]
        {
            if (auto a = token.lock(); a && *a && gen == generation) install(out);
        });
    });
}

void PastelProcessor::rebuildSync(bool redetect)
{
    if (decoded == nullptr) return;
    ++generation;
    install(build(makeInput(redetect, false), nullptr));
}

void PastelProcessor::setEdits(const SampleEdits& e, bool rebuildNow, bool redetect)
{
    {
        const juce::ScopedLock l(stateLock);
        edits = e;
    }
    if (rebuildNow) rebuild(redetect);
}

void PastelProcessor::toggleReverse()
{
    if (decoded == nullptr) return;
    SampleEdits e = edits;
    const int n = uiInstrument != nullptr ? uiInstrument->raw.getNumSamples() : decoded->audio.getNumSamples();
    e.reverse = !e.reverse;
    int s = e.start, en = e.end < 0 ? n : e.end;
    e.start = n - en;
    e.end = n - s;
    e.loopStart = e.loopEnd = -1;
    e.anaStart = e.anaEnd = -1;
    setEdits(e, true, true);
}

void PastelProcessor::toggleNormalize()
{
    SampleEdits e = edits;
    e.normalize = !e.normalize;
    setEdits(e, true, false);
}

void PastelProcessor::trimToSelection()
{
    if (uiInstrument == nullptr) return;
    SampleEdits e = edits;
    const int len = uiInstrument->raw.getNumSamples();
    const int s = juce::jlimit(0, len, e.start), en = juce::jlimit(s, len, e.end < 0 ? len : e.end);
    if (en - s < 16 || (s == 0 && en == len)) return;
    // selection is in (possibly reversed) cropped coordinates; map back to the decoded file
    const int base = e.cropStart;
    e.cropStart = base + (e.reverse ? len - en : s);
    e.cropEnd = base + (e.reverse ? len - s : en);
    e.start = 0;
    e.end = -1;
    e.loopStart = e.loopEnd = -1;
    e.anaStart = e.anaEnd = -1;
    setEdits(e, true, true);
}

void PastelProcessor::untrim()
{
    SampleEdits e = edits;
    if (e.cropStart == 0 && e.cropEnd < 0) return;
    e = SampleEdits();
    e.reverse = edits.reverse;
    e.normalize = edits.normalize;
    setEdits(e, true, true);
}

void PastelProcessor::findLoopNow()
{
    if (decoded == nullptr) return;
    // finding a loop also switches looping on
    if (auto* loopP = apvts.getParameter(PP::LOOP_ON); loopP != nullptr && loopP->getValue() < 0.5f)
        loopP->setValueNotifyingHost(1.0f);
    SampleEdits e = edits;
    e.loopStart = e.loopEnd = -1;
    {
        const juce::ScopedLock l(stateLock);
        edits = e;
    }
    BuildInput in = makeInput(false, false);
    in.autoLoop = true;
    if (!asyncLoads) { ++generation; install(build(in, nullptr)); return; }
    const int gen = ++generation;
    rebuildPending = false;
    std::weak_ptr<bool> token = alive;
    startJob([this, in, gen, token]
    {
        auto out = build(in, nullptr);
        juce::MessageManager::callAsync([this, out, gen, token]
        {
            if (auto a = token.lock(); a && *a && gen == generation) install(out);
        });
    });
}

void PastelProcessor::resetPartialGains()
{
    for (auto& g : partialGains) g.store(1.0f);
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// preview
void PastelProcessor::previewFile(const juce::String& path)
{
    const int gen = ++previewGen;
    std::weak_ptr<bool> token = alive;
    previewPool.addJob([this, path, gen, token]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(juce::File(path)));
        if (reader == nullptr || gen != previewGen) return;
        const int len = (int) juce::jmin(reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
        auto buf = std::make_shared<juce::AudioBuffer<float>>(2, juce::jmax(1, len));
        buf->clear();
        const int nch = (int) reader->numChannels;
        juce::AudioBuffer<float> tmp(juce::jmax(1, nch), juce::jmax(1, len));
        reader->read(&tmp, 0, len, 0, true, nch > 1);
        buf->copyFrom(0, 0, tmp, 0, 0, len);
        buf->copyFrom(1, 0, tmp, nch > 1 ? 1 : 0, 0, len);
        const double sr = reader->sampleRate;
        std::vector<float> peaks(150, 0.0f);
        for (int c = 0; c < 150; ++c)
        {
            int s0 = (int) ((juce::int64) c * len / 150), s1 = (int) ((juce::int64) (c + 1) * len / 150);
            peaks[(size_t) c] = buf->getMagnitude(s0, juce::jmax(1, s1 - s0));
        }
        juce::MessageManager::callAsync([this, buf, sr, gen, token, peaks]
        {
            auto a = token.lock();
            if (!a || !*a || gen != previewGen) return;
            {
                const juce::SpinLock::ScopedLockType l(previewLock);
                previewNext = { buf, sr };
                previewSwap = true;
            }
            previewUi = buf;
            previewPeaks = peaks;
            previewActive = true;
        });
    });
}

void PastelProcessor::stopPreview()
{
    ++previewGen;
    previewStopReq = true;
    previewActive = false;
}

std::vector<float> PastelProcessor::getPreviewPeaks() const { return previewPeaks; }
float PastelProcessor::getPreviewPosition() const { return previewProgress.load(); }

std::vector<float> PastelProcessor::getGrainPositions() const
{
    std::vector<float> v;
    const int c = juce::jlimit(0, 64, engine.grainVizCount.load());
    for (int i = 0; i < c; ++i) v.push_back(engine.grainViz[(size_t) i].load());
    return v;
}

std::vector<int> PastelProcessor::getActiveNotes() const
{
    std::vector<int> v;
    uint64_t lo = engine.activeLo.load(), hi = engine.activeHi.load();
    for (int n = 0; n < 64; ++n) if (lo & (1ull << n)) v.push_back(n);
    for (int n = 0; n < 64; ++n) if (hi & (1ull << n)) v.push_back(n + 64);
    return v;
}

// ---------------------------------------------------------------------------
void PastelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int num = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer(midi, 0, num, true);

    if (pendingFlag.load())
    {
        const juce::SpinLock::ScopedTryLockType l(pendingLock);
        if (l.isLocked())
        {
            retire(engine.setInstrument(std::move(pendingInst)));
            pendingInst = nullptr;
            pendingFlag = false;
        }
    }
    if (panicRequested.exchange(false)) engine.allNotesOff(true);

    const EngineParams p = snapshotParams();
    if (buffer.getNumChannels() > 0)
    {
        // render into a stereo scratch view even for mono outputs
        juce::AudioBuffer<float> stereo;
        juce::AudioBuffer<float>* target = &buffer;
        float* chans[2];
        float monoScratch[2048];
        juce::HeapBlock<float> big;
        if (buffer.getNumChannels() == 1)
        {
            float* r = monoScratch;
            if (num > 2048) { big.calloc((size_t) num); r = big.get(); }
            else std::fill(monoScratch, monoScratch + num, 0.0f);
            chans[0] = buffer.getWritePointer(0);
            chans[1] = r;
            stereo.setDataToReferTo(chans, 2, num);
            target = &stereo;
        }

        int pos = 0;
        for (const auto md : midi)
        {
            const int t = juce::jlimit(0, num, md.samplePosition);
            if (t > pos) { engine.render(*target, pos, t - pos, p); pos = t; }
            const auto m = md.getMessage();
            if (m.isNoteOn())
            {
                if (learnArmed.exchange(false)) learnedNote.store(m.getNoteNumber());
                engine.noteOn(m.getNoteNumber(), m.getFloatVelocity(), p);
            }
            else if (m.isNoteOff()) engine.noteOff(m.getNoteNumber(), p);
            else if (m.isPitchWheel()) engine.pitchBend((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
            else if (m.isSustainPedalOn()) engine.setSustain(true, p);
            else if (m.isSustainPedalOff()) engine.setSustain(false, p);
            else if (m.isAllSoundOff()) engine.allNotesOff(true);
            else if (m.isAllNotesOff()) engine.allNotesOff(false);
        }
        if (pos < num) engine.render(*target, pos, num - pos, p);

        // effects + master volume / mono / limiter (Pastel player DSP chain)
        DspParams dp = fxParams();
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm()) dp.bpm = *bpm;
        fx.process(*target, dp);
        {
            // master peak limiter, -0.3 dBFS ceiling (constant latency whether on or off)
            float* l = target->getWritePointer(0);
            float* r = target->getWritePointer(1);
            limiter.process(l, r, num, 0.9661f, apvts.getRawParameterValue(PP::LIMIT)->load() > 0.5f);
            limiterGr = limiter.reductionDb();
            float pkL = 0, pkR = 0;
            for (int i = 0; i < num; ++i) { pkL = juce::jmax(pkL, std::abs(l[i])); pkR = juce::jmax(pkR, std::abs(r[i])); }
            const float decay = std::pow(0.5f, (float) num / (float) (hostRate * 0.08));
            meters[0] = juce::jmax(pkL, meters[0].load() * decay);
            meters[1] = juce::jmax(pkR, meters[1].load() * decay);
        }

        // browser preview: the raw file, after the effects (dry audition)
        if (previewStopReq.exchange(false))
        {
            retire(std::move(previewCur));
            previewCur = nullptr;
        }
        if (previewSwap.load())
        {
            const juce::SpinLock::ScopedTryLockType l(previewLock);
            if (l.isLocked())
            {
                retire(std::move(previewCur));
                previewCur = std::move(previewNext.buf);
                previewNext.buf = nullptr;
                previewSr = previewNext.sr;
                previewPos = 0;
                previewSwap = false;
            }
        }
        if (previewCur != nullptr)
        {
            const float* l = previewCur->getReadPointer(0);
            const float* r = previewCur->getReadPointer(1);
            const int len = previewCur->getNumSamples();
            const double inc = previewSr / hostRate;
            float* oL = target->getWritePointer(0);
            float* oR = target->getWritePointer(1);
            for (int i = 0; i < num; ++i)
            {
                if (previewPos >= len - 1)
                {
                    retire(std::move(previewCur));
                    previewCur = nullptr;
                    previewActive = false;
                    break;
                }
                int i0 = (int) previewPos;
                float f = (float) (previewPos - i0);
                oL[i] += 0.5f * (l[i0] + f * (l[i0 + 1] - l[i0]));
                oR[i] += 0.5f * (r[i0] + f * (r[i0 + 1] - r[i0]));
                previewPos += inc;
            }
            previewProgress = previewCur != nullptr ? (float) (previewPos / len) : 0.0f;
        }
        if (buffer.getNumChannels() == 1)
        {
            float* oL = target->getWritePointer(0);
            const float* oR = target->getReadPointer(1);
            for (int i = 0; i < num; ++i) oL[i] = 0.5f * (oL[i] + oR[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// state
juce::ValueTree PastelProcessor::makeStateTree()
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName("SAMPLE"); old.isValid()) state.removeChild(old, nullptr);
    juce::ValueTree s("SAMPLE");
    {
        const juce::ScopedLock l(stateLock);
        s.setProperty("path", samplePath, nullptr);
        s.setProperty("reverse", edits.reverse, nullptr);
        s.setProperty("normalize", edits.normalize, nullptr);
        s.setProperty("cropStart", edits.cropStart, nullptr);
        s.setProperty("cropEnd", edits.cropEnd, nullptr);
        s.setProperty("start", edits.start, nullptr);
        s.setProperty("end", edits.end, nullptr);
        s.setProperty("loopStart", edits.loopStart, nullptr);
        s.setProperty("loopEnd", edits.loopEnd, nullptr);
        s.setProperty("anaStart", edits.anaStart, nullptr);
        s.setProperty("anaEnd", edits.anaEnd, nullptr);
    }
    juce::StringArray g;
    for (auto& v : partialGains) g.add(juce::String(v.load(), 3));
    s.setProperty("partialGains", g.joinIntoString(","), nullptr);
    state.addChild(s, -1, nullptr);
    return state;
}

void PastelProcessor::applyStateTree(const juce::ValueTree& tree)
{
    auto copy = tree.createCopy();
    auto s = copy.getChildWithName("SAMPLE");
    if (s.isValid()) copy.removeChild(s, nullptr);
    apvts.replaceState(copy);
    if (!s.isValid()) return;
    juce::StringArray g;
    g.addTokens(s.getProperty("partialGains").toString(), ",", "");
    for (int k = 0; k < 128; ++k)
        partialGains[(size_t) k].store(k < g.size() ? juce::jlimit(0.0f, 4.0f, g[k].getFloatValue()) : 1.0f);
    SampleEdits e;
    e.reverse = s.getProperty("reverse", false);
    e.normalize = s.getProperty("normalize", false);
    e.cropStart = s.getProperty("cropStart", 0);
    e.cropEnd = s.getProperty("cropEnd", -1);
    e.start = s.getProperty("start", 0);
    e.end = s.getProperty("end", -1);
    e.loopStart = s.getProperty("loopStart", -1);
    e.loopEnd = s.getProperty("loopEnd", -1);
    e.anaStart = s.getProperty("anaStart", -1.0);
    e.anaEnd = s.getProperty("anaEnd", -1.0);
    const juce::String path = s.getProperty("path").toString();
    if (path.isNotEmpty())
    {
        if (juce::File(path).existsAsFile()) loadInternal(path, &e, !asyncLoads);
        else
        {
            status = "Missing sample: " + path;
            sendChangeMessage();
        }
    }
}

void PastelProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = makeStateTree().createXml()) copyXmlToBinary(*xml, dest);
}

void PastelProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) applyStateTree(juce::ValueTree::fromXml(*xml));
}

juce::File PastelProcessor::presetFolder()
{
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Pastel Sampler/Presets");
    d.createDirectory();
    return d;
}

bool PastelProcessor::savePreset(const juce::File& f)
{
    if (auto xml = makeStateTree().createXml()) return xml->writeTo(f);
    return false;
}

bool PastelProcessor::loadPreset(const juce::File& f)
{
    auto xml = juce::XmlDocument::parse(f);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return false;
    applyStateTree(juce::ValueTree::fromXml(*xml));
    return true;
}

void PastelProcessor::resetToInit()
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm))
            r->setValueNotifyingHost(r->getDefaultValue());
    resetPartialGains();
}

juce::AudioProcessorEditor* PastelProcessor::createEditor() { return new PastelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PastelProcessor(); }
