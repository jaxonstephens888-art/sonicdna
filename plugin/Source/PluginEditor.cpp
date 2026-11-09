#include "PluginEditor.h"

using namespace sdna;
using namespace ui;

static constexpr int kHeaderH = 150, kFooterH = 40, kProfileW = 318, kTabH = 40;

// ---------------------------------------------------------------------------
class SettingsComponent : public juce::Component {
public:
    std::function<void()> onClose;
    SettingsComponent() {
        auto& s = services::Settings::get();
        for (auto* c : std::initializer_list<juce::Component*>{ &key, &model, &folderBtn, &autosave, &save, &cancel }) addAndMakeVisible(c);
        key.setPasswordCharacter((juce::juce_wchar)0x2022);
        key.setText(s.apiKey(), false);
        key.setTextToShowWhenEmpty(U8("sk-ant-…"), col::faint);
        model.setText(s.model(), false);
        folder = s.kitsFolder();
        folderBtn.setButtonText(folder.getFullPathName());
        folderBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Kits folder", folder);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                if (fc.getResult().isDirectory()) { folder = fc.getResult(); folderBtn.setButtonText(folder.getFullPathName()); }
            });
        };
        autosave.setToggleState(s.autoSave(), juce::dontSendNotification);
        for (auto* e : { &key, &model }) { e->setFont(mono(13.0f)); e->setIndents(10, 8); }
        save.primary = true;
        save.onClick = [this] {
            auto& st = services::Settings::get();
            st.setApiKey(key.getText()); st.setModel(model.getText()); st.setKitsFolder(folder); st.setAutoSave(autosave.getToggleState());
            if (onClose) onClose();
        };
        cancel.onClick = [this] { if (onClose) onClose(); };
        setSize(560, 430);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(col::panel);
        auto r = getLocalBounds().reduced(24);
        g.setColour(col::text); g.setFont(sans(18.0f, 600)); g.drawText("Settings", r.removeFromTop(30), juce::Justification::centredLeft);
        drawSectionLabel(g, U8("Anthropic API key — online analysis only"), { 24, 64, 500, 14 });
        drawSectionLabel(g, "Model", { 24, 140, 500, 14 });
        drawSectionLabel(g, "Kits folder (auto-save)", { 24, 206, 500, 14 });
        g.setColour(col::dim); g.setFont(sans(12.0f));
        g.drawFittedText("Sound generation is 100% local and works offline. Only RESEARCH ONLINE sends the artist name to api.anthropic.com "
                         "(Claude with web search) and needs internet. The key is stored in plain text in " + services::Settings::get().settingsFile().getFullPathName()
                         + U8(" — or set the SONICDNA_ANTHROPIC_API_KEY environment variable instead."), 24, 300, 512, 64, juce::Justification::topLeft, 5);
    }
    void resized() override {
        key.setBounds(24, 84, 512, 34);
        model.setBounds(24, 160, 260, 34);
        folderBtn.setBounds(24, 226, 512, 30);
        autosave.setBounds(24, 262, 300, 24);
        cancel.setBounds(getWidth() - 24 - 100 - 110, getHeight() - 24 - 34, 100, 34);
        save.setBounds(getWidth() - 24 - 100, getHeight() - 24 - 34, 100, 34);
    }
private:
    juce::TextEditor key, model;
    FlatButton folderBtn{ "", false }, save{ "SAVE" }, cancel{ "CANCEL", false };
    juce::ToggleButton autosave{ "Auto-save every generated kit" };
    juce::File folder;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ---------------------------------------------------------------------------
void SonicDNAEditor::Suggest::paint(juce::Graphics& g) {
    g.fillAll(col::panel2);
    g.setColour(col::lineHi); g.drawRect(getLocalBounds());
    for (int i = 0; i < items.size(); ++i) {
        auto r = juce::Rectangle<int>(0, i * 28, getWidth(), 28);
        if (i == hover) { g.setColour(col::raised); g.fillRect(r.reduced(1)); }
        g.setColour(col::text); g.setFont(sans(14.0f));
        g.drawText(items[i], r.withTrimmedLeft(16), juce::Justification::centredLeft);
    }
}

