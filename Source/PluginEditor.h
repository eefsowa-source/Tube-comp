#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include "PluginProcessor.h"
#include "UI/TubeCompLookAndFeel.h"
#include "UI/VUMeterComponent.h"

#if defined(MELATONIN_INSPECTOR_ENABLED)
#include <melatonin_inspector/melatonin_inspector.h>
#endif

class TubeCompAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::AsyncUpdater,
                                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit TubeCompAudioProcessorEditor (TubeCompAudioProcessor&);
    ~TubeCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String&, float) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void commitGestureTransaction();
    void applyRestoredState (const juce::ValueTree& state);
    void updateTimeConstantDimming();
    void updateLookAheadDimming();
    void updateMeterMode();
    void updateReadout (juce::Component* source);
    int getDisplayedProgram() const;

    TubeCompAudioProcessor& processorRef;
    TubeCompLookAndFeel lookAndFeel;

    /** Rotary slider with shift-key fine drag. JUCE has no per-knob fine
        mode for rotary drags, so holding shift rescales the drag distance
        around the gesture start point. */
    class FineSlider : public juce::Slider
    {
    public:
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isShiftDown() && gestureActive)
            {
                const auto scaled = dragStart + (e.position - dragStart) * fineFactor;
                juce::Slider::mouseDrag (e.withNewPosition (scaled));
                return;
            }
            juce::Slider::mouseDrag (e);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            gestureActive = true;
            dragStart = e.position;
            juce::Slider::mouseDown (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            gestureActive = false;
            juce::Slider::mouseUp (e);
        }
    private:
        static constexpr float fineFactor = 0.15f;
        juce::Point<float> dragStart;
        bool gestureActive = false;
    };

    VUMeterComponent leftMeter { "L" }, rightMeter { "R" };
    VUMeterComponent gainReductionMeter { "GR" };
    juce::ToggleButton bypassButton { "BYPASS" };
    juce::ComboBox meterModeBox;
    juce::Label meterModeLabel;

    FineSlider thresholdSlider, ratioSlider, attackSlider, releaseSlider, kneeSlider;
    FineSlider lookAheadSlider, sidechainHPFSlider, biasDriveSlider;
    FineSlider inputGainSlider, outputGainSlider, driveSlider, harmonicsSlider, brightnessSlider, mixSlider;
    juce::ComboBox oversampleBox, circuitModelBox, topologyBox, timeConstantBox, linkModeBox,
        transformerBox;
    juce::Label thresholdLabel, ratioLabel, attackLabel, releaseLabel, kneeLabel;
    juce::Label lookAheadLabel, sidechainHPFLabel, biasDriveLabel;
    juce::Label inputGainLabel, outputGainLabel, driveLabel, harmonicsLabel, brightnessLabel, mixLabel,
        oversampleLabel, circuitModelLabel, topologyLabel, timeConstantLabel, linkModeLabel,
        transformerLabel;
    juce::Label compressorSectionLabel, tubeSectionLabel;

    // Toolbar: preset prev/next + name, undo/redo, A/B compare, UI scale.
    juce::TextButton presetPrevButton { "<" }, presetNextButton { ">" };
    juce::Label presetNameLabel;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::TextButton abButton { "A/B" };
    juce::ComboBox sizeBox;
    juce::Label sizeLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
        releaseAttachment, kneeAttachment;
    std::unique_ptr<SliderAttachment> lookAheadAttachment, sidechainHPFAttachment, biasDriveAttachment;
    std::unique_ptr<SliderAttachment> inputGainAttachment, outputGainAttachment, driveAttachment,
        harmonicsAttachment, brightnessAttachment, mixAttachment;
    std::unique_ptr<ComboBoxAttachment> oversampleAttachment, circuitModelAttachment, topologyAttachment,
        timeConstantAttachment, linkModeAttachment, transformerAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // Undo/redo + A/B compare operate on whole APVTS state snapshots taken at
    // gesture boundaries. Host and programmatic changes only move the
    // committed baseline; user gestures push undo transactions.
    juce::ValueTree gesturePreState, lastCommittedState;
    juce::ValueTree abSnapshotA, abSnapshotB;
    std::deque<juce::ValueTree> undoStack, redoStack;
    int activeAB = 0; // 0 = A, 1 = B
    bool gestureTracked = false;
    bool programmaticChange = false;
    static constexpr int maxUndoDepth = 64;

    // "Hover readout" line under the product title: shows the control under
    // the pointer as "Name: value" without cluttering the faceplate.
    juce::String readoutText;
    std::vector<std::pair<juce::Slider*, juce::String>> readoutMap;
    std::vector<std::pair<juce::ComboBox*, juce::String>> readoutComboMap;

#if defined(MELATONIN_INSPECTOR_ENABLED)
    melatonin::Inspector inspector { *this, false };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeCompAudioProcessorEditor)
};
