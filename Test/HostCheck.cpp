// VST3 host check (dev only, never shipped): loads the BUILT .vst3 through JUCE's VST3
// hosting, like a DAW would: buses, parameters, latency, a sample load + note, state, editor.
//   PastelSamplerHostCheck <path/to/Pastel Sampler.vst3>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <cstdio>
#include <cstdlib>

// setenv is POSIX only; MSVC has _putenv_s
static void setEnvVar(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { printf("usage: PastelSamplerHostCheck <plugin.vst3>\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what)
    {
        printf("%s  %s\n", ok ? "  ok " : "FAIL ", what.toRawUTF8());
        if (!ok) ++fails;
    };
    auto pump = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };

    // a short harmonic tone the plugin auto-loads on construction (PASTEL_AUTOLOAD dev hook);
    // settings go to a scratch folder so the user's library is never touched
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("PastelSamplerHostCheck");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setEnvVar("PASTEL_SETTINGS_FOLDER", tmp.getChildFile("settings").getFullPathName().toRawUTF8());
    auto wav = tmp.getChildFile("tone220.wav");
    {
        const double sr = 48000.0;
        juce::AudioBuffer<float> b(2, (int) sr);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            double v = 0;
            for (int k = 1; k <= 8; ++k) v += std::sin(juce::MathConstants<double>::twoPi * 220.0 * k * t) / k;
            const float s = (float) (0.3 * v * juce::jmin(1.0, t / 0.005) * std::exp(-t / 1.5));
            b.setSample(0, i, s);
            b.setSample(1, i, s);
        }
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), sr, 2, 24, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    }
    setEnvVar("PASTEL_AUTOLOAD", wav.getFullPathName().toRawUTF8());

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile(descs, juce::File(argv[1]).getFullPathName());
    check(descs.size() == 1, "VST3 scanned: " + juce::String(descs.size()) + " plugin(s)");
    if (descs.isEmpty()) return 1;
    auto& d = *descs[0];
    check(d.isInstrument, "registered as instrument: " + d.name + " / " + d.category);
    juce::String err;
    auto inst = fmt.createInstanceFromDescription(d, 48000.0, 512, err);
    check(inst != nullptr, "instantiated " + err);
    if (!inst) return 1;
    check(inst->getBusCount(false) == 1 && inst->getBusCount(true) == 0,
          "buses: " + juce::String(inst->getBusCount(true)) + " in / " + juce::String(inst->getBusCount(false)) + " out");
    check(inst->getParameters().size() >= 100, "parameters: " + juce::String(inst->getParameters().size()));
    inst->prepareToPlay(48000.0, 512);
    const int nch = inst->getTotalNumOutputChannels();
    check(nch == 2, "stereo output (" + juce::String(nch) + " ch)");
    check(inst->getLatencySamples() > 0, "limiter latency reported: " + juce::String(inst->getLatencySamples()) + " samples");

    // play a note until the (re)loaded sample sounds: builds run on a background thread,
    // then install on the message thread
    juce::AudioBuffer<float> buf(juce::jmax(2, nch), 512);
    bool finite = true;
    auto playUntilSound = [&]
    {
        float peak = 0.0f;
        for (int attempt = 0; attempt < 100 && peak < 0.01f; ++attempt)
        {
            pump(50);
            for (int b = 0; b < 12; ++b)
            {
                juce::MidiBuffer mb;
                if (b == 0) mb.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8) 110), 0);
                if (b == 10) mb.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
                buf.clear();
                inst->processBlock(buf, mb);
                for (int c = 0; c < buf.getNumChannels(); ++c)
                {
                    peak = juce::jmax(peak, buf.getMagnitude(c, 0, buf.getNumSamples()));
                    for (int i = 0; i < buf.getNumSamples(); ++i) finite = finite && std::isfinite(buf.getSample(c, i));
                }
            }
        }
        return peak;
    };
    const float peak = playUntilSound();
    check(peak > 0.01f && peak <= 1.0f, "auto-loaded sample plays a note (peak " + juce::String(peak, 3) + ")");
    check(finite, "output is finite");

    // aftertouch / mod wheel / pitch bend in a block must not break anything
    {
        juce::MidiBuffer mb;
        mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        mb.addEvent(juce::MidiMessage::controllerEvent(1, 1, 90), 64);
        mb.addEvent(juce::MidiMessage::channelPressureChange(1, 70), 128);
        mb.addEvent(juce::MidiMessage::pitchWheel(1, 12000), 200);
        mb.addEvent(juce::MidiMessage::noteOff(1, 60), 400);
        buf.clear();
        inst->processBlock(buf, mb);
        check(std::isfinite(buf.getMagnitude(0, 0, 512)), "controllers mid-block");
    }

    juce::MemoryBlock st;
    inst->getStateInformation(st);
    check(st.getSize() > 1000, "state saved (" + juce::String((int) st.getSize()) + " bytes)");
    inst->setStateInformation(st.getData(), (int) st.getSize());
    const float peak2 = playUntilSound();
    check(peak2 > 0.01f, "state restored: the sample reloads and plays (peak " + juce::String(peak2, 3) + ")");

    std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
    check(ed != nullptr && ed->getWidth() > 600 && ed->getHeight() > 300,
          "editor opens (" + (ed ? juce::String(ed->getWidth()) + "x" + juce::String(ed->getHeight()) : juce::String("none")) + ")");
    pump(100);
    ed.reset();
    inst->releaseResources();
    inst.reset();
    tmp.deleteRecursively();
    printf(fails == 0 ? "HOST CHECK PASSED\n" : "%d HOST CHECK(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
