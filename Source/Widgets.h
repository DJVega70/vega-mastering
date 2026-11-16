#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VegaLookAndFeel.h"

using APVTS = juce::AudioProcessorValueTreeState;

// =====================================================================
//  Labelled controls
// =====================================================================
class KnobWithLabel : public juce::Component
{
public:
    KnobWithLabel (APVTS& state, const juce::String& paramId, const juce::String& title, bool big = false,
                   const juce::String& stepLabels = {})
    {
        label.setText (title, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (VegaFonts::ui (big ? 12.5f : 11.5f));
        label.setColour (juce::Label::textColourId, VegaColours::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (stepLabels.isNotEmpty() ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 90, 16);
        slider.setMouseDragSensitivity (big ? 320 : 220);
        slider.setVelocityBasedMode (false);
        slider.getProperties().set ("big", big);
        if (stepLabels.isNotEmpty()) slider.getProperties().set ("stepLabels", stepLabels);
        addAndMakeVisible (slider);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (
                                                    state.getParameter (paramId)->getDefaultValue()));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (16));
        slider.setBounds (b);
    }

    juce::Slider slider;
    juce::Label label;
private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

class ToggleWithLabel : public juce::Component
{
public:
    ToggleWithLabel (APVTS& state, const juce::String& paramId, const juce::String& title,
                     const juce::String& switchLabels = {})
    {
        label.setText (title, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (VegaFonts::ui (11.5f));
        label.setColour (juce::Label::textColourId, VegaColours::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        isSwitch = switchLabels.isNotEmpty();
        if (isSwitch) { button.getProperties().set ("switch", true); button.setButtonText (switchLabels); }
        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, button);
    }
    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (16));
        if (isSwitch)
            button.setBounds (b.withSizeKeepingCentre (std::min (b.getWidth(), 76), 34).translated (8, 0));
        else
            button.setBounds (b.withSizeKeepingCentre (34, 52).withY (b.getY() + 6));
    }
    juce::ToggleButton button;
    juce::Label label;
private:
    bool isSwitch = false;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// =====================================================================
//  Segmented LED meters
// =====================================================================
class LedMeter : public juce::Component
{
public:
    void setLevel (float linearPeak)
    {
        const float db = vega::gainToDb (linearPeak);
        level = db > level ? db : level - 1.2f;          // fall‑back ballistics
        if (db > hold) { hold = db; holdTicks = 45; }
        else if (--holdTicks <= 0) hold -= 0.8f;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const int n = (int) std::size (segDb);
        const float segH = b.getHeight() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto seg = juce::Rectangle<float> (b.getX(), b.getY() + i * segH, b.getWidth(), segH).reduced (0, segH * 0.18f);
            const float th = segDb[i];
            const bool lit = level >= th;
            juce::Colour c = th >= -0.1f ? juce::Colour (0xffff5a5a) : (th > -18.0f ? juce::Colours::white : VegaColours::accent);
            g.setColour (lit ? c : juce::Colour (0xff3a3a3a));
            g.fillRect (seg);
            if (! lit && std::abs (hold - th) < (i < n - 1 ? (segDb[i] - segDb[i + 1]) * 0.5f : 3.0f))
            {
                g.setColour (c.withAlpha (0.7f));
                g.fillRect (seg);
            }
        }
    }
    static constexpr float segDb[] { 0, -1, -2, -3, -4, -5, -6, -8, -10, -12, -15, -18, -21, -24, -30, -36, -42, -48, -54, -60 };
private:
    float level = -100, hold = -100; int holdTicks = 0;
};

class LedScale : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        const int n = (int) std::size (LedMeter::segDb);
        const float segH = (float) getHeight() / (float) n;
        g.setFont (VegaFonts::ui (9.5f));
        g.setColour (VegaColours::textDim);
        for (int i = 0; i < n; ++i)
        {
            const int db = (int) LedMeter::segDb[i];
            if (db == 0 || db == -3 || db == -6 || db == -12 || db == -18 || db == -24 || db == -36 || db == -48 || db == -60)
                g.drawText (juce::String (std::abs (db)), 0, (int) (i * segH + segH * 0.5f) - 6, getWidth(), 12,
                            juce::Justification::centred);
        }
    }
};

