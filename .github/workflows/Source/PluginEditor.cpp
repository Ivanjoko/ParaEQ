#include "PluginEditor.h"

using EqMath::numBands;

//==============================================================================
//  EqGraph
//==============================================================================
EqGraph::EqGraph (ParaEQProcessor& p, std::function<void (int)> cb)
    : proc (p), onSelect (std::move (cb))
{
    spec.fill (-120.f);
    startTimerHz (30);
}

juce::Colour EqGraph::bandColour (int b)
{
    static const juce::Colour c[numBands] = {
        juce::Colour (0xffef5350), juce::Colour (0xffffa726), juce::Colour (0xffffee58),
        juce::Colour (0xff66bb6a), juce::Colour (0xff4fc3f7), juce::Colour (0xffba68c8) };
    return c[juce::jlimit (0, numBands - 1, b)];
}

void EqGraph::timerCallback()
{
    if (showSpectrum)
        updateSpectrum();
    repaint();
}

void EqGraph::updateSpectrum()
{
    const int mask = ParaEQProcessor::scopeSize - 1;
    const int end = proc.scopePos.load (std::memory_order_acquire);

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = proc.scope[(size_t) ((end - fftSize + i) & mask)].load (std::memory_order_relaxed);
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int k = 0; k < fftSize / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] / ((float) fftSize * 0.25f), -120.f);
        auto& s = spec[(size_t) k];
        s = db > s ? db : s * 0.85f + db * 0.15f;     // subida rápida, caída suave
    }
}

juce::Rectangle<float> EqGraph::plot() const
{
    return getLocalBounds().toFloat().reduced (10.f, 10.f).withTrimmedLeft (28.f).withTrimmedBottom (14.f);
}

float EqGraph::freqToX (float f) const
{
    auto r = plot();
    return r.getX() + r.getWidth() * std::log (f / 20.f) / std::log (1000.f);
}

float EqGraph::xToFreq (float x) const
{
    auto r = plot();
    const float t = juce::jlimit (0.f, 1.f, (x - r.getX()) / r.getWidth());
    return 20.f * std::pow (1000.f, t);
}

float EqGraph::gainToY (float g) const
{
    auto r = plot();
    return r.getBottom() - (g + rangeDb) / (2.f * rangeDb) * r.getHeight();
}

float EqGraph::yToGain (float y) const
{
    auto r = plot();
    return juce::jlimit (-24.f, 24.f, -rangeDb + (r.getBottom() - y) / r.getHeight() * 2.f * rangeDb);
}

float EqGraph::getParam (const juce::String& id) const
{
    return proc.apvts.getRawParameterValue (id)->load();
}

