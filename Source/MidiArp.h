#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <limits>

// MIDI arpeggiator ahead of the engine: rewrites a block's MIDI so held keys become a
// pattern of notes on their exact samples. While the host transport runs, steps sit on its
// beat grid; otherwise the arp free-runs from the first key. Swing delays odd steps by up
// to half a step; latch keeps a chord playing after the keys go up; played velocity uses
// each key's velocity. Fixed storage: process() never allocates once prepare() ran.
// (Same timing model as the Pastel Soundfont Player arp.)
class MidiArp
{
public:
    struct Settings
    {
        bool on = false;
        int mode = 0;            // 0 up 1 down 2 up-down 3 random
        double stepBeats = 0.25; // step length in quarter notes
        int octaves = 1;
        float gate = 0.8f;       // fraction of a step
        float swing = 0.0f;      // 0..1: odd steps late by swing * half a step
        bool latch = false;
        bool playedVel = true;   // off: fixed velocity
    };
    struct Host { double bpm = 120.0, ppq = 0.0; bool playing = false, hasPpq = false; };

    static double divBeats(int div)
    {
        static const double b[] { 1.0, 2.0 / 3.0, 1.5, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125, 1.0 / 12.0 };
        return b[juce::jlimit(0, 10, div)];
    }

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        out.ensureSize(4096);
        reset();
    }

    void reset()
    {
        heldCount = offCount = 0;
        std::fill(std::begin(keyDown), std::end(keyDown), false);
        untilNext = 0.0;
        gridIdx = 0;
        lastGridIdx = std::numeric_limits<long long>::min();
        step = 0;
        lastNote = -1;
    }

    // keys the arp holds right now (UI)
    bool isRunning() const { return wasOn && heldCount > 0; }

    void process(juce::MidiBuffer& midi, int numSamples, const Settings& s, const Host& h)
    {
        if (!s.on)
        {
            if (wasOn)
            {
                // switched off: end every note the arp is sounding, drop the pattern
                out.clear();
                for (int i = 0; i < offCount; ++i) out.addEvent(juce::MidiMessage::noteOff(1, offs[i].note), 0);
                if (lastNote >= 0) out.addEvent(juce::MidiMessage::noteOff(1, lastNote), 0);
                for (const auto m : midi) out.addEvent(m.getMessage(), m.samplePosition);
                writeBack(midi);
                reset();
            }
            wasOn = false;
            return;
        }
        if (!wasOn) reset();
        wasOn = true;
        out.clear();

        const double bpm = h.bpm > 20.0 ? h.bpm : 120.0;
        const double spb = 60.0 / bpm * sr;
        const double stepBeats = juce::jmax(1.0 / 64.0, s.stepBeats);
        const double swing = juce::jlimit(0.0f, 1.0f, s.swing);
        const bool grid = h.playing && h.hasPpq;
        auto stepBeat = [&](long long k) { return ((double) k + ((k & 1) ? 0.5 * swing : 0.0)) * stepBeats; };

        if (!s.latch)
        {
            bool anyDown = false;
            for (bool d : keyDown) anyDown = anyDown || d;
            if (!anyDown) heldCount = 0; // latch released with no key down
        }
        if (grid)
        {
            // the first grid line at/after the block start not fired yet (a jump back re-arms)
            if (h.ppq < prevPpq - 1.0e-6) lastGridIdx = std::numeric_limits<long long>::min();
            long long k = (long long) std::floor(h.ppq / stepBeats) - 1;
            while (stepBeat(k) < h.ppq - 1.0e-9 || k <= lastGridIdx) ++k;
            gridIdx = k;
            untilNext = (stepBeat(k) - h.ppq) * spb;
            prevPpq = h.ppq;
        }
        else prevPpq = -1.0e9;

        auto it = midi.cbegin();
        const auto end = midi.cend();
        int pos = 0;
        for (;;)
        {
            while (it != end && (*it).samplePosition <= pos)
            {
                handle((*it).getMessage(), pos, s);
                ++it;
            }
            for (int i = 0; i < offCount;)
            {
                if (offs[i].samplesLeft > 0) { ++i; continue; }
                out.addEvent(juce::MidiMessage::noteOff(1, offs[i].note), pos);
                if (offs[i].note == lastNote) lastNote = -1;
                offs[i] = offs[--offCount];
            }
            if (heldCount == 0) step = 0;
            while (untilNext <= 0.0)
            {
                if (heldCount == 0 && !grid) { untilNext = 0.0; gridIdx = 0; break; }
                int n = -1; float v = 0.8f;
                if (nextNote(s, n, v))
                {
                    if (lastNote >= 0) out.addEvent(juce::MidiMessage::noteOff(1, lastNote), pos);
                    for (int i = 0; i < offCount;)
                        if (offs[i].note == n || offs[i].note == lastNote) offs[i] = offs[--offCount];
                        else ++i;
                    out.addEvent(juce::MidiMessage::noteOn(1, n, s.playedVel ? v : 0.8f), pos);
                    lastNote = n;
                    const double len = (stepBeat(gridIdx + 1) - stepBeat(gridIdx)) * spb;
                    if (offCount < kMaxOffs) offs[offCount++] = { n, juce::jmax(1, (int) (len * s.gate * 0.98)) };
                    ++step;
                }
                if (grid) lastGridIdx = gridIdx;
                untilNext += (stepBeat(gridIdx + 1) - stepBeat(gridIdx)) * spb;
                ++gridIdx;
            }
            if (pos >= numSamples) break;

            int next = numSamples;
            if (it != end) next = juce::jmin(next, juce::jmax(pos + 1, (*it).samplePosition));
            if (heldCount > 0 || grid) next = juce::jmin(next, pos + juce::jmax(1, (int) std::ceil(untilNext)));
            for (int i = 0; i < offCount; ++i) next = juce::jmin(next, pos + offs[i].samplesLeft);
            const int seg = next - pos;
            if (heldCount > 0 || grid) untilNext -= seg;
            for (int i = 0; i < offCount; ++i) offs[i].samplesLeft -= seg;
            pos = next;
        }
        for (; it != end; ++it) handle((*it).getMessage(), numSamples - 1, s); // past the end (non-conforming hosts)
        writeBack(midi);
    }

