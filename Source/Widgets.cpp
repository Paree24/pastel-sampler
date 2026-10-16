#include "Widgets.h"
#include "PastelLookAndFeel.h"

using PC = PastelColors;

namespace
{
juce::Font uiFont(float h) { return juce::Font(juce::FontOptions(h)); }
juce::Font boldFont(float h) { return juce::Font(juce::FontOptions(h, juce::Font::bold)); }

void drawWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(PC::bg);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(PC::panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
}
}

// ============================================================ WaveformView
void WaveformView::refresh()
{
    auto ni = proc.getInstrument();
    const bool newFile = ni == nullptr || inst == nullptr || ni->path != inst->path
                      || ni->raw.getNumSamples() != inst->raw.getNumSamples();
    inst = ni;
    if (drag == None) e = proc.getEdits();
    if (newFile)
    {
        zoomLevel = 0;
        v0 = 0;
        v1 = inst ? juce::jmax(1, inst->raw.getNumSamples()) : 1;
    }
    clampView();
    repaint();
}

void WaveformView::clampView()
{
    const double n = inst ? juce::jmax(1, inst->raw.getNumSamples()) : 1;
    double span = juce::jlimit(juce::jmin(n, 64.0), n, v1 - v0);
    v0 = juce::jlimit(0.0, n - span, v0);
    v1 = v0 + span;
}

void WaveformView::zoomStep()
{
    if (inst == nullptr) return;
    const double n = inst->raw.getNumSamples();
    zoomLevel = (zoomLevel + 1) % 4;
    const double span = n / (double) (1 << zoomLevel);
    const double centre = zoomLevel == 0 ? n * 0.5 : 0.5 * (e.loopStart + e.loopEnd);
    v0 = centre - span * 0.5;
    v1 = v0 + span;
    clampView();
    repaint();
}

float WaveformView::sampleToX(double s) const
{
    auto a = waveArea();
    return a.getX() + (float) ((s - v0) / (v1 - v0)) * a.getWidth();
}

double WaveformView::xToSample(float x) const
{
    auto a = waveArea();
    return v0 + (double) (x - a.getX()) / a.getWidth() * (v1 - v0);
}

WaveformView::Target WaveformView::hit(juce::Point<float> p) const
{
    if (inst == nullptr || p.y > waveArea().getBottom()) return None;
    const bool looping = proc.loopMode() != 0;
    struct M { Target t; double s; bool on; };
    M ms[] { { Start, (double) e.start, true }, { LoopS, (double) e.loopStart, looping },
             { LoopE, (double) e.loopEnd, looping }, { End, (double) e.end, true } };
    Target best = None;
    float bestD = 7.0f;
    for (auto& m : ms)
    {
        if (!m.on) continue;
        float d = std::abs(p.x - sampleToX(m.s));
        if (d < bestD) { bestD = d; best = m.t; }
    }
    return best;
}

