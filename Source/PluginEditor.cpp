#include "PluginEditor.h"

// =====================================================================
//  Icon buttons
// =====================================================================
void IconButton::paintButton (juce::Graphics& g, bool over, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (over ? 0xff474747 : 0xff3a3a3a).darker (isDown ? 0.2f : 0.0f));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    const auto c = b.getCentre();
    g.setColour (VegaColours::text);
    juce::Path p;
    switch (kind)
    {
        case Kind::up:   p.addTriangle (c.x - 3.5f, c.y + 2, c.x + 3.5f, c.y + 2, c.x, c.y - 2.5f); g.fillPath (p); break;
        case Kind::down: p.addTriangle (c.x - 3.5f, c.y - 2, c.x + 3.5f, c.y - 2, c.x, c.y + 2.5f); g.fillPath (p); break;
        case folder:
            p.startNewSubPath (c.x - 6, c.y - 4); p.lineTo (c.x - 2, c.y - 4); p.lineTo (c.x - 1, c.y - 2.5f);
            p.lineTo (c.x + 6, c.y - 2.5f); p.lineTo (c.x + 6, c.y + 4.5f); p.lineTo (c.x - 6, c.y + 4.5f); p.closeSubPath();
            g.fillPath (p); break;
        case save:
            g.fillRoundedRectangle (c.x - 5.5f, c.y - 5.5f, 11, 11, 1.2f);
            g.setColour (juce::Colour (0xff3a3a3a));
            g.fillRect (c.x - 3.0f, c.y - 5.5f, 6.0f, 3.5f);
            g.fillRect (c.x - 3.5f, c.y + 1.0f, 7.0f, 4.5f);
            break;
        case info:
            g.setFont (VegaFonts::ui (12.0f, true));
            g.drawText ("i", b.toNearestInt(), juce::Justification::centred);
            break;
    }
}