void EqGraph::setParam (juce::RangedAudioParameter* p, float v)
{
    if (p != nullptr)
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

juce::Point<float> EqGraph::nodePos (int b) const
{
    const int type = (int) std::lround (getParam (bandId (b, "type")));
    const float g = (type == EqMath::Notch) ? 0.f : getParam (bandId (b, "gain"));
    auto pl = plot();
    return { freqToX (getParam (bandId (b, "freq"))),
             juce::jlimit (pl.getY(), pl.getBottom(), gainToY (g)) };
}

int EqGraph::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 14.f;
    for (int b = 0; b < numBands; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff12151a));
    g.fillRoundedRectangle (area, 8.f);

    auto pl = plot();
    const double sr = proc.currentSr.load();

    // ---- Rejilla
    g.setFont (juce::FontOptions (11.f));
    for (int k = -3; k <= 3; ++k)
    {
        const int d = (int) std::lround (k * rangeDb / 3.f);
        const float y = gainToY ((float) d);
        g.setColour (d == 0 ? juce::Colour (0xff4a5360) : juce::Colour (0xff262c35));
        g.drawHorizontalLine ((int) y, pl.getX(), pl.getRight());
        g.setColour (juce::Colours::grey);
        g.drawText ((d > 0 ? "+" : "") + juce::String (d), 0, (int) y - 7, 32, 14, juce::Justification::centredRight);
    }

    const float fl[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char* ft[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const float x = freqToX (fl[i]);
        g.setColour (juce::Colour (0xff262c35));
        g.drawVerticalLine ((int) x, pl.getY(), pl.getBottom());
        g.setColour (juce::Colours::grey);
        g.drawText (ft[i], (int) x - 16, (int) pl.getBottom() + 1, 32, 14, juce::Justification::centred);
    }

    g.saveState();
    g.reduceClipRegion (pl.toNearestInt());

    // ---- Analizador de espectro (salida)
    if (showSpectrum)
    {
        const float binHz = (float) (sr / fftSize);
        juce::Path sp;
        bool started = false;

        for (float x = pl.getX(); x <= pl.getRight(); x += 2.f)
        {
            const float bLo = xToFreq (x - 1.f) / binHz, bHi = xToFreq (x + 1.f) / binHz;
            float db;
            if (bHi - bLo < 1.f)
            {
                const float bc = 0.5f * (bLo + bHi);
                const int k0 = juce::jlimit (0, fftSize / 2 - 2, (int) bc);
                const float fr = bc - (float) k0;
                db = spec[(size_t) k0] * (1.f - fr) + spec[(size_t) k0 + 1] * fr;
            }
            else
            {
                const int k0 = juce::jmax (1, (int) bLo), k1 = juce::jmin (fftSize / 2 - 1, (int) bHi);
                db = -120.f;
                for (int k = k0; k <= k1; ++k) db = juce::jmax (db, spec[(size_t) k]);
            }
            const float y = juce::jmap (juce::jlimit (-90.f, 0.f, db), -90.f, 0.f, pl.getBottom(), pl.getY());
            if (! started) { sp.startNewSubPath (x, y); started = true; }
            else           sp.lineTo (x, y);
        }
        sp.lineTo (pl.getRight(), pl.getBottom());
        sp.lineTo (pl.getX(), pl.getBottom());
        sp.closeSubPath();
        g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.18f));
        g.fillPath (sp);
    }

    // ---- Coeficientes actuales (los mismos que usa el motor de audio)
    const bool bypassed = getParam ("bypass") > 0.5f;
    EqMath::Coef hp[EqMath::maxStages], lp[EqMath::maxStages];
    int nh = 0, nl = 0;
    if (getParam ("hpfOn") > 0.5f) nh = EqMath::designCut (true,  sr, getParam ("hpfFreq"), (int) std::lround (getParam ("hpfSlope")), hp);
    if (getParam ("lpfOn") > 0.5f) nl = EqMath::designCut (false, sr, getParam ("lpfFreq"), (int) std::lround (getParam ("lpfSlope")), lp);

    EqMath::Coef bc[numBands];
    bool act[numBands] {};
    for (int b = 0; b < numBands; ++b)
    {
        act[b] = getParam (bandId (b, "on")) > 0.5f;
        if (act[b])
            bc[b] = EqMath::designBand ((int) std::lround (getParam (bandId (b, "type"))), sr, getParam (bandId (b, "freq")),
                                        getParam (bandId (b, "gain")), getParam (bandId (b, "q")));
    }

    auto dbToY = [this] (double mag)
    {
        return gainToY (juce::jlimit (-rangeDb, rangeDb, juce::Decibels::gainToDecibels ((float) mag)));
    };

    // ---- Relleno de la banda seleccionada
    if (act[selected] && ! bypassed)
    {
        juce::Path p;
        const float y0 = gainToY (0.f);
        p.startNewSubPath (pl.getX(), y0);
        for (float x = pl.getX(); x <= pl.getRight(); x += 2.f)
            p.lineTo (x, dbToY (EqMath::magnitude (bc[selected], xToFreq (x), sr)));
        p.lineTo (pl.getRight(), y0);
        p.closeSubPath();
        g.setColour (bandColour (selected).withAlpha (0.18f));
        g.fillPath (p);
    }

    // ---- Curva total
    juce::Path curve;
    for (float x = pl.getX(); x <= pl.getRight(); x += 2.f)
    {
        const double f = xToFreq (x);
        double mag = 1.0;
        for (int i = 0; i < nh; ++i) mag *= EqMath::magnitude (hp[i], f, sr);
        for (int i = 0; i < nl; ++i) mag *= EqMath::magnitude (lp[i], f, sr);
        for (int b = 0; b < numBands; ++b) if (act[b]) mag *= EqMath::magnitude (bc[b], f, sr);

        const float y = dbToY (mag);
        if (x == pl.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (bypassed ? juce::Colours::grey : juce::Colours::white);
    g.strokePath (curve, juce::PathStrokeType (2.f));

    g.restoreState();

    // ---- Puntos de las bandas
    for (int b = 0; b < numBands; ++b)
    {
        auto c = nodePos (b);
        auto r = juce::Rectangle<float> (18.f, 18.f).withCentre (c);

        if (act[b])
        {
            g.setColour (bandColour (b));
            g.fillEllipse (r);
        }
        else
        {
            g.setColour (juce::Colour (0xff12151a));
            g.fillEllipse (r);
            g.setColour (bandColour (b).withAlpha (0.6f));
            g.drawEllipse (r, 2.f);
        }

        g.setColour (act[b] ? juce::Colours::black : bandColour (b));
        g.setFont (juce::FontOptions (12.f, juce::Font::bold));
        g.drawText (juce::String (b + 1), r.toNearestInt(), juce::Justification::centred);

        if (b == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (r.expanded (4.f), 2.f);
        }
    }

    // ---- Lectura de la banda seleccionada
    auto txt = [this] (const juce::String& id) { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
    g.setColour (bandColour (selected));
    g.setFont (juce::FontOptions (13.f, juce::Font::bold));
    g.drawText ("Banda " + juce::String (selected + 1) + "   " + txt (bandId (selected, "freq"))
                + "   " + txt (bandId (selected, "gain")) + "   Q " + txt (bandId (selected, "q")),
                (int) pl.getX() + 6, (int) pl.getY() + 4, 380, 18, juce::Justification::centredLeft);
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitTest (e.position);
    if (dragBand < 0)
        return;

    selected = dragBand;
    onSelect (dragBand);

    dragFreq = proc.apvts.getParameter (bandId (dragBand, "freq"));
    dragGain = proc.apvts.getParameter (bandId (dragBand, "gain"));
    dragFreq->beginChangeGesture();
    dragGain->beginChangeGesture();

    anchorMouse = e.position;
    anchorNode  = nodePos (dragBand);
    lastFine    = e.mods.isShiftDown();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    // Si la banda estaba apagada, al arrastrarla se enciende
    if (auto* on = proc.apvts.getParameter (bandId (dragBand, "on")))
        if (on->getValue() < 0.5f)
            on->setValueNotifyingHost (1.f);

    // Shift = ajuste fino (10 veces más lento)
    const bool fine = e.mods.isShiftDown();
    if (fine != lastFine)
    {
        anchorMouse = e.position;
        anchorNode  = nodePos (dragBand);
        lastFine    = fine;
    }
    const auto target = anchorNode + (e.position - anchorMouse) * (fine ? 0.1f : 1.0f);

    setParam (dragFreq, xToFreq (target.x));
    if ((int) std::lround (getParam (bandId (dragBand, "type"))) != EqMath::Notch)
        setParam (dragGain, yToGain (target.y));
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0)
        return;

    dragFreq->endChangeGesture();
    dragGain->endChangeGesture();
    dragBand = -1;
    dragFreq = dragGain = nullptr;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitTest (e.position);
    if (b < 0)
        return;

    if (auto* p = proc.apvts.getParameter (bandId (b, "gain")))
    {
        p->beginChangeGesture();
        setParam (p, 0.f);
        p->endChangeGesture();
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitTest (e.position);
    if (b < 0) b = selected;

    if (auto* p = proc.apvts.getParameter (bandId (b, "q")))
    {
        const float q = getParam (bandId (b, "q"));
        p->beginChangeGesture();
        setParam (p, q * std::exp (w.deltaY * (e.mods.isShiftDown() ? 0.15f : 1.5f)));
        p->endChangeGesture();
    }
}

//==============================================================================
//  ParaEQEditor
//==============================================================================
ParaEQEditor::ParaEQEditor (ParaEQProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      graph (p, [this] (int b) { selectBand (b); })
{
    laf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setLookAndFeel (&laf);

    addAndMakeVisible (graph);

    title.setText ("ParaEQ", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    label (lblIn, "ENTRADA");    label (lblOut, "SALIDA");
    label (lblType, "TIPO");     label (lblFreq, "FRECUENCIA");
    label (lblGain, "GANANCIA"); label (lblQ, "Q (ANCHO)");

    for (auto* s : { &inSl, &outSl })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
        s->setColour (juce::Slider::trackColourId, juce::Colour (0xff4fc3f7));
        addAndMakeVisible (*s);
    }

    bypassBtn.setButtonText ("BYPASS");
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffef5350));
    addAndMakeVisible (bypassBtn);

    analyzerBtn.setButtonText ("ANALIZADOR");
    analyzerBtn.setClickingTogglesState (true);
    analyzerBtn.setToggleState (true, juce::dontSendNotification);
    analyzerBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fc3f7));
    analyzerBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    analyzerBtn.onClick = [this] { graph.setSpectrum (analyzerBtn.getToggleState()); };
    addAndMakeVisible (analyzerBtn);

    rangeBox.addItemList ({ "Rango ±6 dB", "Rango ±12 dB", "Rango ±18 dB", "Rango ±24 dB" }, 1);
    rangeBox.setSelectedId (3, juce::dontSendNotification);
    rangeBox.onChange = [this] { graph.setRange (rangeBox.getSelectedId() * 6); };
    addAndMakeVisible (rangeBox);

    hpfOn.setButtonText ("Corte de graves (HPF)");
    lpfOn.setButtonText ("Corte de agudos (LPF)");
    for (auto* b : { &hpfOn, &lpfOn })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff26a69a));
        addAndMakeVisible (*b);
    }
    knob (hpfFreq);
    knob (lpfFreq);
    for (auto* c : { &hpfSlope, &lpfSlope })
    {
        c->addItemList ({ "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
        addAndMakeVisible (*c);
    }

    for (int i = 0; i < numBands; ++i)
    {
        auto& b = bandBtn[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.setColour (juce::TextButton::buttonOnColourId, EqGraph::bandColour (i));
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (b);
    }
    bandOn.setButtonText ("ACTIVA");
    bandOn.setClickingTogglesState (true);
    bandOn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff66bb6a));
    addAndMakeVisible (bandOn);

    bType.addItemList ({ "Campana", "Shelf graves", "Shelf agudos", "Notch" }, 1);
    bType.onChange = [this] { updateGainEnabled(); };
    addAndMakeVisible (bType);
    knob (bFreq); knob (bGain); knob (bQ);

    for (auto* l : { &lblIn, &lblOut, &lblType, &lblFreq, &lblGain, &lblQ })
        addAndMakeVisible (*l);

    auto& a = proc.apvts;
    aIn = std::make_unique<SA> (a, "inGain", inSl);
    aOut = std::make_unique<SA> (a, "outGain", outSl);
    aBypass = std::make_unique<BA> (a, "bypass", bypassBtn);
    aHpfOn = std::make_unique<BA> (a, "hpfOn", hpfOn);
    aHpfFreq = std::make_unique<SA> (a, "hpfFreq", hpfFreq);
    aHpfSlope = std::make_unique<CA> (a, "hpfSlope", hpfSlope);
    aLpfOn = std::make_unique<BA> (a, "lpfOn", lpfOn);
    aLpfFreq = std::make_unique<SA> (a, "lpfFreq", lpfFreq);
    aLpfSlope = std::make_unique<CA> (a, "lpfSlope", lpfSlope);

    selectBand (0);
    setSize (980, 640);
    startTimerHz (30);
}

