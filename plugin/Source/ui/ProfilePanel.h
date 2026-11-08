#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace ui {

class ProfilePanel : public juce::Component, private SonicDNAProcessor::Listener {
public:
    explicit ProfilePanel(SonicDNAProcessor& p);
    ~ProfilePanel() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();

private:
    void profileChanged() override { refresh(); }
    void kitChanged() override { refresh(); }

    class Content : public juce::Component {
    public:
        explicit Content(ProfilePanel& o) : owner(o) {}
        void paint(juce::Graphics&) override;
        void resized() override;
        int layoutHeight(int width);
        ProfilePanel& owner;
        int summaryH = 60, descY = 0, infY = 0, sigY = 0, tagsY = 0, metaY = 0, promptY = 0;
    };

    struct Row { juce::String label; std::unique_ptr<juce::Slider> slider; };
    SonicDNAProcessor& proc;
    Content content{ *this };
    juce::Viewport viewport;
    std::vector<Row> desc, infl;
    friend class Content;
};

} // namespace ui