// =====================================================================
//  Main panel
// =====================================================================
MainPanel::MainPanel (VegaMasteringProcessor& p) : proc (p), state (p.apvts)
{
    // ---- top bar
    presetName.setJustificationType (juce::Justification::centredLeft);
    presetName.setFont (VegaFonts::ui (11.0f));
    presetName.setColour (juce::Label::textColourId, VegaColours::textDim);
    presetName.setColour (juce::Label::backgroundColourId, juce::Colour (0xff3a3a3a));
    presetName.setColour (juce::Label::outlineColourId, juce::Colours::black.withAlpha (0.4f));
    presetName.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    presetName.addMouseListener (this, false);
    addAndMakeVisible (presetName);
    for (auto* b : { (juce::Component*) &presetUp, (juce::Component*) &presetDown, (juce::Component*) &loadBtn,
                     (juce::Component*) &saveBtn, (juce::Component*) &infoBtn })
        addAndMakeVisible (b);
    presetUp.onClick   = [this] { stepPreset (-1); };
    presetDown.onClick = [this] { stepPreset (+1); };
    loadBtn.onClick    = [this] { loadPresetFromFile(); };
    saveBtn.onClick    = [this] { savePresetAs(); };
    infoBtn.onClick    = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Vega Productions — Mastering Suite",
            "M/S EQ (Analog / Linear Phase)  •  Glue Compressor  •  Tape / Tube Character\n"
            "Soft Clipper  •  True-Peak Limiter  •  2x/4x/8x Oversampling\n"
            "EBU R128 loudness, true peak, spectrum, phase scope and Delta listening.\n\n"
            "Current latency: " + juce::String (proc.getLatencySamples()) + " samples\n\n"
            "Tip: use 4x or 8x oversampling for true-peak compliant output.");
    };

    for (auto* b : { &aBtn, &bBtn, &copyBtn, &bypassBtn }) addAndMakeVisible (b);
    aBtn.onClick = [this] { proc.selectABSlot (0); updateAbButtons(); };
    bBtn.onClick = [this] { proc.selectABSlot (1); updateAbButtons(); };
    copyBtn.onClick = [this] { proc.copyToOtherSlot(); };
    copyBtn.setTooltip ("Copy current settings to the other A/B slot");
    bypassBtn.setClickingTogglesState (true);
    bypassAtt = std::make_unique<APVTS::ButtonAttachment> (state, ID::bypass, bypassBtn);
    updateAbButtons();

    // ---- input / output
    for (auto* c : { (juce::Component*) &input, (juce::Component*) &unity, (juce::Component*) &output })
        addAndMakeVisible (c);

    // ---- M/S EQ
    midBtn.setClickingTogglesState (true);  sideBtn.setClickingTogglesState (true);
    midBtn.setRadioGroupId (101);           sideBtn.setRadioGroupId (101);
    midBtn.setColour (juce::TextButton::buttonOnColourId, VegaColours::accent);
    sideBtn.setColour (juce::TextButton::buttonOnColourId, VegaColours::gold);
    midBtn.onClick  = [this] { showEqChannel (0); };
    sideBtn.onClick = [this] { showEqChannel (1); };
    addAndMakeVisible (midBtn); addAndMakeVisible (sideBtn);
    addAndMakeVisible (eqOn); addAndMakeVisible (eqMode);

    const char* names[] { ID::lowFreq, ID::lowGain, ID::midFreq, ID::midGain, ID::midQ, ID::highFreq, ID::highGain };
    const char* titles[] { "Low Freq", "Low Gain", "Mid Freq", "Mid Gain", "Mid Q", "Air Freq", "Air Gain" };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 7; ++i)
        {
            eqKnobs[c][(size_t) i] = std::make_unique<KnobWithLabel> (state, ID::eq (c == 0 ? 'm' : 's', names[i]), titles[i]);
            addChildComponent (*eqKnobs[c][(size_t) i]);
        }
    showEqChannel (proc.eqUiChannel.load());

    // ---- stereo
    addAndMakeVisible (monoBass);
    widthFader.setSliderStyle (juce::Slider::LinearVertical);
    widthFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    widthFader.setMouseDragSensitivity (200);
    widthAtt = std::make_unique<APVTS::SliderAttachment> (state, ID::width, widthFader);
    widthFader.setDoubleClickReturnValue (true, 100.0);
    addAndMakeVisible (widthFader);
    widthLabel.setText ("Width", juce::dontSendNotification);
    widthLabel.setJustificationType (juce::Justification::centred);
    widthLabel.setFont (VegaFonts::ui (11.5f));
    addAndMakeVisible (widthLabel);

    // ---- compressor
    addAndMakeVisible (grColumn);
    grLabel.setText ("GR", juce::dontSendNotification);
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setFont (VegaFonts::ui (11.5f));
    addAndMakeVisible (grLabel);
    for (auto* c : { &compThresh, &compRatio, &compScHpf, &compAttack, &compRelease, &compMix, &compMakeup })
        addAndMakeVisible (c);
    addAndMakeVisible (compOn); addAndMakeVisible (compMode);

    // ---- character / clip
    for (auto* c : { &satMode, &satDrive, &satMix, &clipThresh }) addAndMakeVisible (c);
    addAndMakeVisible (clipOn);

    // ---- limiter
    addAndMakeVisible (oled);
    for (auto* c : { &limGain, &limCeiling, &limRelease }) addAndMakeVisible (c);
    addAndMakeVisible (limOn); addAndMakeVisible (delta);

    // ---- meters
    addAndMakeVisible (meterL); addAndMakeVisible (meterR); addAndMakeVisible (meterScale);

    // ---- analysis
    addAndMakeVisible (spectrum); addAndMakeVisible (scope); addAndMakeVisible (loudness);

    // ---- bottom bar
    osBox.addItemList (Choices::oversample, 1);
    osAtt = std::make_unique<APVTS::ComboBoxAttachment> (state, ID::oversample, osBox);
    targetBox.addItemList (Choices::target, 1);
    targetAtt = std::make_unique<APVTS::ComboBoxAttachment> (state, ID::target, targetBox);
    for (auto [label, text] : { std::pair<juce::Label*, const char*> { &osLabel, "OVERSAMPLING" }, { &targetLabel, "TARGET" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (VegaFonts::ui (10.0f, true));
        label->setColour (juce::Label::textColourId, VegaColours::textDim);
        label->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
    }
    const juce::StringArray zooms { "75%", "85%", "100%", "115%", "125%", "150%" };
    zoomBox.addItemList (zooms, 1);
    const float scales[] { 0.75f, 0.85f, 1.0f, 1.15f, 1.25f, 1.5f };
    int zi = 2;
    for (int i = 0; i < 6; ++i) if (std::abs (scales[i] - proc.uiScale.load()) < 0.01f) zi = i;
    zoomBox.setSelectedItemIndex (zi, juce::dontSendNotification);
    zoomBox.onChange = [this]
    {
        const float sc[] { 0.75f, 0.85f, 1.0f, 1.15f, 1.25f, 1.5f };
        proc.uiScale.store (sc[juce::jlimit (0, 5, zoomBox.getSelectedItemIndex())]);
        juce::Component::SafePointer<MainPanel> sp (this);
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr && sp->onScaleChanged) sp->onScaleChanged(); });
    };
    for (auto* c : { &osBox, &targetBox, &zoomBox }) addAndMakeVisible (c);

    setSize (W, H);
}