class GrColumn : public juce::Component
{
public:
    void setGr (float db)
    {
        value = db > value ? db : std::max (0.0f, value - 0.25f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto meter = b.removeFromLeft (14.0f);
        constexpr int n = 16;
        const float segH = meter.getHeight() / n;
        for (int i = 0; i < n; ++i)
        {
            const float th = (i + 0.5f) * 12.0f / n;  // 0..12 dB top → bottom
            auto seg = juce::Rectangle<float> (meter.getX(), meter.getY() + i * segH, meter.getWidth(), segH).reduced (0, segH * 0.18f);
            g.setColour (value >= th ? VegaColours::accent : juce::Colour (0xff3a3a3a));
            g.fillRect (seg);
        }
        g.setFont (VegaFonts::ui (9.0f));
        g.setColour (VegaColours::textDim);
        for (int db : { 0, 3, 6, 9, 12 })
            g.drawText (juce::String (db), (int) b.getX() + 2,
                        juce::jlimit ((int) meter.getY(), (int) meter.getBottom() - 12, (int) (meter.getY() + db / 12.0f * meter.getHeight()) - 6), 20, 12,
                        juce::Justification::left);
    }
private:
    float value = 0;
};

// =====================================================================
//  OLED read‑out (cycles Short‑Term / Momentary / Integrated / True Peak / Target)
// =====================================================================
class OledDisplay : public juce::Component
{
public:
    struct Values { float momentary = -100, shortTerm = -100, integrated = -100, truePeak = -100, target = -14, compGr = 0, limGr = 0; };

    void setValues (const Values& v) { vals = v; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        mode = (mode + (e.x < getWidth() / 2 ? numModes - 1 : 1)) % numModes;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101010));
        g.fillRoundedRectangle (b, 3.0f);
        auto screen = b.reduced (4.0f);
        g.setColour (VegaColours::oled);
        g.fillRect (screen);

        const juce::Colour ink = juce::Colours::white.withAlpha (0.92f);
        auto fmt = [] (float v, int dp) { return v <= -99.0f ? juce::String ("--.-") : juce::String (v, dp); };

        static const char* titles[] { "SHORT TERM", "MOMENTARY", "INTEGRATED", "TRUE PEAK", "TO TARGET" };
        juce::String value;
        switch (mode)
        {
            case 0: value = fmt (vals.shortTerm, 1) + " LUFS"; break;
            case 1: value = fmt (vals.momentary, 1) + " LUFS"; break;
            case 2: value = fmt (vals.integrated, 1) + " LUFS"; break;
            case 3: value = fmt (vals.truePeak, 1) + " dBTP"; break;
            default:
            {
                const float ref = vals.integrated > -99.0f ? vals.integrated : vals.shortTerm;
                value = ref <= -99.0f ? juce::String ("--.- LU") : ((ref - vals.target >= 0 ? "+" : "") + juce::String (ref - vals.target, 1) + " LU");
            }
        }

        auto top = screen.removeFromTop (screen.getHeight() * 0.48f);
        g.setFont (VegaFonts::mono (11.0f));
        g.setColour (ink);
        auto row1 = top.removeFromTop (top.getHeight() * 0.5f);
        g.drawText (titles[mode], row1, juce::Justification::centredBottom);
        g.drawText ("<", row1.withTrimmedLeft (10).toNearestInt(), juce::Justification::bottomLeft);
        g.drawText (">", row1.withTrimmedRight (10).toNearestInt(), juce::Justification::bottomRight);
        g.setFont (VegaFonts::mono (12.5f));
        g.drawText (value, top, juce::Justification::centred);

        g.setFont (VegaFonts::mono (10.5f));
        auto left = screen.removeFromLeft (screen.getWidth() * 0.5f);
        g.drawText ("COMP GR", left.removeFromTop (left.getHeight() * 0.5f), juce::Justification::centredBottom);
        g.drawText (juce::String (vals.compGr, 1) + " dB", left, juce::Justification::centredTop);
        g.drawText ("LIMITER", screen.removeFromTop (screen.getHeight() * 0.5f), juce::Justification::centredBottom);
        g.drawText (juce::String (vals.limGr, 1) + " dB", screen, juce::Justification::centredTop);