SonicDNAEditor::SonicDNAEditor(SonicDNAProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), profilePanel(p), library(p), drums(p) {
    setLookAndFeel(&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);

    artist.setFont(sans(26.0f, 500));
    artist.setTextToShowWhenEmpty(U8("Enter an artist, producer or genre…"), col::faint);
    artist.setIndents(18, 12);
    artist.setText(p.query, false);
    artist.addListener(this);
    artist.setSelectAllWhenFocused(true);
    prompt.setFont(sans(14.0f));
    prompt.setTextToShowWhenEmpty(U8("Direction — e.g. make it darker, more distorted, more lo-fi, less reverb, faster"), col::faint);
    prompt.setIndents(14, 7);
    prompt.setText(p.prompt, false);
    prompt.onTextChange = [this] { proc.prompt = prompt.getText(); profilePanel.refresh(); };

    generateBtn.primary = true;
    generateBtn.setTooltip("Synthesise a complete original kit from the sonic profile (offline, runs in the background)");
    generateBtn.onClick = [this] { onGenerate(); };
    cancelBtn.onClick = [this] { proc.cancelGeneration(); };
    onlineToggle.setTooltip(U8("Research the artist with Claude + web search before generating (needs internet and an API key — see Settings)"));
    settingsBtn.onClick = [this] { openSettings(); };
    settingsBtn.setTooltip("Settings");

    for (int n : { 5, 8, 12, 16, 20 }) perCat.addItem(juce::String(n) + " / category", n);
    perCat.setSelectedId(services::Settings::get().perCategory(), juce::dontSendNotification);
    if (perCat.getSelectedId() == 0) perCat.setSelectedId(8, juce::dontSendNotification);
    perCat.onChange = [this] { services::Settings::get().setPerCategory(perCat.getSelectedId()); };
    perCat.setTooltip("Variations per category (39 categories)");
    rate.addItem("44.1 kHz", 44100); rate.addItem("48 kHz", 48000);
    rate.setSelectedId((int)services::Settings::get().sampleRate(), juce::dontSendNotification);
    rate.onChange = [this] { services::Settings::get().setSampleRate(rate.getSelectedId()); };
    rate.setTooltip("Generation and export sample rate");
    bits.addItem("16-bit WAV", 16); bits.addItem("24-bit WAV", 24); bits.addItem("32-bit float", 32);
    bits.setSelectedId(services::Settings::get().exportBits(), juce::dontSendNotification);
    bits.onChange = [this] { services::Settings::get().setExportBits(bits.getSelectedId()); };
    bits.setTooltip("Export bit depth (16-bit is TPDF-dithered)");

    tabLib.setClickingTogglesState(false); tabDrum.setClickingTogglesState(false);
    tabLib.onClick = [this] { showTab(0); };
    tabDrum.onClick = [this] { showTab(1); };

    saveBtn.onClick = [this] {
        if (!proc.kit) return;
        chooser = std::make_unique<juce::FileChooser>("Save kit", services::Settings::get().kitsFolder().getChildFile(juce::String(proc.kit->slug()) + ".sdnakit"), "*.sdnakit");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc.saveKitAsync(fc.getResult().withFileExtension("sdnakit")); });
    };
    loadBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load kit", services::Settings::get().kitsFolder(), "*.sdnakit");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadKitAsync(fc.getResult()); });
    };
    exportBtn.onClick = [this] {
        if (!proc.kit) return;
        chooser = std::make_unique<juce::FileChooser>("Export sample pack", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("SonicDNA_" + juce::String(proc.kit->slug()) + ".zip"), "*.zip");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc.exportKitZipAsync(fc.getResult().withFileExtension("zip")); });
    };
    saveBtn.setTooltip("Save kit + pattern as a .sdnakit file (lossless)");
    loadBtn.setTooltip("Load a previously generated .sdnakit");
    exportBtn.setTooltip("Export every sample as WAV in folders, plus MIDI pattern, rendered loop and kit.json");

    for (auto* c : std::initializer_list<juce::Component*>{ &artist, &prompt, &generateBtn, &cancelBtn, &onlineToggle, &perCat, &rate, &bits, &settingsBtn,
                                                            &tabLib, &tabDrum, &saveBtn, &loadBtn, &exportBtn, &profilePanel, &library, &drums })
        addAndMakeVisible(c);
    addChildComponent(suggest);
    suggest.onPick = [this](juce::String s) { artist.setText(s, false); suggest.setVisible(false); lastAnalysed = s; proc.analyse(s, false); };

    proc.addListener(this);
    showTab(0);
    statusChanged();
    setResizable(true, true);
    setResizeLimits(1120, 720, 2400, 1600);
    setSize(1320, 860);
    startTimerHz(20);

    auto at = juce::SystemStats::getEnvironmentVariable("SONICDNA_AUTOTEST", {});
    if (at.isNotEmpty()) { autotestDir = juce::File(at); autotestDir.createDirectory(); autotestStep = 0; }
}

