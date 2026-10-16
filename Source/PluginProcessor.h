#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Analysis.h"
#include "SynthEngine.h"
#include "DSPEngine.h"
#include "Limiter.h"
#include "FileLibrary.h"
#include "Params.h"

class PastelProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    PastelProcessor();
    ~PastelProcessor() override;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Pastel Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- sample loading / editing (message thread) ----
    void loadSample(const juce::String& path);           // async
    bool loadSampleSync(const juce::String& path);       // harness / offline
    void rebuild(bool redetect);                         // async, after edits
    void rebuildSync(bool redetect);
    bool isLoading() const { return loadingCount.load() > 0; }
    float getLoadProgress() const { return loadProgress.load(); }
    juce::String getStatus() const { return status; }
    std::shared_ptr<const Instrument> getInstrument() const { return uiInstrument; }

    SampleEdits getEdits() const { return edits; }
    void setEdits(const SampleEdits& e, bool rebuildNow, bool redetect);
    void toggleReverse();
    void toggleNormalize();
    void trimToSelection(); // crop the sample to the S..E selection
    void untrim();          // restore the whole file
    void findLoopNow();

    float getPartialGain(int k) const { return partialGains[(size_t) k].load(); }
    void setPartialGain(int k, float g) { partialGains[(size_t) k].store(juce::jlimit(0.0f, 4.0f, g)); }
    void resetPartialGains();

    // effective root as used by the engine
    float getRootMidi() const;
    float getRootHz() const;

    // ---- MIDI learn ----
    void armRootLearn(bool on) { learnArmed.store(on); }
    bool isRootLearnArmed() const { return learnArmed.load(); }

    // ---- preview (browser audition) ----
    void previewFile(const juce::String& path);
    void stopPreview();
    bool isPreviewing() const { return previewActive.load(); }
    std::vector<float> getPreviewPeaks() const;
    float getPreviewPosition() const;

    // ---- presets ----
    static juce::File presetFolder();
    bool savePreset(const juce::File& f);
    bool loadPreset(const juce::File& f);
    void resetToInit();

    // ---- UI helpers ----
    void auditionNoteOn(int note, float vel) { keyboardState.noteOn(1, note, vel); }
    void auditionNoteOff(int note) { keyboardState.noteOff(1, note, 0.0f); }
    void panic() { panicRequested.store(true); }
    std::vector<int> getActiveNotes() const;
    std::vector<float> getGrainPositions() const; // normalised positions of live grains (UI)
    float getMeter(int ch) const { return meters[(size_t) juce::jlimit(0, 1, ch)].load(); }
    float getLimiterReductionDb() const { return limiterGr.load(); }
    EngineParams snapshotParams() const;
    int loopMode() const; // 0 off, 1 forward, 2 ping-pong, 3 sustain (LOOP toggle + loop type)

    juce::AudioProcessorValueTreeState apvts;
    FileLibrary library;
    RootScanner rootScanner;
    juce::MidiKeyboardState keyboardState;
    juce::AudioFormatManager formats;

    // test hook: render a block without a host
    void renderOffline(juce::AudioBuffer<float>& out, juce::MidiBuffer& midi) { processBlock(out, midi); }

private:
    struct Decoded
    {
        juce::String path, info;
        double sr = 44100.0;
        juce::AudioBuffer<float> audio;
    };
    struct BuildInput
    {
        std::shared_ptr<const Decoded> decoded;
        SampleEdits edits;
        Analysis::Algo algo = Analysis::Algo::YinHps;
        int quality = 1; // spectral FFT size is derived from this and the detected f0
        int loopMode = 0;
        float xfadeMs = 60;
        bool autoLoop = true, redetect = true, freshLoad = false;
        std::shared_ptr<const Instrument> previous; // reuse analyses when possible
    };
    struct BuildOutput
    {
        std::shared_ptr<Instrument> inst;
        SampleEdits edits;
    };

    static std::shared_ptr<const Decoded> decode(juce::AudioFormatManager& fm, const juce::String& path, juce::String& error);
    static BuildOutput build(const BuildInput& in, std::atomic<float>* progress);
    BuildInput makeInput(bool redetect, bool fresh) const;
    void install(BuildOutput out);
    void startJob(std::function<void()> job);

    void parameterChanged(const juce::String& id, float) override;
    void timerCallback() override;
    void retire(std::shared_ptr<const void> p);

    SynthEngine engine;
    DSPEngine fx;
    PeakLimiter limiter;
    DspParams fxParams() const;
    double hostRate = 44100.0;

    // instrument handoff to the audio thread
    juce::SpinLock pendingLock;
    std::shared_ptr<const Instrument> pendingInst;
    std::atomic<bool> pendingFlag { false };
    std::shared_ptr<const Instrument> uiInstrument;
    std::shared_ptr<const Decoded> decoded;
    SampleEdits edits;
    std::array<std::atomic<float>, 128> partialGains;

    // objects released by the audio thread, freed on the message thread
    std::array<std::shared_ptr<const void>, 64> retireSlots;
    juce::AbstractFifo retireFifo { 64 };

    juce::ThreadPool pool { 1 };
    std::atomic<int> generation { 0 }, loadingCount { 0 };
    std::atomic<float> loadProgress { 1.0f };
    juce::String status { juce::String(juce::CharPointer_UTF8("No sample loaded — browse on the left, double-click to load")) };

    std::atomic<bool> rebuildPending { false }, redetectPending { false };
    std::atomic<bool> learnArmed { false }, panicRequested { false };
    std::atomic<int> learnedNote { -1 };
    std::array<std::atomic<float>, 2> meters {};
    std::atomic<float> limiterGr { 0.0f };

    // preview
    struct Preview { std::shared_ptr<const juce::AudioBuffer<float>> buf; double sr = 44100.0; };
    juce::SpinLock previewLock;
    Preview previewNext;
    std::atomic<bool> previewSwap { false }, previewActive { false }, previewStopReq { false };
    std::shared_ptr<const juce::AudioBuffer<float>> previewCur, previewUi;
    double previewSr = 44100.0, previewPos = 0;
    std::atomic<float> previewProgress { 0 };
    std::atomic<int> previewGen { 0 };

    juce::CriticalSection stateLock; // guards samplePath + edits for getStateInformation
    juce::String samplePath;
    std::vector<float> previewPeaks;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    juce::ThreadPool previewPool { 1 };
    bool asyncLoads = true;
    void loadInternal(const juce::String& path, const SampleEdits* restore, bool sync);
    void applyStateTree(const juce::ValueTree& tree);
    juce::ValueTree makeStateTree();
    float rootMidiFor(const Instrument* inst) const;
public:
    void setAsyncLoads(bool a) { asyncLoads = a; } // harness: run builds inline
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PastelProcessor)
};