        // faint scan lines for an OLED feel
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (float y = b.getY() + 4; y < b.getBottom() - 4; y += 2.0f) g.drawHorizontalLine ((int) y, b.getX() + 4, b.getRight() - 4);
    }

private:
    Values vals;
    int mode = 0; static constexpr int numModes = 5;
};

// =====================================================================
//  Spectrum analyser with M/S EQ curve overlay
// =====================================================================
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (VegaMasteringProcessor& p) : proc (p)
    {
        ring.assign (fftSize, 0.0f);
        fftData.assign (fftSize * 2, 0.0f);
        display.assign (fftSize / 2 + 1, -120.0f);
    }

    void refresh()
    {
        float l[2048], r[2048];
        bool got = false;
        while (proc.analyserFifo.ready() > 0)
        {
            const int n = proc.analyserFifo.pull (l, r, 2048);
            for (int i = 0; i < n; ++i) { ring[(size_t) ringPos] = 0.5f * (l[i] + r[i]); ringPos = (ringPos + 1) % fftSize; }
            got = true;
        }
        if (got)
        {
            for (int i = 0; i < fftSize; ++i) fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)];
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            const double fs = proc.getCurrentSampleRate();
            for (int k = 1; k <= fftSize / 2; ++k)
            {
                const double f = (double) k * fs / fftSize;
                float db = vega::gainToDb (fftData[(size_t) k] / (fftSize * 0.25f))
                           + 3.0f * (float) std::log2 (std::max (f, 20.0) / 1000.0);   // +3 dB/oct tilt
                auto& d = display[(size_t) k];
                d = db > d ? db : d - 1.4f;
            }
        }
        else
            for (auto& d : display) d -= 1.4f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (b, 3.0f);
        auto plot = b.reduced (8.0f, 6.0f).withTrimmedBottom (12.0f).withTrimmedLeft (24.0f);
        const double fs = proc.getCurrentSampleRate();

        auto xFor = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        auto ySpec = [&] (float db) { return juce::jmap (juce::jlimit (-84.0f, 6.0f, db), -84.0f, 6.0f, plot.getBottom(), plot.getY()); };
        auto yEq = [&] (double db) { return juce::jmap ((float) juce::jlimit (-15.0, 15.0, db), -15.0f, 15.0f, plot.getBottom(), plot.getY()); };

        // grid
        g.setFont (VegaFonts::ui (9.5f));
        for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xFor (f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (VegaColours::textDim);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        (int) x - 15, (int) plot.getBottom() + 1, 30, 12, juce::Justification::centred);
        }
        for (int db : { 12, 6, 0, -6, -12 })
        {
            const float y = yEq (db);
            g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.12f : 0.05f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (VegaColours::textDim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), (int) b.getX() + 2, (int) y - 6, 26, 12, juce::Justification::centredLeft);
        }

        // spectrum
        juce::Path spec;
        bool started = false;
        const int W = (int) plot.getWidth();
        for (int px = 0; px <= W; px += 2)
        {
            const double f0 = 20.0 * std::pow (1000.0, (double) px / W), f1 = 20.0 * std::pow (1000.0, (double) (px + 2) / W);
            int k0 = juce::jlimit (1, fftSize / 2, (int) (f0 * fftSize / fs));
            int k1 = juce::jlimit (k0, fftSize / 2, (int) (f1 * fftSize / fs));
            float mx = -200.0f;
            for (int k = k0; k <= k1; ++k) mx = std::max (mx, display[(size_t) k]);
            const float y = ySpec (mx), x = plot.getX() + (float) px;
            if (! started) { spec.startNewSubPath (x, plot.getBottom()); spec.lineTo (x, y); started = true; }
            else spec.lineTo (x, y);
        }
        juce::Path fill (spec);
        fill.lineTo (plot.getRight(), plot.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0, plot.getY(),
                                                 juce::Colours::white.withAlpha (0.02f), 0, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));

        // EQ curves (Mid = cyan, Side = gold)
        const bool eqOn = proc.apvts.getRawParameterValue (ID::eqOn)->load() > 0.5f;
        const int sel = proc.eqUiChannel.load();
        for (int ch : { 1 - sel, sel })
        {
            const auto s = readEq (ch == 0 ? 'm' : 's');
            juce::Path curve;
            for (int px = 0; px <= W; px += 2)
            {
                const double f = 20.0 * std::pow (1000.0, (double) px / W);
                const double db = eqOn ? 20.0 * std::log10 (s.magnitude (f, fs)) : 0.0;
                const float x = plot.getX() + (float) px, y = yEq (db);
                if (px == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            const auto col = ch == 0 ? VegaColours::accent : VegaColours::gold;
            g.setColour (col.withAlpha (ch == sel ? 0.95f : 0.35f));
            g.strokePath (curve, juce::PathStrokeType (ch == sel ? 1.8f : 1.2f));
        }

        g.setFont (VegaFonts::ui (10.0f, true));
        g.setColour (VegaColours::accent);
        g.drawText ("MID", (int) plot.getRight() - 70, (int) plot.getY() + 2, 30, 12, juce::Justification::right);
        g.setColour (VegaColours::gold);
        g.drawText ("SIDE", (int) plot.getRight() - 36, (int) plot.getY() + 2, 34, 12, juce::Justification::right);
        g.setColour (VegaColours::textDim);
        g.drawText ("SPECTRUM  +3 dB/oct", (int) plot.getX() + 4, (int) plot.getY() + 2, 160, 12, juce::Justification::left);
    }

private:
    vega::EqSettings readEq (char ch) const
    {
        auto get = [&] (const char* n) { return proc.apvts.getRawParameterValue (ID::eq (ch, n))->load(); };
        vega::EqSettings s;
        s.lowF = get (ID::lowFreq); s.lowG = get (ID::lowGain); s.midF = get (ID::midFreq);
        s.midG = get (ID::midGain); s.midQ = get (ID::midQ); s.highF = get (ID::highFreq); s.highG = get (ID::highGain);
        return s;
    }

    VegaMasteringProcessor& proc;
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring, fftData, display;
    int ringPos = 0;
};

