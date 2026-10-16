#include "PianoStrip.h"
#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"

PianoStrip::PianoStrip(PastelProcessor& p) : proc(p)
{
    refresh();
}

bool PianoStrip::isBlack(int midi)
{
    int pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void PianoStrip::refresh()
{
    auto p = proc.snapshotParams();
    p.rootMidi = proc.getRootMidi();
    const int root = juce::jlimit(0, 127, (int) std::lround(p.rootMidi));
    // six octaves around the root, starting on a C
    int lo = juce::jlimit(0, 127 - 60, (root / 12 - 2) * 12);
    juce::String key;
    key << lo << ":" << root << ":" << p.loKey << ":" << p.hiKey << ":" << p.engine << ":" << p.rootMidi
        << ":" << p.sLevel << ":" << p.aLevel << ":" << p.pLevel;
    if (key == stateKey) return;
    stateKey = key;
    loKey = lo;
    hiKey = lo + 60;
    rootKey = root;
    rangeLo = p.loKey;
    rangeHi = p.hiKey;
    blend.assign((size_t) (hiKey - loKey + 1), { 0, 0, 0, 0 });
    for (int m = loKey; m <= hiKey; ++m)
    {
        float ws, wa, wp, wg;
        Blend::weights(p, ws, wa, wp, wg);
        float sum = ws + wa + wp + wg;
        if (sum > 0) blend[(size_t) (m - loKey)] = { ws / sum, wa / sum, wp / sum, wg / sum };
    }
    rebuildLayout();
    repaint();
}

void PianoStrip::updateActive()
{
    auto v = proc.getActiveNotes();
    std::set<int> s(v.begin(), v.end());
    if (s != active) { active = std::move(s); repaint(); }
}

void PianoStrip::rebuildLayout()
{
    whiteKeys.clear();
    blackKeys.clear();
    for (int m = loKey; m <= hiKey; ++m)
        if (!isBlack(m)) whiteKeys.push_back(m);
    float W = (float) juce::jmax(1, getWidth());
    whiteW = whiteKeys.empty() ? W : W / (float) whiteKeys.size();
    float bw = whiteW * 0.62f;
    for (int m = loKey; m <= hiKey; ++m)
    {
        if (!isBlack(m)) continue;
        int wi = 0;
        for (size_t i = 0; i < whiteKeys.size(); ++i)
            if (whiteKeys[i] < m) wi = (int) i + 1;
        float cx = wi * whiteW;
        blackKeys.push_back({ m, cx - bw * 0.5f, cx + bw * 0.5f });
    }
}

void PianoStrip::resized() { rebuildLayout(); }

int PianoStrip::keyAt(float x, float y) const
{
    float keysH = (float) getHeight() - kBand;
    float blackH = keysH * 0.6f;
    if (y - kBand <= blackH)
        for (auto& b : blackKeys)
            if (x >= b.x0 && x <= b.x1) return b.midi;
    int wi = juce::jlimit(0, (int) whiteKeys.size() - 1, (int) (x / whiteW));
    return whiteKeys.empty() ? -1 : whiteKeys[(size_t) wi];
}

void PianoStrip::pressAt(float x, float y)
{
    int k = keyAt(x, y);
    if (k == heldMouseNote) return;
    if (heldMouseNote >= 0) proc.auditionNoteOff(heldMouseNote);
    heldMouseNote = k;
    if (heldMouseNote >= 0) proc.auditionNoteOn(heldMouseNote, 0.8f);
}

void PianoStrip::mouseDown(const juce::MouseEvent& e) { pressAt(e.position.x, e.position.y); }
void PianoStrip::mouseDrag(const juce::MouseEvent& e) { pressAt(e.position.x, e.position.y); }
void PianoStrip::mouseUp(const juce::MouseEvent&)
{
    if (heldMouseNote >= 0) { proc.auditionNoteOff(heldMouseNote); heldMouseNote = -1; }
}

void PianoStrip::paint(juce::Graphics& g)
{
    using PC = PastelColors;
    float W = (float) getWidth(), H = (float) getHeight();
    g.setColour(PC::panel);
    g.fillRect(0, 0, (int) W, (int) H);
    const float top = kBand, keysH = H - kBand;

    for (size_t i = 0; i < whiteKeys.size(); ++i)
    {
        int m = whiteKeys[i];
        float x = i * whiteW;
        bool inRange = m >= rangeLo && m <= rangeHi;
        juce::Colour fill = inRange ? PC::keyWhite : PC::keyOut;
        if (m == rootKey) fill = PC::amber;
        if (active.count(m) > 0) fill = juce::Colours::white;
        g.setColour(fill);
        g.fillRect(x, top, whiteW + 0.5f, keysH);
        g.setColour(PC::knobEdge);
        g.drawLine(x + whiteW - 0.5f, top, x + whiteW - 0.5f, H, 1.0f);
        g.drawLine(x, H - 0.5f, x + whiteW, H - 0.5f, 1.0f);
        if (active.count(m) > 0)
        {
            g.setColour(PC::accentDeep);
            g.fillRect(x, top, whiteW + 0.5f, 3.0f);
        }
        if (m == rootKey && whiteW > 26.0f)
        {
            g.setColour(PC::barText);
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText("ROOT", (int) x, (int) (H - 30), (int) whiteW, 12, juce::Justification::centred, false);
        }
        if (m % 12 == 0 && whiteW > 26.0f)
        {
            g.setColour(m == rootKey ? PC::barText : PC::textDim);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("C" + juce::String(m / 12 - 1), (int) x, (int) (H - 16), (int) whiteW, 14,
                       juce::Justification::centred, false);
        }
        // engine blend band
        if (inRange)
        {
            const auto& b = blend[(size_t) (m - loKey)];
            float y = 0;
            const juce::Colour cols[4] { PC::accent, PC::accentBright, PC::accentDark, PC::accentDeep };
            for (int e = 0; e < 4; ++e)
            {
                float h = b[(size_t) e] * (kBand - 1.0f);
                if (h <= 0.05f) continue;
                g.setColour(cols[e]);
                g.fillRect(x + 1.0f, y, whiteW - 2.0f, h);
                y += h;
            }
        }
    }
    g.setColour(PC::panelEdge);
    g.drawLine(0, top + 0.5f, W, top + 0.5f, 1.0f);

    float blackH = keysH * 0.6f;
    for (auto& b : blackKeys)
    {
        bool inRange = b.midi >= rangeLo && b.midi <= rangeHi;
        juce::Colour fill = juce::Colour(0xFF26292F);
        if (!inRange) fill = juce::Colour(0xFF4A4C50);
        if (b.midi == rootKey) fill = PC::amber.darker(0.15f);
        if (active.count(b.midi) > 0) fill = PC::accentBright;
        g.setColour(fill);
        g.fillRoundedRectangle(b.x0, top, b.x1 - b.x0, blackH, 2.0f);
        g.setColour(PC::knobEdge);
        g.drawRoundedRectangle(b.x0 + 0.5f, top + 0.5f, b.x1 - b.x0 - 1, blackH - 1, 2.0f, 1.0f);
    }
}