void WaveformView::paint(juce::Graphics& g)
{
    auto a = waveArea();
    drawWell(g, a);
    if (inst == nullptr)
    {
        g.setColour(PC::textDim);
        g.setFont(uiFont(13.0f));
        g.drawText("Drop an audio file here, or double-click a file in the browser", a, juce::Justification::centred);
        return;
    }
    const auto inner = a.reduced(6.0f, 4.0f);
    const float cy = inner.getCentreY(), half = inner.getHeight() * 0.42f;
    const int n = inst->raw.getNumSamples();
    const double sr = inst->sr;
    const auto loopMode = proc.loopMode();
    g.saveState();
    g.reduceClipRegion(a.toNearestInt());

    // loop region + crossfade ramp (only when a loop mode is on)
    const float xls = sampleToX(e.loopStart), xle = sampleToX(e.loopEnd);
    if (loopMode != 0)
    {
        g.setColour(PC::accent.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<float>(xls, a.getY(), xle - xls, a.getHeight()));
    }
    if (loopMode == 1 || loopMode == 3)
    {
        const double xf = juce::jmin((double) proc.apvts.getRawParameterValue(PP::S_XFADE)->load() * 0.001 * sr,
                                     (double) e.loopStart, 0.5 * (e.loopEnd - e.loopStart));
        juce::Path tri;
        float xx = sampleToX(e.loopEnd - xf);
        tri.addTriangle(xx, a.getBottom(), xle, a.getY(), xle, a.getBottom());
        g.setColour(PC::accent.withAlpha(0.16f));
        g.fillPath(tri);
    }

    // waveform
    g.setColour(PC::accentDeep.withAlpha(0.92f));
    const int cols = (int) inner.getWidth();
    const double spp = (v1 - v0) / juce::jmax(1, cols);
    const auto& pmin = inst->peaksMin;
    const auto& pmax = inst->peaksMax;
    const bool usePeaks = spp > (double) n / (double) pmin.size() && !pmin.empty();
    for (int c = 0; c < cols; ++c)
    {
        double s0 = v0 + c * spp, s1 = s0 + spp;
        float lo = 0, hi = 0;
        if (usePeaks)
        {
            int p0 = (int) (s0 / n * pmin.size()), p1 = juce::jmax(p0 + 1, (int) (s1 / n * pmin.size()));
            for (int k = juce::jmax(0, p0); k < juce::jmin((int) pmin.size(), p1); ++k)
            {
                lo = juce::jmin(lo, pmin[(size_t) k]);
                hi = juce::jmax(hi, pmax[(size_t) k]);
            }
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = inst->raw.getReadPointer(ch);
                for (int k = juce::jmax(0, (int) s0); k <= juce::jmin(n - 1, (int) s1); ++k)
                {
                    lo = juce::jmin(lo, d[k]);
                    hi = juce::jmax(hi, d[k]);
                }
            }
        }
        float x = inner.getX() + (float) c;
        g.fillRect(x, cy - hi * half, 1.0f, juce::jmax(1.0f, (hi - lo) * half));
    }
    // outside start/end
    g.setColour(PC::bg.withAlpha(0.7f));
    g.fillRect(juce::Rectangle<float>(a.getX(), a.getY(), juce::jmax(0.0f, sampleToX(e.start) - a.getX()), a.getHeight()));
    float xe = sampleToX(e.end);
    g.fillRect(juce::Rectangle<float>(xe, a.getY(), juce::jmax(0.0f, a.getRight() - xe), a.getHeight()));

    // live grains (granular engine): short bright ticks at each grain's read position
    {
        auto grains = proc.getGrainPositions();
        const float n = (float) inst->raw.getNumSamples();
        g.setColour(PC::accentBright.withAlpha(0.85f));
        for (float gp : grains)
        {
            const float x = sampleToX(gp * n);
            g.fillRect(x - 1.0f, a.getY() + 16.0f, 2.0f, a.getHeight() * 0.35f);
        }
    }

    // markers
    struct Mk { double s; const char* lab; Target t; };
    Mk mks[] { { (double) e.start, "S", Start }, { (double) e.loopStart, "L", LoopS }, { (double) e.loopEnd, "L", LoopE }, { (double) e.end, "E", End } };
    for (auto& m : mks)
    {
        if (loopMode == 0 && (m.t == LoopS || m.t == LoopE)) continue;
        float x = sampleToX(m.s);
        bool hl = hover == m.t || drag == m.t;
        g.setColour(hl ? juce::Colours::white : PC::accentBright);
        g.drawLine(x, a.getY(), x, a.getBottom(), hl ? 1.8f : 1.2f);
        juce::Rectangle<float> flag(m.t == End ? x - 14 : x, a.getY(), 14, 13);
        g.setColour(PC::accent);
        g.fillRoundedRectangle(flag, 2.0f);
        g.setColour(PC::barText);
        g.setFont(boldFont(9.5f));
        g.drawText(m.lab, flag, juce::Justification::centred);
    }
    if (loopMode != 0)
    {
        g.setColour(PC::accentBright);
        g.setFont(boldFont(9.5f));
        juce::String lbl = juce::String(PP::loopChoices[loopMode]).toUpperCase() + " LOOP";
        if (loopMode == 1 || loopMode == 3)
            lbl << juce::String(juce::CharPointer_UTF8(" · XFADE ")) << (int) proc.apvts.getRawParameterValue(PP::S_XFADE)->load() << " ms";
        g.drawText(lbl, juce::Rectangle<float>(xls, a.getBottom() - 16, xle - xls, 12), juce::Justification::centred);
    }
    g.restoreState();

    // time ruler
    const float by = a.getBottom() + 4;
    g.setColour(PC::textDim);
    g.setFont(uiFont(10.0f));
    const double secs = (v1 - v0) / sr;
    double stepS = secs > 20 ? 5 : secs > 8 ? 2 : secs > 3 ? 1 : secs > 1.2 ? 0.5 : secs > 0.5 ? 0.2 : 0.05;
    for (double t = std::ceil(v0 / sr / stepS) * stepS; t * sr <= v1; t += stepS)
    {
        float x = sampleToX(t * sr);
        g.drawLine(x, by, x, by + 4, 1.0f);
        g.drawText(juce::String(t, stepS < 0.1 ? 2 : (stepS < 1 ? 1 : 0)) + "s", (int) x + 3, (int) by + 2, 50, 14,
                   juce::Justification::centredLeft);
    }
}