// =====================================================================
//  Phase scope (goniometer) + correlation meter
// =====================================================================
class PhaseScope : public juce::Component
{
public:
    explicit PhaseScope (VegaMasteringProcessor& p) : proc (p) { pts.resize (1536); }

    void refresh (float correlation)
    {
        float l[1024], r[1024];
        while (proc.scopeFifo.ready() > 0)
        {
            const int n = proc.scopeFifo.pull (l, r, 1024);
            for (int i = 0; i < n; i += 2)   // decimate a little
            {
                pts[(size_t) pos] = { l[i], r[i] };
                pos = (pos + 1) % (int) pts.size();
            }
        }
        corr = corr * 0.8f + correlation * 0.2f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (b, 3.0f);
        auto corrArea = b.removeFromBottom (22.0f).reduced (10.0f, 5.0f);
        auto sq = b.reduced (10.0f);
        sq = sq.withSizeKeepingCentre (std::min (sq.getWidth(), sq.getHeight()), std::min (sq.getWidth(), sq.getHeight()));
        const auto c = sq.getCentre();
        const float R = sq.getWidth() * 0.5f;

        // guides
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        juce::Path diamond;
        diamond.startNewSubPath (c.x, c.y - R); diamond.lineTo (c.x + R, c.y); diamond.lineTo (c.x, c.y + R); diamond.lineTo (c.x - R, c.y); diamond.closeSubPath();
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
        g.drawLine (c.x, c.y - R, c.x, c.y + R, 1.0f);
        g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.0f);
        g.drawLine (c.x - R * 0.707f, c.y - R * 0.707f, c.x + R * 0.707f, c.y + R * 0.707f, 0.7f);
        g.drawLine (c.x + R * 0.707f, c.y - R * 0.707f, c.x - R * 0.707f, c.y + R * 0.707f, 0.7f);
        g.setFont (VegaFonts::ui (9.5f));
        g.setColour (VegaColours::textDim);
        g.drawText ("M", (int) c.x - 6, (int) (c.y - R) - 1, 12, 12, juce::Justification::centred);
        g.drawText ("L", (int) (c.x - R * 0.72f) - 12, (int) (c.y - R * 0.72f) - 12, 12, 12, juce::Justification::centred);
        g.drawText ("R", (int) (c.x + R * 0.72f), (int) (c.y - R * 0.72f) - 12, 12, 12, juce::Justification::centred);

