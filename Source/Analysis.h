#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <memory>
#include <vector>

// ============================================================
// Offline analysis of the single loaded sample. Everything here
// runs on the loader thread; the results are immutable once an
// Instrument is published to the audio thread.
// ============================================================

struct PitchResult
{
    float f0 = 0.0f;         // Hz, 0 = unpitched
    float confidence = 0.0f; // 0..1
    float midi = 0.0f;       // fractional MIDI note (69 = A4 440)
    int nearestNote() const { return (int) std::lround(midi); }
    float cents() const { return (midi - (float) nearestNote()) * 100.0f; }
    bool pitched() const { return f0 > 0.0f && confidence >= 0.25f; }
};

struct PitchTrack
{
    std::vector<float> hz;    // per frame, 0 = unvoiced
    int hop = 0;              // in source samples
    int firstSample = 0;      // sample index of frame 0 centre
};

// Time-varying harmonic amplitudes for the additive engine.
struct AdditiveData
{
    double sr = 44100.0;
    float f0 = 0.0f;
    int hop = 512;
    int window = 2048;            // analysis FFT size
    int numFrames = 0;
    int numPartials = 0;
    std::vector<float> ratio;     // measured frequency / f0 for each partial (inharmonicity)
    std::vector<float> amps;      // numFrames * numPartials, linear peak amplitude
    std::vector<float> phase;     // numFrames * numPartials, cosine phase at the frame centre
    std::vector<float> rms;       // per frame, for the noise layer
    std::vector<float> meanAmps;  // for display
    float amp(int frame, int k) const { return amps[(size_t) frame * (size_t) numPartials + (size_t) k]; }
};

// STFT magnitude / true-frequency frames for the spectral engine.
struct SpectralData
{
    double sr = 44100.0;
    int N = 2048, hop = 512, bins = 1025, numFrames = 0;
    float f0 = 0.0f;         // detected fundamental (0 = unpitched): enables harmonic phase locking
    std::vector<float> mag;  // numFrames * bins
    std::vector<float> freq; // true frequency in (fractional) bins
    std::vector<float> env;  // smoothed spectral envelope (formants)
    std::vector<float> phase; // analysis phase (for phase locking / resets)
    std::vector<uint8_t> transient; // per frame: onset detected (spectral flux)
    const float* magAt(int f) const { return mag.data() + (size_t) f * (size_t) bins; }
    const float* freqAt(int f) const { return freq.data() + (size_t) f * (size_t) bins; }
    const float* envAt(int f) const { return env.data() + (size_t) f * (size_t) bins; }
    const float* phaseAt(int f) const { return phase.data() + (size_t) f * (size_t) bins; }
};

// Non-destructive edits to the decoded file (stored in plugin state).
struct SampleEdits
{
    bool reverse = false, normalize = false;
    int cropStart = 0, cropEnd = -1;    // TRIM: kept region of the decoded file (decoded coordinates)
    int start = 0, end = -1;            // end = -1 -> length
    int loopStart = -1, loopEnd = -1;   // -1 -> auto
    double anaStart = -1.0, anaEnd = -1.0; // analysis window (samples), -1 -> auto
    bool valid(int len) const { return end > start && end <= len; }
};

// Everything the voices need, built once per load / edit.
struct Instrument
{
    juce::String path, name, formatInfo;
    juce::String audioKey, anaKey; // cache keys for re-using analyses across rebuilds
    double sr = 44100.0;
    juce::AudioBuffer<float> audio;   // edited, 2 channels; loop crossfade baked in when looping
    juce::AudioBuffer<float> raw;     // edited, no bake (display + ping-pong)
    int start = 0, end = 0, loopStart = 0, loopEnd = 0;
    bool loopBaked = false;
    double anaStart = 0, anaEnd = 0;
    PitchResult pitch;
    PitchTrack track;
    std::shared_ptr<const AdditiveData> additive;
    std::shared_ptr<const SpectralData> spectral;  // left (or mono)
    std::shared_ptr<const SpectralData> spectralR; // right; same object as `spectral` for mono files
    std::vector<float> peaksMin, peaksMax; // display overview (for raw)
};

namespace Analysis
{
    enum class Algo { Yin = 0, Hps = 1, YinHps = 2 };

    // mono mix helper
    std::vector<float> mixToMono(const juce::AudioBuffer<float>& b, int start, int end);

    PitchResult detectPitch(const float* mono, int n, double sr, Algo algo);
    PitchTrack trackPitch(const float* mono, int n, double sr, int maxFrames = 300);

    std::shared_ptr<AdditiveData> analyseAdditive(const float* mono, int n, double sr, float f0, int maxPartials = 128);
    std::shared_ptr<SpectralData> analyseSpectral(const float* mono, int n, double sr, int fftSize, float f0);

    // default analysis window: skip the attack, take the steady part
    void defaultAnalysisWindow(const float* mono, int n, double sr, double& a0, double& a1);
    // trim to the audible region
    void findTrim(const float* mono, int n, int& start, int& end);
    // automatic loop points (zero-crossing snapped, correlation matched)
    bool findLoop(const float* mono, int n, double sr, float f0, int start, int end, int& ls, int& le);

    // bake an equal-power crossfade before loopEnd so the jump to loopStart is seamless
    int bakeLoopCrossfade(juce::AudioBuffer<float>& b, int loopStart, int loopEnd, int xfadeSamples);

    void computePeaks(const juce::AudioBuffer<float>& b, int columns, std::vector<float>& mn, std::vector<float>& mx);

    inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(hz / 440.0f); }
    inline float midiToHz(float m) { return 440.0f * std::pow(2.0f, (m - 69.0f) / 12.0f); }
}