void MainPanel::showEqChannel (int ch)
{
    proc.eqUiChannel.store (ch);
    (ch == 0 ? midBtn : sideBtn).setToggleState (true, juce::dontSendNotification);
    for (int c = 0; c < 2; ++c)
        for (auto& k : eqKnobs[c]) k->setVisible (c == ch);
    repaint();
}

void MainPanel::updateAbButtons()
{
    aBtn.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
}

// ---------------------------------------------------------------------
void MainPanel::resized()
{
    sectionX = { 0, 290, 640, 745, 975, 1110, 1330, W };

    // top bar
    presetName.setBounds (W / 2 - 120, 7, 200, 20);
    presetUp.setBounds (W / 2 + 82, 7, 16, 10);
    presetDown.setBounds (W / 2 + 82, 17, 16, 10);
    loadBtn.setBounds (W / 2 + 102, 7, 20, 20);
    saveBtn.setBounds (W / 2 + 126, 7, 20, 20);
    bypassBtn.setBounds (W - 66, 7, 56, 20);
    copyBtn.setBounds (W - 116, 7, 44, 20);
    bBtn.setBounds (W - 140, 7, 20, 20);
    aBtn.setBounds (W - 164, 7, 20, 20);

    // A — input / output
    input.setBounds (12, 66, 196, 250);
    unity.setBounds (210, 84, 70, 76);
    output.setBounds (204, 196, 82, 120);

    // B — M/S EQ (columns: Low / Mid / Air)
    midBtn.setBounds (sectionX[1] + 16, 60, 48, 20);
    sideBtn.setBounds (sectionX[1] + 66, 60, 48, 20);
    const int colW = 110, x0 = sectionX[1] + 10;
    for (int c = 0; c < 2; ++c)
    {
        auto& k = eqKnobs[c];
        k[0]->setBounds (x0,            90, colW, 112); k[1]->setBounds (x0,            210, colW, 112);
        k[2]->setBounds (x0 + colW,     90, colW, 112); k[3]->setBounds (x0 + colW,     210, colW, 112);
        k[4]->setBounds (x0 + colW + 15, 330, colW - 30, 92);
        k[5]->setBounds (x0 + 2 * colW, 90, colW, 112); k[6]->setBounds (x0 + 2 * colW, 210, colW, 112);
    }
    eqOn.setBounds (x0 + 20, 334, colW - 40, 80);
    eqMode.setBounds (x0 + 2 * colW + 10, 340, colW - 10, 60);

    // C — stereo
    monoBass.setBounds (sectionX[2] + 8, 62, sectionX[3] - sectionX[2] - 16, 112);
    widthLabel.setBounds (sectionX[2], 186, sectionX[3] - sectionX[2], 16);
    widthFader.setBounds (sectionX[2] + 12, 204, sectionX[3] - sectionX[2] - 24, 214);

    // D — compressor
    grLabel.setBounds (sectionX[3] + 6, 62, 34, 16);
    grColumn.setBounds (sectionX[3] + 14, 84, 32, 326);
    {
        const int cx = sectionX[3] + 48, cw = (sectionX[4] - cx - 4) / 3;
        compThresh.setBounds (cx,          62, cw, 100); compRatio.setBounds (cx + cw,   62, cw, 100); compScHpf.setBounds (cx + 2 * cw, 62, cw, 100);
        compAttack.setBounds (cx,         170, cw, 100); compRelease.setBounds (cx + cw, 170, cw, 100); compMix.setBounds (cx + 2 * cw, 170, cw, 100);
        compMakeup.setBounds (cx,         278, cw, 100);
        compOn.setBounds (cx + cw + 4,    282, cw - 8, 80);
        compMode.setBounds (cx + 2 * cw - 6, 290, cw + 10, 60);
    }

    // E — character + clipper
    {
        const int ex = sectionX[4] + 4, ew = sectionX[5] - sectionX[4] - 8;
        satMode.setBounds (ex, 60, ew, 110);
        satDrive.setBounds (ex, 176, ew / 2, 100);
        satMix.setBounds (ex + ew / 2, 176, ew / 2, 100);
        clipOn.setBounds (ex + 4, 290, ew / 2 - 8, 80);
        clipThresh.setBounds (ex + ew / 2, 284, ew / 2, 100);
    }

    // F — limiter + OLED
    {
        const int fx = sectionX[5] + 10, fw = sectionX[6] - sectionX[5] - 20;
        oled.setBounds (fx, 60, fw, 96);
        const int cw = fw / 3;
        limGain.setBounds (fx, 168, cw, 100); limCeiling.setBounds (fx + cw, 168, cw, 100); limRelease.setBounds (fx + 2 * cw, 168, cw, 100);
        limOn.setBounds (fx + cw / 2 - 4, 290, cw, 80);
        delta.setBounds (fx + cw / 2 + cw + 4, 290, cw, 80);
    }

    // G — output LED meters
    meterScale.setBounds (sectionX[6] + 4, 76, 22, 320);
    meterL.setBounds (sectionX[6] + 28, 76, 14, 320);
    meterR.setBounds (sectionX[6] + 48, 76, 14, 320);

    // analysis row
    spectrum.setBounds (10, 440, 960, 226);
    scope.setBounds (980, 440, 226, 226);
    loudness.setBounds (1216, 440, 174, 226);

    // bottom bar
    osLabel.setBounds (10, 678, 90, 20);
    osBox.setBounds (104, 678, 52, 20);
    infoBtn.setBounds (W - 356, 678, 20, 20);
    zoomBox.setBounds (W - 330, 678, 62, 20);
    targetLabel.setBounds (W - 250, 678, 70, 20);
    targetBox.setBounds (W - 176, 678, 166, 20);
}

