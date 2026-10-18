#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs + choice lists shared by processor, engine and editor.
namespace PP
{
    // header / master
    inline const juce::String VOLUME   = "volume";
    inline const juce::String QUALITY  = "quality";
    // root note
    inline const juce::String ROOTOVR  = "rootOverride";
    inline const juce::String ROOTNOTE = "rootNote";
    inline const juce::String ROOTFINE = "rootFine";
    inline const juce::String ALGO     = "algo";
    // engines
    // engine selector: one engine, or Blend (the three levels act as a mixer)
    inline const juce::String ENGINE   = "engine";
    inline const juce::String S_INTERP = "sampInterp", LOOP_ON = "loopOn", LOOP_TYPE = "loopType",
                              S_START = "sampStart", S_XFADE = "sampXfade", S_TUNE = "sampTune",
                              S_LEVEL = "sampLevel", S_AUTOLOOP = "sampAutoLoop";
    inline const juce::String A_PARTIALS = "addPartials", A_TILT = "addTilt",
                              A_NOISE = "addNoise", A_LEVEL = "addLevel";
    inline const juce::String G_POS = "grainPos", G_SCAN = "grainScan", G_SIZE = "grainSize", G_DENS = "grainDensity",
                              G_SPRAY = "grainSpray", G_JITTER = "grainJitter", G_WIDTH = "grainWidth",
                              G_SHAPE = "grainShape", G_LEVEL = "grainLevel";
    inline const juce::String P_SHIFT = "specShift", P_BLUR = "specBlur",
                              P_FREEZE = "specFreeze", P_LEVEL = "specLevel";
    // envelope
    inline const juce::String E_ON = "envOn", E_A = "envA", E_D = "envD", E_S = "envS",
                              E_R = "envR", E_CURVE = "envCurve";
    // filter
    inline const juce::String F_ON = "filtOn", F_CUT = "filtCutoff", F_RES = "filtReso",
                              F_DRIVE = "filtDrive", F_KEY = "filtKeytrack", F_TYPE = "filtType",
                              F_SLOPE = "filtSlope";
    // voice & keymap
    inline const juce::String V_MODE = "voiceMode", V_VOICES = "voices", V_GLIDE = "glide",
                              V_BEND = "bendRange", V_SPREAD = "spread", V_FORMANT = "formant",
                              V_KEEPLEN = "keepLength", V_RR = "roundRobin", V_MONO = "monoOut",
                              V_TUNING = "tuning", V_TONIC = "tuningTonic", V_A4 = "a4", V_LOKEY = "lowKey", V_HIKEY = "highKey";
    // effects + master (same IDs / ranges as the Pastel Soundfont Player)
    inline const juce::String CHON = "chon", CHMODE = "chmode", CHRATE = "chrate", CHDEPTH = "chdepth", CHMIX = "chmix";
    inline const juce::String PHON = "phon", PHRATE = "phrate", PHDEPTH = "phdepth", PHFB = "phfb", PHMIX = "phmix";
    inline const juce::String FLON = "flon", FLRATE = "flrate", FLDEPTH = "fldepth", FLFB = "flfb", FLMIX = "flmix";
    inline const juce::String DION = "dion", DIDRIVE = "didrive", DIMIX = "dimix";
    inline const juce::String SAON = "saon", SAAMT = "saamt", SATONE = "satone", SAMODE = "samode";
    inline const juce::String RVON = "rvon", RVSIZE = "rvsize", RVDAMP = "rvdamp", RVMIX = "rvmix";
    inline const juce::String DLON = "dlon", DLTIME = "dltime", DLFB = "dlfb", DLMIX = "dlmix", DLSYNC = "dlsync", DLDIV = "dldiv";
    inline const juce::String LIMIT = "limit";
    // arpeggiator (MIDI arp ahead of the engine)
    inline const juce::String ARP_ON = "arpOn", ARP_MODE = "arpMode", ARP_DIV = "arpDiv", ARP_OCT = "arpOct",
                              ARP_GATE = "arpGate", ARP_SWING = "arpSwing", ARP_LATCH = "arpLatch", ARP_VEL = "arpVel";
    inline const juce::StringArray arpModes { "Up", "Down", "Up-Down", "Random" };
    inline const juce::StringArray arpDivs { "1/4", "1/4T", "1/4 D", "1/8", "1/8T", "1/8 D",
                                             "1/16", "1/16T", "1/16 D", "1/32", "1/32T" };
    // modulation: 2 LFOs + an 8-slot matrix (source, amount, destination), after the Monosynth matrix
    inline const juce::String L_RATE[2] { "lfo1Rate", "lfo2Rate" }, L_WAVE[2] { "lfo1Wave", "lfo2Wave" },
                              L_SYNC[2] { "lfo1Sync", "lfo2Sync" }, L_DIV[2] { "lfo1Div", "lfo2Div" };
    inline const juce::StringArray lfoWaves { "Sine", "Triangle", "Saw", "Square", "S&H" };
    inline const juce::StringArray lfoDivs { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/32" };
    inline const double lfoDivBeats[] { 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
    static constexpr int kModSlots = 8;
    inline juce::String modSrcId(int i) { return "mod" + juce::String(i) + "src"; }
    inline juce::String modAmtId(int i) { return "mod" + juce::String(i) + "amt"; }
    inline juce::String modDstId(int i) { return "mod" + juce::String(i) + "dst"; }
    // order = ModSrc / ModDst in SynthEngine.h
    inline const juce::StringArray modSources { "None", "LFO 1", "LFO 2", "Mod Wheel", "Aftertouch", "Velocity", "Key", "Random" };
    inline const juce::StringArray modDests { "None", "Pitch", "Cutoff", "Resonance", "Level", "Pan",
                                              "Grain Pos", "Grain Size", "Grain Density", "Grain Spray",
                                              "Formant Shift", "Smear", "Freeze", "Tilt" };
    inline const juce::StringArray chorusModes { "Manual", "Vintage I", "Vintage II", "Wide" };
    inline const juce::StringArray satModes { "Tape", "Tube" };
    inline const juce::StringArray delayDivs { "1/4", "1/8", "1/8 Dotted", "1/4 Triplet", "1/16", "1/2" };

    inline const juce::StringArray qualityChoices { "Eco", "HQ", "Ultra" };
    inline const juce::StringArray algoChoices { "YIN", "HPS", "YIN + HPS" };
    inline const juce::StringArray engineChoices { "Sampler", "Additive", "Spectral", "Granular", "Blend" };
    inline const int kGranular = 3, kBlend = 4, kNumEngines = 4;
    inline const juce::StringArray interpChoices { "Linear", "Cubic", "Sinc HQ" };
    inline const juce::StringArray loopChoices { "Off", "Forward", "Ping-pong", "Sustain" }; // engine-side loop modes
    inline const juce::StringArray loopTypeChoices { "Forward", "Ping-pong", "Sustain" };
    inline const juce::StringArray filterTypeChoices { "Low Pass", "High Pass", "Band Pass" };
    inline const juce::StringArray slopeChoices { "12 dB", "24 dB" };
    inline const juce::StringArray modeChoices { "Poly", "Mono", "Legato" };
    inline const juce::StringArray tuningChoices { "12-TET", "Just", "Pythagorean" };
    // tonic of the Just / Pythagorean scales (pitch class)
    inline const juce::StringArray tonicChoices { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const juce::StringArray a4Choices { "415 Hz", "430 Hz", "432 Hz", "435 Hz", "438 Hz",
                                               "440 Hz", "442 Hz", "443 Hz", "444 Hz", "446 Hz" };
    inline const float a4Values[] { 415, 430, 432, 435, 438, 440, 442, 443, 444, 446 };

    // Spectral FFT size: enough pitch periods per frame to resolve the harmonics (bass needs
    // long frames), never below the quality's minimum (short frames keep attacks sharp).
    inline int spectralMinFftSize(int quality) { return quality <= 0 ? 1024 : (quality == 1 ? 2048 : 4096); }
    inline int spectralFftSize(int quality, double sr, float f0)
    {
        const int q = quality <= 0 ? 0 : (quality == 1 ? 1 : 2);
        const int minN = q == 0 ? 1024 : (q == 1 ? 2048 : 4096);
        const double periods = q == 0 ? 4.0 : (q == 1 ? 6.0 : 8.0);
        int N = minN;
        if (f0 > 0.0f)
            while (N < 16384 && N < periods * sr / f0) N <<= 1;
        return N;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // "C4" naming (MIDI 60 = C4, same as the Pastel player keyboard)
    juce::String noteName(int midi);
    juce::StringArray allNoteNames();
}
