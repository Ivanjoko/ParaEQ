#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const int   defType[EqMath::numBands] = { EqMath::LowShelf, EqMath::Bell, EqMath::Bell, EqMath::Bell, EqMath::Bell, EqMath::HighShelf };
    const float defFreq[EqMath::numBands] = { 60.f, 200.f, 800.f, 2500.f, 6000.f, 12000.f };
}

ParaEQProcessor::ParaEQProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& s : scope) s.store (0.0f);

    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    pIn = raw ("inGain");     pOut = raw ("outGain");     pBypass = raw ("bypass");
    pHpfOn = raw ("hpfOn");   pHpfFreq = raw ("hpfFreq"); pHpfSlope = raw ("hpfSlope");
    pLpfOn = raw ("lpfOn");   pLpfFreq = raw ("lpfFreq"); pLpfSlope = raw ("lpfSlope");

    for (int b = 0; b < EqMath::numBands; ++b)
        pBand[(size_t) b] = { raw (bandId (b, "on")), raw (bandId (b, "type")), raw (bandId (b, "freq")),
                              raw (bandId (b, "gain")), raw (bandId (b, "q")) };
}

juce::AudioProcessorValueTreeState::ParameterLayout ParaEQProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using R = juce::NormalisableRange<float>;
    using Attr = juce::AudioParameterFloatAttributes;

    auto hz = [] (float v, int)
    {
        if (v >= 1000.f) return juce::String (v / 1000.f, 3) + " kHz";
        if (v >= 100.f)  return juce::String (v, 1) + " Hz";
        return juce::String (v, 2) + " Hz";
    };
    auto hzParse = [] (const juce::String& t)
    {
        auto x = t.trim().toLowerCase();
        float v = x.getFloatValue();
        if (x.contains ("k")) v *= 1000.f;
        return v;
    };
    auto db      = [] (float v, int) { return (v > 0.f ? "+" : "") + juce::String (v, 2) + " dB"; };
    auto dbParse = [] (const juce::String& t) { return t.getFloatValue(); };
    auto qTxt    = [] (float v, int) { return juce::String (v, 2); };
    auto qParse  = [] (const juce::String& t) { return t.getFloatValue(); };

    auto freqRange = [] (float lo, float hi, float centre)
    {
        R r (lo, hi, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };
    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    const juce::StringArray slopes { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    const juce::StringArray types  { "Campana", "Shelf graves", "Shelf agudos", "Notch" };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<F> (id ("inGain"),  "Entrada", R (-24.f, 24.f, 0.01f), 0.f,
                                     Attr().withStringFromValueFunction (db).withValueFromStringFunction (dbParse)));
    layout.add (std::make_unique<F> (id ("outGain"), "Salida",  R (-24.f, 24.f, 0.01f), 0.f,
                                     Attr().withStringFromValueFunction (db).withValueFromStringFunction (dbParse)));
    layout.add (std::make_unique<B> (id ("bypass"), "Bypass", false));

    layout.add (std::make_unique<B> (id ("hpfOn"), "HPF On", false));
    layout.add (std::make_unique<F> (id ("hpfFreq"), "HPF Freq", freqRange (20.f, 1000.f, 120.f), 30.f,
                                     Attr().withStringFromValueFunction (hz).withValueFromStringFunction (hzParse)));
    layout.add (std::make_unique<C> (id ("hpfSlope"), "HPF Slope", slopes, 1));

    layout.add (std::make_unique<B> (id ("lpfOn"), "LPF On", false));
    layout.add (std::make_unique<F> (id ("lpfFreq"), "LPF Freq", freqRange (1000.f, 20000.f, 5000.f), 18000.f,
                                     Attr().withStringFromValueFunction (hz).withValueFromStringFunction (hzParse)));
    layout.add (std::make_unique<C> (id ("lpfSlope"), "LPF Slope", slopes, 1));

    for (int b = 0; b < EqMath::numBands; ++b)
    {
        const bool shelf = (defType[b] != EqMath::Bell);
        const juce::String n = "Banda " + juce::String (b + 1) + " ";
        layout.add (std::make_unique<B> (id (bandId (b, "on")),   n + "On", true));
        layout.add (std::make_unique<C> (id (bandId (b, "type")), n + "Tipo", types, defType[b]));
        layout.add (std::make_unique<F> (id (bandId (b, "freq")), n + "Freq", freqRange (20.f, 20000.f, 1000.f), defFreq[b],
                                         Attr().withStringFromValueFunction (hz).withValueFromStringFunction (hzParse)));
        layout.add (std::make_unique<F> (id (bandId (b, "gain")), n + "Ganancia", R (-24.f, 24.f, 0.01f), 0.f,
                                         Attr().withStringFromValueFunction (db).withValueFromStringFunction (dbParse)));
        R qr (0.1f, 18.f, 0.01f);
        qr.setSkewForCentre (1.f);
        layout.add (std::make_unique<F> (id (bandId (b, "q")), n + "Q", qr, shelf ? 0.71f : 1.0f,
                                         Attr().withStringFromValueFunction (qTxt).withValueFromStringFunction (qParse)));
    }
    return layout;
}

void ParaEQProcessor::prepareToPlay (double sampleRate, int)
{
    currentSr.store (sampleRate);
    engine.prepare (sampleRate);
}

bool ParaEQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

EqParams ParaEQProcessor::readParams() const
{
    EqParams p;
    p.inDb   = pIn->load();
    p.outDb  = pOut->load();
    p.bypass = pBypass->load() > 0.5f;

    p.hpf.on = pHpfOn->load() > 0.5f;  p.hpf.freq = pHpfFreq->load();  p.hpf.slope = (int) std::lround (pHpfSlope->load());
    p.lpf.on = pLpfOn->load() > 0.5f;  p.lpf.freq = pLpfFreq->load();  p.lpf.slope = (int) std::lround (pLpfSlope->load());

    for (int b = 0; b < EqMath::numBands; ++b)
    {
        const auto& s = pBand[(size_t) b];
        auto& d = p.band[(size_t) b];
        d.on = s.on->load() > 0.5f;
        d.type = (int) std::lround (s.type->load());
        d.freq = s.freq->load();
        d.gainDb = s.gain->load();
        d.q = s.q->load();
    }
    return p;
}

template <typename T>
void ParaEQProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0)
        return;

    T* l = buffer.getWritePointer (0);
    T* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;   // mono: mismo canal

    engine.setParams (readParams());
    engine.process (l, r, n);

    inPeak.store ((float) engine.getPeakIn());
    outPeak.store ((float) engine.getPeakOut());

    const int pos = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        scope[(size_t) ((pos + i) & (scopeSize - 1))].store (0.5f * (float) (l[i] + r[i]), std::memory_order_relaxed);
    scopePos.store ((pos + n) & (scopeSize - 1), std::memory_order_release);
}

void ParaEQProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { process (b); }
void ParaEQProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

juce::AudioProcessorEditor* ParaEQProcessor::createEditor()
{
    return new ParaEQEditor (*this);
}

void ParaEQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ParaEQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaEQProcessor();
}
