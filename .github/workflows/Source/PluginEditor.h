#pragma once
#include "PluginProcessor.h"

//==============================================================================
// Gráfica: arrastra los puntos (Shift = ajuste fino x10), rueda = Q,
// doble clic = ganancia a 0. Muestra el analizador de espectro de la salida.
class EqGraph : public juce::Component, private juce::Timer
{
public:
    EqGraph (ParaEQProcessor&, std::function<void (int)> onSelectBand);

    void setSelected (int b)   { selected = b; repaint(); }
    void setRange (int db)     { rangeDb = (float) db; repaint(); }
    void setSpectrum (bool on) { showSpectrum = on; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour bandColour (int b);

private:
    void timerCallback() override;
    void updateSpectrum();

    juce::Rectangle<float> plot() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    float getParam (const juce::String& id) const;
    void  setParam (juce::RangedAudioParameter*, float realValue);
    juce::Point<float> nodePos (int band) const;
    int hitTest (juce::Point<float>) const;

    ParaEQProcessor& proc;
    std::function<void (int)> onSelect;
    int selected = 0, dragBand = -1;
    float rangeDb = 18.f;
    bool showSpectrum = true, lastFine = false;
    juce::RangedAudioParameter *dragFreq = nullptr, *dragGain = nullptr;
    juce::Point<float> anchorMouse, anchorNode;

    static constexpr int fftSize = ParaEQProcessor::fftSize;
    juce::dsp::FFT fft { ParaEQProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize / 2> spec {};
};

//==============================================================================
class ParaEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ParaEQEditor (ParaEQProcessor&);
    ~ParaEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectBand (int b);
    void updateGainEnabled();
    void knob (juce::Slider&);
    void label (juce::Label&, const juce::String&);

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::LookAndFeel_V4 laf;
    ParaEQProcessor& proc;
    EqGraph graph;

    juce::Label title, lblIn, lblOut, lblType, lblFreq, lblGain, lblQ;
    juce::Slider inSl, outSl, hpfFreq, lpfFreq, bFreq, bGain, bQ;
    juce::TextButton bypassBtn, analyzerBtn, hpfOn, lpfOn, bandOn;
    std::array<juce::TextButton, EqMath::numBands> bandBtn;
    juce::ComboBox hpfSlope, lpfSlope, bType, rangeBox;

    std::unique_ptr<SA> aIn, aOut, aHpfFreq, aLpfFreq, aBFreq, aBGain, aBQ;
    std::unique_ptr<BA> aBypass, aHpfOn, aLpfOn, aBandOn;
    std::unique_ptr<CA> aHpfSlope, aLpfSlope, aBType;

    int selected = 0;
    float inShown = 0.f, outShown = 0.f;
    juce::Rectangle<int> inMeter, outMeter, filtersPanel, bandPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQEditor)
};
