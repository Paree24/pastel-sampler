#include "PluginEditor.h"

using PC = PastelColors;
static constexpr int kW = 1400, kH = 800;

// ============================================================ file list
void FileListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& e = entries[(size_t) row];
    if (selected)
    {
        g.setColour(PC::accent.withAlpha(0.22f));
        g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 3.0f);
    }
    const bool light = !e.isDir && e.lossless;
    g.setColour(light ? PC::accent : PC::accentDark);
    g.fillRoundedRectangle(6.0f, h * 0.5f - 8.0f, 34.0f, 16.0f, 3.0f);
    g.setColour(light ? PC::barText : juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText(e.tag, 6, (int) (h * 0.5f - 8), 34, 16, juce::Justification::centred, false);
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    if (e.isDir)
    {
        g.setColour(selected ? PC::text : PC::textDim);
        g.drawText(e.name, 46, 0, w - 60, h, juce::Justification::centredLeft, true);
        g.setColour(PC::textDim);
        g.drawText(">", w - 18, 0, 12, h, juce::Justification::centred, false);
        return;
    }
    g.setColour(e.path == loadedPath ? PC::accentBright : PC::text);
    g.drawText(e.name, 46, 0, w - 90, h, juce::Justification::centredLeft, true);
    RootScanner::Result r;
    if (scanner != nullptr && scanner->lookup(e.path, r))
    {
        g.setColour(r.label.endsWith("?") ? PC::amber : (r.label == juce::String(juce::CharPointer_UTF8("—")) ? PC::textDim : PC::accentBright));
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(r.label, w - 44, 0, 38, h, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour(PC::track);
        g.fillEllipse((float) w - 12.0f, h * 0.5f - 2.0f, 4.0f, 4.0f);
    }
}

// ============================================================ helpers
juce::GroupComponent& PastelEditor::card(const juce::String& title, juce::Rectangle<int> r)
{
    auto g = std::make_unique<juce::GroupComponent>("", title);
    g->setBounds(r);
    g->setInterceptsMouseClicks(false, false);
    parent->addAndMakeVisible(*g);
    g->toBack();
    cards.push_back(std::move(g));
    return *cards.back();
}

PastelEditor::Knob& PastelEditor::knob(const juce::String& id, const juce::String& title, int cx, int top, int size)
{
    auto k = std::make_unique<Knob>();
    k->s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k->s.setPopupDisplayEnabled(true, true, &view);
    k->a = std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, k->s);
    if (auto* prm = proc.apvts.getParameter(id))
    {
        k->s.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
        k->s.setTooltip(prm->getName(64));
    }
    k->s.setBounds(cx - size / 2, top, size, size);
    k->l.setText(title, juce::dontSendNotification);
    k->l.setFont(lnf.uiFont(11.5f));
    k->l.setJustificationType(juce::Justification::centred);
    k->l.setColour(juce::Label::textColourId, PC::text);
    k->l.setBounds(cx - 40, top + size, 80, 15);
    k->l.setInterceptsMouseClicks(false, false);
    parent->addAndMakeVisible(k->s);
    parent->addAndMakeVisible(k->l);
    knobs.push_back(std::move(k));
    return *knobs.back();
}

PastelEditor::Switch& PastelEditor::headerSwitch(const juce::String& id, juce::Rectangle<int> c)
{
    auto s = std::make_unique<Switch>();
    s->a = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, id, s->b);
    s->b.setBounds(c.getRight() - 30, c.getY() + 2, 24, 22);
    s->b.getProperties().set("power", true);   // v3: section on/off is the power glyph
    if (auto* prm = proc.apvts.getParameter(id)) s->b.setTooltip(prm->getName(64));
    parent->addAndMakeVisible(s->b);
    switches.push_back(std::move(s));
    return *switches.back();
}

PastelEditor::Switch& PastelEditor::captionSwitch(const juce::String& id, const juce::String& caption, int cx, int y)
{
    auto& s = headerSwitch(id, {});
    s.b.getProperties().remove("power");   // a captioned pill, not a section switch
    s.b.setBounds(cx - 23, y, 46, 20);
    s.cap.setText(caption, juce::dontSendNotification);
    s.cap.setFont(lnf.uiFont(9.5f));
    s.cap.setJustificationType(juce::Justification::centred);
    s.cap.setColour(juce::Label::textColourId, PC::textDim);
    s.cap.setBounds(cx - 45, y + 21, 90, 12);
    parent->addAndMakeVisible(s.cap);
    return s;
}

PastelEditor::Choice& PastelEditor::choice(const juce::String& id, const juce::String& title, const juce::StringArray& items,
                                           juce::Rectangle<int> box, int)
{
    auto c = std::make_unique<Choice>();
    c->c.addItemList(items, 1);
    c->a = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, id, c->c);
    c->c.setBounds(box);
    if (title.isNotEmpty())
    {
        c->l.setText(title, juce::dontSendNotification);
        c->l.setFont(lnf.uiFont(11.0f));
        c->l.setColour(juce::Label::textColourId, PC::textDim);
        c->l.setBounds(box.getX() - 2, box.getY() - 15, box.getWidth() + 4, 13);
        parent->addAndMakeVisible(c->l);
    }
    parent->addAndMakeVisible(c->c);
    choices.push_back(std::move(c));
    return *choices.back();
}

void PastelEditor::button(juce::TextButton& b, juce::Rectangle<int> r, const juce::String& tip)
{
    b.setBounds(r);
    b.setColour(juce::TextButton::textColourOffId, PC::text);
    b.setColour(juce::TextButton::textColourOnId, PC::accentBright);
    if (tip.isNotEmpty()) b.setTooltip(tip);
    parent->addAndMakeVisible(b);
}

