#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"
#include "PianoStrip.h"
#include "Widgets.h"
#include "PresetBrowser.h"
#include "PastelViews.h"
#include "MidiLearn.h"

// Browser list: folders first, then audio files with a format tag and detected root.
class FileListModel : public juce::ListBoxModel
{
public:
    std::vector<FileLibrary::Entry> entries;
    RootScanner* scanner = nullptr;
    juce::String loadedPath;
    std::function<void(int)> onOpen, onSelect;

    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { if (onOpen) onOpen(row); }
    void returnKeyPressed(int row) override { if (onOpen) onOpen(row); }
    void selectedRowsChanged(int row) override { if (onSelect) onSelect(row); }
};

class PastelEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit PastelEditor(PastelProcessor&);
    ~PastelEditor() override;

    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct View : juce::Component
    {
        std::function<void(juce::Graphics&)> painter;
        void paint(juce::Graphics& g) override { if (painter) painter(g); }
    };
    struct Knob
    {
        juce::Slider s;
        juce::Label l;
        std::unique_ptr<APVTS::SliderAttachment> a;
    };
    struct Switch
    {
        juce::ToggleButton b;
        juce::Label cap;
        std::unique_ptr<APVTS::ButtonAttachment> a;
    };
    struct Choice
    {
        juce::ComboBox c;
        juce::Label l;
        std::unique_ptr<APVTS::ComboBoxAttachment> a;
    };

    Knob& knob(const juce::String& id, const juce::String& title, int cx, int top, int size);
    Switch& headerSwitch(const juce::String& id, juce::Rectangle<int> card);
    Switch& captionSwitch(const juce::String& id, const juce::String& caption, int cx, int y);
    Choice& choice(const juce::String& id, const juce::String& title, const juce::StringArray& items,
                   juce::Rectangle<int> box, int idOffset = 1);
    juce::GroupComponent& card(const juce::String& title, juce::Rectangle<int> r);
    void button(juce::TextButton& b, juce::Rectangle<int> r, const juce::String& tip = {});

    void paintView(juce::Graphics&);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshFileList();
    void refreshShortcuts();
    void refreshPresetBox();
    void openRow(int row);
    void selectRow(int row);
    void stepFile(int delta);
    void instrumentChanged();

    PastelProcessor& proc;
    PastelLookAndFeel lnf;
    View view;
    juce::TooltipWindow tooltips { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<juce::GroupComponent>> cards;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::Component>> views;   // v3 direct-manipulation views (PastelViews)
    pastel::DriveView* driveViews[2] {};                     // distortion / saturation: dimmed while off
    template <typename V, typename... Args>
    V* addView(juce::Rectangle<int> bounds, Args&&... args)
    {
        auto* v = new V(std::forward<Args>(args)...);
        views.emplace_back(v);
        parent->addAndMakeVisible(v);
        v->setBounds(bounds);
        return v;
    }

    // header
    juce::TextButton loadBtn { "LOAD" }, prevBtn { "<" }, nextBtn { ">" }, panicBtn { "PANIC" };
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" };
    void afterStateJump(); // after undo / redo / preset / randomize: refresh custom views
    juce::TextButton presetBtn; // shows the current preset name, opens the browser
    PresetBrowser presetBrowser;
    pastel::MidiLearnButton midiBtn { proc.midiLearn };

    // browser
    juce::TextEditor searchBox;
    juce::ComboBox dirBox;
    juce::TextButton addDirBtn { "+ ADD" }, delDirBtn { "DEL" }, upBtn { "UP" }, scanBtn { "SCAN" }, loadSelBtn { "LOAD >" };
    juce::Label pathLabel;
    LoadBar loadBar;
    juce::ListBox fileList;
    FileListModel listModel;
    PreviewStrip preview;
    juce::ToggleButton autoPreviewBtn;

    // sample
    juce::TextButton reverseBtn { "REVERSE" }, normBtn { "NORMALIZE" }, trimBtn { "TRIM" }, restoreBtn { "RESTORE" },
                     findLoopBtn { "FIND LOOP" }, zoomBtn { "ZOOM +" };
    WaveformView wave;
    juce::Label infoLabel;

    // engines / root
    PartialView partials;
    SpectrumView spectrum;
    RootDisplay rootDisplay;
    juce::Label rootInfo;
    juce::TextButton learnBtn { "MIDI LEARN" }, redetectBtn { "RE-DETECT" };
    Switch* overrideSwitch = nullptr;
    Choice* rootChoice = nullptr;
    Choice* tonicChoice = nullptr;
    Knob* fineKnob = nullptr;
    juce::ComboBox voicesBox;
    std::array<juce::TextButton, 5> engineBtns;
    struct Fader
    {
        juce::Slider s;
        juce::Label name, val;
        std::unique_ptr<APVTS::SliderAttachment> a;
    };
    std::array<Fader, 4> faders;
    std::array<std::vector<juce::Component*>, 5> tabComps; // engine tabs: Sampler .. Blend
    int currentTab = 0, currentPage = 0;
    juce::TextButton loopBtn { "LOOP" };
    std::unique_ptr<APVTS::ButtonAttachment> loopAttach;
    void showTab(int tab);
    void updateVisibility();
    // pages: MAIN (sample / engines / root / env / filter) and EFFECTS
    juce::TextButton mainTab { "MAIN" }, fxTab { "EFFECTS" }, modTab { "MOD" };
    juce::Component fxPage, modPage;
    // MOD page: LFOs, matrix, arp, randomize
    juce::TextButton randomBtn { "RANDOMIZE" }, mutateBtn { "MUTATE" };
    std::array<juce::TextButton, PastelProcessor::RG_COUNT> lockBtns;
    std::array<View, 2> lfoMeters; // live LFO value per card
    void buildModPage();
    std::vector<juce::Component*> mainComps;
    juce::Component* parent = &view; // where the build helpers add components
    void buildEffectsPage();
    void setPage(int page);
    void syncUi(); // reflect parameters in custom controls (also called by the timer)
    int shownEngine = -1, lastGrainCount = 0;

    PianoStrip piano;
    View legend;

    int lastScanVersion = -1, flash = 0;
    juce::String lastStatus, lastParamsKey;
    juce::Rectangle<int> meterArea { 1258, 26, 122, 38 }; // OUT bars + limiter GR readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PastelEditor)
};
