#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PastelLookAndFeel.h"
#include "FactoryPresets.h"

class PastelProcessor;

// Preset browser overlay (shared design, as in the Pastel Soundfont Player / ERSA / Midi
// Note Modifier): search + All/Factory/User filter + audition list. Factory rows are sound
// recipes played on the loaded sample; user rows are .pastelsampler files (sample + all
// settings). Clicking a row loads it, double-click or a click on the scrim closes. Inline
// name field + Save; saving over an existing preset needs a second click.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    PresetBrowser(PastelProcessor& p, PastelLookAndFeel& lnf);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onClose, onLoaded; // onLoaded: refresh the editor's custom views

private:
    struct Row { bool isUser = false; int index = -1; juce::String name; juce::File file; };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void rebuildRows();
    void refreshBankButtons();
    void clickSave();
    void setOverwriteArmed(bool on);

    PastelProcessor& proc;
    PastelLookAndFeel& lnf;
    juce::TextEditor search, nameBox;
    bool overwriteArmed = false;
    juce::ListBox list { "presets", this };
    juce::TextButton closeBtn { "X" }, saveBtn { "SAVE" }, delBtn { "DELETE" };
    juce::TextButton bankBtns[3] { juce::TextButton { "ALL" }, juce::TextButton { "FACTORY" }, juce::TextButton { "USER" } };
    std::vector<Row> rows;
    int activeBank = 0; // 0 all, 1 factory, 2 user
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