// ============================================================ construction
PastelEditor::PastelEditor(PastelProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), presetBrowser(p, lnf), preview(p), wave(p), partials(p), spectrum(p),
      rootDisplay(p), piano(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    setLookAndFeel(&lnf);
    addAndMakeVisible(view);
    view.setBounds(0, 0, kW, kH);
    view.painter = [this](juce::Graphics& g) { paintView(g); };

    // ---------------- header ----------------
    button(loadBtn, { 520, 22, 60, 30 }, "Open an audio file");
    button(prevBtn, { 584, 22, 28, 30 }, "Previous file in folder");
    button(nextBtn, { 616, 22, 28, 30 }, "Next file in folder");
    button(panicBtn, { 648, 22, 62, 30 }, "Stop all sound");
    button(mainTab, { 716, 22, 58, 30 }, "Sample, engines, root, envelope and filter");
    button(fxTab, { 778, 22, 76, 30 }, "Effects and master");
    button(modTab, { 858, 22, 56, 30 }, "LFOs, mod matrix, arpeggiator, randomize");
    button(undoBtn, { 932, 22, 52, 30 }, "Undo the last change (Ctrl+Z)");
    button(redoBtn, { 988, 22, 52, 30 }, "Redo what UNDO took back (Ctrl+Shift+Z)");
    undoBtn.onClick = [this] { proc.undo(); afterStateJump(); };
    redoBtn.onClick = [this] { proc.redo(); afterStateJump(); };
    mainTab.onClick = [this] { setPage(0); };
    fxTab.onClick = [this] { setPage(1); };
    modTab.onClick = [this] { setPage(2); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Load sample", juce::File(proc.library.getCurrentDir()), FileLibrary::wildcard());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc)
                             {
                                 auto f = fc.getResult();
                                 if (!f.existsAsFile()) return;
                                 proc.library.setCurrentDir(f.getParentDirectory().getFullPathName());
                                 proc.loadSample(f.getFullPathName());
                             });
    };
    prevBtn.onClick = [this] { stepFile(-1); };
    nextBtn.onClick = [this] { stepFile(1); };
    panicBtn.onClick = [this] { proc.panic(); proc.stopPreview(); };

    auto* presetLbl = labels.emplace_back(std::make_unique<juce::Label>("", "Preset")).get();
    presetLbl->setFont(lnf.uiFont(11.0f));
    presetLbl->setColour(juce::Label::textColourId, PC::textDim);
    presetLbl->setBounds(1050, 15, 100, 13);
    view.addAndMakeVisible(presetLbl);
    button(presetBtn, { 1052, 29, 100, 26 }, "Presets: factory sounds for the loaded sample, and your saved presets");
    presetBtn.onClick = [this] { presetBrowser.setVisible(true); presetBrowser.toFront(true); };
    presetBrowser.onClose = [this] { presetBrowser.setVisible(false); refreshPresetBox(); };
    presetBrowser.onLoaded = [this] { afterStateJump(); refreshPresetBox(); };
    refreshPresetBox();

    // ---------------- browser ----------------
    card("Browser", { 10, 76, 250, 512 });
    searchBox.setBounds(20, 110, 230, 24);
    searchBox.setTextToShowWhenEmpty("Search here...", PC::textDim);
    searchBox.setFont(lnf.uiFont(13.0f));
    searchBox.onTextChange = [this] { refreshFileList(); };
    view.addAndMakeVisible(searchBox);
    dirBox.setBounds(20, 140, 230, 24);
    dirBox.setTextWhenNothingSelected("(select shortcut)");
    dirBox.onChange = [this]
    {
        auto sc = proc.library.getShortcuts();
        int i = dirBox.getSelectedItemIndex();
        if (i >= 0 && i < sc.size()) proc.library.setCurrentDir(sc[i]);
    };
    view.addAndMakeVisible(dirBox);
    button(addDirBtn, { 20, 170, 53, 24 }, "Add a folder shortcut");
    button(delDirBtn, { 79, 170, 53, 24 }, "Remove the selected shortcut");
    button(upBtn, { 138, 170, 53, 24 }, "Parent folder");
    button(scanBtn, { 197, 170, 53, 24 }, "Rescan this folder");
    addDirBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Add folder", juce::File(proc.library.getCurrentDir()));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& fc)
                             {
                                 auto d = fc.getResult();
                                 if (!d.isDirectory()) return;
                                 proc.library.addShortcut(d.getFullPathName());
                                 proc.library.setCurrentDir(d.getFullPathName());
                             });
    };
    delDirBtn.onClick = [this] { proc.library.removeShortcut(dirBox.getSelectedItemIndex()); };
    upBtn.onClick = [this]
    {
        auto parent = juce::File(proc.library.getCurrentDir()).getParentDirectory();
        if (parent.isDirectory()) proc.library.setCurrentDir(parent.getFullPathName());
    };
    scanBtn.onClick = [this] { refreshFileList(); };
    pathLabel.setFont(lnf.uiFont(9.5f));
    pathLabel.setColour(juce::Label::textColourId, PC::textDim);
    pathLabel.setBounds(17, 198, 236, 14);
    pathLabel.setMinimumHorizontalScale(0.6f);
    view.addAndMakeVisible(pathLabel);
    loadBar.setBounds(20, 222, 146, 12);
    view.addAndMakeVisible(loadBar);
    button(loadSelBtn, { 174, 216, 76, 24 }, "Load the selected file");
    loadSelBtn.onClick = [this] { openRow(fileList.getSelectedRow()); };
    listModel.scanner = &proc.rootScanner;
    listModel.onOpen = [this](int r) { openRow(r); };
    listModel.onSelect = [this](int r) { selectRow(r); };
    fileList.setModel(&listModel);
    fileList.setRowHeight(26);
    fileList.setColour(juce::ListBox::backgroundColourId, PC::panel);
    fileList.setBounds(16, 246, 238, 266);
    view.addAndMakeVisible(fileList);
    preview.setBounds(22, 534, 182, 28);
    preview.onPlay = [this]
    {
        int r = fileList.getSelectedRow();
        if (r >= 0 && r < (int) listModel.entries.size() && !listModel.entries[(size_t) r].isDir)
            proc.previewFile(listModel.entries[(size_t) r].path);
    };
    view.addAndMakeVisible(preview);
    autoPreviewBtn.setToggleState(proc.library.getAutoPreview(), juce::dontSendNotification);
    autoPreviewBtn.onClick = [this] { proc.library.setAutoPreview(autoPreviewBtn.getToggleState()); };
    autoPreviewBtn.setBounds(205, 530, 46, 20);
    autoPreviewBtn.setTooltip("Audition files on selection");
    view.addAndMakeVisible(autoPreviewBtn);
    {
        auto* cap = labels.emplace_back(std::make_unique<juce::Label>("", "AUTO")).get();
        cap->setFont(lnf.uiFont(9.5f));
        cap->setColour(juce::Label::textColourId, PC::textDim);
        cap->setJustificationType(juce::Justification::centred);
        cap->setBounds(198, 551, 60, 12);
        view.addAndMakeVisible(cap);
        auto* hint = labels.emplace_back(std::make_unique<juce::Label>("", juce::String(juce::CharPointer_UTF8("plays the raw file  ·  drag files onto the window")))).get();
        hint->setFont(lnf.uiFont(9.5f));
        hint->setColour(juce::Label::textColourId, PC::textDim);
        hint->setJustificationType(juce::Justification::centred);
        hint->setBounds(14, 568, 242, 14);
        view.addAndMakeVisible(hint);
    }

    // ---------------- sample ----------------
    card("Sample", { 268, 76, 700, 212 });
    button(reverseBtn, { 278, 108, 76, 22 }, "Reverse the sample (re-analyses)");
    button(normBtn, { 360, 108, 92, 22 }, "Normalise to -0.2 dBFS");
    button(trimBtn, { 458, 108, 56, 22 }, "Cut the sample to the S..E selection");
    button(restoreBtn, { 520, 108, 76, 22 }, "Undo TRIM: bring back the whole file");
    button(loopBtn, { 602, 108, 56, 22 }, "Looping on / off (loop type is in the Sampler tab)");
    loopBtn.setClickingTogglesState(true);
    loopAttach = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, PP::LOOP_ON, loopBtn);
    button(findLoopBtn, { 664, 108, 84, 22 }, "Find a seamless loop inside the selection (turns looping on)");
    button(zoomBtn, { 754, 108, 64, 22 }, "Zoom 1x / 2x / 4x / 8x around the loop (mouse wheel also zooms)");
    reverseBtn.onClick = [this] { proc.toggleReverse(); };
    normBtn.onClick = [this] { proc.toggleNormalize(); };
    trimBtn.onClick = [this] { proc.trimToSelection(); };
    restoreBtn.onClick = [this] { proc.untrim(); };
    findLoopBtn.onClick = [this] { proc.findLoopNow(); };
    zoomBtn.onClick = [this] { wave.zoomStep(); };
    infoLabel.setFont(lnf.uiFont(11.0f));
    infoLabel.setColour(juce::Label::textColourId, PC::textDim);
    infoLabel.setJustificationType(juce::Justification::centredRight);
    infoLabel.setMinimumHorizontalScale(0.7f);
    infoLabel.setBounds(824, 108, 134, 22);
    view.addAndMakeVisible(infoLabel);
    wave.setBounds(278, 138, 680, 148);
    wave.setTooltip(juce::String(juce::CharPointer_UTF8("Drag across the waveform to select S..E (or drag the S / E flags) \u00b7 "
                                                        "mouse wheel zooms, shift+wheel or shift+drag scrolls")));
    view.addAndMakeVisible(wave);

    // ---------------- engines (tabbed) ----------------
    card("Engines", { 268, 296, 700, 292 });
    {
        // the tabs ARE the engine selector: clicking one plays that engine (BLEND = all, mixed)
        const char* tips[5] { "Play the Sampler: the recording, repitched",
                              "Play the Additive engine: resynthesis from tracked harmonics",
                              "Play the Spectral engine: phase-vocoder resynthesis",
                              "Play the Granular engine: grains around a moving playhead",
                              "Play all engines together, mixed with the sliders in this tab" };
        for (int i = 0; i < 5; ++i)
        {
            auto& b = engineBtns[(size_t) i];
            b.setButtonText(PP::engineChoices[i].toUpperCase());
            button(b, { 278 + i * 136, 328, 132, 26 }, tips[i]);
            b.onClick = [this, i]
            {
                auto* prm = proc.apvts.getParameter(PP::ENGINE);
                prm->setValueNotifyingHost(prm->convertTo0to1((float) i));
                showTab(i);
            };
        }
    }
    auto describe = [this](const juce::String& text, juce::Rectangle<int> r)
    {
        auto* d = labels.emplace_back(std::make_unique<juce::Label>("", text)).get();
        d->setFont(lnf.uiFont(11.0f));
        d->setColour(juce::Label::textColourId, PC::textDim);
        d->setJustificationType(juce::Justification::topLeft);
        d->setBounds(r);
        d->setInterceptsMouseClicks(false, false);
        view.addAndMakeVisible(d);
    };
    auto beginTab = [this] { return view.getNumChildComponents(); };
    auto endTab = [this](int tab, int before)
    {
        for (int k = before; k < view.getNumChildComponents(); ++k) tabComps[(size_t) tab].push_back(view.getChildComponent(k));
    };
    {   // SAMPLER
        const int b0 = beginTab();
        choice(PP::S_INTERP, "Interpolation", PP::interpChoices, { 290, 392, 180, 24 });
        choice(PP::LOOP_TYPE, "Loop type", PP::loopTypeChoices, { 490, 392, 180, 24 })
            .c.setTooltip("Forward / ping-pong / sustain (loop while held, then play out). LOOP in the Sample card turns it on.");
        knob(PP::S_START, "Start", 712, 374, 50)
            .s.setTooltip("Start offset into the selection (up to the loop start when looping); every engine starts here");
        knob(PP::S_XFADE, "Loop xfade", 784, 374, 50);
        knob(PP::S_TUNE, "Tune", 856, 374, 50);
        captionSwitch(PP::S_AUTOLOOP, "AUTO LOOP ON LOAD", 922, 390).b.setTooltip("Find a loop automatically when a sample loads");
        describe("Plays the recording itself, repitched with the chosen interpolation. "
                 "Looping is switched on with LOOP in the Sample card; FIND LOOP places the points.", { 290, 450, 420, 40 });
        endTab(0, b0);
    }
    {   // ADDITIVE
        const int b0 = beginTab();
        partials.setBounds(290, 366, 440, 190);
        partials.setTooltip("Drag to redraw partial levels, double-click to reset");
        view.addAndMakeVisible(partials);
        knob(PP::A_PARTIALS, "Partials", 790, 372, 50);
        knob(PP::A_TILT, "Tilt", 880, 372, 50);
        knob(PP::A_NOISE, "Noise", 835, 452, 50);
        endTab(1, b0);
    }
    {   // SPECTRAL
        const int b0 = beginTab();
        spectrum.setBounds(290, 366, 380, 190);
        view.addAndMakeVisible(spectrum);
        knob(PP::P_SHIFT, "Shift", 722, 372, 48).s.setTooltip("Formant shift: moves the timbre up or down without changing the pitch");
        knob(PP::P_BLUR, "Smear", 802, 372, 48).s.setTooltip("Blurs the spectrum over time (0 - 2 s): transients and movement wash into a pad");
        knob(PP::P_FREEZE, "Freeze", 882, 372, 48).s.setTooltip("Slows the sound to a standstill after the attack; 100% holds the tone forever");
        captionSwitch(PP::V_FORMANT, "FORMANT", 722, 446).b.setTooltip("Keep the formants (timbre) of the root on every key");
        describe("Shift: moves formants without changing pitch.\nSmear: blurs the spectrum over time.\n"
                 "Freeze: slows to a standstill after the attack.", { 760, 444, 198, 56 });
        choice(PP::QUALITY, "Quality", PP::qualityChoices, { 762, 524, 96, 24 })
            .c.setTooltip("Spectral resolution (minimum FFT 1024 / 2048 / 4096)");
        endTab(2, b0);
    }
    {   // GRANULAR
        const int b0 = beginTab();
        struct GK { const juce::String* id; const char* name; const char* tip; };
        const GK ks[8] { { &PP::G_POS, "Position", "Where the playhead starts (0 = the selection start, keeps the attack)" },
                         { &PP::G_SCAN, "Scan", "Playhead speed: 1 = real time, 0 = freeze, negative = backwards" },
                         { &PP::G_SIZE, "Size", "Grain length" },
                         { &PP::G_DENS, "Density", "Grains per second" },
                         { &PP::G_SPRAY, "Spray", "Random time offset of each grain" },
                         { &PP::G_JITTER, "Jitter", "Random pitch offset of each grain" },
                         { &PP::G_WIDTH, "Width", "Stereo spread of the grains" },
                         { &PP::G_SHAPE, "Shape", "Grain window: smooth (Hann) to hard-edged" } };
        for (int i = 0; i < 8; ++i) knob(*ks[i].id, ks[i].name, (int) (278 + 680.0f / 8 * (i + 0.5f)), 384, 50).s.setTooltip(ks[i].tip);
        describe("Grains of the sample around a moving playhead, pitched to the key. Scan 1 = real time, 0 = freeze. "
                 "Live grains are drawn on the waveform.", { 290, 470, 660, 34 });
        endTab(3, b0);
    }
    {   // BLEND: one vertical level slider per engine
        const int b0 = beginTab();
        const juce::String ids[4] { PP::S_LEVEL, PP::A_LEVEL, PP::P_LEVEL, PP::G_LEVEL };
        for (int r = 0; r < 4; ++r)
        {
            auto& f = faders[(size_t) r];
            const int cx = (int) (278 + 680.0f / 4 * (r + 0.5f));
            f.s.setSliderStyle(juce::Slider::LinearVertical);
            f.s.setBounds(cx - 16, 364, 32, 156);
            f.s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            f.s.setPopupDisplayEnabled(true, true, &view);
            f.a = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids[r], f.s);
            f.s.setDoubleClickReturnValue(true, 0.0);
            f.s.setTooltip(PP::engineChoices[r] + " level (bottom = off)");

            f.name.setText(PP::engineChoices[r], juce::dontSendNotification);
            f.name.setFont(lnf.uiFont(11.5f));
            f.name.setColour(juce::Label::textColourId, PC::text);
            f.name.setJustificationType(juce::Justification::centred);
            f.name.setBounds(cx - 50, 522, 100, 14);
            f.val.setFont(lnf.uiFont(10.5f));
            f.val.setColour(juce::Label::textColourId, PC::textDim);
            f.val.setJustificationType(juce::Justification::centred);
            f.val.setBounds(cx - 50, 537, 100, 14);
            for (juce::Component* comp : { (juce::Component*) &f.s, (juce::Component*) &f.name, (juce::Component*) &f.val })
                view.addAndMakeVisible(comp);
        }
        describe("Blend plays every engine at once; these levels apply only here. Single engines always play at full level.",
                 { 290, 562, 660, 16 });
        endTab(4, b0);
    }

    // ---------------- root note ----------------
    {
        juce::Rectangle<int> c(976, 76, 414, 212);
        card("Root note", c);
        overrideSwitch = &headerSwitch(PP::ROOTOVR, c);
        overrideSwitch->b.setTooltip("Override the detected root note");
        overrideSwitch->b.onClick = [this]
        {
            auto inst = proc.getInstrument();
            if (overrideSwitch->b.getToggleState() && inst != nullptr && inst->pitch.pitched())
            {
                auto set = [this](const juce::String& id, float v)
                {
                    auto* prm = proc.apvts.getParameter(id);
                    prm->setValueNotifyingHost(prm->convertTo0to1(v));
                };
                set(PP::ROOTNOTE, (float) inst->pitch.nearestNote());
                set(PP::ROOTFINE, juce::jlimit(-50.0f, 50.0f, inst->pitch.cents()));
            }
        };
        rootDisplay.setBounds(986, 104, 200, 118);
        view.addAndMakeVisible(rootDisplay);
        rootChoice = &choice(PP::ROOTNOTE, "Root override", PP::allNoteNames(), { 1190, 120, 100, 24 });
        rootInfo.setFont(lnf.uiFont(10.0f));
        rootInfo.setColour(juce::Label::textColourId, PC::textDim);
        rootInfo.setBounds(1188, 148, 150, 14);
        view.addAndMakeVisible(rootInfo);
        fineKnob = &knob(PP::ROOTFINE, "Fine", 1342, 104, 52);
        choice(PP::ALGO, "Algorithm", PP::algoChoices, { 990, 244, 176, 24 }).c.setTooltip("Pitch detector used for the root");
        button(learnBtn, { 1190, 244, 92, 24 }, "Press a key on your MIDI keyboard to set the root");
        button(redetectBtn, { 1288, 244, 92, 24 }, "Run pitch detection again on the analysis window");
        learnBtn.onClick = [this] { proc.armRootLearn(!proc.isRootLearnArmed()); };
        redetectBtn.onClick = [this] { proc.rebuild(true); };
    }

    // ---------------- envelope / filter ----------------
    {
        juce::Rectangle<int> c(976, 296, 414, 114);
        card("Envelope", c);
        headerSwitch(PP::E_ON, c);
        // v3: the envelope you drag on the left, its knobs in a compact block on the right
        auto* ev = addView<pastel::EnvView>({ 988, 324, 200, 80 }, proc.apvts,
                                            juce::StringArray { PP::E_A, PP::E_D, PP::E_S, PP::E_R }, juce::String());
        ev->setShowValues(false);
        ev->setTooltip("Drag the points: attack, decay / sustain, release (double-click = defaults)");
        const juce::String ids[] { PP::E_A, PP::E_D, PP::E_S, PP::E_R, PP::E_CURVE };
        const char* names[] { "Attack", "Decay", "Sustain", "Release", "Curve" };
        for (int i = 0; i < 5; ++i) knob(ids[i], names[i], 1222 + (i % 3) * 58, i < 3 ? 322 : 366, 26);
    }
    {
        juce::Rectangle<int> c(976, 418, 414, 170);
        card("Filter", c);
        headerSwitch(PP::F_ON, c);
        const juce::String ids[] { PP::F_CUT, PP::F_RES, PP::F_DRIVE, PP::F_KEY };
        const char* names[] { "Cutoff", "Reso", "Drive", "Key track" };
        // v3: the response you drag on the left, its knobs in a 2 x 2 block on the right
        addView<pastel::FilterView>({ 988, 446, 220, 82 }, proc.apvts, PP::F_CUT, PP::F_RES, PP::F_TYPE, juce::String(), juce::String(),
                                    std::vector<pastel::FilterView::Mode> { pastel::FilterView::Mode::lp12, pastel::FilterView::Mode::hp12,
                                                                            pastel::FilterView::Mode::bp12 })
            ->setTooltip("Drag: cutoff sideways, resonance up / down");
        for (int i = 0; i < 4; ++i) knob(ids[i], names[i], 1252 + (i % 2) * 76, i < 2 ? 444 : 490, 28);
        choice(PP::F_TYPE, "Type", PP::filterTypeChoices, { 990, 542, 186, 24 });
        choice(PP::F_SLOPE, "Slope", PP::slopeChoices, { 1190, 542, 186, 24 });
    }

    // ---------------- voice & keymap ----------------
    card("Voice & keymap", { 10, 596, kW - 20, 90 });
    choice(PP::V_MODE, "Mode", PP::modeChoices, { 20, 644, 110, 24 });
    for (int v = 1; v <= 32; ++v) voicesBox.addItem(juce::String(v), v);
    voicesBox.setBounds(138, 644, 70, 24);
    voicesBox.onChange = [this]
    {
        auto* prm = proc.apvts.getParameter(PP::V_VOICES);
        prm->setValueNotifyingHost(prm->convertTo0to1((float) voicesBox.getSelectedId()));
    };
    view.addAndMakeVisible(voicesBox);
    {
        auto* l = labels.emplace_back(std::make_unique<juce::Label>("", "Voices")).get();
        l->setFont(lnf.uiFont(11.0f));
        l->setColour(juce::Label::textColourId, PC::textDim);
        l->setBounds(136, 629, 74, 13);
        view.addAndMakeVisible(l);
    }
    knob(PP::V_GLIDE, "Glide", 252, 627, 40);
    knob(PP::V_BEND, "Bend", 310, 627, 40);
    knob(PP::V_SPREAD, "Spread", 368, 627, 40);
    knob(PP::VOLUME, "Volume", 426, 627, 40);
    captionSwitch(PP::LIMIT, "LIMITER", 496, 638).b.setTooltip("Master peak limiter: -0.3 dBFS ceiling, 1.5 ms lookahead");
    captionSwitch(PP::V_KEEPLEN, "KEEP LENGTH", 572, 638).b.setTooltip("Additive / spectral notes keep the original duration on every key");
    captionSwitch(PP::V_RR, "ROUND ROBIN", 648, 638).b.setTooltip("Micro pitch / start / level variation per note");
    captionSwitch(PP::V_MONO, "MONO", 724, 638).b.setTooltip("Sum the output to mono");
    choice(PP::V_TUNING, "Tuning", PP::tuningChoices, { 790, 644, 100, 24 });
    tonicChoice = &choice(PP::V_TONIC, "Tonic", PP::tonicChoices, { 896, 644, 52, 24 });
    tonicChoice->c.setTooltip("Tonic of the Just / Pythagorean scale (12-TET ignores it)");
    choice(PP::V_A4, "A4", PP::a4Choices, { 954, 644, 74, 24 });
    choice(PP::V_LOKEY, "Low key", PP::allNoteNames(), { 1034, 644, 56, 24 });
    choice(PP::V_HIKEY, "High key", PP::allNoteNames(), { 1096, 644, 56, 24 });

    // colour key for the engine band above the keyboard
    legend.setBounds(1156, 646, 232, 20);
    legend.setInterceptsMouseClicks(false, false);
    legend.painter = [](juce::Graphics& g)
    {
        const juce::Colour cols[4] { PC::accent, PC::accentBright, PC::accentDark, PC::accentDeep };
        const char* names[4] { "Sampler", "Additive", "Spectral", "Granular" };
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        int lx = 2;
        for (int i = 0; i < 4; ++i)
        {
            g.setColour(cols[i]);
            g.fillRoundedRectangle((float) lx, 5.0f, 10.0f, 10.0f, 2.0f);
            g.setColour(PC::textDim);
            g.drawText(names[i], lx + 14, 2, 46, 16, juce::Justification::centredLeft);
            lx += 58;
        }
    };
    view.addAndMakeVisible(legend);
    piano.setBounds(10, 694, kW - 20, 96);
    view.addAndMakeVisible(piano);

    // first-created card at the very back so nested sub-cards draw over their parent card
    // everything inside the centre/right area belongs to the MAIN page
    const juce::Rectangle<int> pageArea(268, 76, 1122, 512);
    for (auto* c : view.getChildren())
        if (pageArea.contains(c->getBounds())) mainComps.push_back(c);
    buildEffectsPage();
    buildModPage();
    addChildComponent(presetBrowser); // overlay above the scaled view

    for (auto it = cards.rbegin(); it != cards.rend(); ++it) (*it)->toBack();
    setPage(0);
    syncUi();

    proc.addChangeListener(this);
    proc.library.addChangeListener(this);
    refreshShortcuts();
    refreshFileList();
    instrumentChanged();

    const double scale = juce::jlimit(0.6, 1.5, proc.library.getUiScale());
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(kW * 6 / 10, kH * 6 / 10, kW * 3 / 2, kH * 3 / 2);
    if (auto* cons = getConstrainer()) cons->setFixedAspectRatio((double) kW / kH);
    setSize((int) (kW * scale), (int) (kH * scale));
    startTimerHz(30);
}

