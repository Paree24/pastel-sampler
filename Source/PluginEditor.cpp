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
    s->b.setBounds(c.getRight() - 52, c.getY() + 3, 46, 20);
    if (auto* prm = proc.apvts.getParameter(id)) s->b.setTooltip(prm->getName(64));
    parent->addAndMakeVisible(s->b);
    switches.push_back(std::move(s));
    return *switches.back();
}

PastelEditor::Switch& PastelEditor::captionSwitch(const juce::String& id, const juce::String& caption, int cx, int y)
{
    auto& s = headerSwitch(id, {});
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
    : juce::AudioProcessorEditor(p), proc(p), preview(p), wave(p), partials(p), spectrum(p), rootDisplay(p), piano(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    setLookAndFeel(&lnf);
    addAndMakeVisible(view);
    view.setBounds(0, 0, kW, kH);
    view.painter = [this](juce::Graphics& g) { paintView(g); };

    // ---------------- header ----------------
    button(loadBtn, { 520, 22, 76, 30 }, "Open an audio file");
    button(prevBtn, { 602, 22, 30, 30 }, "Previous file in folder");
    button(nextBtn, { 638, 22, 30, 30 }, "Next file in folder");
    button(panicBtn, { 674, 22, 70, 30 }, "Stop all sound");
    button(mainTab, { 764, 22, 80, 30 }, "Sample, engines, root, envelope and filter");
    button(fxTab, { 850, 22, 96, 30 }, "Effects and master");
    mainTab.onClick = [this] { setPage(0); };
    fxTab.onClick = [this] { setPage(1); };
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

    presetBox.setBounds(980, 30, 180, 24);
    view.addAndMakeVisible(presetBox);
    auto* presetLbl = labels.emplace_back(std::make_unique<juce::Label>("", "Preset")).get();
    presetLbl->setFont(lnf.uiFont(11.0f));
    presetLbl->setColour(juce::Label::textColourId, PC::textDim);
    presetLbl->setBounds(978, 15, 120, 13);
    view.addAndMakeVisible(presetLbl);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == 0) return;
        if (id == 1) { proc.resetToInit(); presetName = "Init"; }
        else if (id >= 100)
        {
            juce::Array<juce::File> files = PastelProcessor::presetFolder().findChildFiles(juce::File::findFiles, false, "*.pastelsampler");
            files.sort();
            if (id - 100 < files.size() && proc.loadPreset(files[id - 100])) presetName = files[id - 100].getFileNameWithoutExtension();
        }
        else if (id == 2)
        {
            chooser = std::make_unique<juce::FileChooser>("Save preset", PastelProcessor::presetFolder().getChildFile(presetName + ".pastelsampler"),
                                                          "*.pastelsampler");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this](const juce::FileChooser& fc)
                                 {
                                     auto f = fc.getResult();
                                     if (f == juce::File()) return;
                                     f = f.withFileExtension("pastelsampler");
                                     if (proc.savePreset(f)) presetName = f.getFileNameWithoutExtension();
                                     refreshPresetBox();
                                 });
        }
        else if (id == 3)
        {
            chooser = std::make_unique<juce::FileChooser>("Load preset", PastelProcessor::presetFolder(), "*.pastelsampler");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& fc)
                                 {
                                     auto f = fc.getResult();
                                     if (f.existsAsFile() && proc.loadPreset(f)) presetName = f.getFileNameWithoutExtension();
                                     refreshPresetBox();
                                 });
        }
        refreshPresetBox();
    };
    refreshPresetBox();
    choice(PP::QUALITY, "Quality", PP::qualityChoices, { 1170, 30, 76, 24 }).c.setTooltip("Spectral resolution (FFT 1024 / 2048 / 4096)");

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
        knob(PP::S_XFADE, "Loop xfade", 728, 374, 50);
        knob(PP::S_TUNE, "Tune", 812, 374, 50);
        captionSwitch(PP::S_AUTOLOOP, "AUTO LOOP ON LOAD", 900, 390).b.setTooltip("Find a loop automatically when a sample loads");
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
                 "Freeze: slows to a standstill after the attack.", { 760, 444, 198, 64 });
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
        const juce::String ids[] { PP::E_A, PP::E_D, PP::E_S, PP::E_R, PP::E_CURVE };
        const char* names[] { "Attack", "Decay", "Sustain", "Release", "Curve" };
        for (int i = 0; i < 5; ++i) knob(ids[i], names[i], (int) (976 + 414.0f / 5 * (i + 0.5f)), 326, 46);
    }
    {
        juce::Rectangle<int> c(976, 418, 414, 170);
        card("Filter", c);
        headerSwitch(PP::F_ON, c);
        const juce::String ids[] { PP::F_CUT, PP::F_RES, PP::F_DRIVE, PP::F_KEY };
        const char* names[] { "Cutoff", "Reso", "Drive", "Key track" };
        for (int i = 0; i < 4; ++i) knob(ids[i], names[i], (int) (976 + 414.0f / 4 * (i + 0.5f)), 450, 50);
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
    choice(PP::V_TUNING, "Tuning", PP::tuningChoices, { 790, 644, 110, 24 });
    choice(PP::V_A4, "A4", PP::a4Choices, { 908, 644, 84, 24 });
    choice(PP::V_LOKEY, "Low key", PP::allNoteNames(), { 1000, 644, 70, 24 });
    choice(PP::V_HIKEY, "High key", PP::allNoteNames(), { 1078, 644, 70, 24 });

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

    for (auto it = cards.rbegin(); it != cards.rend(); ++it) (*it)->toBack();
    setPage(0);
    syncUi();

    proc.addChangeListener(this);
    proc.library.addChangeListener(this);
    refreshShortcuts();
    refreshFileList();
    instrumentChanged();

    const double scale = juce::jlimit(0.6, 1.5, proc.library.getUiScale());
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

    auto c = cell(0);
    card("Chorus", c); headerSwitch(PP::CHON, c);
    row(c, { { PP::CHRATE, "Rate" }, { PP::CHDEPTH, "Depth" }, { PP::CHMIX, "Mix" } }, c.getY() + 48, 58);
    bottomCombo(PP::CHMODE, PP::chorusModes, c, c.getX() + 10, c.getWidth() - 20);

    c = cell(1);
    card("Phaser", c); headerSwitch(PP::PHON, c);
    row(c, { { PP::PHRATE, "Rate" }, { PP::PHDEPTH, "Depth" } }, c.getY() + 40, 58);
    row(c, { { PP::PHFB, "Fdbk" }, { PP::PHMIX, "Mix" } }, c.getY() + 132, 58);

    c = cell(2);
    card("Flanger", c); headerSwitch(PP::FLON, c);
    row(c, { { PP::FLRATE, "Rate" }, { PP::FLDEPTH, "Depth" } }, c.getY() + 40, 58);
    row(c, { { PP::FLFB, "Fdbk" }, { PP::FLMIX, "Mix" } }, c.getY() + 132, 58);

    c = cell(3);
    card("Distortion", c); headerSwitch(PP::DION, c);
    row(c, { { PP::DIDRIVE, "Drive" }, { PP::DIMIX, "Mix" } }, c.getY() + 70, 64);

    c = cell(4);
    card("Saturation", c); headerSwitch(PP::SAON, c);
    row(c, { { PP::SAAMT, "Amount" }, { PP::SATONE, "Tone" } }, c.getY() + 48, 58);
    bottomCombo(PP::SAMODE, PP::satModes, c, c.getX() + 10, c.getWidth() - 20);

    c = cell(5);
    card("Reverb", c); headerSwitch(PP::RVON, c);
    row(c, { { PP::RVSIZE, "Size" }, { PP::RVDAMP, "Damp" }, { PP::RVMIX, "Mix" } }, c.getY() + 70, 58);

    c = cell(6);
    card("Delay", c); headerSwitch(PP::DLON, c);
    row(c, { { PP::DLTIME, "Time" }, { PP::DLFB, "Fdbk" }, { PP::DLMIX, "Mix" } }, c.getY() + 48, 58);
    captionSwitch(PP::DLSYNC, "SYNC", c.getX() + 34, c.getBottom() - 36);
    choice(PP::DLDIV, "Div", PP::delayDivs, { c.getX() + 66, c.getBottom() - 36, c.getWidth() - 76, 24 });

    c = cell(7);
    card("Master", c);
    knob(PP::VOLUME, "Volume", c.getCentreX(), c.getY() + 48, 72);
    captionSwitch(PP::LIMIT, "LIMITER", c.getCentreX() - 40, c.getBottom() - 44).b.setTooltip("Soft limiter above -0.9 dBFS");
    captionSwitch(PP::V_MONO, "MONO", c.getCentreX() + 40, c.getBottom() - 44);

    parent = &view;
}

