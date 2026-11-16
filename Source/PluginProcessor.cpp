#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vega;

VegaMasteringProcessor::VegaMasteringProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VegaMastering", createParameterLayout())
{
    const char* names[] { ID::lowFreq, ID::lowGain, ID::midFreq, ID::midGain, ID::midQ, ID::highFreq, ID::highGain };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 7; ++i)
            eqPtrs[(size_t) c][(size_t) i] = apvts.getRawParameterValue (ID::eq (c == 0 ? 'm' : 's', names[i]));
    startTimerHz (30);
}

VegaMasteringProcessor::~VegaMasteringProcessor() { stopTimer(); }

bool VegaMasteringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorParameter* VegaMasteringProcessor::getBypassParameter() const
{
    return apvts.getParameter (ID::bypass);
}

// =====================================================================
void VegaMasteringProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    maxChunk = juce::jmax (32, samplesPerBlock);
    lookaheadBase = juce::jmax (1, (int) std::round (0.0015 * sampleRate));   // 1.5 ms look‑ahead
    firOrder = sampleRate <= 50000.0 ? 12 : (sampleRate <= 100000.0 ? 13 : 14);

    // Oversampling 2x / 4x / 8x (linear‑phase FIR half‑band filters)
    for (int i = 1; i < 4; ++i)
    {
        oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) i, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversamplers[(size_t) i]->initProcessing ((size_t) maxChunk);
    }
    for (int i = 0; i < 4; ++i)
        nonLinear[(size_t) i].prepare (sampleRate * (1 << i));
    limiter.prepare (lookaheadBase);
    truePeakMeter.reset();

    eqMid.reset(); eqSide.reset(); eqMid.initialised = eqSide.initialised = false;
    monoBassHpf.reset(); lastMonoBass = -1.0f;
    compressor.prepare (sampleRate);
    loudness.prepare (sampleRate);

    linearEq.prepare ({ sampleRate, (juce::uint32) maxChunk, 2 });
    linearEq.reset();
    irValid = false;
    rebuildLinearPhaseIr();

    for (auto& r : dryRing) r.assign (1 << 16, 0.0f);
    dryMask = (1 << 16) - 1; dryPos = 0;
    dryBuf.setSize (2, maxChunk);

    for (auto* s : { &inGainS, &outGainS, &limGainS, &bypassS })
        s->reset (sampleRate, 0.03);
    inGainS.setCurrentAndTargetValue (dbToGain (p (ID::inGain)));
    outGainS.setCurrentAndTargetValue (1.0f);
    limGainS.setCurrentAndTargetValue (dbToGain (p (ID::limGain)));
    bypassS.setCurrentAndTargetValue (p (ID::bypass) > 0.5f ? 1.0f : 0.0f);

    corrCoef = std::exp (-1.0 / (0.3 * sampleRate));
    corrLR = corrLL = corrRR = 0;
    lastOsIndex = -1;

    setLatencySamples (computeLatency ((int) p (ID::oversample), p (ID::eqMode) > 0.5f));
}

int VegaMasteringProcessor::computeLatency (int osIndex, bool linear) const
{
    int lat = lookaheadBase + vega::TruePeakInterpolator::half;   // limiter look‑ahead + detector
    if (osIndex > 0 && oversamplers[(size_t) osIndex] != nullptr)
        lat += (int) std::round (oversamplers[(size_t) osIndex]->getLatencyInSamples());
    if (linear) lat += (1 << firOrder) / 2;
    return lat;
}

EqSettings VegaMasteringProcessor::readEq (char ch) const
{
    const auto& e = eqPtrs[ch == 'm' ? 0 : 1];
    EqSettings s;
    s.lowF = e[0]->load(); s.lowG = e[1]->load();
    s.midF = e[2]->load(); s.midG = e[3]->load(); s.midQ = e[4]->load();
    s.highF = e[5]->load(); s.highG = e[6]->load();
    return s;
}

