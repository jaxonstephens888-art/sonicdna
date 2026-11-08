#include "ui/DrumMachineView.h"

using namespace sdna;

namespace ui {

static constexpr int kTransportH = 52, kHeaderH = 22, kPadW = 168, kMS = 22, kKnobW = 34;
static const char* kKnobNames[8] = { "VOL", "PAN", "PITCH", "TONE", "DRIVE", "REV", "DLY", "DECAY" };

ValueBox::ValueBox(const juce::String& l, double min, double max, double def, int dec, const juce::String& sfx)
    : juce::Slider(juce::Slider::LinearBar, juce::Slider::NoTextBox), label(l), suffix(sfx), decimals(dec) {
    setRange(min, max, 0.0);
    setValue(def, juce::dontSendNotification);
    setDoubleClickReturnValue(true, def);
    setVelocityBasedMode(false);
    setMouseDragSensitivity(300);
    if (sfx == "%") scale = 100.0;
}

void ValueBox::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(col::panel2); g.fillRoundedRectangle(r, 3.0f);
    float frac = (float)((getValue() - getMinimum()) / (getMaximum() - getMinimum()));
    g.setColour(col::accent.withAlpha(isMouseOverOrDragging() ? 0.22f : 0.14f));
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * frac), 3.0f);
    g.setColour(isMouseOverOrDragging() ? col::lineHi : col::line); g.drawRoundedRectangle(r, 3.0f, 1.0f);
    auto t = getLocalBounds().reduced(8, 0);
    g.setFont(mono(10.0f, 500)); g.setColour(col::faint);
    g.drawText(label, t, juce::Justification::centredLeft);
    g.setFont(mono(12.0f, 500)); g.setColour(col::text);
    g.drawText(getTextFromValue(getValue()), t, juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------
DrumMachineView::DrumMachineView(SonicDNAProcessor& p) : proc(p) {
    for (auto* c : std::initializer_list<juce::Component*>{ &playBtn, &randomBtn, &clearBtn, &midiBtn, &loopBtn, &bpm, &swing, &master, &density, &groove }) addAndMakeVisible(c);
    playBtn.setClickingTogglesState(true);
    playBtn.setTooltip("Start / stop the internal sequencer (in a DAW the pattern also follows the host transport)");
    playBtn.onClick = [this] { proc.machine.playing = playBtn.getToggleState(); playBtn.setButtonText(playBtn.getToggleState() ? "STOP" : "PLAY"); };
    bpm.onValueChange = [this] { proc.machine.bpm = (float)bpm.getValue(); };
    bpm.setTooltip("Tempo (drag). In a DAW the host tempo is used while the host is playing.");
    swingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, "swing", swing);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, "master", master);
    density.setTooltip("How busy the randomised pattern is");
    for (int i = 0; i < (int)Groove::Count; ++i) groove.addItem(grooveLabel((Groove)i), i + 1);
    groove.setTooltip("Rhythmic style used by RANDOMISE");
    randomBtn.setTooltip("Generate a new pattern in the selected style, shaped by the sonic profile");
    randomBtn.onClick = [this] { proc.randomizePattern((Groove)juce::jmax(0, groove.getSelectedId() - 1), (float)density.getValue()); };
    clearBtn.onClick = [this] { proc.clearPattern(); };
    midiBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Export MIDI pattern", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("SonicDNA_Pattern.mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) proc.exportMidi(f.withFileExtension("mid"), 4); });
    };
    loopBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Export drum loop (4 bars)", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("SonicDNA_Loop.wav"), "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) proc.exportLoopAsync(f.withFileExtension("wav"), 4); });
    };
    midiBtn.setTooltip("Save the pattern as a Standard MIDI File (pads 1-16 = C1..D#2)");
    loopBtn.setTooltip("Render 4 bars of the pattern through the drum machine to a seamless WAV loop");

    for (int c = 0; c < kChannels; ++c) {
        auto& s = strips[c];
        s.k[0] = std::make_unique<Knob>("Volume", -36, 6, 0, " dB");
        s.k[1] = std::make_unique<Knob>("Pan", -1, 1, 0);
        s.k[2] = std::make_unique<Knob>("Pitch", -24, 24, 0, " st");
        s.k[3] = std::make_unique<Knob>("Tone (LP <-> HP)", -1, 1, 0);
        s.k[4] = std::make_unique<Knob>("Drive", 0, 1, 0);
        s.k[5] = std::make_unique<Knob>("Reverb send", 0, 1, 0);
        s.k[6] = std::make_unique<Knob>("Delay send", 0, 1, 0);
        s.k[7] = std::make_unique<Knob>("Decay", 0, 1, 1);
        s.k[2]->setRange(-24, 24, 1);
        for (int k = 0; k < kKnobs; ++k) {
            addAndMakeVisible(*s.k[k]);
            s.k[k]->onValueChange = [this, c, k] {
                auto& pr = proc.machine.params[c];
                float v = (float)strips[c].k[k]->getValue();
                switch (k) { case 0: pr.volumeDb = v; break; case 1: pr.pan = v; break; case 2: pr.pitch = v; break; case 3: pr.tone = v; break;
                             case 4: pr.drive = v; break; case 5: pr.reverb = v; break; case 6: pr.delay = v; break; default: pr.decay = v; }
            };
        }
    }
    proc.addListener(this);
    syncFromMachine();
    startTimerHz(30);
}

