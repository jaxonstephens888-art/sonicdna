#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace ui {

class ProfilePanel : public juce::Component, private SonicDNAProcessor::Listener, private juce::Timer {
public:
    explicit ProfilePanel(SonicDNAProcessor& p);
    ~ProfilePanel() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();

private:
    void profileChanged() override { refresh(); }
    void kitChanged() override { refresh(); }
    void statusChanged() override { refresh(); }
    void timerCallback() override;

    class Content : public juce::Component {
    public:
        explicit Content(ProfilePanel& o) : owner(o) {}
        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;
        std::vector<juce::Rectangle<int>> removeRects;
        int refY = 0;
        int layoutHeight(int width);
        ProfilePanel& owner;
        int summaryH = 60, descY = 0, infY = 0, sigY = 0, tagsY = 0, metaY = 0, promptY = 0;
    };

    struct Row { juce::String label; std::unique_ptr<juce::Slider> slider; };
    SonicDNAProcessor& proc;
    Content content{ *this };
    juce::Viewport viewport;
    std::vector<Row> desc, infl;
    FlatButton addRef{ "+ AUDIO", false }, clearRef{ "CLEAR", false };
    std::unique_ptr<juce::Slider> refWeight;
    std::unique_ptr<juce::FileChooser> chooser;
    bool wasListening = false;
    friend class Content;
};

} // namespace ui
