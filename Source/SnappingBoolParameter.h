#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

/** A boolean parameter that keeps its live value legal.

    juce::AudioParameterBool stores whatever normalised value it is handed, so a
    host (or pluginval's parameter fuzzing) that sends 0.25 leaves the live value
    at 0.25 while the value tree records that unnormalised value. Restoring then
    normalises it through a discrete range, which snaps to 0, so the parameter no
    longer round-trips and pluginval reports "Bypass not restored".

    Snapping on set keeps the live value at exactly 0 or 1, so save and restore
    agree and the host sees a plain Off/On boolean.
*/
class SnappingBoolParameter : public juce::RangedAudioParameter
{
public:
    SnappingBoolParameter (const juce::String& parameterID,
                           const juce::String& parameterName,
                           bool defaultState)
        : RangedAudioParameter ({ parameterID, 1 }, parameterName),
          defaultNormalised (defaultState ? 1.0f : 0.0f),
          value (defaultState ? 1.0f : 0.0f)
    {
    }

    bool get() const noexcept { return value.load (std::memory_order_relaxed) >= 0.5f; }

    float getValue() const override { return value.load (std::memory_order_relaxed); }

    void setValue (float newValue) override
    {
        value.store (newValue >= 0.5f ? 1.0f : 0.0f, std::memory_order_relaxed);
    }

    float getDefaultValue() const override { return defaultNormalised; }

    juce::String getText (float, int) const override { return get() ? "On" : "Off"; }

    float getValueForText (const juce::String& text) const override
    {
        const auto trimmed = text.trim();
        return (trimmed.equalsIgnoreCase ("on") || trimmed == "1") ? 1.0f : 0.0f;
    }

    int getNumSteps() const override { return 2; }
    bool isDiscrete() const override { return true; }
    bool isBoolean() const override { return true; }

    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

private:
    juce::NormalisableRange<float> range { 0.0f, 1.0f, 1.0f };
    float defaultNormalised = 0.0f;
    std::atomic<float> value { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnappingBoolParameter)
};
