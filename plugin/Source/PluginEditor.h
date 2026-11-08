#pragma once

#include "PluginProcessor.h"
#include "ui/DrumMachineView.h"
#include "ui/LibraryView.h"
#include "ui/ProfilePanel.h"
#include "ui/Theme.h"

class SonicDNAEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       private SonicDNAProcessor::Listener,
                       private juce::TextEditor::Listener,
                       private juce::Timer {
public:
    explicit SonicDNAEditor(SonicDNAProcessor&);
    ~SonicDNAEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

protected:
    bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files, bool& canMoveFiles) override;

private:
    void statusChanged() override;
    void profileChanged() override;
    void kitChanged() override;
    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;
    void timerCallback() override;
    void onGenerate();
    void showTab(int t);
    void openSettings();
    void updateSuggestions();
    void runAutotestStep();

    SonicDNAProcessor& proc;
    ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips{ this, 600 };

    juce::TextEditor artist, prompt;
    ui::FlatButton generateBtn{ "GENERATE" }, cancelBtn{ "CANCEL", false };
    juce::ToggleButton onlineToggle{ "RESEARCH ONLINE" };
    juce::ComboBox perCat, rate, bits;
    ui::IconButton settingsBtn{ "Settings", ui::icon::gear() };
    ui::FlatButton tabLib{ "LIBRARY" }, tabDrum{ "DRUM MACHINE" };
    ui::FlatButton saveBtn{ "SAVE KIT", false }, loadBtn{ "LOAD KIT", false }, exportBtn{ "EXPORT KIT .ZIP", false };

    struct Suggest : juce::Component {
        juce::StringArray items; int hover = -1; std::function<void(juce::String)> onPick;
        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent& e) override { hover = e.y / 28; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown(const juce::MouseEvent& e) override { int i = e.y / 28; if (i >= 0 && i < items.size() && onPick) onPick(items[i]); }
    } suggest;

    ui::ProfilePanel profilePanel;
    ui::LibraryView library;
    ui::DrumMachineView drums;
    int tab = 0;
    bool pendingGenerate = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastAnalysed;
    // Automated end-to-end UI test (SONICDNA_AUTOTEST=<output dir>)
    juce::File autotestDir; int autotestStep = -1; int autotestWait = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SonicDNAEditor)
};