ParaEQEditor::~ParaEQEditor()
{
    setLookAndFeel (nullptr);
}

void ParaEQEditor::knob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fc3f7));
    addAndMakeVisible (s);
}

void ParaEQEditor::label (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    l.setFont (juce::FontOptions (11.f, juce::Font::bold));
}

void ParaEQEditor::selectBand (int b)
{
    selected = juce::jlimit (0, numBands - 1, b);
    graph.setSelected (selected);

    for (int i = 0; i < numBands; ++i)
        bandBtn[(size_t) i].setToggleState (i == selected, juce::dontSendNotification);

    aBandOn.reset(); aBType.reset(); aBFreq.reset(); aBGain.reset(); aBQ.reset();

    auto& a = proc.apvts;
    aBandOn = std::make_unique<BA> (a, bandId (selected, "on"), bandOn);
    aBType  = std::make_unique<CA> (a, bandId (selected, "type"), bType);
    aBFreq  = std::make_unique<SA> (a, bandId (selected, "freq"), bFreq);
    aBGain  = std::make_unique<SA> (a, bandId (selected, "gain"), bGain);
    aBQ     = std::make_unique<SA> (a, bandId (selected, "q"), bQ);

    for (auto* s : { &bFreq, &bGain, &bQ })
        s->setColour (juce::Slider::rotarySliderFillColourId, EqGraph::bandColour (selected));

    updateGainEnabled();
    repaint();
}