SonicDNAEditor::~SonicDNAEditor() {
    proc.removeListener(this);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void SonicDNAEditor::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    // Header
    auto head = getLocalBounds().removeFromTop(kHeaderH);
    g.setColour(col::panel); g.fillRect(head);
    g.setColour(col::line); g.drawHorizontalLine(kHeaderH - 1, 0.0f, (float)getWidth());
    auto top = head.removeFromTop(44).reduced(20, 0);
    g.setColour(col::text); g.setFont(mono(14.0f, 500));
    juce::GlyphArrangement ga; ga.addLineOfText(mono(14.0f, 500), "S O N I C   D N A", 0, 0);
    g.drawText("S O N I C   D N A", top, juce::Justification::centredLeft);
    int wx = top.getX() + (int)ga.getBoundingBox(0, -1, true).getWidth() + 14;
    g.setColour(col::accent); g.fillEllipse((float)wx, (float)top.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(col::faint); g.setFont(mono(10.5f));
    g.drawText("AI DRUM KIT & SAMPLE GENERATOR", wx + 16, top.getY(), 300, top.getHeight(), juce::Justification::centredLeft);
    // connectivity badges
    bool hasKey = services::Settings::get().apiKey().isNotEmpty();
    auto badge = [&](juce::Rectangle<int> r, const juce::String& t, juce::Colour c) {
        g.setColour(c.withAlpha(0.1f)); g.fillRoundedRectangle(r.toFloat(), 2.0f);
        g.setColour(c); g.setFont(mono(10.0f, 500)); g.drawText(t, r, juce::Justification::centred);
    };
    auto br = top.withTrimmedRight(40);
    badge(br.removeFromRight(210).withSizeKeepingCentre(210, 20), hasKey ? U8("ONLINE ANALYSIS · KEY SET") : U8("ONLINE ANALYSIS · NO KEY"), hasKey ? col::ok : col::faint);
    br.removeFromRight(8);
    badge(br.removeFromRight(170).withSizeKeepingCentre(170, 20), U8("SYNTHESIS · OFFLINE"), col::text);
    // progress
    if (proc.isGenerating() || proc.isAnalysing() || proc.isListening()) {
        auto pr = juce::Rectangle<float>(0, (float)kHeaderH - 2, (float)getWidth(), 2.0f);
        g.setColour(col::panel2); g.fillRect(pr);
        g.setColour(col::accent);
        if (proc.isGenerating()) g.fillRect(pr.withWidth(pr.getWidth() * proc.generationProgress()));
        else if (proc.isListening()) g.fillRect(pr.withWidth(pr.getWidth() * proc.listeningProgress()));
        else { float t = (float)(juce::Time::getMillisecondCounter() % 1400) / 1400.0f; g.fillRect(pr.withX(pr.getWidth() * t - 120).withWidth(240)); }
    }
    // Tab bar
    auto body = getLocalBounds().withTrimmedTop(kHeaderH).withTrimmedBottom(kFooterH).withTrimmedLeft(kProfileW);
    g.setColour(col::line);
    g.drawVerticalLine(kProfileW - 1, (float)kHeaderH, (float)(getHeight() - kFooterH));
    g.drawHorizontalLine(body.getY() + kTabH - 1, (float)body.getX(), (float)getWidth());
    // Footer
    auto foot = getLocalBounds().removeFromBottom(kFooterH);
    g.setColour(col::panel); g.fillRect(foot);
    g.setColour(col::line); g.drawHorizontalLine(foot.getY(), 0.0f, (float)getWidth());
    juce::String st = proc.status;
    if (proc.isGenerating()) st = "Generating  " + juce::String((int)std::round(proc.generationProgress() * 100)) + "%   " + proc.generationLabel();
    g.setColour(proc.statusIsError ? col::warn : col::dim);
    g.setFont(mono(11.0f));
    g.drawFittedText(st, foot.reduced(20, 0).withTrimmedRight(470), juce::Justification::centredLeft, 1, 0.8f);
}

void SonicDNAEditor::resized() {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(kHeaderH);
    auto top = head.removeFromTop(44).reduced(20, 0);
    settingsBtn.setBounds(top.removeFromRight(30).withSizeKeepingCentre(30, 30));
    auto row = head.removeFromTop(58).reduced(20, 0);
    generateBtn.setBounds(row.removeFromRight(180));
    
    row.removeFromRight(12);
    onlineToggle.setBounds(row.removeFromRight(160).withSizeKeepingCentre(160, 26));
    cancelBtn.setBounds(onlineToggle.getBounds().withSizeKeepingCentre(120, 34));
    row.removeFromRight(12);
    artist.setBounds(row);
    auto row2 = head.removeFromTop(48).reduced(20, 9);
    bits.setBounds(row2.removeFromRight(116)); row2.removeFromRight(6);
    rate.setBounds(row2.removeFromRight(92)); row2.removeFromRight(6);
    perCat.setBounds(row2.removeFromRight(128)); row2.removeFromRight(12);
    prompt.setBounds(row2.withTrimmedRight(192));
    auto foot = r.removeFromBottom(kFooterH).reduced(20, 7);
    exportBtn.setBounds(foot.removeFromRight(150)); foot.removeFromRight(6);
    loadBtn.setBounds(foot.removeFromRight(96)); foot.removeFromRight(6);
    saveBtn.setBounds(foot.removeFromRight(96));
    profilePanel.setBounds(r.removeFromLeft(kProfileW - 1));
    auto tabs = r.removeFromTop(kTabH).reduced(16, 7);
    tabLib.setBounds(tabs.removeFromLeft(100)); tabs.removeFromLeft(6);
    tabDrum.setBounds(tabs.removeFromLeft(130));
    library.setBounds(r);
    drums.setBounds(r);
    suggest.setBounds(artist.getX(), artist.getBottom() + 2, artist.getWidth() / 2, 28 * juce::jmax(1, suggest.items.size()));
}

void SonicDNAEditor::showTab(int t) {
    tab = t;
    library.setVisible(t == 0); drums.setVisible(t == 1);
    tabLib.setToggleState(t == 0, juce::dontSendNotification);
    tabDrum.setToggleState(t == 1, juce::dontSendNotification);
}

void SonicDNAEditor::onGenerate() {
    suggest.setVisible(false);
    if (proc.isGenerating()) return;
    auto q = artist.getText().trim();
    proc.prompt = prompt.getText();
    bool online = onlineToggle.getToggleState();
    if (q != lastAnalysed || (online && proc.profile.source.rfind("online", 0) != 0)) {
        lastAnalysed = q;
        proc.analyse(q, online);
        if (proc.isAnalysing()) { pendingGenerate = true; return; }
    }
    proc.generate();
}

void SonicDNAEditor::statusChanged() {
    bool busy = proc.isGenerating() || proc.isAnalysing();
    generateBtn.setEnabled(!busy);
    generateBtn.setButtonText(proc.isAnalysing() ? U8("RESEARCHING…") : proc.isGenerating() ? U8("GENERATING…") : juce::String("GENERATE"));
    cancelBtn.setVisible(proc.isGenerating());
    onlineToggle.setVisible(!proc.isGenerating());
    saveBtn.setEnabled(proc.kit != nullptr); exportBtn.setEnabled(proc.kit != nullptr);
    if (pendingGenerate && !proc.isAnalysing()) { pendingGenerate = false; proc.generate(); }
    repaint();
}

void SonicDNAEditor::profileChanged() {
    if (!artist.hasKeyboardFocus(false) && proc.query.isNotEmpty() && artist.getText() != proc.query) artist.setText(proc.query, false);
    if (prompt.getText() != proc.prompt) prompt.setText(proc.prompt, false);
    repaint();
}

void SonicDNAEditor::kitChanged() {
    statusChanged();
    if (proc.query.isNotEmpty()) lastAnalysed = artist.getText().trim();
}

void SonicDNAEditor::timerCallback() {
    if (proc.isGenerating() || proc.isAnalysing() || proc.isListening()) {
        repaint(0, kHeaderH - 2, getWidth(), 2);
        repaint(getLocalBounds().removeFromBottom(kFooterH));
    }
    if (autotestStep >= 0) runAutotestStep();
}

void SonicDNAEditor::updateSuggestions() {
    auto t = artist.getText().trim();
    suggest.items.clear();
    if (t.length() >= 2 && artist.hasKeyboardFocus(false))
        for (auto& s : suggestNames(t.toStdString(), 6)) if (juce::String(s).compareIgnoreCase(t) != 0) suggest.items.add(juce::String::fromUTF8(s.c_str()));
    suggest.setVisible(!suggest.items.isEmpty());
    resized();
    suggest.toFront(false);
}

void SonicDNAEditor::textEditorTextChanged(juce::TextEditor& e) { if (&e == &artist) updateSuggestions(); }
void SonicDNAEditor::textEditorReturnKeyPressed(juce::TextEditor& e) {
    if (&e != &artist) return;
    suggest.setVisible(false);
    lastAnalysed = artist.getText().trim();
    proc.analyse(lastAnalysed, onlineToggle.getToggleState());
}
void SonicDNAEditor::textEditorEscapeKeyPressed(juce::TextEditor&) { suggest.setVisible(false); }
void SonicDNAEditor::textEditorFocusLost(juce::TextEditor& e) {
    if (&e == &artist) juce::Timer::callAfterDelay(200, [sp = juce::Component::SafePointer<SonicDNAEditor>(this)] { if (sp) sp->suggest.setVisible(false); });
}

bool SonicDNAEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (auto& f : files) if (SonicDNAProcessor::isSupportedAudioFile(juce::File(f))) return true;
    return false;
}

