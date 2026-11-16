#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =====================================================================
//  Vega Productions — Mastering Suite : parameter definitions
// =====================================================================
namespace ID
{
    // Global
    inline constexpr const char* inGain     = "inGain";
    inline constexpr const char* outGain    = "outGain";
    inline constexpr const char* unity      = "unity";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* delta      = "delta";
    inline constexpr const char* oversample = "oversample";
    inline constexpr const char* target     = "target";

    // M/S EQ
    inline constexpr const char* eqOn   = "eqOn";
    inline constexpr const char* eqMode = "eqMode";   // 0 = Analog (min phase), 1 = Linear phase

    // Per channel EQ (prefix m = Mid, s = Side)
    inline juce::String eq (char ch, const char* name) { return juce::String::charToString (ch) + name; }
    inline constexpr const char* lowFreq  = "LowFreq";
    inline constexpr const char* lowGain  = "LowGain";
    inline constexpr const char* midFreq  = "MidFreq";
    inline constexpr const char* midGain  = "MidGain";
    inline constexpr const char* midQ     = "MidQ";
    inline constexpr const char* highFreq = "HighFreq";
    inline constexpr const char* highGain = "HighGain";

    // Stereo
    inline constexpr const char* width    = "width";
    inline constexpr const char* monoBass = "monoBass";

    // Compressor
    inline constexpr const char* compOn      = "compOn";
    inline constexpr const char* compMode    = "compMode";   // 0 = Stereo linked, 1 = Mid/Side
    inline constexpr const char* compThresh  = "compThresh";
    inline constexpr const char* compRatio   = "compRatio";
    inline constexpr const char* compAttack  = "compAttack";
    inline constexpr const char* compRelease = "compRelease";
    inline constexpr const char* compMakeup  = "compMakeup";
    inline constexpr const char* compMix     = "compMix";
    inline constexpr const char* compScHpf   = "compScHpf";

    // Character / saturation
    inline constexpr const char* satMode  = "satMode";   // Clean / Tape / Tube
    inline constexpr const char* satDrive = "satDrive";
    inline constexpr const char* satMix   = "satMix";

    // Clipper
    inline constexpr const char* clipOn     = "clipOn";
    inline constexpr const char* clipThresh = "clipThresh";

    // Limiter
    inline constexpr const char* limOn      = "limOn";
    inline constexpr const char* limGain    = "limGain";
    inline constexpr const char* limCeiling = "limCeiling";
    inline constexpr const char* limRelease = "limRelease";
}

namespace Choices
{
    inline const juce::StringArray oversample { "1x", "2x", "4x", "8x" };
    inline const juce::StringArray eqMode     { "Analog", "Linear" };
    inline const juce::StringArray compMode   { "Stereo", "Mid/Side" };
    inline const juce::StringArray satMode    { "Clean", "Tape", "Tube" };
    inline const juce::StringArray target     { "-14 Spotify", "-14 YouTube", "-16 Apple Music",
                                                "-11 SoundCloud", "-9 Club", "-8 CD Loud", "-23 Broadcast" };
    inline constexpr float targetLufs[] { -14.0f, -14.0f, -16.0f, -11.0f, -9.0f, -8.0f, -23.0f };
}

namespace ParamFormat
{
    inline juce::String dB (float v, int)      { return juce::String (v, 1) + " dB"; }
    inline juce::String dB2 (float v, int)     { return juce::String (v, 2) + " dB"; }
    inline juce::String ms (float v, int)      { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    inline juce::String pct (float v, int)     { return juce::String (v, 1) + "%"; }
    inline juce::String ratio (float v, int)   { return juce::String (v, 1) + ":1"; }
    inline juce::String q (float v, int)       { return juce::String (v, 2); }
    inline juce::String hz (float v, int)
    {
        if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (v)) + " Hz";
    }
    inline juce::String monoHz (float v, int)  { return v < 20.5f ? juce::String ("Off") : hz (v, 0); }
}