void PastelEditor::setPage(int page)
{
    currentPage = page;
    updateVisibility();
    fxPage.setVisible(page == 1);
    mainTab.setToggleState(page == 0, juce::dontSendNotification);
    fxTab.setToggleState(page == 1, juce::dontSendNotification);
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
    g.drawText("PASTEL SAMPLER", 24, 16, 300, 26, juce::Justification::centredLeft);
    g.setColour(PC::textDim);
    g.setFont(lnf.uiFont(12.0f));
    g.drawFittedText(proc.getStatus(), 24, 43, 480, 16, juce::Justification::centredLeft, 1, 0.8f);

    // OUT meter
    g.setFont(lnf.uiFont(10.0f));
    g.drawText("OUT", meterArea.getX(), meterArea.getY(), 30, meterArea.getHeight(), juce::Justification::centredLeft);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Rectangle<float> bar((float) meterArea.getX() + 30, (float) meterArea.getY() + 4 + ch * 13, 92, 8);
        g.setColour(PC::track);
        g.fillRoundedRectangle(bar, 3.0f);
        float v = juce::jlimit(0.0f, 1.0f, juce::Decibels::gainToDecibels(proc.getMeter(ch), -60.0f) / 60.0f + 1.0f);
        g.setColour(proc.getMeter(ch) > 0.98f ? PC::accentBright : PC::accent);
        if (v > 0.01f) g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * v), 3.0f);
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
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem("Init", 1);
    juce::Array<juce::File> files = PastelProcessor::presetFolder().findChildFiles(juce::File::findFiles, false, "*.pastelsampler");
    files.sort();
    if (!files.isEmpty()) presetBox.addSeparator();
    for (int i = 0; i < files.size(); ++i) presetBox.addItem(files[i].getFileNameWithoutExtension(), 100 + i);
    presetBox.addSeparator();
    presetBox.addItem("Save preset...", 2);
    presetBox.addItem("Load preset file...", 3);
    presetBox.setText(presetName, juce::dontSendNotification);
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

void PastelEditor::timerCallback()
{
    loadBar.setProgress(proc.getLoadProgress(), proc.isLoading());
    piano.updateActive();
    piano.refresh();
    view.repaint(meterArea);
    preview.repaint();
    syncUi();

}
