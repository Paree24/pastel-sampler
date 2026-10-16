#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <map>
#include <mutex>

// ============================================================
// FileLibrary: folder shortcuts (persisted) + a simple browser
// over the current folder, listing audio files.
// (Adapted from the Pastel Soundfont Player.)
// ============================================================
class FileLibrary : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::String path, name, tag; // tag: DIR / WAV / FLAC / ...
        bool isDir = false;
        bool lossless = true;
    };

    FileLibrary();
    ~FileLibrary() override = default;

    juce::StringArray getShortcuts() const { return shortcuts; }
    void addShortcut(const juce::String& path);
    void removeShortcut(int index);

    juce::String getCurrentDir() const { return currentDir; }
    void setCurrentDir(const juce::String& path);
    std::vector<Entry> listEntries(const juce::String& filter = "") const;
    static bool isAudioFile(const juce::File& f);
    static juce::String wildcard() { return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3"; }

    juce::String getLastFile() const { return lastFile; }
    void setLastFile(const juce::String& path);

    bool getAutoPreview();
    void setAutoPreview(bool on);
    double getUiScale();
    void setUiScale(double s);

private:
    void load();
    void save();

    juce::ApplicationProperties props;
    juce::StringArray shortcuts;
    juce::String currentDir, lastFile;
};

// Background root-note detection for browser rows.
class RootScanner : private juce::Thread
{
public:
    struct Result { juce::String label; float confidence = 0; };
    RootScanner();
    ~RootScanner() override;
    void request(const juce::StringArray& paths); // replaces the queue
    bool lookup(const juce::String& path, Result& out) const;
    int version() const { return resultVersion.load(); }

private:
    void run() override;
    juce::AudioFormatManager formats;
    mutable std::mutex mtx;
    juce::StringArray queue;
    std::map<juce::String, Result> results;
    std::atomic<int> resultVersion { 0 };
};
