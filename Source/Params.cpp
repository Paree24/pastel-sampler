#include "Params.h"

namespace PP
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;
using NR = juce::NormalisableRange<float>;

static NR skewed(float lo, float hi, float centre)
{
    NR r(lo, hi);
    r.setSkewForCentre(centre);
    return r;
}

static juce::AudioParameterFloatAttributes unit(const juce::String& u, int decimals = 1)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel(u)
        .withStringFromValueFunction([decimals, u](float v, int) { return juce::String(v, decimals) + " " + u; });
}

juce::String noteName(int midi)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    midi = juce::jlimit(0, 127, midi);
    return juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);
}

juce::StringArray allNoteNames()
{
    juce::StringArray s;
    for (int i = 0; i < 128; ++i) s.add(noteName(i));
    return s;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto add = [&](auto p) { l.add(std::unique_ptr<juce::RangedAudioParameter>(p)); };
    auto noteAttr = juce::AudioParameterIntAttributes()
        .withStringFromValueFunction([](int v, int) { return noteName(v); });

    add(new APF({ VOLUME, 1 }, "Volume", NR(-60.0f, 6.0f, 0.1f), -6.0f, unit("dB")));
    add(new APC({ QUALITY, 1 }, "Quality", qualityChoices, 1));

    add(new APB({ ROOTOVR, 1 }, "Root Override", false));
    add(new API({ ROOTNOTE, 1 }, "Root Note", 0, 127, 60, noteAttr));
    add(new APF({ ROOTFINE, 1 }, "Root Fine", NR(-50.0f, 50.0f, 0.1f), 0.0f, unit("ct")));
    add(new APC({ ALGO, 1 }, "Detection", algoChoices, 2));

    add(new APC({ ENGINE, 1 }, "Engine", engineChoices, 0));

    add(new APC({ S_INTERP, 1 }, "Interpolation", interpChoices, 2));
    add(new APB({ LOOP_ON, 1 }, "Loop", false));
    add(new APC({ LOOP_TYPE, 1 }, "Loop Type", loopTypeChoices, 0));
    add(new APF({ S_START, 1 }, "Start", NR(0.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int)
                { return juce::String(v * 100.0f, 1) + " %"; })));
    add(new APF({ S_XFADE, 1 }, "Loop Xfade", skewed(0.0f, 500.0f, 60.0f), 60.0f, unit("ms", 0)));
    add(new APF({ S_TUNE, 1 }, "Sampler Tune", NR(-100.0f, 100.0f, 0.1f), 0.0f, unit("ct")));
    add(new APF({ S_LEVEL, 1 }, "Sampler Level", NR(-48.0f, 6.0f, 0.1f), 0.0f, unit("dB")));
    add(new APB({ S_AUTOLOOP, 1 }, "Auto Loop", true));

    add(new API({ A_PARTIALS, 1 }, "Partials", 1, 128, 48));
    add(new APF({ A_TILT, 1 }, "Tilt", NR(-12.0f, 12.0f, 0.1f), 0.0f, unit("dB/oct")));
    add(new APF({ A_NOISE, 1 }, "Noise", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APF({ A_LEVEL, 1 }, "Additive Level", NR(-48.0f, 6.0f, 0.1f), 0.0f, unit("dB")));

    add(new APF({ P_SHIFT, 1 }, "Formant Shift", NR(-12.0f, 12.0f, 0.1f), 0.0f, unit("st")));
    add(new APF({ P_BLUR, 1 }, "Smear", NR(0.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
                { return juce::String(2000.0f * v * v, 0) + " ms"; })));
    add(new APF({ P_FREEZE, 1 }, "Freeze", NR(0.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int)
                { return v >= 0.999f ? juce::String("hold") : juce::String((int) std::lround((1.0f - v) * 100.0f)) + "% speed"; })));
    add(new APF({ P_LEVEL, 1 }, "Spectral Level", NR(-48.0f, 6.0f, 0.1f), 0.0f, unit("dB")));

    add(new APF({ G_POS, 1 }, "Grain Position", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APF({ G_SCAN, 1 }, "Grain Scan", NR(-2.0f, 2.0f, 0.01f), 1.0f, unit("x", 2)));
    add(new APF({ G_SIZE, 1 }, "Grain Size", skewed(5.0f, 500.0f, 80.0f), 80.0f, unit("ms", 0)));
    add(new APF({ G_DENS, 1 }, "Grain Density", skewed(1.0f, 200.0f, 30.0f), 30.0f, unit("/s", 0)));
    add(new APF({ G_SPRAY, 1 }, "Grain Spray", skewed(0.0f, 1000.0f, 60.0f), 15.0f, unit("ms", 0)));
    add(new APF({ G_JITTER, 1 }, "Grain Pitch Jitter", skewed(0.0f, 1200.0f, 50.0f), 0.0f, unit("ct", 0)));
    add(new APF({ G_WIDTH, 1 }, "Grain Width", NR(0.0f, 1.0f), 0.5f, unit("", 2)));
    add(new APF({ G_SHAPE, 1 }, "Grain Shape", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APF({ G_LEVEL, 1 }, "Granular Level", NR(-48.0f, 6.0f, 0.1f), 0.0f, unit("dB")));

    add(new APB({ E_ON, 1 }, "Envelope On", true));
    add(new APF({ E_A, 1 }, "Attack", skewed(0.5f, 5000.0f, 80.0f), 1.0f, unit("ms", 0)));
    add(new APF({ E_D, 1 }, "Decay", skewed(1.0f, 10000.0f, 500.0f), 400.0f, unit("ms", 0)));
    add(new APF({ E_S, 1 }, "Sustain", NR(0.0f, 1.0f), 1.0f, unit("", 2)));
    add(new APF({ E_R, 1 }, "Release", skewed(5.0f, 10000.0f, 500.0f), 300.0f, unit("ms", 0)));
    add(new APF({ E_CURVE, 1 }, "Curve", NR(0.0f, 1.0f), 0.5f, unit("", 2)));

    add(new APB({ F_ON, 1 }, "Filter On", false));
    add(new APF({ F_CUT, 1 }, "Cutoff", skewed(20.0f, 20000.0f, 1000.0f), 8000.0f, unit("Hz", 0)));
    add(new APF({ F_RES, 1 }, "Resonance", NR(0.0f, 1.0f), 0.1f, unit("", 2)));
    add(new APF({ F_DRIVE, 1 }, "Drive", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APF({ F_KEY, 1 }, "Key Track", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APC({ F_TYPE, 1 }, "Filter Type", filterTypeChoices, 0));
    add(new APC({ F_SLOPE, 1 }, "Filter Slope", slopeChoices, 1));

    add(new APC({ V_MODE, 1 }, "Voice Mode", modeChoices, 0));
    add(new API({ V_VOICES, 1 }, "Voices", 1, 32, 16));
    add(new APF({ V_GLIDE, 1 }, "Glide", skewed(0.0f, 2000.0f, 200.0f), 0.0f, unit("ms", 0)));
    add(new API({ V_BEND, 1 }, "Bend Range", 0, 24, 2));
    add(new APF({ V_SPREAD, 1 }, "Spread", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APB({ V_FORMANT, 1 }, "Formant Preserve", false));
    add(new APB({ V_KEEPLEN, 1 }, "Keep Length", true));
    add(new APB({ V_RR, 1 }, "Round Robin", false));
    add(new APB({ V_MONO, 1 }, "Mono Out", false));
    add(new APC({ V_TUNING, 1 }, "Tuning", tuningChoices, 0));
    add(new APC({ V_TONIC, 1 }, "Tuning Tonic", tonicChoices, 0)); // C = the old fixed tonic
    add(new APC({ V_A4, 1 }, "A4", a4Choices, 5));
    add(new API({ V_LOKEY, 1 }, "Low Key", 0, 127, 21, noteAttr));
    add(new API({ V_HIKEY, 1 }, "High Key", 0, 127, 108, noteAttr));

    auto fx = [&](const juce::String& id, const juce::String& name, float lo, float hi, float def, const juce::String& u = {})
    {
        add(new APF({ id, 1 }, name, NR(lo, hi), def, unit(u, u == "ms" ? 0 : 2)));
    };
    add(new APB({ CHON, 1 }, "Chorus On", false));
    add(new APC({ CHMODE, 1 }, "Chorus Mode", chorusModes, 0));
    fx(CHRATE, "Chorus Rate", 0.05f, 10.0f, 1.2f, "Hz");
    fx(CHDEPTH, "Chorus Depth", 0.0f, 1.0f, 0.35f);
    fx(CHMIX, "Chorus Mix", 0.0f, 1.0f, 0.4f);
    add(new APB({ PHON, 1 }, "Phaser On", false));
    fx(PHRATE, "Phaser Rate", 0.05f, 10.0f, 0.6f, "Hz");
    fx(PHDEPTH, "Phaser Depth", 0.0f, 1.0f, 0.5f);
    fx(PHFB, "Phaser Feedback", 0.0f, 0.95f, 0.5f);
    fx(PHMIX, "Phaser Mix", 0.0f, 1.0f, 0.5f);
    add(new APB({ FLON, 1 }, "Flanger On", false));
    fx(FLRATE, "Flanger Rate", 0.05f, 10.0f, 0.4f, "Hz");
    fx(FLDEPTH, "Flanger Depth", 0.0f, 1.0f, 0.5f);
    fx(FLFB, "Flanger Feedback", -0.95f, 0.95f, 0.55f);
    fx(FLMIX, "Flanger Mix", 0.0f, 1.0f, 0.45f);
    add(new APB({ DION, 1 }, "Distortion On", false));
    fx(DIDRIVE, "Dist Drive", 0.0f, 1.0f, 0.45f);
    fx(DIMIX, "Dist Mix", 0.0f, 1.0f, 0.8f);
    add(new APB({ SAON, 1 }, "Saturation On", false));
    fx(SAAMT, "Sat Amount", 0.0f, 1.0f, 0.3f);
    fx(SATONE, "Sat Tone", 0.0f, 1.0f, 0.6f);
    add(new APC({ SAMODE, 1 }, "Sat Mode", satModes, 0));
    add(new APB({ RVON, 1 }, "Reverb On", false));
    fx(RVSIZE, "Reverb Size", 0.0f, 1.0f, 0.45f);
    fx(RVDAMP, "Reverb Damp", 0.0f, 1.0f, 0.5f);
    fx(RVMIX, "Reverb Mix", 0.0f, 1.0f, 0.3f);
    add(new APB({ DLON, 1 }, "Delay On", false));
    fx(DLTIME, "Delay Time", 1.0f, 2000.0f, 375.0f, "ms");
    fx(DLFB, "Delay Feedback", 0.0f, 0.9f, 0.35f);
    fx(DLMIX, "Delay Mix", 0.0f, 1.0f, 0.25f);
    add(new APB({ DLSYNC, 1 }, "Delay Sync", false));
    add(new APC({ DLDIV, 1 }, "Delay Division", delayDivs, 1));
    add(new APB({ LIMIT, 1 }, "Limiter", true));

    add(new APB({ ARP_ON, 1 }, "Arp On", false));
    add(new APC({ ARP_MODE, 1 }, "Arp Mode", arpModes, 0));
    add(new APC({ ARP_DIV, 1 }, "Arp Division", arpDivs, 6));
    add(new API({ ARP_OCT, 1 }, "Arp Octaves", 1, 4, 1));
    add(new APF({ ARP_GATE, 1 }, "Arp Gate", NR(0.1f, 1.0f), 0.8f, unit("", 2)));
    add(new APF({ ARP_SWING, 1 }, "Arp Swing", NR(0.0f, 1.0f), 0.0f, unit("", 2)));
    add(new APB({ ARP_LATCH, 1 }, "Arp Latch", false));
    add(new APB({ ARP_VEL, 1 }, "Arp Played Velocity", true));

    for (int k = 0; k < 2; ++k)
    {
        const juce::String n = "LFO " + juce::String(k + 1);
        add(new APF({ L_RATE[k], 1 }, n + " Rate", skewed(0.02f, 20.0f, 2.0f), k == 0 ? 2.0f : 0.5f, unit("Hz", 2)));
        add(new APC({ L_WAVE[k], 1 }, n + " Wave", lfoWaves, 0));
        add(new APB({ L_SYNC[k], 1 }, n + " Sync", false));
        add(new APC({ L_DIV[k], 1 }, n + " Division", lfoDivs, 4));
    }
    for (int k = 0; k < kModSlots; ++k)
    {
        const juce::String n = "Mod " + juce::String(k + 1);
        add(new APC({ modSrcId(k), 1 }, n + " Source", modSources, 0));
        add(new APF({ modAmtId(k), 1 }, n + " Amount", NR(-1.0f, 1.0f), 0.0f, unit("", 2)));
        add(new APC({ modDstId(k), 1 }, n + " Destination", modDests, 0));
    }
    return l;
}
}