private:
    static constexpr int kMaxHeld = 32, kMaxOffs = 32;
    struct Off { int note; int samplesLeft; };

    // copy (not swap) so the pre-sized scratch stays ours: the host's buffer keeps its storage
    void writeBack(juce::MidiBuffer& midi)
    {
        midi.clear();
        midi.addEvents(out, 0, -1, 0);
    }

    void handle(const juce::MidiMessage& m, int pos, const Settings& s)
    {
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            bool anyDown = false;
            for (bool d : keyDown) anyDown = anyDown || d;
            if (s.latch && !anyDown) heldCount = 0; // latched: a fresh press starts a new chord
            keyDown[note] = true;
            for (int i = 0; i < heldCount; ++i)
                if (held[i] == note) { heldVel[i] = m.getFloatVelocity(); return; }
            if (heldCount < kMaxHeld) { held[heldCount] = note; heldVel[heldCount] = m.getFloatVelocity(); ++heldCount; }
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            keyDown[note] = false;
            if (s.latch) return;
            int k = 0;
            for (int i = 0; i < heldCount; ++i)
                if (held[i] != note) { held[k] = held[i]; heldVel[k] = heldVel[i]; ++k; }
            heldCount = k;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldCount = 0;
            std::fill(std::begin(keyDown), std::end(keyDown), false);
            out.addEvent(m, pos);
        }
        else out.addEvent(m, pos); // controllers, bend, pressure pass straight through
    }

    bool nextNote(const Settings& s, int& noteOut, float& velOut)
    {
        if (heldCount <= 0) return false;
        int sorted[kMaxHeld]; float vels[kMaxHeld];
        for (int i = 0; i < heldCount; ++i) { sorted[i] = held[i]; vels[i] = heldVel[i]; }
        for (int i = 1; i < heldCount; ++i) // insertion sort, pitch order
            for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; --j)
            {
                std::swap(sorted[j - 1], sorted[j]);
                std::swap(vels[j - 1], vels[j]);
            }
        int seq[kMaxHeld * 4]; float seqVel[kMaxHeld * 4];
        int n = 0;
        for (int o = 0; o < juce::jlimit(1, 4, s.octaves); ++o)
            for (int i = 0; i < heldCount; ++i)
            {
                const int v = sorted[i] + o * 12;
                if (v <= 127) { seq[n] = v; seqVel[n] = vels[i]; ++n; }
            }
        if (n == 0) return false;
        int idx = 0;
        if (s.mode == 0) idx = step % n;
        else if (s.mode == 1) idx = n - 1 - (step % n);
        else if (s.mode == 2)
        {
            if (n > 1) { const int cyc = n * 2 - 2, t = step % cyc; idx = t < n ? t : cyc - t; }
        }
        else idx = rng.nextInt(n);
        noteOut = seq[idx];
        velOut = seqVel[idx];
        return true;
    }

    double sr = 44100.0;
    juce::MidiBuffer out;
    int held[kMaxHeld] = {};
    float heldVel[kMaxHeld] = {};
    int heldCount = 0;
    bool keyDown[128] = {};
    Off offs[kMaxOffs] = {};
    int offCount = 0;
    double untilNext = 0.0, prevPpq = -1.0e9;
    long long gridIdx = 0, lastGridIdx = 0;
    int step = 0, lastNote = -1;
    bool wasOn = false;
    juce::Random rng;
};
