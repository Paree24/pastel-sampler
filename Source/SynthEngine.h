#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Analysis.h"

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
    int tuning = 0; float a4 = 440; int loKey = 0, hiKey = 127;
    float volumeDb = -6;
    std::array<float, 128> partialGains {};
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
    // audio thread: swap in a new instrument (kills sounding voices); returns the old one
    std::shared_ptr<const Instrument> setInstrument(std::shared_ptr<const Instrument> inst);
    const Instrument* instrument() const { return inst.get(); }

    void noteOn(int note, float velocity, const EngineParams& p);
    void noteOff(int note, const EngineParams& p);
    void setSustain(bool down, const EngineParams& p);
    void pitchBend(float bipolar) { bend = bipolar; }
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

    double sr = 44100.0;
    std::shared_ptr<const Instrument> inst;
    std::vector<std::unique_ptr<Voice>> voices;
    std::array<std::unique_ptr<juce::dsp::FFT>, kNumFft> ffts;
    std::array<float, kNumFft> fftInvScale {};
    std::vector<int> heldStack; // mono / legato
    std::array<bool, 128> sustained {};
    bool sustainDown = false;
    float bend = 0;
    uint64_t ageCounter = 0;
    double lastPitch = -1;
    juce::Random rng;
};
