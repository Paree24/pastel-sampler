#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Waveform with draggable S / L / L / E markers, f0 overlay, analysis window and ruler.
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveformView(PastelProcessor& p) : proc(p) {}
    void refresh();
    void zoomStep();
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isDragging() const { return drag != None; }

private:
    enum Target { None, Start, LoopS, LoopE, End, Select };
    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().withHeight(118.0f); }
    float sampleToX(double s) const;
    double xToSample(float x) const;
    Target hit(juce::Point<float> p) const;
    void clampView();

    PastelProcessor& proc;
    std::shared_ptr<const Instrument> inst;
    SampleEdits e;
    double v0 = 0, v1 = 1;
    int zoomLevel = 0;
    Target drag = None, hover = None;
    double dragAnchor = 0;
};

// Mean partial amplitudes x user gains; click-drag redraws, double-click resets.
class PartialView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PartialView(PastelProcessor& p) : proc(p) {}
    void refresh() { inst = proc.getInstrument(); repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { edit(e.position); }
    void mouseDrag(const juce::MouseEvent& e) override { edit(e.position); }
    void mouseDoubleClick(const juce::MouseEvent&) override { proc.resetPartialGains(); repaint(); }

private:
    int shownPartials() const;
    void edit(juce::Point<float> p);
    PastelProcessor& proc;
    std::shared_ptr<const Instrument> inst;
};

// Static spectrogram of the spectral analysis.
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView(PastelProcessor& p) : proc(p) {}
    void refresh();
    void paint(juce::Graphics&) override;

private:
    PastelProcessor& proc;
    std::shared_ptr<const Instrument> inst;
    juce::Image image;
};

// Detected root readout + confidence bar.
class RootDisplay : public juce::Component
{
public:
    explicit RootDisplay(PastelProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;

private:
    PastelProcessor& proc;
};

// Browser audition strip: play/stop + mini waveform + playhead.
class PreviewStrip : public juce::Component
{
public:
    explicit PreviewStrip(PastelProcessor& p) : proc(p) {}
    std::function<void()> onPlay;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    PastelProcessor& proc;
};

// Slim determinate progress bar for background loads (as in the Pastel player).
class LoadBar : public juce::Component
{
public:
    void setProgress(float p, bool loading)
    {
        if (std::abs(p - progress) > 0.005f || loading != active) { progress = p; active = loading; repaint(); }
    }
    void paint(juce::Graphics& g) override;

private:
    float progress = 1.0f;
    bool active = false;
};