PastelEditor::~PastelEditor()
{
    stopTimer();
    proc.removeChangeListener(this);
    proc.library.removeChangeListener(this);
    fileList.setModel(nullptr);
    setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void PastelEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    view.setTransform(juce::AffineTransform::scale(s));
    presetBrowser.setBounds(getLocalBounds());
    proc.library.setUiScale(s);
}

// ============================================================ effects page
void PastelEditor::buildEffectsPage()
{
    fxPage.setBounds(0, 0, kW, kH);
    fxPage.setInterceptsMouseClicks(false, true);
    view.addChildComponent(fxPage);
    parent = &fxPage;

    card("Effects", { 268, 76, 1122, 512 });
    const int gx = 278, gy = 108, cw = 271, ch = 233, cg = 6;
    auto cell = [&](int i) { return juce::Rectangle<int>(gx + (i % 4) * (cw + cg), gy + (i / 4) * (ch + cg), cw, ch); };
    auto row = [&](juce::Rectangle<int> c, std::initializer_list<std::pair<juce::String, const char*>> ks, int top, int size)
    {
        int i = 0;
        const int n = (int) ks.size();
        for (auto& k : ks) knob(k.first, k.second, c.getX() + (int) (c.getWidth() * (i++ + 0.5f) / n), top, size);
    };
    auto bottomCombo = [&](const juce::String& id, const juce::StringArray& items, juce::Rectangle<int> c, int x, int w)
    {
        choice(id, "Mode", items, { x, c.getBottom() - 36, w, 24 });
    };

    // v3: each card = the picture you drag, a mode switch (if any), one row of knobs along the bottom
    using FS = pastel::FxView::Style;
    auto picture = [&](juce::Rectangle<int> c, bool withSeg)
    {
        return juce::Rectangle<int>(c.getX() + 10, c.getY() + 30, c.getWidth() - 20, c.getHeight() - 30 - 84 - (withSeg ? 28 : 0));
    };
    auto segAt = [&](juce::Rectangle<int> c) { return juce::Rectangle<int>(c.getX() + 10, c.getBottom() - 84 - 26, c.getWidth() - 20, 22); };
    auto knobRow = [&](juce::Rectangle<int> c, std::initializer_list<std::pair<juce::String, const char*>> ks)
    {
        row(c, ks, c.getBottom() - 78, (int) ks.size() > 3 ? 34 : 38);
    };

    auto c = cell(0);
    card("Chorus", c); headerSwitch(PP::CHON, c);
    addView<pastel::FxView>(picture(c, true), proc.apvts, FS::chorus, PP::CHRATE, PP::CHDEPTH, PP::CHON)
        ->setTooltip("Drag: rate sideways, depth up / down");
    addView<pastel::SegChoice>(segAt(c), proc.apvts, PP::CHMODE, juce::StringArray { "MANUAL", "I", "II", "WIDE" });
    knobRow(c, { { PP::CHRATE, "Rate" }, { PP::CHDEPTH, "Depth" }, { PP::CHMIX, "Mix" } });

    c = cell(1);
    card("Phaser", c); headerSwitch(PP::PHON, c);
    addView<pastel::FxView>(picture(c, false), proc.apvts, FS::phaser, PP::PHRATE, PP::PHDEPTH, PP::PHON)
        ->setTooltip("Drag: rate sideways, depth up / down");
    knobRow(c, { { PP::PHRATE, "Rate" }, { PP::PHDEPTH, "Depth" }, { PP::PHFB, "Fdbk" }, { PP::PHMIX, "Mix" } });

    c = cell(2);
    card("Flanger", c); headerSwitch(PP::FLON, c);
    addView<pastel::FxView>(picture(c, false), proc.apvts, FS::chorus, PP::FLRATE, PP::FLDEPTH, PP::FLON)
        ->setTooltip("Drag: rate sideways, depth up / down");
    knobRow(c, { { PP::FLRATE, "Rate" }, { PP::FLDEPTH, "Depth" }, { PP::FLFB, "Fdbk" }, { PP::FLMIX, "Mix" } });

    c = cell(3);
    card("Distortion", c); headerSwitch(PP::DION, c);
    driveViews[0] = addView<pastel::DriveView>(picture(c, false), proc.apvts, PP::DIDRIVE);
    driveViews[0]->setTooltip("Drag up / down: drive");
    knobRow(c, { { PP::DIDRIVE, "Drive" }, { PP::DIMIX, "Mix" } });

    c = cell(4);
    card("Saturation", c); headerSwitch(PP::SAON, c);
    driveViews[1] = addView<pastel::DriveView>(picture(c, true), proc.apvts, PP::SAAMT, PP::SAMODE);
    driveViews[1]->setTooltip("Drag up / down: amount");
    addView<pastel::SegChoice>(segAt(c), proc.apvts, PP::SAMODE, juce::StringArray { "TAPE", "TUBE" });
    knobRow(c, { { PP::SAAMT, "Amount" }, { PP::SATONE, "Tone" } });

    c = cell(5);
    card("Reverb", c); headerSwitch(PP::RVON, c);
    addView<pastel::FxView>(picture(c, false), proc.apvts, FS::reverb, PP::RVSIZE, PP::RVDAMP, PP::RVON,
                            juce::String(), juce::String(), "SIZE", "DAMP")
        ->setTooltip("Drag: size sideways, damping up / down");
    knobRow(c, { { PP::RVSIZE, "Size" }, { PP::RVDAMP, "Damp" }, { PP::RVMIX, "Mix" } });

    c = cell(6);
    card("Delay", c); headerSwitch(PP::DLON, c);
    addView<pastel::FxView>(picture(c, true), proc.apvts, FS::delay, PP::DLTIME, PP::DLFB, PP::DLON, PP::DLSYNC, PP::DLDIV)
        ->setTooltip("Drag: time sideways (when not synced), feedback up / down");
    {
        const auto sr = segAt(c);
        captionSwitch(PP::DLSYNC, "SYNC", sr.getX() + 24, sr.getY() - 2);
        choice(PP::DLDIV, "", PP::delayDivs, { sr.getX() + 56, sr.getY() - 1, sr.getWidth() - 56, 24 });
    }
    knobRow(c, { { PP::DLTIME, "Time" }, { PP::DLFB, "Fdbk" }, { PP::DLMIX, "Mix" } });

    c = cell(7);
    card("Output", c);
    knob(PP::VOLUME, "Volume", c.getCentreX(), c.getY() + 48, 72);
    captionSwitch(PP::LIMIT, "LIMITER", c.getCentreX() - 40, c.getBottom() - 44).b.setTooltip("Master peak limiter: -0.3 dBFS ceiling, 1.5 ms lookahead");
    captionSwitch(PP::V_MONO, "MONO", c.getCentreX() + 40, c.getBottom() - 44);

    parent = &view;
}