void WaveformView::mouseMove(const juce::MouseEvent& ev)
{
    auto h = hit(ev.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor(h == None ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void WaveformView::mouseDown(const juce::MouseEvent& ev)
{
    if (inst == nullptr) return;
    e = proc.getEdits();
    drag = hit(ev.position);
    dragAnchor = xToSample(ev.position.x);
    if (drag == None && !ev.mods.isShiftDown() && ev.position.y <= waveArea().getBottom()) drag = Select;
}

void WaveformView::mouseDrag(const juce::MouseEvent& ev)
{
    if (inst == nullptr) return;
    const int n = inst->raw.getNumSamples();
    const int s = juce::jlimit(0, n, (int) xToSample(ev.position.x));
    const int minGap = (int) (0.005 * inst->sr);
    switch (drag)
    {
        case None:
        {
            // shift-drag scrolls when zoomed
            double d = xToSample(ev.position.x) - dragAnchor;
            v0 -= d; v1 -= d;
            clampView();
            break;
        }
        case Select:
        {
            const int a0 = juce::jlimit(0, n, (int) dragAnchor);
            e.start = juce::jmin(a0, s);
            e.end = juce::jmax(e.start + minGap, juce::jmax(a0, s));
            e.end = juce::jmin(e.end, n);
            break;
        }
        case Start: e.start = juce::jlimit(0, e.end - minGap, s); break;
        case End: e.end = juce::jlimit(e.start + minGap, n, s); break;
        case LoopS: e.loopStart = juce::jlimit(e.start + 1, e.loopEnd - minGap, s); break;
        case LoopE: e.loopEnd = juce::jlimit(e.loopStart + minGap, e.end, s); break;
    }
    repaint();
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    if (drag != None)
    {
        const bool selection = drag == Select || drag == Start || drag == End;
        drag = None;
        if (selection)
        {
            // keep the loop inside the selection; detection follows the selection
            if (e.loopStart < e.start || e.loopEnd > e.end || e.loopEnd <= e.loopStart) e.loopStart = e.loopEnd = -1;
            e.anaStart = e.anaEnd = -1;
        }
        proc.setEdits(e, true, false);
    }
    repaint();
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& ev, const juce::MouseWheelDetails& w)
{
    if (inst == nullptr) return;
    const double n = inst->raw.getNumSamples();
    if (ev.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY))
    {
        double d = (v1 - v0) * (double) (w.deltaX != 0 ? w.deltaX : w.deltaY) * -0.5;
        v0 += d; v1 += d;
    }
    else
    {
        double at = xToSample(ev.position.x);
        double f = w.deltaY > 0 ? 0.8 : 1.25;
        double span = juce::jlimit(64.0, n, (v1 - v0) * f);
        double rel = (at - v0) / (v1 - v0);
        v0 = at - rel * span;
        v1 = v0 + span;
    }
    clampView();
    repaint();
}

// ============================================================ PartialView
int PartialView::shownPartials() const
{
    if (inst == nullptr || inst->additive == nullptr) return 0;
    int k = (int) proc.apvts.getRawParameterValue(PP::A_PARTIALS)->load();
    return juce::jlimit(1, juce::jmin(64, inst->additive->numPartials), k);
}

void PartialView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell(g, r);
    if (inst == nullptr || inst->additive == nullptr) return;
    const auto& ad = *inst->additive;
    const int N = shownPartials();
    float ref = 1e-9f;
    for (int k = 0; k < ad.numPartials; ++k) ref = juce::jmax(ref, ad.meanAmps[(size_t) k]);
    auto plot = r.reduced(4.0f, 4.0f).withTrimmedTop(14.0f);
    const float bw = plot.getWidth() / (float) N;
    for (int k = 0; k < N; ++k)
    {
        float v = ad.meanAmps[(size_t) k] / ref * proc.getPartialGain(k);
        float db = juce::Decibels::gainToDecibels(v, -60.0f);
        float h = juce::jmax(1.0f, (db + 60.0f) / 60.0f * plot.getHeight());
        g.setColour(proc.getPartialGain(k) != 1.0f ? PC::accentBright : PC::accent);
        g.fillRect(plot.getX() + k * bw + bw * 0.15f, plot.getBottom() - h, juce::jmax(1.0f, bw * 0.7f), h);
    }
    g.setColour(PC::textDim);
    g.setFont(uiFont(9.5f));
    g.drawText(juce::String(N) + (N < (int) proc.apvts.getRawParameterValue(PP::A_PARTIALS)->load() ? "+" : "") + " partials",
               r.reduced(6, 3).withHeight(13), juce::Justification::topRight);
}

void PartialView::edit(juce::Point<float> p)
{
    if (inst == nullptr || inst->additive == nullptr) return;
    const auto& ad = *inst->additive;
    const int N = shownPartials();
    auto plot = getLocalBounds().toFloat().reduced(4.0f, 4.0f).withTrimmedTop(14.0f);
    int k = juce::jlimit(0, N - 1, (int) ((p.x - plot.getX()) / plot.getWidth() * N));
    float ref = 1e-9f;
    for (int j = 0; j < ad.numPartials; ++j) ref = juce::jmax(ref, ad.meanAmps[(size_t) j]);
    float frac = juce::jlimit(0.0f, 1.0f, (plot.getBottom() - p.y) / plot.getHeight());
    float target = juce::Decibels::decibelsToGain(frac * 60.0f - 60.0f, -60.0f);
    float mean = ad.meanAmps[(size_t) k] / ref;
    proc.setPartialGain(k, mean > 1e-6f ? target / mean : (frac > 0.02f ? 4.0f : 0.0f));
    repaint();
}

// ============================================================ SpectrumView
void SpectrumView::refresh()
{
    auto ni = proc.getInstrument();
    if (ni == inst) return;
    inst = ni;
    image = {};
    if (inst != nullptr && inst->spectral != nullptr && inst->spectral->numFrames > 0)
    {
        const auto& sd = *inst->spectral;
        const int W = 200, H = 72;
        image = juce::Image(juce::Image::ARGB, W, H, true);
        float mx = 1e-9f;
        for (float m : sd.mag) mx = juce::jmax(mx, m);
        const int f0 = inst->start / sd.hop, f1 = juce::jmax(f0 + 1, inst->end / sd.hop);
        const double lo = std::log(40.0), hi = std::log(sd.sr * 0.5);
        for (int x = 0; x < W; ++x)
        {
            int fr = juce::jlimit(0, sd.numFrames - 1, f0 + (f1 - f0) * x / W);
            const float* m = sd.magAt(fr);
            for (int y = 0; y < H; ++y)
            {
                double hz = std::exp(lo + (hi - lo) * (1.0 - (y + 0.5) / H));
                int b = juce::jlimit(0, sd.bins - 1, (int) (hz / sd.sr * sd.N));
                float db = juce::Decibels::gainToDecibels(m[b] / mx, -70.0f);
                float a = juce::jlimit(0.0f, 1.0f, (db + 70.0f) / 70.0f);
                if (a > 0.02f) image.setPixelAt(x, y, PC::accentDeep.withAlpha(a * a));
            }
        }
    }
    repaint();
}

void SpectrumView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawWell(g, r);
    if (image.isValid())
        g.drawImage(image, r.reduced(3.0f), juce::RectanglePlacement::stretchToFit);
    if (inst != nullptr && inst->spectral != nullptr)
    {
        g.setColour(PC::textDim);
        g.setFont(uiFont(9.5f));
        g.drawText("FFT " + juce::String(inst->spectral->N), r.reduced(6, 3).withHeight(13), juce::Justification::topRight);
    }
}

