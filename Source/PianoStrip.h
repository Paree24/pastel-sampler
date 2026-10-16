#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <set>
#include <vector>

class PastelProcessor;

// ============================================================
// PianoStrip: full-bleed on-screen keyboard (from the Pastel player).
// - amber  = root key
// - white  = currently sounding
// - grey   = outside the low/high key range
// - band on top = engine blend per key (sampler / additive / spectral)
// Click / drag to audition.
// ============================================================
class PianoStrip : public juce::Component
{
public:
    explicit PianoStrip(PastelProcessor& p);
    void refresh();      // re-read root / range / blend (cheap; call on a timer)
    void updateActive(); // poll sounding notes

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    static bool isBlack(int midi);
    void rebuildLayout();
    int keyAt(float x, float y) const;
    void pressAt(float x, float y);

    PastelProcessor& proc;
    int loKey = 36, hiKey = 96, rootKey = 60, rangeLo = 0, rangeHi = 127;
    std::vector<int> whiteKeys;
    struct BlackGeom { int midi; float x0, x1; };
    std::vector<BlackGeom> blackKeys;
    float whiteW = 20.0f;
    std::set<int> active;
    std::vector<std::array<float, 4>> blend; // per key (lo..hi): sampler / additive / spectral / granular
    juce::String stateKey;
    int heldMouseNote = -1;
    static constexpr float kBand = 10.0f;
};