inline juce::NormalisableRange<float> freqRange (float lo, float hi)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                         std::function<String (float, int)> fmt, const String& label = {})
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withStringFromValueFunction (fmt).withLabel (label)));
    };
    auto addBool = [&] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addChoice = [&] (const String& id, const String& name, const StringArray& c, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def));
    };

    // ---- Global
    addFloat (ID::inGain,  "Input",  { -12.0f, 24.0f, 0.01f }, 0.0f, ParamFormat::dB);
    addFloat (ID::outGain, "Output", { -24.0f, 6.0f, 0.01f },  0.0f, ParamFormat::dB2);
    addBool  (ID::unity,  "Unity",  false);
    addBool  (ID::bypass, "Bypass", false);
    addBool  (ID::delta,  "Delta",  false);
    addChoice (ID::oversample, "Oversampling", Choices::oversample, 2);
    addChoice (ID::target, "Loudness Target", Choices::target, 0);

    // ---- M/S EQ
    addBool   (ID::eqOn, "EQ On", true);
    addChoice (ID::eqMode, "EQ Mode", Choices::eqMode, 0);

    for (char ch : { 'm', 's' })
    {
        const String chName = ch == 'm' ? "Mid " : "Side ";
        addFloat (ID::eq (ch, ID::lowFreq),  chName + "Low Freq",  freqRange (20.0f, 500.0f),    100.0f,   ParamFormat::hz);
        addFloat (ID::eq (ch, ID::lowGain),  chName + "Low Gain",  { -12.0f, 12.0f, 0.01f },     0.0f,     ParamFormat::dB);
        addFloat (ID::eq (ch, ID::midFreq),  chName + "Mid Freq",  freqRange (100.0f, 10000.0f), 1000.0f,  ParamFormat::hz);
        addFloat (ID::eq (ch, ID::midGain),  chName + "Mid Gain",  { -12.0f, 12.0f, 0.01f },     0.0f,     ParamFormat::dB);
        addFloat (ID::eq (ch, ID::midQ),     chName + "Mid Q",     { 0.3f, 4.0f, 0.01f, 0.5f },  0.7f,     ParamFormat::q);
        addFloat (ID::eq (ch, ID::highFreq), chName + "Air Freq",  freqRange (1000.0f, 20000.0f),12000.0f, ParamFormat::hz);
        addFloat (ID::eq (ch, ID::highGain), chName + "Air Gain",  { -12.0f, 12.0f, 0.01f },     0.0f,     ParamFormat::dB);
    }

    // ---- Stereo
    addFloat (ID::width,    "Width",     { 0.0f, 200.0f, 0.1f }, 100.0f, ParamFormat::pct);
    addFloat (ID::monoBass, "Mono Bass", freqRange (20.0f, 300.0f), 20.0f, ParamFormat::monoHz);

    // ---- Compressor
    addBool   (ID::compOn,   "Comp On", false);
    addChoice (ID::compMode, "Comp Mode", Choices::compMode, 0);
    addFloat (ID::compThresh,  "Threshold", { -40.0f, 0.0f, 0.01f },        -12.0f, ParamFormat::dB2);
    addFloat (ID::compRatio,   "Ratio",     { 1.0f, 10.0f, 0.01f, 0.5f },    2.0f,  ParamFormat::ratio);
    addFloat (ID::compAttack,  "Attack",    { 0.1f, 100.0f, 0.01f, 0.4f },   10.0f, ParamFormat::ms);
    addFloat (ID::compRelease, "Release",   { 10.0f, 1500.0f, 0.1f, 0.4f },  150.0f, ParamFormat::ms);
    addFloat (ID::compMakeup,  "Makeup",    { 0.0f, 18.0f, 0.01f },          0.0f,  ParamFormat::dB2);
    addFloat (ID::compMix,     "Comp Mix",  { 0.0f, 100.0f, 0.1f },          100.0f, ParamFormat::pct);
    addFloat (ID::compScHpf,   "SC HPF",    freqRange (20.0f, 300.0f),       90.0f, ParamFormat::hz);

    // ---- Character
    addChoice (ID::satMode, "Character", Choices::satMode, 0);
    addFloat (ID::satDrive, "Drive",   { 0.0f, 12.0f, 0.01f },   3.0f,   ParamFormat::dB);
    addFloat (ID::satMix,   "Sat Mix", { 0.0f, 100.0f, 0.1f },   100.0f, ParamFormat::pct);

    // ---- Clipper
    addBool  (ID::clipOn,     "Clip On", false);
    addFloat (ID::clipThresh, "Clip",    { -12.0f, 0.0f, 0.01f }, -1.0f, ParamFormat::dB);

    // ---- Limiter
    addBool  (ID::limOn,      "Limiter On", true);
    addFloat (ID::limGain,    "Lim Gain",    { 0.0f, 24.0f, 0.01f },         0.0f,  ParamFormat::dB);
    addFloat (ID::limCeiling, "Ceiling", { -3.0f, 0.0f, 0.01f },         -1.0f, ParamFormat::dB2);
    addFloat (ID::limRelease, "Lim Release", { 1.0f, 1000.0f, 0.1f, 0.4f },  100.0f, ParamFormat::ms);

    return layout;
}
