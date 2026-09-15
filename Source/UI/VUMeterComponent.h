#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    Analog-style VU meter: cream dial face, black needle pivoting from the
    bottom, red "hot" zone at the top of the scale -- styled after the
    dual-mono metering pair on Fairchild-class hardware compressors.
*/
class VUMeterComponent : public juce::Component, private juce::Timer
{
public:
    explicit VUMeterComponent (juce::String meterLabel);
    ~VUMeterComponent() override;

    /** Supplies the level in dBFS to display; polled at ~30Hz. */
    void setLevelProvider (std::function<float()> provider) { levelProvider = std::move (provider); }
    void setRange (float minimumDb, float maximumDb) noexcept { minDisplayDb = minimumDb; maxDisplayDb = maximumDb; }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String label;
    std::function<float()> levelProvider;

    float displayedValue01 = 0.0f; // smoothed, normalised 0..1
    float minDisplayDb = -30.0f, maxDisplayDb = 3.0f;
};