DrumMachineView::~DrumMachineView() { proc.removeListener(this); }

void DrumMachineView::syncFromMachine() {
    auto& m = proc.machine;
    bpm.setValue(m.bpm.load(), juce::dontSendNotification);
    playBtn.setToggleState(m.playing.load(), juce::dontSendNotification);
    playBtn.setButtonText(m.playing.load() ? "STOP" : "PLAY");
    if (groove.getSelectedId() == 0 || proc.kit) {
        auto& gp = proc.kit ? proc.kit->profile.grooves : proc.profile.grooves;
        groove.setSelectedId(gp.empty() ? 1 : (int)gp[0] + 1, juce::dontSendNotification);
    }
    for (int c = 0; c < kChannels; ++c) {
        auto& pr = m.params[c];
        float v[8] = { pr.volumeDb, pr.pan, pr.pitch, pr.tone, pr.drive, pr.reverb, pr.delay, pr.decay };
        for (int k = 0; k < kKnobs; ++k) strips[c].k[k]->setValue(v[k], juce::dontSendNotification);
    }
    repaint();
}

juce::Rectangle<int> DrumMachineView::gridArea() const { return getLocalBounds().withTrimmedTop(kTransportH + kHeaderH).reduced(16, 0).withTrimmedBottom(10); }
juce::Rectangle<int> DrumMachineView::rowBounds(int ch) const {
    auto g = gridArea();
    float h = g.getHeight() / (float)kChannels;
    return { g.getX(), g.getY() + (int)std::round(ch * h), g.getWidth(), (int)std::round((ch + 1) * h) - (int)std::round(ch * h) };
}
juce::Rectangle<int> DrumMachineView::padArea(int ch) const { return rowBounds(ch).withWidth(kPadW).reduced(0, 2); }
juce::Rectangle<int> DrumMachineView::muteArea(int ch) const { auto r = rowBounds(ch); return { r.getX() + kPadW + 6, r.getY() + 2, kMS, r.getHeight() - 4 }; }
juce::Rectangle<int> DrumMachineView::soloArea(int ch) const { return muteArea(ch).translated(kMS + 4, 0); }
juce::Rectangle<int> DrumMachineView::stepsArea(int ch) const {
    auto r = rowBounds(ch);
    int left = kPadW + 6 + 2 * kMS + 4 + 12;
    int right = kKnobs * kKnobW + 12;
    return { r.getX() + left, r.getY(), r.getWidth() - left - right, r.getHeight() };
}
juce::Rectangle<int> DrumMachineView::stepArea(int ch, int step) const {
    auto s = stepsArea(ch);
    float w = s.getWidth() / (float)kSteps;
    int gap = (step % 4 == 0 && step) ? 4 : 2;
    return juce::Rectangle<int>(s.getX() + (int)std::round(step * w) + gap / 2, s.getY() + 2, (int)std::round(w) - gap, s.getHeight() - 4);
}
int DrumMachineView::rowAt(int y) const { auto g = gridArea(); if (y < g.getY() || y >= g.getBottom()) return -1; return juce::jlimit(0, kChannels - 1, (int)((y - g.getY()) * kChannels / g.getHeight())); }
int DrumMachineView::stepAt(int x) const { auto s = stepsArea(0); if (x < s.getX() || x >= s.getRight()) return -1; return juce::jlimit(0, kSteps - 1, (int)((x - s.getX()) * kSteps / s.getWidth())); }