void SonicDNAEditor::filesDropped(const juce::StringArray& files, int, int) {
    fileDragActive = false;
    juce::Array<juce::File> list;
    for (auto& f : files) if (SonicDNAProcessor::isSupportedAudioFile(juce::File(f))) list.add(juce::File(f));
    proc.addReferenceFiles(list);
    repaint();
}

void SonicDNAEditor::paintOverChildren(juce::Graphics& g) {
    if (!fileDragActive) return;
    auto r = getLocalBounds().reduced(12).toFloat();
    g.setColour(col::bg.withAlpha(0.82f)); g.fillRect(getLocalBounds());
    g.setColour(col::accent); g.drawRoundedRectangle(r, 8.0f, 2.0f);
    g.setFont(sans(22.0f, 600)); g.setColour(col::text);
    g.drawText("Drop to use as reference audio", r.withTrimmedBottom(40), juce::Justification::centred);
    g.setFont(sans(14.0f)); g.setColour(col::dim);
    g.drawText(U8("Analysed on this computer — tempo, key, chords, groove, drum sound and tone. Nothing is copied or uploaded."), r.withTrimmedTop(40), juce::Justification::centred);
}

bool SonicDNAEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::spaceKey && !artist.hasKeyboardFocus(false) && !prompt.hasKeyboardFocus(false)) {
        proc.machine.playing = !proc.machine.playing.load();
        drums.repaint();
        return true;
    }
    return false;
}