// ---------------------------------------------------------------------
void MainPanel::paint (juce::Graphics& g)
{
    using namespace VegaColours;
    auto b = getLocalBounds();

    // body
    g.setGradientFill (juce::ColourGradient (panelLight, 0, 34, panel, 0, 430, false));
    g.fillRect (0, 34, W, 396);
    g.setColour (background);
    g.fillRect (0, 430, W, 244);

    // top bar
    g.setColour (bar);
    g.fillRect (b.removeFromTop (34));
    g.setColour (line);
    g.drawHorizontalLine (34, 0, (float) W);

    // logo: ring + V mark
    {
        const juce::Point<float> c (22.0f, 17.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawEllipse (c.x - 9, c.y - 9, 18, 18, 1.4f);
        juce::Path v;
        v.startNewSubPath (c.x - 4.5f, c.y - 4); v.lineTo (c.x, c.y + 4.5f); v.lineTo (c.x + 4.5f, c.y - 4);
        g.strokePath (v, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (VegaFonts::ui (14.5f, true));
        g.drawText ("Vega Productions", 38, 0, 160, 34, juce::Justification::centredLeft);
        g.setFont (VegaFonts::ui (10.0f));
        g.setColour (textDim);
        g.drawText ("MASTERING SUITE  VP-1", 182, 0, 160, 34, juce::Justification::centredLeft);
    }

    // section separators + titles
    g.setFont (VegaFonts::ui (9.5f, true));
    const char* titles[] { "INPUT", "M/S EQUALIZER", "STEREO", "GLUE COMPRESSOR", "CHARACTER", "LIMITER", "OUT" };
    for (int i = 0; i < 7; ++i)
    {
        if (i > 0)
        {
            g.setColour (line);
            g.drawVerticalLine (sectionX[(size_t) i], 44.0f, 422.0f);
        }
        g.setColour (textDim.withAlpha (0.7f));
        const int x0 = sectionX[(size_t) i], x1 = sectionX[(size_t) i + 1];
        g.drawText (titles[i], x0 + (i == 1 ? 130 : 0), 40, x1 - x0 - (i == 1 ? 130 : 0), 14, juce::Justification::centred);
    }

    // brand plate under input
    g.setColour (textDim.withAlpha (0.35f));
    g.setFont (VegaFonts::ui (10.0f, true));
    g.drawText ("V E G A   P R O D U C T I O N S", 0, 392, 290, 14, juce::Justification::centred);

    // meter caption
    g.setFont (VegaFonts::ui (9.5f));
    g.setColour (textDim);
    g.drawText ("dB", sectionX[6] + 30, 58, 30, 12, juce::Justification::centred);
    g.drawText ("L", meterL.getX() - 2, 400, 18, 12, juce::Justification::centred);
    g.drawText ("R", meterR.getX() - 2, 400, 18, 12, juce::Justification::centred);

    // bottom bar
    g.setColour (bar);
    g.fillRect (0, 672, W, H - 672);
    g.setColour (line);
    g.drawHorizontalLine (672, 0, (float) W);
}

// ---------------------------------------------------------------------
void MainPanel::refresh()
{
    auto& m = proc.meters;
    meterL.setLevel (m.outL.exchange (0.0f));
    meterR.setLevel (m.outR.exchange (0.0f));
    m.inL.store (0.0f); m.inR.store (0.0f);

    const float cg = m.compGr.exchange (0.0f), lg = m.limGr.exchange (0.0f);
    compDisp = std::max (cg, compDisp * 0.88f);
    limDisp  = std::max (lg, limDisp * 0.88f);
    grColumn.setGr (cg);
    m.truePeakRecent.store (0.0f);

    OledDisplay::Values v;
    v.momentary  = proc.loudness.momentary.load();
    v.shortTerm  = proc.loudness.shortTerm.load();
    v.integrated = m.integrated.load();
    v.truePeak   = m.truePeakMax.load();
    v.target     = Choices::targetLufs[juce::jlimit (0, Choices::target.size() - 1,
                                                     (int) state.getRawParameterValue (ID::target)->load())];
    v.compGr = compDisp; v.limGr = limDisp;
    oled.setValues (v);
    loudness.setValues (v);

    spectrum.refresh();
    scope.refresh (m.correlation.load());

    const auto name = proc.currentPresetName;
    if (presetName.getText() != name) presetName.setText (name, juce::dontSendNotification);
    updateAbButtons();
}

// ---------------------------------------------------------------------
//  Presets
// ---------------------------------------------------------------------
void MainPanel::stepPreset (int d)
{
    const int n = VegaMasteringProcessor::getNumFactoryPresets();
    const int idx = ((proc.currentPresetIndex < 0 ? 0 : proc.currentPresetIndex) + d + n) % n;
    proc.loadFactoryPreset (idx);
}

void MainPanel::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory Presets");
    for (int i = 0; i < VegaMasteringProcessor::getNumFactoryPresets(); ++i)
        menu.addItem (1 + i, VegaMasteringProcessor::getFactoryPresetName (i), true, i == proc.currentPresetIndex);

    auto files = VegaMasteringProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vegapreset");
    files.sort();
    if (! files.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("User Presets");
        for (int i = 0; i < files.size(); ++i)
            menu.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    }
    menu.addSeparator();
    menu.addItem (9001, "Save Preset As...");
    menu.addItem (9002, "Load Preset From File...");
    menu.addItem (9003, "Show Preset Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName),
        [this, files] (int r)
        {
            if (r <= 0) return;
            if (r < 1000) proc.loadFactoryPreset (r - 1);
            else if (r < 9000) proc.loadUserPreset (files[r - 1000]);
            else if (r == 9001) savePresetAs();
            else if (r == 9002) loadPresetFromFile();
            else if (r == 9003) VegaMasteringProcessor::getUserPresetFolder().revealToUser();
        });
}

void MainPanel::savePresetAs()
{
    chooser = std::make_unique<juce::FileChooser> ("Save Vega preset",
        VegaMasteringProcessor::getUserPresetFolder().getChildFile (proc.currentPresetName + ".vegapreset"), "*.vegapreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != juce::File()) proc.saveUserPreset (f.withFileExtension (".vegapreset"));
        });
}

void MainPanel::loadPresetFromFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Vega preset", VegaMasteringProcessor::getUserPresetFolder(), "*.vegapreset");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile()) proc.loadUserPreset (f);
        });
}

void MainPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &presetName) showPresetMenu();
}

// =====================================================================
//  Editor
// =====================================================================
VegaMasteringEditor::VegaMasteringEditor (VegaMasteringProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    panel.onScaleChanged = [this] { applyScale(); };
    setResizable (false, false);
    applyScale();
    startTimerHz (30);
}

VegaMasteringEditor::~VegaMasteringEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VegaMasteringEditor::applyScale()
{
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    panel.setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt (MainPanel::W * s), juce::roundToInt (MainPanel::H * s));
}

void VegaMasteringEditor::resized()
{
    panel.setBounds (0, 0, MainPanel::W, MainPanel::H);
}

juce::AudioProcessorEditor* VegaMasteringProcessor::createEditor() { return new VegaMasteringEditor (*this); }