void DrumMachineView::resized() {
    auto t = getLocalBounds().removeFromTop(kTransportH).reduced(16, 11);
    playBtn.setBounds(t.removeFromLeft(80)); t.removeFromLeft(10);
    bpm.setBounds(t.removeFromLeft(120)); t.removeFromLeft(6);
    swing.setBounds(t.removeFromLeft(110)); t.removeFromLeft(6);
    master.setBounds(t.removeFromLeft(130));
    loopBtn.setBounds(t.removeFromRight(112)); t.removeFromRight(6);
    midiBtn.setBounds(t.removeFromRight(108)); t.removeFromRight(16);
    clearBtn.setBounds(t.removeFromRight(64)); t.removeFromRight(6);
    randomBtn.setBounds(t.removeFromRight(100)); t.removeFromRight(6);
    density.setBounds(t.removeFromRight(118)); t.removeFromRight(6);
    groove.setBounds(t.removeFromRight(juce::jmin(150, juce::jmax(90, t.getWidth() - 10))));
    for (int c = 0; c < kChannels; ++c) {
        auto r = rowBounds(c);
        int x = r.getRight() - kKnobs * kKnobW;
        int sz = juce::jmin(kKnobW - 2, r.getHeight() - 2);
        for (int k = 0; k < kKnobs; ++k) strips[c].k[k]->setBounds(x + k * kKnobW + (kKnobW - sz) / 2, r.getCentreY() - sz / 2, sz, sz);
    }
}

