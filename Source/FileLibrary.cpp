#include "FileLibrary.h"
#include "Analysis.h"
#include "Params.h"
#include <cstdlib>

FileLibrary::FileLibrary()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "PastelSampler";
    opts.filenameSuffix = ".settings";
    juce::String folder = "Pastel Sampler";
    // test hook: isolated settings for the headless harness
    if (const char* e = std::getenv("PASTEL_SETTINGS_FOLDER")) if (*e != 0) folder = e;
    opts.folderName = folder;
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);
    load();
}

void FileLibrary::load()
{
    shortcuts.clear();
    if (auto* f = props.getUserSettings())
    {
        for (int i = 0; i < 64; ++i)
        {
            auto d = f->getValue("shortcut" + juce::String(i), "");
            if (d.isEmpty()) break;
            if (juce::File(d).isDirectory() && !shortcuts.contains(d)) shortcuts.add(d);
        }
        currentDir = f->getValue("currentDir", "");
        lastFile = f->getValue("lastFile", "");
    }
    if (!juce::File(currentDir).isDirectory())
    {
        if (juce::File(lastFile).existsAsFile()) currentDir = juce::File(lastFile).getParentDirectory().getFullPathName();
        else if (shortcuts.size() > 0) currentDir = shortcuts[0];
        else currentDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
    }
}

void FileLibrary::save()
{
    if (auto* f = props.getUserSettings())
    {
        for (int i = 0; i < 64; ++i) f->removeValue("shortcut" + juce::String(i));
        for (int i = 0; i < shortcuts.size(); ++i) f->setValue("shortcut" + juce::String(i), shortcuts[i]);
        f->setValue("currentDir", currentDir);
        f->setValue("lastFile", lastFile);
        f->saveIfNeeded();
    }
}

void FileLibrary::addShortcut(const juce::String& path)
{
    juce::File d(path);
    if (!d.isDirectory()) return;
    auto full = d.getFullPathName();
    if (!shortcuts.contains(full))
    {
        shortcuts.add(full);
        save();
        sendChangeMessage();
    }
}

void FileLibrary::removeShortcut(int index)
{
    if (index >= 0 && index < shortcuts.size())
    {
        shortcuts.remove(index);
        save();
        sendChangeMessage();
    }
}

void FileLibrary::setCurrentDir(const juce::String& path)
{
    if (juce::File(path).isDirectory() && path != currentDir)
    {
        currentDir = juce::File(path).getFullPathName();
        save();
        sendChangeMessage();
    }
}

bool FileLibrary::isAudioFile(const juce::File& f)
{
    auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".wave" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".ogg" || e == ".mp3";
}

std::vector<FileLibrary::Entry> FileLibrary::listEntries(const juce::String& filter) const
{
    std::vector<Entry> out;
    juce::File dir(currentDir);
    if (!dir.isDirectory()) return out;
    juce::Array<juce::File> children;
    dir.findChildFiles(children, juce::File::findFilesAndDirectories, false);
    juce::String flt = filter.trim();
    for (auto& c : children)
    {
        if (c.getFileName().startsWith(".")) continue;
        if (flt.isNotEmpty() && !c.getFileName().containsIgnoreCase(flt)) continue;
        if (c.isDirectory())
            out.push_back({ c.getFullPathName(), c.getFileName(), "DIR", true, true });
        else if (isAudioFile(c))
        {
            auto e = c.getFileExtension().toLowerCase();
            juce::String tag = e == ".wave" ? "WAV" : (e == ".aif" ? "AIFF" : e.substring(1).toUpperCase());
            bool lossless = !(e == ".mp3" || e == ".ogg");
            out.push_back({ c.getFullPathName(), c.getFileName(), tag, false, lossless });
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b)
    {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    return out;
}

void FileLibrary::setLastFile(const juce::String& path)
{
    lastFile = path;
    save();
}

bool FileLibrary::getAutoPreview()
{
    if (auto* f = props.getUserSettings()) return f->getBoolValue("autoPreview", true);
    return true;
}

void FileLibrary::setAutoPreview(bool on)
{
    if (auto* f = props.getUserSettings()) { f->setValue("autoPreview", on); f->saveIfNeeded(); }
}

double FileLibrary::getUiScale()
{
    if (auto* f = props.getUserSettings()) return f->getDoubleValue("uiScale", 1.0);
    return 1.0;
}

void FileLibrary::setUiScale(double s)
{
    if (auto* f = props.getUserSettings()) { f->setValue("uiScale", s); f->saveIfNeeded(); }
}

// ---------------------------------------------------------------------------
RootScanner::RootScanner() : juce::Thread("PastelRootScanner")
{
    formats.registerBasicFormats();   // the thread starts with the first request (see PastelProcessor)
}

RootScanner::~RootScanner()
{
    stopThread(3000);
}

void RootScanner::request(const juce::StringArray& paths)
{
    {
        std::lock_guard<std::mutex> l(mtx);
        queue.clear();
        for (auto& p : paths)
            if (results.find(p) == results.end()) queue.add(p);
    }
    if (! isThreadRunning()) startThread(juce::Thread::Priority::low);
    notify();
}

bool RootScanner::lookup(const juce::String& path, Result& out) const
{
    std::lock_guard<std::mutex> l(mtx);
    auto it = results.find(path);
    if (it == results.end()) return false;
    out = it->second;
    return true;
}

void RootScanner::run()
{
    while (!threadShouldExit())
    {
        juce::String path;
        {
            std::lock_guard<std::mutex> l(mtx);
            if (!queue.isEmpty()) { path = queue[0]; queue.remove(0); }
        }
        if (path.isEmpty()) { wait(500); continue; }

        Result r { juce::String(juce::CharPointer_UTF8("—")), 0.0f };
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(path)));
        if (reader != nullptr && reader->sampleRate > 0)
        {
            const int n = (int) juce::jmin<juce::int64>(reader->lengthInSamples, (juce::int64) (reader->sampleRate * 3.0));
            juce::AudioBuffer<float> b((int) juce::jmax(1u, reader->numChannels), juce::jmax(1, n));
            b.clear();
            reader->read(&b, 0, n, 0, true, true);
            auto mono = Analysis::mixToMono(b, 0, n);
            double a0, a1;
            Analysis::defaultAnalysisWindow(mono.data(), n, reader->sampleRate, a0, a1);
            auto pr = Analysis::detectPitch(mono.data() + (int) a0, (int) (a1 - a0), reader->sampleRate, Analysis::Algo::YinHps);
            if (pr.pitched())
            {
                r.label = PP::noteName(pr.nearestNote()) + (pr.confidence < 0.6f ? "?" : "");
                r.confidence = pr.confidence;
            }
        }
        {
            std::lock_guard<std::mutex> l(mtx);
            results[path] = r;
        }
        ++resultVersion;
    }
}
