// Offline sanity test: drives the processor with loud, peaky material and checks output.
#include "../Source/PluginProcessor.h"
#include <iostream>
// independent reference: 8x windowed-sinc interpolation peak
static float refTruePeak (const std::vector<float>& x)
{
    const int up = 8, half = 32; float pk = 0;
    for (size_t n = half; n + half < x.size(); ++n)
        for (int ph = 0; ph < up; ++ph)
        {
            const double t = (double) ph / up; double acc = 0;
            for (int k = -half + 1; k <= half; ++k)
            {
                const double d = k - t; const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * d) / (juce::MathConstants<double>::pi * d);
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * d / half);
                acc += x[n + (size_t) k] * sinc * w;
            }
            pk = std::max (pk, (float) std::abs (acc));
        }
    return pk;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<VegaMasteringProcessor&> (*base);
    const double fs = 48000; const int block = 480;
    p.setPlayConfigDetails (2, 2, fs, block);
    p.prepareToPlay (fs, block);

    int fails = 0;
    for (int preset = 0; preset < VegaMasteringProcessor::getNumFactoryPresets(); ++preset)
      for (int os = 0; os < 4; ++os)
      {
        p.loadFactoryPreset (preset);
        p.apvts.getParameter (ID::oversample)->setValueNotifyingHost (p.apvts.getParameter (ID::oversample)->convertTo0to1 ((float) os));
        p.apvts.getParameter (ID::limGain)->setValueNotifyingHost (p.apvts.getParameter (ID::limGain)->convertTo0to1 (12.0f));
        p.resetLoudness();
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        juce::Random rng (42);
        float maxOut = 0; bool bad = false; double phase = 0; std::vector<float> capL;
        for (int b = 0; b < 600; ++b)  // 6 s
        {
            for (int i = 0; i < block; ++i)
            {
                phase += 2 * juce::MathConstants<double>::pi * 11000.0 / fs;   // near‑Nyquist content -> inter‑sample peaks
                const float kick = (i < 40 && b % 25 == 0) ? 0.95f : 0.0f;
                const float x = 0.5f * (float) std::sin (phase) + 0.3f * (rng.nextFloat() * 2 - 1) + kick;
                buf.setSample (0, i, x); buf.setSample (1, i, x * 0.8f + 0.1f * (rng.nextFloat() * 2 - 1));
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) bad = true;
                    if (b > 10) maxOut = std::max (maxOut, std::abs (v));
                    if (c == 0 && b > 300 && b < 340) capL.push_back (v);
                }
        }
        const float ceilDb = p.apvts.getRawParameterValue (ID::limCeiling)->load();
        const float tp = p.meters.truePeakMax.load();
        const bool ok = ! bad && vega::gainToDb (maxOut) <= ceilDb + 0.001f && (os < 2 || tp <= ceilDb + 0.3f);
        if (! ok) ++fails;
        std::cout << (ok ? "PASS " : "FAIL ") << VegaMasteringProcessor::getFactoryPresetName (preset) << " os=" << (1 << os)
                  << "x  peak=" << vega::gainToDb (maxOut) << " dBFS  truePeak=" << tp << " dBTP  ref=" << vega::gainToDb (refTruePeak (capL)) << " ceiling=" << ceilDb
                  << "  ST=" << p.loudness.shortTerm.load() << " LUFS  latency=" << p.getLatencySamples() << "\n";
      }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << std::endl;
    return fails;
}