void VegaMasteringProcessor::rebuildLinearPhaseIr()
{
    irMid = readEq ('m'); irSide = readEq ('s'); irEqOn = p (ID::eqOn) > 0.5f;
    const int N = 1 << firOrder;
    const double fs = sampleRateHz;
    juce::AudioBuffer<float> ir (2, N);
    const auto mid = irMid, side = irSide; const bool on = irEqOn;
    designLinearPhaseFir ([&] (double f) { return on ? mid.magnitude (f, fs) : 1.0; },  fs, firOrder, ir.getWritePointer (0));
    designLinearPhaseFir ([&] (double f) { return on ? side.magnitude (f, fs) : 1.0; }, fs, firOrder, ir.getWritePointer (1));
    linearEq.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    irValid = true; ticksSinceIr = 0;
}

void VegaMasteringProcessor::timerCallback()
{
    ++ticks; ++ticksSinceIr;

    // keep the host's latency compensation in sync with oversampling / EQ mode
    const int wanted = computeLatency ((int) p (ID::oversample), p (ID::eqMode) > 0.5f);
    if (wanted != getLatencySamples()) setLatencySamples (wanted);

    // rebuild the linear‑phase FIR when EQ settings change (throttled to ~10 Hz)
    if (irValid && ticksSinceIr >= 3)
        if (readEq ('m') != irMid || readEq ('s') != irSide || (p (ID::eqOn) > 0.5f) != irEqOn)
            rebuildLinearPhaseIr();

    if (ticks % 10 == 0)
        meters.integrated.store (loudness.computeIntegrated());
}

// =====================================================================
void VegaMasteringProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    if (buffer.getNumChannels() < 2) return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int start = 0, total = buffer.getNumSamples(); start < total; )
    {
        const int n = juce::jmin (maxChunk, total - start);
        processChunk (L + start, R + start, n);
        start += n;
    }
}