bool SonicDNAEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& d, juce::StringArray& files, bool& canMove) {
    auto desc = d.description.toString();
    if (!desc.startsWith("sample:") || !proc.kit) return false;
    auto s = proc.kit->find(desc.fromFirstOccurrenceOf("sample:", false, false).toStdString());
    if (!s) return false;
    auto f = proc.writeTempSample(s);
    if (!f.existsAsFile()) return false;
    files.add(f.getFullPathName());
    canMove = false;
    return true;
}

void SonicDNAEditor::openSettings() {
    auto* c = new SettingsComponent();
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(c);
    o.dialogTitle = "SONIC DNA Settings";
    o.dialogBackgroundColour = col::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    auto* w = o.launchAsync();
    c->onClose = [w, sp = juce::Component::SafePointer<SonicDNAEditor>(this)] { if (w) w->exitModalState(0); if (sp) sp->repaint(); };
}

// ---------------------------------------------------------------------------
// End-to-end UI automation: exercises the real editor & processor and saves
// screenshots. Enabled only when SONICDNA_AUTOTEST is set.
void SonicDNAEditor::runAutotestStep() {
    auto snap = [this](const juce::String& name) {
        auto img = createComponentSnapshot(getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(autotestDir.getChildFile(name + ".png"));
        if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
    };
    auto log = [this](const juce::String& s) { autotestDir.getChildFile("autotest.log").appendText(s + "\n"); };
    if (autotestWait > 0) { --autotestWait; return; }
    auto query = juce::SystemStats::getEnvironmentVariable("SONICDNA_AUTOTEST_QUERY", "The Hellp");
    switch (autotestStep) {
        case 0: snap("01_empty"); artist.grabKeyboardFocus(); artist.setText(query.substring(0, 4), true); autotestWait = 5; break;
        case 1: { snap("02_suggestions"); artist.setText(query, false); prompt.setText("make it darker and more industrial", true); suggest.setVisible(false);
                lastAnalysed = query; proc.analyse(query, false);
                auto ref = juce::SystemStats::getEnvironmentVariable("SONICDNA_AUTOTEST_REF", {});
                if (ref.isNotEmpty()) { juce::Array<juce::File> fl; fl.add(juce::File(ref)); proc.addReferenceFiles(fl); fileDragActive = true; repaint(); }
                autotestWait = 5; break; }
        case 2: if (fileDragActive) { snap("02b_drop_overlay"); fileDragActive = false; repaint(); }
                if (proc.isListening()) return;
                if (!proc.references.empty()) log("reference: " + juce::String(describeReference(proc.references[0])[0]) + " | profile source=" + juce::String(proc.profile.source));
                snap("03_analysed"); log("analysis: " + proc.analysisMessage + " | source=" + juce::String(proc.profile.source)); onGenerate(); autotestWait = 2; break;
        case 3: if (proc.isGenerating()) { snap("04_generating"); autotestWait = 0; return; } break; // wait until done
        case 4: if (!proc.kit) return; log("kit: " + juce::String((int)proc.kit->samples.size()) + " samples | " + proc.status);
                library.select(Category::Snare); autotestWait = 4; break;
        case 5: snap("05_library_snares"); library.select(Category::Pad); autotestWait = 4; break;
        case 6: snap("06_library_pads"); proc.previewSample(proc.kit->inCategory(Category::Pad)[0]); showTab(1); proc.machine.playing = true; drums.repaint(); autotestWait = 30; break;
        case 7: { snap("07_drum_machine"); log("currentStep=" + juce::String(proc.machine.currentStep.load()) + " masterMeter=" + juce::String(proc.machine.masterMeter.load(), 3));
                  auto zip = autotestDir.getChildFile("export.zip"); proc.exportKitZipAsync(zip); proc.exportMidi(autotestDir.getChildFile("pattern.mid"), 4);
                  proc.exportLoopAsync(autotestDir.getChildFile("loop.wav"), 2); proc.saveKitAsync(autotestDir.getChildFile("kit.sdnakit")); autotestWait = 60; break; }
        case 8: { auto s = proc.kit->inCategory(Category::Kick)[0]; log("regenerate " + juce::String(s->id)); proc.regenerateSample(s->id); showTab(0); library.select(Category::Kick); autotestWait = 40; break; }
        case 9: { snap("08_library_kicks_regen"); proc.machine.playing = false; log("final status: " + proc.status);
                  log(juce::String("zip exists: ") + (autotestDir.getChildFile("export.zip").existsAsFile() ? "yes" : "no")
                      + " size=" + juce::String(autotestDir.getChildFile("export.zip").getSize()));
                  log("DONE"); autotestStep = -1;
                  if (juce::JUCEApplicationBase::isStandaloneApp()) juce::JUCEApplicationBase::quit();
                  return; }
        default: break;
    }
    ++autotestStep;
}