void DrumMachineView::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    g.setColour(col::line); g.drawHorizontalLine(kTransportH - 1, 0.0f, (float)getWidth());
    auto& m = proc.machine;
    // Host status
    if (proc.hostDrivesTransport()) {
        g.setColour(col::ok); g.setFont(mono(10.0f, 500));
        g.drawText("HOST SYNC " + juce::String(proc.hostBpm(), 1), bpm.getBounds().translated(0, 20).withHeight(12), juce::Justification::centredLeft);
    }
    // Column headers
    auto hdr = juce::Rectangle<int>(16, kTransportH, getWidth() - 32, kHeaderH);
    g.setFont(mono(9.5f, 500)); g.setColour(col::faint);
    g.drawText("PAD", hdr.withWidth(kPadW).withTrimmedLeft(8), juce::Justification::centredLeft);
    auto s0 = stepsArea(0);
    for (int k = 0; k < kSteps; k += 4) g.drawText(juce::String(k / 4 + 1), stepArea(0, k).withY(kTransportH).withHeight(kHeaderH), juce::Justification::centredLeft);
    int kx = getWidth() - 16 - kKnobs * kKnobW;
    for (int k = 0; k < kKnobs; ++k) g.drawText(kKnobNames[k], kx + k * kKnobW - 4, kTransportH, kKnobW + 8, kHeaderH, juce::Justification::centred);
    (void)s0;
    int cur = m.currentStep.load();
    bool running = m.playing.load() || proc.hostDrivesTransport();
    bool anySolo = false; for (auto& p : m.params) anySolo |= p.solo.load();
    for (int c = 0; c < kChannels; ++c) {
        auto row = rowBounds(c);
        if (c % 2 == 0) { g.setColour(col::panel.withAlpha(0.45f)); g.fillRect(row); }
        if (dropRow == c) { g.setColour(col::accent.withAlpha(0.12f)); g.fillRect(row); }
        // pad
        auto pa = padArea(c);
        auto smp = m.channelSample(c);
        float meter = meters[c];
        g.setColour(col::panel2); g.fillRoundedRectangle(pa.toFloat(), 3.0f);
        if (meter > 0.01f) { g.setColour(col::accent.withAlpha(juce::jlimit(0.0f, 0.5f, meter))); g.fillRoundedRectangle(pa.toFloat().withWidth(3.0f), 1.5f); }
        g.setFont(mono(10.0f, 500)); g.setColour(col::faint);
        g.drawText(juce::String(c + 1).paddedLeft('0', 2), pa.withWidth(26).withTrimmedLeft(6), juce::Justification::centredLeft);
        g.setFont(sans(12.0f, 500)); g.setColour(smp ? col::text : col::faint);
        bool muted = m.params[c].mute.load() || (anySolo && !m.params[c].solo.load());
        if (muted) g.setColour(col::faint);
        g.drawFittedText(smp ? juce::String::fromUTF8(smp->name.c_str()) : U8("empty — drop a sample"), pa.withTrimmedLeft(28).withTrimmedRight(16), juce::Justification::centredLeft, 1, 0.75f);
        drawIcon(g, icon::chevron(), juce::Rectangle<float>((float)pa.getRight() - 13.0f, (float)pa.getCentreY() - 3.0f, 7.0f, 7.0f), col::faint, true, 1.2f);
        // M / S
        auto drawMS = [&](juce::Rectangle<int> r, const char* t, bool on, juce::Colour onCol) {
            g.setColour(on ? onCol.withAlpha(0.2f) : col::panel2); g.fillRoundedRectangle(r.toFloat(), 2.0f);
            g.setColour(on ? onCol : col::line); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 2.0f, 1.0f);
            g.setColour(on ? onCol : col::faint); g.setFont(mono(10.0f, 500)); g.drawText(t, r, juce::Justification::centred);
        };
        drawMS(muteArea(c), "M", m.params[c].mute.load(), col::warn);
        drawMS(soloArea(c), "S", m.params[c].solo.load(), col::ok);
        // steps
        int len = m.length.load();
        for (int k = 0; k < kSteps; ++k) {
            auto sr = stepArea(c, k).toFloat();
            uint8_t v = m.steps[c][k].load();
            bool beat = (k / 4) % 2 == 0;
            g.setColour(k >= len ? col::bg : (beat ? col::panel2 : col::panel2.darker(0.25f)));
            g.fillRoundedRectangle(sr, 2.0f);
            if (v) {
                float a = 0.35f + 0.65f * v / 127.0f;
                g.setColour((muted ? col::dim : col::accent).withAlpha(a));
                g.fillRoundedRectangle(sr, 2.0f);
                float vh = (sr.getHeight() - 4) * v / 127.0f;
                g.setColour(col::bg.withAlpha(0.35f));
                g.fillRect(sr.getX() + 3.0f, sr.getBottom() - 2.0f - vh, 2.0f, vh);
            }
            if (running && k == cur) { g.setColour(col::text.withAlpha(v ? 0.55f : 0.12f)); g.drawRoundedRectangle(sr.reduced(0.5f), 2.0f, 1.2f); }
        }
    }
    if (!proc.kit) {
        g.setColour(col::bg.withAlpha(0.6f)); g.fillRect(gridArea());
        g.setColour(col::dim); g.setFont(sans(14.0f, 500));
        g.drawText("Generate a kit to load the pads", gridArea(), juce::Justification::centred);
    }
}

void DrumMachineView::timerCallback() {
    bool changed = false;
    if (playBtn.getToggleState() != proc.machine.playing.load()) {
        playBtn.setToggleState(proc.machine.playing.load(), juce::dontSendNotification);
        playBtn.setButtonText(proc.machine.playing.load() ? "STOP" : "PLAY");
    }
    for (int c = 0; c < kChannels; ++c) { float v = proc.machine.channelMeter[c].load(); if (std::abs(v - meters[c]) > 0.01f) { meters[c] = v; changed = true; } }
    int cur = proc.machine.currentStep.load();
    if (cur != lastStep) { lastStep = cur; changed = true; }
    static bool lastHost = false;
    if (proc.hostDrivesTransport() != lastHost) { lastHost = proc.hostDrivesTransport(); changed = true; }
    if (changed) repaint(gridArea().withX(0).withWidth(getWidth()));
    if (lastHost) repaint(bpm.getBounds().translated(0, 18).withHeight(16));
}