void VegaMasteringProcessor::processChunk (float* L, float* R, int n)
{
    const int osIndex  = juce::jlimit (0, 3, (int) p (ID::oversample));
    const bool linear  = p (ID::eqMode) > 0.5f;
    const bool eqOn    = p (ID::eqOn) > 0.5f;
    const int latency  = computeLatency (osIndex, linear);
    const float inDb   = p (ID::inGain);

    if (osIndex != lastOsIndex)
    {
        if (oversamplers[(size_t) osIndex]) oversamplers[(size_t) osIndex]->reset();
        nonLinear[(size_t) osIndex].reset();
        lastOsIndex = osIndex;
    }

    // ---- latency‑aligned dry copy (for Delta + Bypass) and input metering
    float* dL = dryBuf.getWritePointer (0);
    float* dR = dryBuf.getWritePointer (1);
    float inPkL = 0, inPkR = 0;
    for (int i = 0; i < n; ++i)
    {
        inPkL = std::max (inPkL, std::abs (L[i])); inPkR = std::max (inPkR, std::abs (R[i]));
        dryRing[0][(size_t) dryPos] = L[i]; dryRing[1][(size_t) dryPos] = R[i];
        const int rd = (dryPos - latency) & dryMask;
        dL[i] = dryRing[0][(size_t) rd]; dR[i] = dryRing[1][(size_t) rd];
        dryPos = (dryPos + 1) & dryMask;
    }
    MeterData::atomicMax (meters.inL, inPkL);
    MeterData::atomicMax (meters.inR, inPkR);

    // ---- input gain
    inGainS.setTargetValue (dbToGain (inDb));
    for (int i = 0; i < n; ++i) { const float g = inGainS.getNextValue(); L[i] *= g; R[i] *= g; }

    // ---- L/R -> M/S
    for (int i = 0; i < n; ++i)
    {
        const float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f;
        L[i] = m; R[i] = s;
    }

    // ---- M/S EQ
    if (linear)
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        linearEq.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    else if (eqOn)
    {
        eqMid.update (readEq ('m'), sampleRateHz);
        eqSide.update (readEq ('s'), sampleRateHz);
        for (int i = 0; i < n; ++i) { L[i] = eqMid.process (L[i]); R[i] = eqSide.process (R[i]); }
    }

    // ---- stereo width + mono bass (high‑pass on the side channel)
    const float width = p (ID::width) * 0.01f;
    const float mono  = p (ID::monoBass);
    if (mono != lastMonoBass) { monoBassHpf.c = BiquadCoeffs::highPass (sampleRateHz, mono); lastMonoBass = mono; }
    const bool monoOn = mono > 20.5f;
    for (int i = 0; i < n; ++i)
    {
        float s = R[i] * width;
        if (monoOn) s = monoBassHpf.process (s);
        const float m = L[i];
        L[i] = m + s; R[i] = m - s;
    }

    // ---- glue compressor
    float compGr = 0.0f;
    if (p (ID::compOn) > 0.5f)
    {
        compressor.setParams (p (ID::compThresh), p (ID::compRatio), p (ID::compAttack), p (ID::compRelease),
                              p (ID::compMakeup), p (ID::compMix), p (ID::compScHpf), p (ID::compMode) > 0.5f);
        compGr = compressor.process (L, R, n);
    }
    MeterData::atomicMax (meters.compGr, compGr);

    // ---- drive into the oversampled stage
    limGainS.setTargetValue (dbToGain (p (ID::limGain)));
    for (int i = 0; i < n; ++i) { const float g = limGainS.getNextValue(); L[i] *= g; R[i] *= g; }

    // ---- oversampled: saturation -> clipper, then true‑peak limiter at base rate
    const bool limOn = p (ID::limOn) > 0.5f;
    const float ceilingDb = p (ID::limCeiling);
    NonLinearStage::Settings s;
    s.satMode  = (int) p (ID::satMode);
    s.drive    = dbToGain (p (ID::satDrive));
    s.satMix   = p (ID::satMix) * 0.01f;
    s.clipOn   = p (ID::clipOn) > 0.5f;
    s.clipT    = dbToGain (p (ID::clipThresh));

    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    const bool nonLinearActive = (s.satMode > 0 && s.satMix > 0.0f) || s.clipOn;
    if (osIndex > 0)
    {
        auto& os = *oversamplers[(size_t) osIndex];
        auto up = os.processSamplesUp (block);
        if (nonLinearActive) nonLinear[(size_t) osIndex].process (up, s);
        os.processSamplesDown (block);   // always run so latency stays constant
    }
    else if (nonLinearActive)
    {
        nonLinear[0].process (block, s);
    }

    const float relCoef = std::exp (-1.0f / (p (ID::limRelease) * 0.001f * (float) sampleRateHz));
    const float limGain = limiter.process (L, R, n, dbToGain (ceilingDb - 0.15f), relCoef, limOn);
    MeterData::atomicMax (meters.limGr, -gainToDb (limGain));

    if (limOn)  // sample‑peak safety net after reconstruction
    {
        const float c = dbToGain (ceilingDb);
        for (int i = 0; i < n; ++i) { L[i] = juce::jlimit (-c, c, L[i]); R[i] = juce::jlimit (-c, c, R[i]); }
    }

    // ---- output gain (Unity = level‑matched: output trims by the input gain)
    const float outDb = p (ID::outGain) - (p (ID::unity) > 0.5f ? inDb : 0.0f);
    outGainS.setTargetValue (dbToGain (outDb));
    const bool delta = p (ID::delta) > 0.5f;
    const float dryMatch = dbToGain (inDb);
    bypassS.setTargetValue (p (ID::bypass) > 0.5f ? 1.0f : 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float g = outGainS.getNextValue();
        L[i] *= g; R[i] *= g;
        if (delta) { L[i] -= dL[i] * dryMatch * g; R[i] -= dR[i] * dryMatch * g; }
        const float b = bypassS.getNextValue();
        if (b > 0.0f) { L[i] += b * (dL[i] - L[i]); R[i] += b * (dR[i] - R[i]); }
    }

    // ---- output metering
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        pkL = std::max (pkL, std::abs (L[i])); pkR = std::max (pkR, std::abs (R[i]));
        const double l = L[i], r = R[i];
        corrLR = corrCoef * corrLR + (1 - corrCoef) * l * r;
        corrLL = corrCoef * corrLL + (1 - corrCoef) * l * l;
        corrRR = corrCoef * corrRR + (1 - corrCoef) * r * r;
    }
    MeterData::atomicMax (meters.outL, pkL);
    MeterData::atomicMax (meters.outR, pkR);
    const double den = std::sqrt (corrLL * corrRR);
    meters.correlation.store (den > 1.0e-10 ? (float) (corrLR / den) : 1.0f);

    // true peak (4x interpolated, BS.1770)
    float tp = std::max (pkL, pkR);
    for (int i = 0; i < n; ++i) tp = std::max (tp, truePeakMeter.push (L[i], R[i]));
    MeterData::atomicMax (meters.truePeakRecent, tp);
    MeterData::atomicMax (meters.truePeakMax, gainToDb (tp));

    loudness.process (L, R, n);
    analyserFifo.push (L, R, n);
    scopeFifo.push (L, R, n);
}