// ============================================================ mod page
void PastelEditor::buildModPage()
{
    modPage.setBounds(0, 0, kW, kH);
    modPage.setInterceptsMouseClicks(false, true);
    view.addChildComponent(modPage);
    parent = &modPage;

    // LFOs
    for (int k = 0; k < 2; ++k)
    {
        const juce::Rectangle<int> c(268, 76 + k * 262, 268, 250);
        card("LFO " + juce::String(k + 1), c);
        knob(PP::L_RATE[k], "Rate", c.getX() + 60, c.getY() + 44, 64);
        choice(PP::L_WAVE[k], "Wave", PP::lfoWaves, { c.getX() + 126, c.getY() + 60, 128, 24 });
        addView<pastel::LfoView>({ c.getX() + 14, c.getY() + 130, c.getWidth() - 28, 54 }, proc.apvts, PP::L_WAVE[k], juce::String(),
                                 std::vector<pastel::Wave> { pastel::Wave::sine, pastel::Wave::tri, pastel::Wave::saw,
                                                             pastel::Wave::square, pastel::Wave::snh });
        captionSwitch(PP::L_SYNC[k], "SYNC", c.getX() + 40, c.getBottom() - 44);
        choice(PP::L_DIV[k], "Division", PP::lfoDivs, { c.getX() + 84, c.getBottom() - 44, c.getWidth() - 98, 24 });
    }
    for (int k = 0; k < 2; ++k)
    {
        auto& m = lfoMeters[(size_t) k];
        m.setBounds(268 + 126, 76 + k * 262 + 104, 128, 10);
        m.setInterceptsMouseClicks(false, false);
        m.painter = [this, k, &m](juce::Graphics& g)
        {
            // live value: a bar from the centre line
            auto r = m.getLocalBounds().toFloat();
            g.setColour(PC::track);
            g.fillRoundedRectangle(r, 2.0f);
            const float v = juce::jlimit(-1.0f, 1.0f, proc.getLfoValue(k));
            const float cx = r.getCentreX(), w = v * r.getWidth() * 0.5f;
            g.setColour(PC::accent);
            g.fillRect(juce::Rectangle<float>(juce::jmin(cx, cx + w), r.getY(), std::abs(w), r.getHeight()));
        };
        modPage.addAndMakeVisible(m);
    }

    // matrix: 8 rows of source -> amount -> destination
    {
        const juce::Rectangle<int> c(544, 76, 562, 512);
        card("Mod Matrix", c);
        for (auto [text, x, w] : { std::tuple<const char*, int, int> { "SOURCE", 40, 190 }, { "AMOUNT", 246, 60 },
                                   { "DESTINATION", 322, 226 } })
        {
            auto* hdr = labels.emplace_back(std::make_unique<juce::Label>("", text)).get();
            hdr->setFont(lnf.uiFont(10.0f));
            hdr->setColour(juce::Label::textColourId, PC::textDim);
            hdr->setJustificationType(juce::String(text) == "AMOUNT" ? juce::Justification::centred : juce::Justification::centredLeft);
            hdr->setBounds(c.getX() + x - 2, c.getY() + 32, w, 14);
            modPage.addAndMakeVisible(hdr);
        }
        for (int k = 0; k < PP::kModSlots; ++k)
        {
            const int y = c.getY() + 52 + k * 56;
            auto* n = labels.emplace_back(std::make_unique<juce::Label>("", juce::String(k + 1))).get();
            n->setFont(lnf.titleFont(13.0f));
            n->setColour(juce::Label::textColourId, PC::textDim);
            n->setBounds(c.getX() + 14, y + 10, 20, 24);
            modPage.addAndMakeVisible(n);
            choice(PP::modSrcId(k), {}, PP::modSources, { c.getX() + 40, y + 10, 190, 24 });
            auto& amt = knob(PP::modAmtId(k), {}, c.getX() + 276, y + 2, 40);
            amt.s.setTooltip("Amount (double-click: 0)");
            choice(PP::modDstId(k), {}, PP::modDests, { c.getX() + 322, y + 10, 226, 24 });
        }
    }

    // arpeggiator
    {
        const juce::Rectangle<int> c(1114, 76, 276, 250);
        card("Arpeggiator", c);
        headerSwitch(PP::ARP_ON, c);
        choice(PP::ARP_MODE, "Mode", PP::arpModes, { c.getX() + 12, c.getY() + 52, 124, 24 });
        choice(PP::ARP_DIV, "Division", PP::arpDivs, { c.getX() + 144, c.getY() + 52, 120, 24 });
        knob(PP::ARP_OCT, "Octaves", c.getX() + 46, c.getY() + 96, 46);
        knob(PP::ARP_GATE, "Gate", c.getX() + 138, c.getY() + 96, 46);
        knob(PP::ARP_SWING, "Swing", c.getX() + 230, c.getY() + 96, 46);
        captionSwitch(PP::ARP_LATCH, "LATCH", c.getX() + 92, c.getBottom() - 44).b.setTooltip("Keep the pattern going after the keys go up");
        captionSwitch(PP::ARP_VEL, "PLAYED VEL", c.getX() + 184, c.getBottom() - 44).b.setTooltip("Notes use each key's velocity (off: fixed)");
    }

    // randomize with locks
    {
        const juce::Rectangle<int> c(1114, 338, 276, 250);
        card("Randomize", c);
        button(randomBtn, { c.getX() + 12, c.getY() + 36, 124, 30 }, "New random patch: locked groups stay as they are (one undo step)");
        button(mutateBtn, { c.getX() + 140, c.getY() + 36, 124, 30 }, "Nudge the unlocked knobs a little (one undo step)");
        randomBtn.onClick = [this] { proc.randomize(); afterStateJump(); };
        mutateBtn.onClick = [this] { proc.mutate(); afterStateJump(); };
        auto* cap = labels.emplace_back(std::make_unique<juce::Label>("", "LOCKED (never changed)")).get();
        cap->setFont(lnf.uiFont(10.0f));
        cap->setColour(juce::Label::textColourId, PC::textDim);
        cap->setBounds(c.getX() + 10, c.getY() + 74, 200, 14);
        modPage.addAndMakeVisible(cap);
        const auto names = PastelProcessor::randGroupNames();
        for (int g = 0; g < PastelProcessor::RG_COUNT; ++g)
        {
            auto& b = lockBtns[(size_t) g];
            b.setButtonText(names[g]);
            b.setClickingTogglesState(true);
            b.setToggleState(proc.isGroupLocked(g), juce::dontSendNotification);
            button(b, { c.getX() + 12 + (g % 2) * 128, c.getY() + 92 + (g / 2) * 36, 124, 30 },
                   "Lock " + names[g] + ": randomize and mutate leave it alone");
            b.onClick = [this, g] { proc.setGroupLocked(g, lockBtns[(size_t) g].getToggleState()); };
        }
    }
    parent = &view;
}