void DrumMachineView::sampleMenu(int ch) {
    if (!proc.kit) return;
    juce::PopupMenu m;
    m.addItem(10000, "Audition pad");
    m.addSeparator();
    std::vector<SamplePtr> flat;
    for (int gi = 0; gi < kNumGroups; ++gi) {
        juce::PopupMenu gm;
        for (auto c : categoriesInGroup((Group)gi)) {
            juce::PopupMenu cm;
            for (auto& s : proc.kit->inCategory(c)) { flat.push_back(s); cm.addItem((int)flat.size(), juce::String::fromUTF8(s->name.c_str())); }
            gm.addSubMenu(categoryInfo(c).label, cm);
        }
        m.addSubMenu(groupLabel((Group)gi), gm);
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(padArea(ch))), [this, ch, flat](int r) {
        if (r == 10000) proc.machine.triggerFromUI(ch, 0.9f);
        else if (r > 0 && r <= (int)flat.size()) { proc.assignPad(ch, flat[(size_t)r - 1]); proc.machine.triggerFromUI(ch, 0.9f); }
    });
}

void DrumMachineView::mouseDown(const juce::MouseEvent& e) {
    int ch = rowAt(e.y);
    if (ch < 0) return;
    auto& m = proc.machine;
    if (padArea(ch).contains(e.getPosition())) {
        if (e.mods.isPopupMenu() || e.x > padArea(ch).getRight() - 18) sampleMenu(ch);
        else m.triggerFromUI(ch, 0.95f);
        return;
    }
    if (muteArea(ch).contains(e.getPosition())) { m.params[ch].mute = !m.params[ch].mute.load(); repaint(); return; }
    if (soloArea(ch).contains(e.getPosition())) { m.params[ch].solo = !m.params[ch].solo.load(); repaint(); return; }
    int st = stepAt(e.x);
    if (st < 0 || !stepsArea(ch).contains(e.getPosition())) return;
    dragCh = ch; dragStep = st; moved = false;
    uint8_t v = m.steps[ch][st].load();
    if (e.mods.isPopupMenu()) { m.steps[ch][st] = v ? 0 : 45; dragMode = Drag::None; repaint(); return; }
    if (v == 0) { uint8_t nv = e.mods.isShiftDown() ? 127 : 100; m.steps[ch][st] = nv; paintValue = nv; dragMode = Drag::Paint; }
    else { dragStartVel = v; dragMode = Drag::Velocity; paintValue = 0; }
    repaint();
}

void DrumMachineView::mouseDrag(const juce::MouseEvent& e) {
    if (dragCh < 0) return;
    auto& m = proc.machine;
    if (dragMode == Drag::Velocity) {
        int dy = e.getDistanceFromDragStartY(), dx = e.getDistanceFromDragStartX();
        if (!moved && std::abs(dx) > 6 && std::abs(dx) > std::abs(dy)) { dragMode = Drag::Paint; paintValue = 0; m.steps[dragCh][dragStep] = 0; moved = true; }
        else if (std::abs(dy) > 2) { moved = true; m.steps[dragCh][dragStep] = (uint8_t)juce::jlimit(1, 127, dragStartVel - dy); }
    }
    if (dragMode == Drag::Paint) {
        int st = stepAt(e.x);
        if (st >= 0) { m.steps[dragCh][st] = paintValue; moved = true; }
    }
    repaint(gridArea());
}

void DrumMachineView::mouseUp(const juce::MouseEvent&) {
    if (dragCh >= 0 && dragMode == Drag::Velocity && !moved) proc.machine.steps[dragCh][dragStep] = 0;
    dragCh = -1; dragMode = Drag::None;
    repaint(gridArea());
}

bool DrumMachineView::isInterestedInDragSource(const SourceDetails& d) { return d.description.toString().startsWith("sample:"); }

void DrumMachineView::itemDropped(const SourceDetails& d) {
    int ch = rowAt(d.localPosition.y);
    dropRow = -1;
    if (ch < 0 || !proc.kit) { repaint(); return; }
    auto id = d.description.toString().fromFirstOccurrenceOf("sample:", false, false).toStdString();
    if (auto s = proc.kit->find(id)) { proc.assignPad(ch, s); proc.machine.triggerFromUI(ch, 0.9f); }
    repaint();
}

} // namespace ui