void VegaMasteringProcessor::resetLoudness()
{
    loudness.requestReset();
    meters.truePeakMax.store (-100.0f);
    meters.integrated.store (-100.0f);
}

// =====================================================================
//  State, presets, A/B
// =====================================================================
void VegaMasteringProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void VegaMasteringProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPresetName = tree.getProperty ("presetName", "Init").toString();
            uiScale.store ((float) (double) tree.getProperty ("uiScale", 1.0));
            apvts.replaceState (tree);
        }
}

namespace
{
struct FactoryPreset { const char* name; std::vector<std::pair<juce::String, float>> values; };

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
        { "Soulful House Master", {
            { "mLowFreq", 80 }, { "mLowGain", 1.0f }, { "mMidFreq", 320 }, { "mMidGain", -1.0f }, { "mMidQ", 0.8f },
            { "mHighFreq", 12000 }, { "mHighGain", 1.0f }, { "sLowFreq", 150 }, { "sLowGain", -2.0f },
            { "sHighFreq", 10000 }, { "sHighGain", 2.0f }, { ID::width, 112 }, { ID::monoBass, 120 },
            { ID::compOn, 1 }, { ID::compThresh, -18 }, { ID::compRatio, 2 }, { ID::compAttack, 30 }, { ID::compRelease, 200 },
            { ID::compMakeup, 1.5f }, { ID::satMode, 1 }, { ID::satDrive, 2 }, { ID::satMix, 60 },
            { ID::clipOn, 1 }, { ID::clipThresh, -1.5f }, { ID::limGain, 4 }, { ID::limCeiling, -1 }, { ID::limRelease, 120 } } },
        { "Streaming -14 LUFS", {
            { ID::compOn, 1 }, { ID::compThresh, -20 }, { ID::compRatio, 1.5f }, { ID::compAttack, 30 }, { ID::compRelease, 300 },
            { ID::compMakeup, 1 }, { ID::limGain, 3 }, { ID::limCeiling, -1 }, { ID::limRelease, 150 }, { ID::target, 0 } } },
        { "Club Loud", {
            { ID::compOn, 1 }, { ID::compThresh, -16 }, { ID::compRatio, 3 }, { ID::compAttack, 10 }, { ID::compRelease, 100 },
            { ID::compMakeup, 2 }, { ID::satMode, 2 }, { ID::satDrive, 4 }, { ID::satMix, 50 }, { ID::clipOn, 1 }, { ID::clipThresh, -2 },
            { ID::limGain, 7 }, { ID::limCeiling, -0.3f }, { ID::limRelease, 50 }, { ID::monoBass, 150 }, { ID::target, 4 },
            { ID::oversample, 3 } } },
        { "Gentle Glue", {
            { ID::compOn, 1 }, { ID::compThresh, -14 }, { ID::compRatio, 1.5f }, { ID::compAttack, 30 }, { ID::compRelease, 400 },
            { ID::compMakeup, 1 }, { ID::compMix, 70 }, { ID::limGain, 1 } } },
        { "Wide & Airy", {
            { "mHighFreq", 14000 }, { "mHighGain", 1.5f }, { "sHighFreq", 10000 }, { "sHighGain", 3 },
            { ID::width, 130 }, { ID::monoBass, 100 }, { ID::limGain, 2 } } },
        { "Vinyl Warmth", {
            { "mLowFreq", 100 }, { "mLowGain", 1.5f }, { "mHighFreq", 14000 }, { "mHighGain", -1 },
            { ID::satMode, 2 }, { ID::satDrive, 5 }, { ID::satMix, 70 }, { ID::compOn, 1 }, { ID::compThresh, -16 },
            { ID::compRatio, 2 }, { ID::compAttack, 20 }, { ID::compRelease, 250 }, { ID::width, 95 }, { ID::monoBass, 200 },
            { ID::limGain, 2 }, { ID::limCeiling, -1 } } },
        { "Deep House Low End", {
            { "mLowFreq", 60 }, { "mLowGain", 2 }, { "mMidFreq", 250 }, { "mMidGain", -1.5f }, { "mMidQ", 1.0f },
            { "sLowFreq", 150 }, { "sLowGain", -4 }, { ID::monoBass, 180 }, { ID::compOn, 1 }, { ID::compMode, 1 },
            { ID::compThresh, -18 }, { ID::compRatio, 2 }, { ID::compAttack, 25 }, { ID::compRelease, 180 },
            { ID::compScHpf, 60 }, { ID::compMakeup, 1.5f }, { ID::satMode, 1 }, { ID::satDrive, 3 }, { ID::satMix, 50 },
            { ID::limGain, 4 }, { ID::limCeiling, -1 } } },
        { "Linear Phase Polish", {
            { ID::eqMode, 1 }, { "mMidFreq", 3500 }, { "mMidGain", -0.8f }, { "mMidQ", 1.2f }, { "mHighFreq", 16000 },
            { "mHighGain", 1.2f }, { "sHighFreq", 12000 }, { "sHighGain", 1.5f }, { ID::limGain, 3 }, { ID::limCeiling, -1 } } },
    };
    return presets;
}
}