void PastelEditor::setPage(int page)
{
    currentPage = page;
    updateVisibility();
    fxPage.setVisible(page == 1);
    modPage.setVisible(page == 2);
    mainTab.setToggleState(page == 0, juce::dontSendNotification);
    fxTab.setToggleState(page == 1, juce::dontSendNotification);
    modTab.setToggleState(page == 2, juce::dontSendNotification);
}

void PastelEditor::showTab(int tab)
{
    currentTab = juce::jlimit(0, 4, tab);
    for (int i = 0; i < 5; ++i) engineBtns[(size_t) i].setToggleState(i == currentTab, juce::dontSendNotification);
    updateVisibility();
    syncUi();
}

void PastelEditor::updateVisibility()
{
    for (auto* c : mainComps)
    {
        bool inTab = false, inCurrent = false;
        for (int t = 0; t < 5; ++t)
            for (auto* tc : tabComps[(size_t) t])
                if (tc == c) { inTab = true; inCurrent = inCurrent || t == currentTab; }
        c->setVisible(currentPage == 0 && (!inTab || inCurrent));
    }
}

// ============================================================ painting
void PastelEditor::paintView(juce::Graphics& g)
{
    g.fillAll(PC::bg);
    g.setColour(PC::panel);
    g.fillRoundedRectangle(10, 8, kW - 20, 58, 6.0f);
    g.setColour(PC::panelEdge);
    g.drawRoundedRectangle(10.5f, 8.5f, kW - 21, 57, 6.0f, 1.0f);
    g.setColour(PC::text);
    g.setFont(lnf.logoFont(21.0f));
    g.drawText("PASTEL SAMPLER", 50, 16, 300, 26, juce::Justification::centredLeft);
    {   // the Pastel family mark: four rounded squares
        const juce::uint32 cols[4] = { 0xffe88a8a, 0xff8fd0c2, 0xffe9a3b8, 0xffa9d58e };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour(juce::Colour(cols[i]));
            g.fillRoundedRectangle(24.0f + (float) (i % 2) * 10.0f, 19.0f + (float) (i / 2) * 10.0f, 8.0f, 8.0f, 2.0f);
        }
    }
    g.setColour(PC::textDim);
    g.setFont(lnf.uiFont(12.0f));
    g.drawFittedText(proc.getStatus(), 50, 43, 454, 16, juce::Justification::centredLeft, 1, 0.8f);

    // OUT meter
    g.setFont(lnf.uiFont(10.0f));
    g.drawText("OUT", meterArea.getX(), meterArea.getY(), 30, 26, juce::Justification::centredLeft);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Rectangle<float> bar((float) meterArea.getX() + 30, (float) meterArea.getY() + 4 + ch * 13, 92, 8);
        g.setColour(PC::track);
        g.fillRoundedRectangle(bar, 3.0f);
        float v = juce::jlimit(0.0f, 1.0f, juce::Decibels::gainToDecibels(proc.getMeter(ch), -60.0f) / 60.0f + 1.0f);
        g.setColour(proc.getMeter(ch) > 0.98f ? PC::accentBright : PC::accent);
        if (v > 0.01f) g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * v), 3.0f);
    }
    // limiter gain reduction (lit while the limiter turns peaks down)
    {
        const float gr = proc.getLimiterReductionDb();
        const bool on = proc.apvts.getRawParameterValue(PP::LIMIT)->load() > 0.5f;
        g.setColour(on && gr > 0.05f ? PC::amber : PC::textDim);
        g.setFont(lnf.uiFont(9.5f));
        const juce::String t = !on ? juce::String("LIMIT OFF")
                                   : "GR " + (gr > 0.05f ? "-" + juce::String(gr, 1) : juce::String("0.0")) + " dB";
        g.drawText(t, meterArea.getX() + 30, meterArea.getY() + 26, 92, 11, juce::Justification::centredRight);
    }

    // sample info + engines description
    g.setColour(PC::textDim);
    g.setFont(lnf.uiFont(11.0f));

}