// ============================================================ RootDisplay
void RootDisplay::paint(juce::Graphics& g)
{
    auto inst = proc.getInstrument();
    const bool over = proc.apvts.getRawParameterValue(PP::ROOTOVR)->load() > 0.5f;
    g.setColour(PC::textDim);
    g.setFont(uiFont(11.0f));
    g.drawText(over ? "Detected (override active)" : "Detected", 4, 2, 190, 14, juce::Justification::centredLeft);
    if (inst == nullptr) return;
    const auto& p = inst->pitch;
    const bool pitched = p.pitched();
    g.setColour(over ? PC::textDim : PC::text);
    g.setFont(juce::Font(juce::FontOptions(46.0f, juce::Font::bold)));
    juce::String nn = pitched ? PP::noteName(p.nearestNote()) : juce::String(juce::CharPointer_UTF8("—"));
    g.drawText(nn, 2, 16, 100, 52, juce::Justification::centredLeft);
    if (pitched)
    {
        int nameW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), nn) + 12;
        g.setColour(PC::text);
        g.setFont(boldFont(15.0f));
        float c = p.cents();
        g.drawText((c >= 0 ? "+" : "") + juce::String(juce::roundToInt(c)) + " ct", nameW, 24, 90, 18, juce::Justification::centredLeft);
        g.setColour(PC::textDim);
        g.setFont(uiFont(12.0f));
        g.drawText(juce::String(p.f0, 1) + " Hz", nameW, 43, 90, 16, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(PC::amber);
        g.setFont(uiFont(11.0f));
        g.drawText(juce::String(juce::CharPointer_UTF8("no clear pitch — set root manually")), 4, 64, 196, 14, juce::Justification::centredLeft);
    }
    g.setColour(PC::textDim);
    g.setFont(uiFont(11.0f));
    g.drawText("Confidence", 4, 82, 120, 14, juce::Justification::centredLeft);
    juce::Rectangle<float> bar(4, 100, 150, 8);
    g.setColour(PC::track);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(p.confidence < 0.6f ? PC::amber : PC::accent);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * p.confidence), 3.0f);
    g.setColour(PC::text);
    g.setFont(boldFont(11.5f));
    g.drawText(juce::String(juce::roundToInt(p.confidence * 100)) + "%", 160, 96, 40, 16, juce::Justification::centredLeft);
}