void ParaEQEditor::updateGainEnabled()
{
    const bool hasGain = (bType.getSelectedId() - 1) != EqMath::Notch;   // el Notch no tiene ganancia
    bGain.setEnabled (hasGain);
    bGain.setAlpha (hasGain ? 1.f : 0.4f);
}

void ParaEQEditor::timerCallback()
{
    inShown  = juce::jmax (proc.inPeak.load(),  inShown  * 0.85f);
    outShown = juce::jmax (proc.outPeak.load(), outShown * 0.85f);
    repaint (inMeter.expanded (2));
    repaint (outMeter.expanded (2));
}

void ParaEQEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto top = r.removeFromTop (46);
    title.setBounds (top.removeFromLeft (120));
    analyzerBtn.setBounds (top.removeFromLeft (100).reduced (0, 9));
    top.removeFromLeft (8);
    rangeBox.setBounds (top.removeFromLeft (110).reduced (0, 9));
    bypassBtn.setBounds (top.removeFromRight (90).reduced (0, 8));
    top.removeFromRight (12);

    auto placeGain = [] (juce::Label& l, juce::Slider& s, juce::Rectangle<int>& meter, juce::Rectangle<int> b)
    {
        l.setBounds (b.removeFromLeft (60));
        meter = b.removeFromBottom (8).withTrimmedRight (80);
        s.setBounds (b);
    };
    placeGain (lblOut, outSl, outMeter, top.removeFromRight (250));
    top.removeFromRight (12);
    placeGain (lblIn, inSl, inMeter, top.removeFromRight (250));

    r.removeFromTop (6);
    graph.setBounds (r.removeFromTop (340));
    r.removeFromTop (10);
    filtersPanel = r.removeFromLeft (330);
    r.removeFromLeft (10);
    bandPanel = r;

    {
        auto fp = filtersPanel.reduced (10, 8);
        fp.removeFromTop (20);
        const int half = fp.getWidth() / 2;

        auto layoutCut = [] (juce::Rectangle<int> c, juce::TextButton& on, juce::Slider& f, juce::ComboBox& sl)
        {
            on.setBounds (c.removeFromTop (26).reduced (4, 0));
            sl.setBounds (c.removeFromBottom (26).reduced (4, 0));
            c.removeFromBottom (4);
            f.setBounds (c);
        };
        layoutCut (fp.removeFromLeft (half), hpfOn, hpfFreq, hpfSlope);
        layoutCut (fp, lpfOn, lpfFreq, lpfSlope);
    }

    {
        auto bp = bandPanel.reduced (10, 8);
        bp.removeFromTop (20);

        auto row = bp.removeFromTop (30);
        bandOn.setBounds (row.removeFromRight (90));
        row.removeFromRight (8);
        for (int i = 0; i < numBands; ++i)
            bandBtn[(size_t) i].setBounds (row.removeFromLeft (44).reduced (2, 0));

        bp.removeFromTop (6);
        auto left = bp.removeFromLeft (170);
        lblType.setBounds (left.removeFromTop (16));
        bType.setBounds (left.removeFromTop (28).reduced (4, 0));

        const int kw = bp.getWidth() / 3;
        auto k1 = bp.removeFromLeft (kw), k2 = bp.removeFromLeft (kw), k3 = bp;
        lblFreq.setBounds (k1.removeFromTop (16)); bFreq.setBounds (k1);
        lblGain.setBounds (k2.removeFromTop (16)); bGain.setBounds (k2);
        lblQ.setBounds    (k3.removeFromTop (16)); bQ.setBounds    (k3);
    }
}

void ParaEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1217));

    for (auto r : { filtersPanel, bandPanel })
    {
        g.setColour (juce::Colour (0xff181c23));
        g.fillRoundedRectangle (r.toFloat(), 8.f);
        g.setColour (juce::Colour (0xff2a313b));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.f, 1.f);
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.f, juce::Font::bold));
    g.drawText ("FILTROS DE CORTE", filtersPanel.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    g.drawText ("BANDA SELECCIONADA  (clic en un punto de la gráfica · Shift = ajuste fino)",
                bandPanel.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);

    auto drawMeter = [&g] (juce::Rectangle<int> r, float peak)
    {
        g.setColour (juce::Colour (0xff1f242b));
        g.fillRoundedRectangle (r.toFloat(), 3.f);

        const float db = juce::Decibels::gainToDecibels (peak, -60.f);
        const float lvl = juce::jmap (juce::jlimit (-60.f, 6.f, db), -60.f, 6.f, 0.f, 1.f);
        g.setColour (db > -0.5f ? juce::Colours::red : (db > -6.f ? juce::Colours::orange : juce::Colour (0xff66bb6a)));
        g.fillRoundedRectangle (r.toFloat().withWidth ((float) r.getWidth() * lvl), 3.f);
    };
    drawMeter (inMeter, inShown);
    drawMeter (outMeter, outShown);
}