// ============================================================ browser logic
void PastelEditor::refreshShortcuts()
{
    dirBox.clear(juce::dontSendNotification);
    auto sc = proc.library.getShortcuts();
    for (int i = 0; i < sc.size(); ++i) dirBox.addItem(juce::File(sc[i]).getFileName(), i + 1);
    int cur = sc.indexOf(proc.library.getCurrentDir());
    if (cur >= 0) dirBox.setSelectedItemIndex(cur, juce::dontSendNotification);
}

void PastelEditor::refreshFileList()
{
    listModel.entries = proc.library.listEntries(searchBox.getText());
    auto inst = proc.getInstrument();
    listModel.loadedPath = inst ? inst->path : juce::String();
    fileList.updateContent();
    fileList.repaint();
    pathLabel.setText(proc.library.getCurrentDir(), juce::dontSendNotification);
    juce::StringArray paths;
    for (auto& e : listModel.entries) if (!e.isDir) paths.add(e.path);
    proc.rootScanner.request(paths);
}

void PastelEditor::refreshPresetBox()
{
    presetBtn.setButtonText(proc.getPresetName());
}

void PastelEditor::openRow(int row)
{
    if (row < 0 || row >= (int) listModel.entries.size()) return;
    const auto e = listModel.entries[(size_t) row];
    if (e.isDir) proc.library.setCurrentDir(e.path);
    else
    {
        proc.stopPreview();
        proc.loadSample(e.path);
    }
}