// ============================================================ PreviewStrip
void PreviewStrip::paint(juce::Graphics& g)
{
    const bool playing = proc.isPreviewing();
    juce::Rectangle<float> btn(0, 2, 24, 24);
    g.setColour(PC::accent);
    g.fillEllipse(btn);
    g.setColour(PC::barText);
    if (playing) g.fillRect(btn.withSizeKeepingCentre(9, 9));
    else
    {
        juce::Path tri;
        tri.addTriangle(btn.getCentreX() - 3.5f, btn.getCentreY() - 6.5f, btn.getCentreX() - 3.5f, btn.getCentreY() + 6.5f,
                        btn.getCentreX() + 7.0f, btn.getCentreY());
        g.fillPath(tri);
    }
    auto peaks = proc.getPreviewPeaks();
    juce::Rectangle<float> w(32, 0, (float) getWidth() - 32, (float) getHeight());
    float mx = 1e-6f;
    for (float p : peaks) mx = juce::jmax(mx, p);
    g.setColour(PC::accentDeep);
    if (peaks.empty())
    {
        g.setColour(PC::track);
        g.fillRect(w.getX(), w.getCentreY() - 0.5f, w.getWidth(), 1.0f);
    }
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        float x = w.getX() + w.getWidth() * (float) i / (float) peaks.size();
        float h = juce::jmax(1.0f, peaks[i] / mx * w.getHeight() * 0.9f);
        g.fillRect(x, w.getCentreY() - h * 0.5f, juce::jmax(1.0f, w.getWidth() / (float) peaks.size()), h);
    }
    if (playing)
    {
        float x = w.getX() + w.getWidth() * proc.getPreviewPosition();
        g.setColour(PC::text);
        g.drawLine(x, w.getY(), x, w.getBottom(), 1.0f);
    }
}

void PreviewStrip::mouseDown(const juce::MouseEvent& e)
{
    if (e.position.x < 28)
    {
        if (proc.isPreviewing()) proc.stopPreview();
        else if (onPlay) onPlay();
        repaint();
    }
}

// ============================================================ LoadBar
void LoadBar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(PC::track);
    g.fillRoundedRectangle(r, 3.0f);
    if (!active) return;
    g.setColour(PC::accent);
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * progress), 3.0f);
    g.setColour(PC::barText);
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText("LOADING " + juce::String((int) (progress * 100.0f)) + "%", r, juce::Justification::centred);
}