        // points
        g.setColour (VegaColours::accent.withAlpha (0.55f));
        for (auto& pt : pts)
        {
            const float m = (pt.x + pt.y) * 0.7071f, s = (pt.x - pt.y) * 0.7071f;
            const float x = c.x - s * R, y = c.y - m * R;
            if (std::abs (x - c.x) + std::abs (y - c.y) < 0.5f) continue;
            g.fillRect (x, y, 1.4f, 1.4f);
        }

        // correlation bar
        g.setColour (juce::Colour (0xff222222));
        g.fillRoundedRectangle (corrArea, 2.0f);
        const float cx = corrArea.getCentreX();
        const float mx = juce::jmap (juce::jlimit (-1.0f, 1.0f, corr), -1.0f, 1.0f, corrArea.getX(), corrArea.getRight());
        g.setColour (corr < 0 ? juce::Colour (0xffff6060) : VegaColours::accent);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, mx), corrArea.getY() + 2, std::max (cx, mx), corrArea.getBottom() - 2));
        g.setColour (VegaColours::textDim);
        g.setFont (VegaFonts::ui (9.0f));
        g.drawText ("-1", corrArea.toNearestInt(), juce::Justification::centredLeft);
        g.drawText ("+1", corrArea.toNearestInt(), juce::Justification::centredRight);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawVerticalLine ((int) cx, corrArea.getY(), corrArea.getBottom());
    }

private:
    VegaMasteringProcessor& proc;
    std::vector<juce::Point<float>> pts;
    int pos = 0;
    float corr = 1.0f;
};

// =====================================================================
//  Loudness panel (LUFS M / S / I, True Peak, target)
// =====================================================================
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (VegaMasteringProcessor& p) : proc (p)
    {
        reset.setButtonText ("RESET");
        reset.onClick = [this] { proc.resetLoudness(); };
        addAndMakeVisible (reset);
    }
    void setValues (const OledDisplay::Values& v) { vals = v; repaint(); }

    void resized() override { reset.setBounds (getLocalBounds().removeFromBottom (26).reduced (10, 3)); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (b, 3.0f);
        auto area = b.reduced (10.0f, 8.0f).withTrimmedBottom (24.0f);

        auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("--.-") : juce::String (v, 1); };
        const float ref = vals.integrated > -99.0f ? vals.integrated : vals.shortTerm;
        const juce::String delta = ref <= -99.0f ? "--.-" : ((ref - vals.target >= 0 ? "+" : "") + juce::String (ref - vals.target, 1));
        const bool tpOver = vals.truePeak > proc.apvts.getRawParameterValue (ID::limCeiling)->load() + 0.05f;

        struct Row { const char* name; juce::String value; const char* unit; juce::Colour col; };
        const Row rows[] {
            { "MOMENTARY",  fmt (vals.momentary),  "LUFS", VegaColours::text },
            { "SHORT TERM", fmt (vals.shortTerm),  "LUFS", VegaColours::text },
            { "INTEGRATED", fmt (vals.integrated), "LUFS", VegaColours::accent },
            { "TRUE PEAK",  fmt (vals.truePeak),   "dBTP", tpOver ? juce::Colour (0xffff6060) : VegaColours::text },
            { "TO TARGET",  delta,                 "LU",   VegaColours::gold },
        };
        const float rh = area.getHeight() / (float) std::size (rows);
        for (auto& r : rows)
        {
            auto row = area.removeFromTop (rh);
            g.setFont (VegaFonts::ui (9.5f));
            g.setColour (VegaColours::textDim);
            g.drawText (r.name, row.removeFromTop (rh * 0.4f), juce::Justification::bottomLeft);
            g.setFont (VegaFonts::mono (15.0f));
            g.setColour (r.col);
            g.drawText (r.value, row.withTrimmedRight (34), juce::Justification::centredLeft);
            g.setFont (VegaFonts::ui (9.5f));
            g.setColour (VegaColours::textDim);
            g.drawText (r.unit, row, juce::Justification::centredRight);
        }
    }

private:
    VegaMasteringProcessor& proc;
    juce::TextButton reset;
    OledDisplay::Values vals;
};