void PastelEditor::selectRow(int row)
{
    if (row < 0 || row >= (int) listModel.entries.size()) return;
    const auto& e = listModel.entries[(size_t) row];
    if (!e.isDir && autoPreviewBtn.getToggleState()) proc.previewFile(e.path);
}

void PastelEditor::stepFile(int delta)
{
    juce::StringArray files;
    for (auto& e : proc.library.listEntries()) if (!e.isDir) files.add(e.path);
    if (files.isEmpty()) return;
    auto inst = proc.getInstrument();
    int idx = inst ? files.indexOf(inst->path) : -1;
    idx = idx < 0 ? 0 : (idx + delta + files.size()) % files.size();
    proc.loadSample(files[idx]);
}

bool PastelEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files) if (FileLibrary::isAudioFile(juce::File(f))) return true;
    return false;
}

void PastelEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (FileLibrary::isAudioFile(juce::File(f)))
        {
            proc.library.setCurrentDir(juce::File(f).getParentDirectory().getFullPathName());
            proc.loadSample(f);
            return;
        }
}

// ============================================================ updates
void PastelEditor::instrumentChanged()
{
    wave.refresh();
    partials.refresh();
    spectrum.refresh();
    rootDisplay.repaint();
    piano.refresh();
    auto inst = proc.getInstrument();
    const auto e = proc.getEdits();
    reverseBtn.setToggleState(e.reverse, juce::dontSendNotification);
    infoLabel.setText(inst ? inst->formatInfo : juce::String(), juce::dontSendNotification);
    restoreBtn.setEnabled(e.cropStart > 0 || e.cropEnd >= 0);
    restoreBtn.setAlpha(restoreBtn.isEnabled() ? 1.0f : 0.4f);
    normBtn.setToggleState(e.normalize, juce::dontSendNotification);
    listModel.loadedPath = inst ? inst->path : juce::String();
    fileList.repaint();
    view.repaint(0, 0, kW, 70);
}

