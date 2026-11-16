#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"

// Small icon buttons (arrows / folder / save / info)
class IconButton : public juce::Button
{
public:
    enum Kind { up, down, folder, save, info };
    IconButton (Kind k) : juce::Button ("icon"), kind (k) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override;
private:
    Kind kind;
};

class MainPanel : public juce::Component
{
public:
    static constexpr int W = 1400, H = 704;
    explicit MainPanel (VegaMasteringProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                     // called by the editor timer
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onScaleChanged;

private:
    void showEqChannel (int ch);
    void showPresetMenu();
    void savePresetAs();
    void loadPresetFromFile();
    void stepPreset (int delta);
    void updateAbButtons();

    VegaMasteringProcessor& proc;
    APVTS& state;

    // top bar
    juce::Label presetName;
    IconButton presetUp { IconButton::up }, presetDown { IconButton::down },
               loadBtn { IconButton::folder }, saveBtn { IconButton::save }, infoBtn { IconButton::info };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "COPY" }, bypassBtn { "BYPASS" };
    std::unique_ptr<APVTS::ButtonAttachment> bypassAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    // input / output
    KnobWithLabel input  { state, ID::inGain, "Input", true };
    ToggleWithLabel unity { state, ID::unity, "Unity" };
    KnobWithLabel output { state, ID::outGain, "Output" };

    // M/S EQ
    juce::TextButton midBtn { "MID" }, sideBtn { "SIDE" };
    ToggleWithLabel eqOn   { state, ID::eqOn, "EQ" };
    ToggleWithLabel eqMode { state, ID::eqMode, "Phase", "Analog|Linear" };
    std::array<std::unique_ptr<KnobWithLabel>, 7> eqKnobs[2];

    // stereo
    KnobWithLabel monoBass { state, ID::monoBass, "Mono Bass" };
    juce::Slider widthFader;
    juce::Label widthLabel;
    std::unique_ptr<APVTS::SliderAttachment> widthAtt;

    // compressor
    GrColumn grColumn;
    juce::Label grLabel;
    KnobWithLabel compThresh  { state, ID::compThresh, "Threshold" },
                  compRatio   { state, ID::compRatio, "Ratio" },
                  compScHpf   { state, ID::compScHpf, "SC HPF" },
                  compAttack  { state, ID::compAttack, "Attack" },
                  compRelease { state, ID::compRelease, "Release" },
                  compMix     { state, ID::compMix, "Mix" },
                  compMakeup  { state, ID::compMakeup, "Makeup" };
    ToggleWithLabel compOn   { state, ID::compOn, "Comp" };
    ToggleWithLabel compMode { state, ID::compMode, "Detect", "Stereo|M/S" };

    // character + clipper
    KnobWithLabel satMode  { state, ID::satMode, "Mode", false, "Clean|Tape|Tube" };
    KnobWithLabel satDrive { state, ID::satDrive, "Drive" };
    KnobWithLabel satMix   { state, ID::satMix, "Blend" };
    ToggleWithLabel clipOn { state, ID::clipOn, "Clipper" };
    KnobWithLabel clipThresh { state, ID::clipThresh, "Clip" };

    // limiter
    OledDisplay oled;
    KnobWithLabel limGain    { state, ID::limGain, "Gain" },
                  limCeiling { state, ID::limCeiling, "Ceiling" },
                  limRelease { state, ID::limRelease, "Release" };
    ToggleWithLabel limOn  { state, ID::limOn, "True Peak" };
    ToggleWithLabel delta  { state, ID::delta, "Delta" };

    // meters
    LedMeter meterL, meterR;
    LedScale meterScale;

    // analysis row
    SpectrumView spectrum { proc };
    PhaseScope scope { proc };
    LoudnessPanel loudness { proc };

    // bottom bar
    juce::ComboBox osBox, zoomBox, targetBox;
    juce::Label osLabel, targetLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> osAtt, targetAtt;

    std::array<int, 8> sectionX {};
    float compDisp = 0, limDisp = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

class VegaMasteringEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaMasteringEditor (VegaMasteringProcessor&);
    ~VegaMasteringEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (VegaColours::background); }

private:
    void timerCallback() override { panel.refresh(); }
    void applyScale();

    VegaMasteringProcessor& proc;
    VegaLookAndFeel lnf;
    MainPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaMasteringEditor)
};