int VegaMasteringProcessor::getNumFactoryPresets() { return (int) factoryPresets().size(); }
juce::String VegaMasteringProcessor::getFactoryPresetName (int i)
{
    return juce::isPositiveAndBelow (i, getNumFactoryPresets()) ? factoryPresets()[(size_t) i].name : "";
}

void VegaMasteringProcessor::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumFactoryPresets())) return;
    const auto& preset = factoryPresets()[(size_t) index];

    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (ranged->getParameterID() != ID::bypass && ranged->getParameterID() != ID::oversample
                && ranged->getParameterID() != ID::target)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (auto& [id, value] : preset.values)
        if (auto* ranged = apvts.getParameter (id))
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));

    currentPresetName = preset.name;
    currentPresetIndex = index;
}

juce::File VegaMasteringProcessor::getUserPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Vega Productions").getChildFile ("Mastering Presets");
    dir.createDirectory();
    return dir;
}

bool VegaMasteringProcessor::saveUserPreset (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    if (auto xml = state.createXml())
        if (xml->writeTo (file))
        {
            currentPresetName = file.getFileNameWithoutExtension();
            currentPresetIndex = -1;
            return true;
        }
    return false;
}

bool VegaMasteringProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                auto child = tree.getChild (i);
                const auto id = child.getProperty ("id").toString();
                if (id == ID::bypass) continue;
                if (auto* ranged = apvts.getParameter (id))
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) (double) child.getProperty ("value")));
            }
            currentPresetName = file.getFileNameWithoutExtension();
            currentPresetIndex = -1;
            return true;
        }
    return false;
}

void VegaMasteringProcessor::selectABSlot (int slot)
{
    if (slot == abSlot) return;
    abStates[abSlot] = apvts.copyState();
    abSlot = slot;
    if (abStates[slot].isValid())
    {
        auto tree = abStates[slot].createCopy();
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild (i);
            const auto id = child.getProperty ("id").toString();
            if (id == ID::bypass) continue;
            if (auto* ranged = apvts.getParameter (id))
                ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) (double) child.getProperty ("value")));
        }
    }
}

void VegaMasteringProcessor::copyToOtherSlot()
{
    abStates[1 - abSlot] = apvts.copyState();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaMasteringProcessor(); }