void PastelEditor::changeListenerCallback(juce::ChangeBroadcaster* src)
{
    if (src == &proc.library)
    {
        refreshShortcuts();
        refreshFileList();
        return;
    }
    instrumentChanged();
}

void PastelEditor::syncUi()
{
    if (proc.getStatus() != lastStatus)
    {
        lastStatus = proc.getStatus();
        view.repaint(0, 0, 520, 66);
    }
    if (proc.rootScanner.version() != lastScanVersion)
    {
        lastScanVersion = proc.rootScanner.version();
        fileList.repaint();
    }

    // learn button blinks while armed
    const bool armed = proc.isRootLearnArmed();
    flash = (flash + 1) % 30;
    learnBtn.setToggleState(armed && flash < 15, juce::dontSendNotification);

    const int voices = (int) proc.apvts.getRawParameterValue(PP::V_VOICES)->load();
    if (voicesBox.getSelectedId() != voices) voicesBox.setSelectedId(voices, juce::dontSendNotification);

    undoBtn.setEnabled(proc.canUndo());
    redoBtn.setEnabled(proc.canRedo());
    undoBtn.setAlpha(proc.canUndo() ? 1.0f : 0.4f);
    redoBtn.setAlpha(proc.canRedo() ? 1.0f : 0.4f);

    const bool tempered = proc.apvts.getRawParameterValue(PP::V_TUNING)->load() < 0.5f;
    tonicChoice->c.setEnabled(!tempered);
    tonicChoice->c.setAlpha(tempered ? 0.45f : 1.0f);

    const bool over = proc.apvts.getRawParameterValue(PP::ROOTOVR)->load() > 0.5f;
    rootChoice->c.setEnabled(over);
    rootChoice->c.setAlpha(over ? 1.0f : 0.45f);
    fineKnob->s.setEnabled(over);
    fineKnob->s.setAlpha(over ? 1.0f : 0.45f);
    const float rm = proc.getRootMidi();
    rootInfo.setText("MIDI " + juce::String(rm, 2) + juce::String(juce::CharPointer_UTF8("  ·  ")) + juce::String(Analysis::midiToHz(rm), 1) + " Hz",
                     juce::dontSendNotification);

    // repaint parameter-dependent views when relevant values change
    const int eng = juce::jlimit(0, PP::kBlend, (int) proc.apvts.getRawParameterValue(PP::ENGINE)->load());
    if (eng != shownEngine)
    {
        shownEngine = eng;
        showTab(eng); // jump to the panel of the engine that now plays
        return;
    }
    {
        // in Blend, a dot marks every engine in the mix (level not off)
        const juce::String lv[4] { PP::S_LEVEL, PP::A_LEVEL, PP::P_LEVEL, PP::G_LEVEL };
        for (int i = 0; i < 5; ++i)
        {
            const bool dot = eng == PP::kBlend && i < 4 && proc.apvts.getRawParameterValue(lv[i])->load() > -47.9f;
            juce::String t = (dot ? juce::String(juce::CharPointer_UTF8("\u25CF ")) : juce::String()) + PP::engineChoices[i].toUpperCase();
            if (engineBtns[(size_t) i].getButtonText() != t) engineBtns[(size_t) i].setButtonText(t);
        }
        for (auto& f : faders)
        {
            juce::String t = f.s.getValue() <= -47.9 ? juce::String("off") : juce::String(f.s.getValue(), 1) + " dB";
            if (f.val.getText() != t) f.val.setText(t, juce::dontSendNotification);
        }
    }

    // live grains on the waveform while granular voices sound
    const int grains = (int) proc.getGrainPositions().size();
    if (grains > 0 || lastGrainCount > 0) wave.repaint();
    lastGrainCount = grains;

    juce::String key;
    key << proc.loopMode() << ":" << proc.apvts.getRawParameterValue(PP::S_XFADE)->load()
        << ":" << proc.apvts.getRawParameterValue(PP::A_PARTIALS)->load() << ":" << (int) over;
    if (key != lastParamsKey)
    {
        lastParamsKey = key;
        wave.repaint();
        partials.repaint();
        rootDisplay.repaint();
    }
}

void PastelEditor::afterStateJump()
{
    for (int g = 0; g < PastelProcessor::RG_COUNT; ++g)
        lockBtns[(size_t) g].setToggleState(proc.isGroupLocked(g), juce::dontSendNotification);
    refreshPresetBox();
    syncUi();
    instrumentChanged();
    partials.repaint();
}

bool PastelEditor::keyPressed(const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) proc.redo(); else proc.undo();
        afterStateJump();
        return true;
    }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y'))
    {
        proc.redo();
        afterStateJump();
        return true;
    }
    return false;
}

void PastelEditor::timerCallback()
{
    loadBar.setProgress(proc.getLoadProgress(), proc.isLoading());
    // the drive curves dim with their effect's power (the FX pads show BYPASSED themselves)
    const juce::String drivePower[2] { PP::DION, PP::SAON };
    for (int i = 0; i < 2; ++i)
        if (auto* raw = proc.apvts.getRawParameterValue(drivePower[i]); raw != nullptr && driveViews[i] != nullptr)
        {
            const float a = raw->load() > 0.5f ? 1.0f : 0.4f;
            if (driveViews[i]->getAlpha() != a) driveViews[i]->setAlpha(a);
        }
    piano.updateActive();
    piano.refresh();
    view.repaint(meterArea);
    if (currentPage == 2) for (auto& m : lfoMeters) m.repaint();
    preview.repaint();
    syncUi();

}
