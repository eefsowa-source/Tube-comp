#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TubeCompLookAndFeel.h"
#include "UI/VUMeterComponent.h"

class TubeCompAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TubeCompAudioProcessorEditor (TubeCompAudioProcessor&);
    ~TubeCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TubeCompAudioProcessor& processorRef;
    TubeCompLookAndFeel lookAndFeel;

    VUMeterComponent leftMeter { "L" }, rightMeter { "R" };
    VUMeterComponent gainReductionMeter { "GR" };
    juce::ToggleButton bypassButton { "BYPASS" };

    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, kneeSlider;
    juce::Slider lookAheadSlider, sidechainHPFSlider, biasDriveSlider;
    juce::Slider inputGainSlider, outputGainSlider, driveSlider, harmonicsSlider, brightnessSlider, mixSlider;
    juce::ComboBox oversampleBox, circuitModelBox;
    juce::Label thresholdLabel, ratioLabel, attackLabel, releaseLabel, kneeLabel;
    juce::Label lookAheadLabel, sidechainHPFLabel, biasDriveLabel;
    juce::Label inputGainLabel, outputGainLabel, driveLabel, harmonicsLabel, brightnessLabel, mixLabel,
        oversampleLabel, circuitModelLabel;
    juce::Label compressorSectionLabel, tubeSectionLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
        releaseAttachment, kneeAttachment;
    std::unique_ptr<SliderAttachment> lookAheadAttachment, sidechainHPFAttachment, biasDriveAttachment;
    std::unique_ptr<SliderAttachment> inputGainAttachment, outputGainAttachment, driveAttachment,
        harmonicsAttachment, brightnessAttachment, mixAttachment;
    std::unique_ptr<ComboBoxAttachment> oversampleAttachment, circuitModelAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeCompAudioProcessorEditor)
};
