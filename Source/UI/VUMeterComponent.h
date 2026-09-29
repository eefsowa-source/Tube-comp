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
    enum class DisplayMode
    {
        level,
        gainReduction
    };

    /** Reference alignment used by the level scale: 0 VU equals -18 dBFS. */
    static constexpr float zeroVuDbFs = -18.0f;

    explicit VUMeterComponent (juce::String meterLabel);
    ~VUMeterComponent() override;

    /** Supplies the level in dBFS to display; polled at ~30Hz. */
    void setLevelProvider (std::function<float()> provider) { levelProvider = std::move (provider); }
    void setMeterLabel (juce::String newLabel) { label = std::move (newLabel); repaint(); }
    void setDisplayMode (DisplayMode newMode) noexcept;

    /** Pure scale conversions exposed for regression tests and consistent UI calibration. */
    static float levelDbFsToVuNormalised (float levelDbFs) noexcept;
    static float gainReductionDbToNormalised (float gainReductionDb) noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String label;
    std::function<float()> levelProvider;

    float displayedValue01 = 0.0f; // smoothed, normalised 0..1
    DisplayMode displayMode = DisplayMode::level;
};
