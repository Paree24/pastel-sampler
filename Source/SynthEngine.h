#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Analysis.h"

// Mod matrix (order = PP::modSources / PP::modDests). LFOs are bipolar, the rest 0..1
// (Key: -1..1 around C4, Random: -1..1 per note). Amount 1 = the full range below.
enum ModSrc { MS_NONE, MS_LFO1, MS_LFO2, MS_WHEEL, MS_AT, MS_VEL, MS_KEY, MS_RAND, MS_COUNT };
enum ModDst { MD_NONE, MD_PITCH /* +-12 st */, MD_CUTOFF /* +-5 oct */, MD_RESO, MD_LEVEL /* x0..2 */, MD_PAN,
              MD_GPOS /* +-1 span */, MD_GSIZE /* x4^m */, MD_GDENS /* x4^m */, MD_GSPRAY /* +-200 ms */,
              MD_SHIFT /* +-12 st */, MD_BLUR, MD_FREEZE, MD_TILT /* +-12 dB/oct */, MD_COUNT };
struct ModRoute { int src = MS_NONE; float amt = 0.0f; int dst = MD_NONE; };

// Per-block parameter snapshot (read from the APVTS atomics once per block).
struct EngineParams
{
    float rootHz = 261.63f, rootMidi = 60.0f;
    int engine = 0; // 0 sampler, 1 additive, 2 spectral, 3 granular, 4 blend (levels = mixer)
    int interp = 2, loopMode = 0; float start = 0, sTune = 0, sLevel = 0;
    int partials = 48; float tilt = 0, noise = 0, aLevel = 0;
    float shift = 0, blur = 0, freeze = 0, pLevel = 0;
    float gPos = 0, gScan = 1, gSizeMs = 80, gDensity = 30, gSprayMs = 15, gJitter = 0, gWidth = 0.5f, gShape = 0, gLevel = 0;
    bool eOn = true; float A = 1, D = 400, S = 1, R = 300, curve = 0.5f;
    bool fOn = false; float cutoff = 8000, reso = 0.1f, drive = 0, keytrack = 0; int ftype = 0, slope = 1;
    int mode = 0, voices = 16; float glideMs = 0; int bendRange = 2; float spread = 0;
    bool formant = true, keepLen = true, rr = false, mono = false;
    int tuning = 0, tonic = 0; float a4 = 440; int loKey = 0, hiKey = 127;
    float volumeDb = -6;
    bool rootOverride = false; // rootHz is the user's: voices on an older instrument follow it too
    std::array<float, 128> partialGains {};
    // modulation
    float gPosMod = 0;                // grain position offset (span fraction), matrix-driven
    float lfoHz[2] { 2.0f, 0.5f };
    int lfoWave[2] {};
    std::array<ModRoute, 8> routes {};
};

namespace Blend
{
    // engine gains (sampler, additive, spectral): the selected engine, or all three in Blend
    void weights(const EngineParams& p, float& ws, float& wa, float& wp, float& wg);
    float noteHz(const EngineParams& p, double note);
}

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kSub = 32;
    static constexpr int kNumFft = 5; // spectral FFT orders 10..14 (1024 .. 16384)

    SynthEngine();
    ~SynthEngine();
    void prepare(double sampleRate, int maxBlock);
    // audio thread: swap in a new instrument; returns one to free off the audio thread (or null).
    // keepVoices: sounding notes finish on the instrument they started on (rebuilds after loop /
    // xfade / quality / algorithm edits); otherwise they are cut.
    std::shared_ptr<const Instrument> setInstrument(std::shared_ptr<const Instrument> inst, bool keepVoices = false);
    // audio thread: a lingering instrument no voice plays any more (call until null)
    std::shared_ptr<const Instrument> takeRetired();
    const Instrument* instrument() const { return inst.get(); }

    void noteOn(int note, float velocity, const EngineParams& p);
    void noteOff(int note, const EngineParams& p);
    void setSustain(bool down, const EngineParams& p);
    void pitchBend(float bipolar) { bend = bipolar; }
    void setModWheel(float v01) { modWheel = v01; }
    void setAftertouch(float v01) { aftertouch = v01; }
    // LFO value at the start of the last render call (UI)
    float lfoValue(int k) const { return lfoViz[k & 1].load(); }
    void allNotesOff(bool hard);

    // adds into out[ch][start .. start+num)
    void render(juce::AudioBuffer<float>& out, int start, int num, const EngineParams& p);

    std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
    // granular display: normalised positions of the newest granular voice's live grains
    std::array<std::atomic<float>, 64> grainViz {};
    std::atomic<int> grainVizCount { 0 };
    int numActiveVoices() const;

    struct Voice;
private:
    void startVoice(Voice& v, int note, float vel, const EngineParams& p, double fromPitch, bool glide);
    void releaseVoice(Voice& v);
    void publishActive();
    bool usedByVoice(const Instrument* in) const;
    static float rootHzOf(const Instrument& in); // detected root, as the processor derives it

    double sr = 44100.0;
    std::shared_ptr<const Instrument> inst;
    std::array<std::shared_ptr<const Instrument>, 4> lingering; // still played by older voices
    int lingerNext = 0;
    std::vector<std::unique_ptr<Voice>> voices;
    std::array<std::unique_ptr<juce::dsp::FFT>, kNumFft> ffts;
    std::array<float, kNumFft> fftInvScale {};
    std::vector<int> heldStack; // mono / legato
    std::array<bool, 128> sustained {};
    bool sustainDown = false;
    float bend = 0, modWheel = 0, aftertouch = 0;
    // LFOs: free-running phase (rate from the processor, tempo-synced there), one value per
    // kSub block of the current render call
    double lfoPhase[2] {};
    float lfoHold[2] {};
    std::vector<float> lfoBuf[2];
    std::array<std::atomic<float>, 2> lfoViz {};
    void advanceLfos(int num, const EngineParams& p);
    uint64_t ageCounter = 0;
    double lastPitch = -1;
    juce::Random rng;
};
