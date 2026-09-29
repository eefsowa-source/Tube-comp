#include "PluginEditor.h"

#include <algorithm>
#include <array>

namespace
{
    // Fairchild 670 reference palette: graphite panel, ivory engraved legends,
    // cream switch caps, and a two-tone channel caption where the secondary
    // channel is picked out in red. The 670's characteristic move is printing
    // the scale on the panel around each knob, so the panel does most of the
    // visual work and the controls stay plain black hardware.
    // This is an original EON surface: the layout grammar is referenced, not
    // the commercial product's faceplate, logo, or artwork.
    const juce::Colour panelTop    { 0xff3a4045 };
    const juce::Colour panelBottom { 0xff1c2023 };
    const juce::Colour panelEdge   { 0xff0e1012 };
    const juce::Colour plateCream  { 0xffe9dfc6 };
    const juce::Colour plateShadow { 0xff111315 };
    const juce::Colour screwColour { 0xffa9a096 };
    const juce::Colour creamText   { 0xfff6ecd2 };
    const juce::Colour dimText     { 0xffd8c9a6 };
    const juce::Colour accentGold  { 0xffe4bd6a };
    const juce::Colour accentRed   { 0xffc8503c };

    constexpr int legendLineHeight = 15;

    struct EditorLayout
    {
        juce::Rectangle<int> toolbar;
        juce::Rectangle<int> face;
        juce::Rectangle<int> upperStrip;
        juce::Rectangle<int> lowerStrip;
        juce::Rectangle<int> utilityStrip;
        juce::Rectangle<int> namePlate;
        juce::Rectangle<int> footer;
    };

    EditorLayout makeEditorLayout (juce::Rectangle<int> bounds)
    {
        // The 670 sits in a shallow chassis: a thin cream name plate along the
        // bottom, and the control field filling everything above it.
        auto frame = bounds.reduced (18, 12);
        EditorLayout layout;
        layout.namePlate = frame.removeFromBottom (38);
        layout.footer = frame.removeFromBottom (18);
        layout.toolbar = frame.removeFromTop (26);
        layout.face = frame;

        // Two stacked channel rows (upper = LEFT, lower = RIGHT) match the
        // 670's dual-mono presentation, with a shared utility strip beneath.
        // Tall enough for a full-size knob cell (legend + gap + hardware
        // knob) plus the dial-shaped gain-reduction meter beside it.
        constexpr int utilityHeight = 104;
        constexpr int utilityGap = 12;
        auto field = layout.face;
        layout.utilityStrip = field.removeFromBottom (utilityHeight);
        field.removeFromBottom (utilityGap);
        constexpr int rowGap = 6;
        const int rowHeight = (field.getHeight() - rowGap) / 2;
        layout.upperStrip = field.removeFromTop (rowHeight);
        field.removeFromTop (rowGap);
        layout.lowerStrip = field;
        return layout;
    }

    void placeKnob (juce::Rectangle<int> bounds, juce::Slider& slider, juce::Label& label)
    {
        auto labelArea = bounds.removeFromBottom (26).reduced (2, 0);
        bounds.removeFromBottom (4); // guaranteed air gap between knob and typography
        auto knobArea = bounds.reduced (5, 2);
        // The reference panel packs its controls on a tight grid with generous
        // knob diameters, so the cap is raised to fill the cell. Legibility is
        // preserved by the printed scale living outside the knob, not by
        // shrinking the knob.
        const int knobSize = juce::jmax (1, juce::jmin (92, juce::jmin (knobArea.getWidth(),
                                                                     knobArea.getHeight())));

        slider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));
        label.setBounds (labelArea);
    }

    void placeKnobRow (juce::Rectangle<int> row,
                       std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls)
    {
        constexpr int gap = 2;
        const int count = static_cast<int> (controls.size());
        const int usableWidth = juce::jmax (0, row.getWidth() - gap * (count - 1));
        const int baseCellWidth = count > 0 ? usableWidth / count : 0;
        const int extraPixels = count > 0 ? usableWidth % count : 0;
        int index = 0;

        for (auto [slider, label] : controls)
        {
            const int cellWidth = baseCellWidth + (index < extraPixels ? 1 : 0);
            auto cell = row.removeFromLeft (cellWidth);
            placeKnob (cell, *slider, *label);

            if (++index < count)
                row.removeFromLeft (gap);
        }
    }

    void setupRotary (juce::Slider& slider, juce::Label& label, const juce::String& text,
                       const juce::String& function, juce::Component& parent, bool big)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::textBoxTextColourId, creamText);
        slider.setTooltip (text);
        slider.setNumDecimalPlacesToDisplay (text == "Ratio" ? 1 : 2);
        parent.addAndMakeVisible (slider);

        // The 670 silkscreens a short all-caps legend under each control and
        // keeps the functional description in the tooltip, so the faceplate
        // stays uncluttered like the hardware.
        label.setText (text.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, creamText);
        label.setFont (juce::FontOptions (big ? 10.0f : 9.0f, juce::Font::bold));
        label.setMinimumHorizontalScale (0.72f);
        juce::ignoreUnused (function);
        parent.addAndMakeVisible (label);
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> centre)
    {
        g.setColour (screwColour.darker (0.45f));
        g.fillEllipse (centre.x - 5.0f, centre.y - 4.5f, 10.0f, 10.0f);
        g.setColour (screwColour);
        g.fillEllipse (centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (centre.x - 2.6f, centre.y, centre.x + 2.6f, centre.y, 1.1f);
    }

    /** Engraved strip caption used to head each dual-mono channel row. */
    void drawStripHeader (juce::Graphics& g, juce::Rectangle<int> header,
                          const juce::String& title, const juce::String& sub)
    {
        auto r = header.toFloat();
        g.setColour (creamText);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (title, r.removeFromLeft (juce::roundToInt (r.getWidth() * 0.66f)),
                    juce::Justification::centredLeft);
        g.setColour (accentRed);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (sub, r, juce::Justification::centredRight);
    }

    /** Places one meter plus a row of knob cells inside a channel strip.
        The 670 leads each row with a large bezelled dial, then walks the
        controls left to right with engraved legends underneath. */
    void placeChannelStrip (juce::Rectangle<int> strip, juce::Component& meter,
                           std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls,
                           juce::Slider* ringDial = nullptr,
                           juce::Label* ringLabel = nullptr)
    {
        auto body = strip.reduced (4, 0);
        body.removeFromTop (legendLineHeight + 2); // strip caption

        constexpr int gap = 4;
        // Meters keep a dial-like aspect ratio instead of stretching with the
        // row, so the engraved scale and needle stay proportional.
        const int meterWidth = juce::jlimit (96, 150,
                                             juce::roundToInt (body.getWidth() * 0.13f));
        const int meterHeight = juce::jmin (body.getHeight(), juce::roundToInt (meterWidth * 1.28f));
        meter.setBounds (body.removeFromLeft (meterWidth)
                             .withSizeKeepingCentre (meterWidth, meterHeight));
        body.removeFromLeft (gap);

        // The ring dial gets a cell of its own so it can be drawn larger than
        // the surrounding knobs, which is what makes it read as the primary
        // control on a 670-class faceplate.
        if (ringDial != nullptr && ringLabel != nullptr)
        {
            const int count = juce::jmax (1, static_cast<int> (controls.size()) + 1);
            const int cellWidth = juce::jmax (1, body.getWidth() / count);
            auto cell = body.removeFromLeft (cellWidth);
            auto labelArea = cell.removeFromBottom (26).reduced (2, 0);
            cell.removeFromBottom (4);
            // The ring dial must stay visibly larger than the surrounding
            // knobs or the hierarchy inverts; the knob cap in placeKnob() is
            // the size it has to beat.
            const int dialSize = juce::jmax (1, juce::jmin (134, juce::jmin (cell.getWidth(),
                                                                           cell.getHeight())));
            ringDial->setBounds (cell.withSizeKeepingCentre (dialSize, dialSize));
            ringLabel->setBounds (labelArea);
            body.removeFromLeft (gap);
        }

        placeKnobRow (body, controls);
    }

    /** Small engraved caption above a combo box or switch in the utility row. */
    void placeUtilityControl (juce::Rectangle<int> area, juce::Label& label, juce::Component& control)
    {
        label.setBounds (area.removeFromTop (legendLineHeight).reduced (1, 0));
        control.setBounds (area.reduced (6, 2));
    }
}

TubeCompAudioProcessorEditor::TubeCompAudioProcessorEditor (TubeCompAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // Paint the chassis ourselves so the editor is opaque on both AU and VST3
    // hosts (some hosts otherwise expose their default green content layer).
    setOpaque (true);
    setBufferedToImage (false);
    setColour (juce::ResizableWindow::backgroundColourId, panelBottom);
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (leftMeter);
    addAndMakeVisible (rightMeter);
    addAndMakeVisible (gainReductionMeter);

    addAndMakeVisible (bypassButton);
    bypassButton.setColour (juce::ToggleButton::textColourId, creamText);
    meterModeBox.addItemList ({ "Input", "Output", "Gain Reduction" }, 1);
    meterModeBox.setSelectedId (2, juce::dontSendNotification);
    meterModeBox.onChange = [this] { updateMeterMode(); };
    meterModeLabel.setText ("Meter Mode", juce::dontSendNotification);
    meterModeLabel.setTooltip ("Meter source: input, output, or gain reduction");
    meterModeLabel.setJustificationType (juce::Justification::centred);
    meterModeLabel.setColour (juce::Label::textColourId, creamText);
    meterModeLabel.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    addAndMakeVisible (meterModeLabel);
    addAndMakeVisible (meterModeBox);

    compressorSectionLabel.setText ("GAIN REDUCTION", juce::dontSendNotification);
    compressorSectionLabel.setColour (juce::Label::textColourId, dimText);
    compressorSectionLabel.setJustificationType (juce::Justification::centredLeft);
    compressorSectionLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (compressorSectionLabel);
    compressorSectionLabel.setVisible (false); // rendered in the recessed card header

    tubeSectionLabel.setText ("TUBE CIRCUIT", juce::dontSendNotification);
    tubeSectionLabel.setColour (juce::Label::textColourId, dimText);
    tubeSectionLabel.setJustificationType (juce::Justification::centredLeft);
    tubeSectionLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (tubeSectionLabel);
    tubeSectionLabel.setVisible (false); // rendered in the recessed card header

    setupRotary (thresholdSlider, thresholdLabel, "Threshold", "Compressor input level", *this, true);
    // The threshold dial is the visual anchor of a 670-class faceplate, so it
    // renders as a large engraved ring rather than a standard knob.
    thresholdSlider.setNumDecimalPlacesToDisplay (1);
    lookAndFeel.setRingDialStyle (&thresholdSlider, true);
    setupRotary (ratioSlider, ratioLabel, "Ratio", "Compression slope", *this, false);
    setupRotary (attackSlider, attackLabel, "Attack", "Envelope rise time", *this, false);
    setupRotary (releaseSlider, releaseLabel, "Release", "Envelope recovery", *this, false);
    setupRotary (kneeSlider, kneeLabel, "Knee", "Softness around threshold", *this, false);
    setupRotary (lookAheadSlider, lookAheadLabel, "Look Ahead", "Predictive detector delay", *this, false);
    setupRotary (sidechainHPFSlider, sidechainHPFLabel, "S-Chain HPF", "Detector low-cut", *this, false);

    setupRotary (inputGainSlider, inputGainLabel, "Input", "Drive into tube stage", *this, false);
    setupRotary (driveSlider, driveLabel, "Drive", "12AX7 nonlinear gain", *this, true);
    setupRotary (harmonicsSlider, harmonicsLabel, "Harmonics", "Odd / even balance", *this, false);
    setupRotary (brightnessSlider, brightnessLabel, "Brightness", "High-frequency tilt", *this, false);
    setupRotary (mixSlider, mixLabel, "Mix", "Dry / processed blend", *this, false);
    setupRotary (outputGainSlider, outputGainLabel, "Output", "Final makeup level", *this, false);
    setupRotary (biasDriveSlider, biasDriveLabel, "Bias Drive", "Cathode operating point", *this, false);

    oversampleBox.addItemList ({ "1x", "2x", "4x", "8x" }, 1);
    addAndMakeVisible (oversampleBox);
    oversampleLabel.setText ("O/Sample", juce::dontSendNotification);
    oversampleLabel.setTooltip ("Oversampling rate");
    oversampleLabel.setJustificationType (juce::Justification::centred);
    oversampleLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (oversampleLabel);

    circuitModelBox.addItemList ({ "Fast", "Tube (WDF)" }, 1);
    addAndMakeVisible (circuitModelBox);
    circuitModelLabel.setText ("Circuit", juce::dontSendNotification);
    circuitModelLabel.setTooltip ("Circuit model: fast waveshaper or WDF tube");
    circuitModelLabel.setJustificationType (juce::Justification::centred);
    circuitModelLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (circuitModelLabel);

    topologyBox.addItemList ({ "Feedforward", "Feedback" }, 1);
    addAndMakeVisible (topologyBox);
    topologyLabel.setText ("Detector", juce::dontSendNotification);
    topologyLabel.setTooltip ("Detector mode: feedforward or feedback");
    topologyLabel.setJustificationType (juce::Justification::centred);
    topologyLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (topologyLabel);

    timeConstantBox.addItemList ({ "Custom", "1", "2", "3", "4", "5", "6" }, 1);
    addAndMakeVisible (timeConstantBox);
    timeConstantLabel.setText ("Time Const", juce::dontSendNotification);
    timeConstantLabel.setTooltip ("Fairchild time constant 1-6, or custom attack/release");
    timeConstantLabel.setJustificationType (juce::Justification::centred);
    timeConstantLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (timeConstantLabel);

    linkModeBox.addItemList ({ "Left/Right", "Linked", "Lat/Ver" }, 1);
    addAndMakeVisible (linkModeBox);
    linkModeLabel.setText ("Link", juce::dontSendNotification);
    linkModeLabel.setTooltip ("Channel link mode");
    linkModeLabel.setJustificationType (juce::Justification::centred);
    linkModeLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (linkModeLabel);

    transformerBox.addItemList ({ "Off", "On" }, 1);
    addAndMakeVisible (transformerBox);
    transformerLabel.setText ("Iron", juce::dontSendNotification);
    transformerLabel.setTooltip ("Output transformer: low-band lift, gentle top-octave rolloff, weak core saturation");
    transformerLabel.setJustificationType (juce::Justification::centred);
    transformerLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (transformerLabel);

    auto& apvts = processorRef.apvts;
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::threshold, thresholdSlider);
    ratioAttachment      = std::make_unique<SliderAttachment> (apvts, ParamIDs::ratio, ratioSlider);
    attackAttachment     = std::make_unique<SliderAttachment> (apvts, ParamIDs::attack, attackSlider);
    releaseAttachment    = std::make_unique<SliderAttachment> (apvts, ParamIDs::release, releaseSlider);
    kneeAttachment       = std::make_unique<SliderAttachment> (apvts, ParamIDs::knee, kneeSlider);
    lookAheadAttachment  = std::make_unique<SliderAttachment> (apvts, ParamIDs::lookAhead, lookAheadSlider);
    sidechainHPFAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::sidechainHPF, sidechainHPFSlider);

    inputGainAttachment  = std::make_unique<SliderAttachment> (apvts, ParamIDs::inputGain, inputGainSlider);
    outputGainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::outputGain, outputGainSlider);
    driveAttachment      = std::make_unique<SliderAttachment> (apvts, ParamIDs::drive, driveSlider);
    harmonicsAttachment  = std::make_unique<SliderAttachment> (apvts, ParamIDs::harmonics, harmonicsSlider);
    brightnessAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::brightness, brightnessSlider);
    mixAttachment        = std::make_unique<SliderAttachment> (apvts, ParamIDs::mix, mixSlider);
    biasDriveAttachment  = std::make_unique<SliderAttachment> (apvts, ParamIDs::biasDrive, biasDriveSlider);
    oversampleAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::oversample, oversampleBox);
    circuitModelAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::circuitModel, circuitModelBox);
    topologyAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::topology, topologyBox);
    timeConstantAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::timeConstant, timeConstantBox);
    linkModeAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::linkMode, linkModeBox);
    transformerAttachment = std::make_unique<ComboBoxAttachment> (apvts, ParamIDs::transformer, transformerBox);
    bypassAttachment      = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass, bypassButton);

    // --- U-grade polish: toolbar (preset/undo/redo/A-B/scale), undo stack,
    // always-on readout, shift fine drag, double-click reset, TC dimming ---

    presetPrevButton.onClick = [this]
    {
        const int next = juce::jlimit (0, processorRef.getNumPrograms() - 1,
                                       getDisplayedProgram() - 1);
        if (next != getDisplayedProgram())
            processorRef.setCurrentProgram (next);
    };
    presetNextButton.onClick = [this]
    {
        const int next = juce::jlimit (0, processorRef.getNumPrograms() - 1,
                                       getDisplayedProgram() + 1);
        if (next != getDisplayedProgram())
            processorRef.setCurrentProgram (next);
    };

    presetNameLabel.setJustificationType (juce::Justification::centred);
    presetNameLabel.setColour (juce::Label::textColourId, creamText);
    presetNameLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    undoButton.onClick = [this]
    {
        if (undoStack.empty())
            return;
        auto state = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back (processorRef.apvts.copyState());
        applyRestoredState (state);
    };
    redoButton.onClick = [this]
    {
        if (redoStack.empty())
            return;
        auto state = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back (processorRef.apvts.copyState());
        applyRestoredState (state);
    };

    abButton.setClickingTogglesState (true);
    abButton.onClick = [this]
    {
        programmaticChange = true;
        auto current = processorRef.apvts.copyState();
        if (activeAB == 0)
        {
            abSnapshotA = current;
            activeAB = 1;
            if (abSnapshotB.isValid())
                applyRestoredState (abSnapshotB);
        }
        else
        {
            abSnapshotB = current;
            activeAB = 0;
            if (abSnapshotA.isValid())
                applyRestoredState (abSnapshotA);
        }
        abButton.setButtonText (activeAB == 0 ? "A/B" : "B/A");
    };

    sizeBox.addItemList ({ "90%", "100%", "110%", "125%" }, 1);
    sizeBox.setSelectedId (1, juce::dontSendNotification);
    sizeBox.onChange = [this]
    {
        static const std::array<juce::Point<int>, 4> sizes {{
            { 1062, 738 }, { 1180, 820 }, { 1298, 902 }, { 1475, 1025 }
        }};
        const int index = juce::jlimit (0, 3, sizeBox.getSelectedItemIndex());
        setSize (sizes[static_cast<size_t> (index)].x, sizes[static_cast<size_t> (index)].y);
    };
    sizeLabel.setText ("Scale", juce::dontSendNotification);
    sizeLabel.setJustificationType (juce::Justification::centredRight);
    sizeLabel.setColour (juce::Label::textColourId, dimText);
    sizeLabel.setFont (juce::FontOptions (9.0f));

    for (auto* b : { &presetPrevButton, &presetNextButton, &undoButton, &redoButton, &abButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a363d));
        b->setColour (juce::TextButton::textColourOffId, creamText);
        addAndMakeVisible (*b);
    }
    abButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffd8b26a));
    addAndMakeVisible (presetNameLabel);
    addAndMakeVisible (sizeBox);
    addAndMakeVisible (sizeLabel);

    // Gesture undo: snapshot state before any parameter control is touched;
    // commit at gesture end or when a non-gesture change settles.
    lastCommittedState = processorRef.apvts.copyState();
    juce::Component* undoSources[] = {
        &thresholdSlider, &ratioSlider, &attackSlider, &releaseSlider, &kneeSlider,
        &lookAheadSlider, &sidechainHPFSlider, &inputGainSlider, &outputGainSlider,
        &driveSlider, &harmonicsSlider, &brightnessSlider, &mixSlider, &biasDriveSlider,
        &oversampleBox, &circuitModelBox, &topologyBox, &timeConstantBox, &linkModeBox,
        &transformerBox, &bypassButton
    };
    for (auto* c : undoSources)
        c->addMouseListener (this, false);
    for (auto* parameter : processorRef.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            processorRef.apvts.addParameterListener (withID->paramID, this);
    bypassButton.onClick = [this] { commitGestureTransaction(); };
    timeConstantBox.onChange = [this]
    {
        commitGestureTransaction();
        updateTimeConstantDimming();
    };

    // Double-click restores each knob to its parameter default.
    auto addDoubleClickReset = [this] (juce::Slider& slider, const juce::String& paramID)
    {
        if (auto* parameter = processorRef.apvts.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    };
    addDoubleClickReset (thresholdSlider, ParamIDs::threshold);
    addDoubleClickReset (ratioSlider, ParamIDs::ratio);
    addDoubleClickReset (attackSlider, ParamIDs::attack);
    addDoubleClickReset (releaseSlider, ParamIDs::release);
    addDoubleClickReset (kneeSlider, ParamIDs::knee);
    addDoubleClickReset (lookAheadSlider, ParamIDs::lookAhead);
    addDoubleClickReset (sidechainHPFSlider, ParamIDs::sidechainHPF);
    addDoubleClickReset (inputGainSlider, ParamIDs::inputGain);
    addDoubleClickReset (outputGainSlider, ParamIDs::outputGain);
    addDoubleClickReset (driveSlider, ParamIDs::drive);
    addDoubleClickReset (harmonicsSlider, ParamIDs::harmonics);
    addDoubleClickReset (brightnessSlider, ParamIDs::brightness);
    addDoubleClickReset (mixSlider, ParamIDs::mix);
    addDoubleClickReset (biasDriveSlider, ParamIDs::biasDrive);

    // Hardware sweep: this class of knob runs about 300 degrees with the gap at
    // the bottom, so the printed scale and the pointer share that arc instead
    // of the JUCE default full revolution.
    for (auto* slider : { &thresholdSlider, &ratioSlider, &attackSlider, &releaseSlider,
                          &kneeSlider, &lookAheadSlider, &sidechainHPFSlider, &inputGainSlider,
                          &outputGainSlider, &driveSlider, &harmonicsSlider, &brightnessSlider,
                          &mixSlider, &biasDriveSlider })
        lookAndFeel.setPrintedSweep (slider, -150.0f, 150.0f);

    updateTimeConstantDimming();
    updateMeterMode();
    startTimerHz (15);

    readoutMap = {
        { &thresholdSlider, "Threshold" }, { &ratioSlider, "Ratio" },
        { &attackSlider, "Attack" }, { &releaseSlider, "Release" },
        { &kneeSlider, "Knee" }, { &lookAheadSlider, "Look-Ahead" },
        { &sidechainHPFSlider, "Sidechain HPF" }, { &inputGainSlider, "Input" },
        { &outputGainSlider, "Output" }, { &driveSlider, "Drive" },
        { &harmonicsSlider, "Harmonics" }, { &brightnessSlider, "Brightness" },
        { &mixSlider, "Mix" }, { &biasDriveSlider, "Bias Drive" }
    };
    readoutComboMap = {
        { &oversampleBox, "Oversampling" }, { &circuitModelBox, "Circuit Model" },
        { &topologyBox, "Detector Mode" }, { &timeConstantBox, "Time Constant" },
        { &linkModeBox, "Link" }
    };

    setResizable (true, true);
    // The proportional layout needs this minimum canvas to preserve readable
    // two-line labels and hardware-sized rotary controls.
    setResizeLimits (980, 800, 1600, 1120);
    setSize (1180, 820);
}

TubeCompAudioProcessorEditor::~TubeCompAudioProcessorEditor()
{
    stopTimer();
    for (auto* parameter : processorRef.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            processorRef.apvts.removeParameterListener (withID->paramID, this);
    setLookAndFeel (nullptr);
}

int TubeCompAudioProcessorEditor::getDisplayedProgram() const
{
    return processorRef.getCurrentProgram();
}

void TubeCompAudioProcessorEditor::applyRestoredState (const juce::ValueTree& state)
{
    if (! state.isValid())
        return;
    programmaticChange = true;
    processorRef.apvts.replaceState (state);
    // matchProgramAndReadouts runs on the timer.
}

void TubeCompAudioProcessorEditor::updateTimeConstantDimming()
{
    const bool customTiming = timeConstantBox.getSelectedItemIndex() == 0;
    attackSlider.setEnabled (customTiming);
    releaseSlider.setEnabled (customTiming);
    const auto alpha = customTiming ? 1.0f : 0.35f;
    attackLabel.setAlpha (alpha);
    releaseLabel.setAlpha (alpha);
}

void TubeCompAudioProcessorEditor::updateMeterMode()
{
    const int mode = meterModeBox.getSelectedItemIndex();
    if (mode == 0)
    {
        leftMeter.setMeterLabel ("L IN");
        rightMeter.setMeterLabel ("R IN");
        gainReductionMeter.setMeterLabel ("IN SUM");
        leftMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        rightMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        gainReductionMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        leftMeter.setLevelProvider ([this] { return processorRef.getInputLevelDb (0); });
        rightMeter.setLevelProvider ([this] { return processorRef.getInputLevelDb (1); });
        gainReductionMeter.setLevelProvider ([this]
        {
            return juce::jmax (processorRef.getInputLevelDb (0), processorRef.getInputLevelDb (1));
        });
        return;
    }

    if (mode == 1)
    {
        leftMeter.setMeterLabel ("L OUT");
        rightMeter.setMeterLabel ("R OUT");
        gainReductionMeter.setMeterLabel ("OUT SUM");
        leftMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        rightMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        gainReductionMeter.setDisplayMode (VUMeterComponent::DisplayMode::level);
        leftMeter.setLevelProvider ([this] { return processorRef.getOutputLevelDb (0); });
        rightMeter.setLevelProvider ([this] { return processorRef.getOutputLevelDb (1); });
        gainReductionMeter.setLevelProvider ([this]
        {
            return juce::jmax (processorRef.getOutputLevelDb (0), processorRef.getOutputLevelDb (1));
        });
        return;
    }

    leftMeter.setMeterLabel ("GR L");
    rightMeter.setMeterLabel ("GR R");
    gainReductionMeter.setMeterLabel ("GR MAX");
    leftMeter.setDisplayMode (VUMeterComponent::DisplayMode::gainReduction);
    rightMeter.setDisplayMode (VUMeterComponent::DisplayMode::gainReduction);
    gainReductionMeter.setDisplayMode (VUMeterComponent::DisplayMode::gainReduction);
    leftMeter.setLevelProvider ([this] { return processorRef.getChannelGainReductionDb (0); });
    rightMeter.setLevelProvider ([this] { return processorRef.getChannelGainReductionDb (1); });
    gainReductionMeter.setLevelProvider ([this] { return processorRef.getGainReductionDb(); });
}

void TubeCompAudioProcessorEditor::updateReadout (juce::Component* source)
{
    auto show = [this] (const juce::String& name, const juce::String& value)
    {
        readoutText = name + ": " + value;
        repaint();
    };

    if (auto* slider = dynamic_cast<juce::Slider*> (source))
    {
        const auto entry = std::find_if (readoutMap.begin(), readoutMap.end(),
                                         [slider] (const std::pair<juce::Slider*, juce::String>& e) { return e.first == slider; });
        if (entry != readoutMap.end())
            show (entry->second, slider->getTextFromValue (slider->getValue()));
        return;
    }
    if (auto* combo = dynamic_cast<juce::ComboBox*> (source))
    {
        const auto entry = std::find_if (readoutComboMap.begin(), readoutComboMap.end(),
                                         [combo] (const std::pair<juce::ComboBox*, juce::String>& e) { return e.first == combo; });
        if (entry != readoutComboMap.end())
            show (entry->second, combo->getText());
        return;
    }
}

void TubeCompAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (auto* c = e.eventComponent != nullptr ? e.eventComponent : e.originalComponent)
    {
        juce::ignoreUnused (c);
        // A click on any parameter control starts a gesture: keep the state
        // that existed before the control got to modify it. If the user
        // releases without changing anything, commitGestureTransaction()
        // discards the snapshot.
        gesturePreState = processorRef.apvts.copyState();
        gestureTracked = true;
    }
}

void TubeCompAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    // ComboBox applies the value on item selection (popup), sliders on
    // release or continuously. Defer one tick so the attachment lands first.
    triggerAsyncUpdate();
}

void TubeCompAudioProcessorEditor::mouseEnter (const juce::MouseEvent& e)
{
    updateReadout (e.eventComponent != nullptr ? e.eventComponent : e.originalComponent);
}

void TubeCompAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    readoutText.clear();
    repaint();
}

void TubeCompAudioProcessorEditor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void TubeCompAudioProcessorEditor::handleAsyncUpdate()
{
    commitGestureTransaction();
}

void TubeCompAudioProcessorEditor::commitGestureTransaction()
{
    auto current = processorRef.apvts.copyState();

    if (! lastCommittedState.isValid())
        lastCommittedState = current;

    const bool changed = ! current.isEquivalentTo (lastCommittedState);

    if (! changed)
    {
        gesturePreState = juce::ValueTree();
        gestureTracked = false;
        return;
    }

    if (programmaticChange)
    {
        // Undo/redo/A-B/preset/programmatic restores move the baseline only;
        // they must not record themselves as undo transactions.
        programmaticChange = false;
        lastCommittedState = current;
        gesturePreState = juce::ValueTree();
        gestureTracked = false;
        return;
    }

    auto pre = gesturePreState.isValid() ? gesturePreState : lastCommittedState;
    if (pre.isEquivalentTo (current))
    {
        lastCommittedState = current;
        gesturePreState = juce::ValueTree();
        gestureTracked = false;
        return;
    }

    undoStack.push_back (pre);
    if (undoStack.size() > static_cast<size_t> (maxUndoDepth))
        undoStack.pop_front();
    redoStack.clear();

    lastCommittedState = current;
    gesturePreState = juce::ValueTree();
    gestureTracked = false;
}

void TubeCompAudioProcessorEditor::timerCallback()
{
    const auto programName = processorRef.getProgramName (getDisplayedProgram());
    if (presetNameLabel.getText() != programName)
        presetNameLabel.setText (programName, juce::dontSendNotification);

    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    repaint();
}

void TubeCompAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto layout = makeEditorLayout (getLocalBounds());

    if (bounds.isEmpty())
        return;

    // Chassis surround, then the oxblood anodised faceplate inset inside it.
    g.setColour (plateShadow);
    g.fillAll();
    juce::ColourGradient panelGrad (panelTop, bounds.getX(), bounds.getY(),
                                     panelBottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (panelGrad);
    auto face = layout.face.toFloat().expanded (8.0f);
    g.fillRoundedRectangle (face, 10.0f);

    // Anodised grain: faint diagonal sheen plus a darker vignette at the
    // edges, which is what makes a flat painted plate read as metal.
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    // Recessed lower section. On the original the utility band below the two
    // channel strips is a separate, inset plate: it sits lower than the channel
    // field and catches less light. A shallow inset drawn as a lit top-left
    // edge and a dark bottom-right edge is what produces that step, and it is
    // the strongest depth cue on the whole faceplate after the knobs.
    {
        const auto recess = layout.utilityStrip.toFloat().reduced (3.0f, 0.0f);
        // Shadow the recess casts upward onto the channel field above it. This
        // is the cue that sells the step, so it is drawn as a short gradient
        // falloff rather than a flat band, which read as a black bar.
        for (int i = 0; i < 7; ++i)
        {
            const float t = static_cast<float> (i) / 6.0f;
            g.setColour (juce::Colours::black.withAlpha (0.34f * (1.0f - t) * (1.0f - t)));
            g.fillRect (recess.getX(), recess.getY() - 7.0f + static_cast<float> (i),
                        recess.getWidth(), 1.0f);
        }

        // The step only reads if the recess is genuinely darker than the field
        // around it. A near-identical value is invisible no matter how strong
        // the lip is, so the band is dropped well clear of the panel tone.
        juce::ColourGradient recessGrad (panelBottom.darker (0.30f), recess.getX(), recess.getY(),
                                         panelBottom.darker (0.52f), recess.getX(), recess.getBottom(),
                                         false);
        g.setGradientFill (recessGrad);
        g.fillRect (recess);

        // Lip and side walls. A straight top edge across the full width reads
        // as a painted band; it needs the vertical returns at each end to read
        // as a well sunk into the panel.
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.drawLine (recess.getX(), recess.getY(), recess.getRight(), recess.getY(), 2.0f);
        g.drawLine (recess.getX(), recess.getY(), recess.getX(), recess.getBottom(), 1.4f);
        g.drawLine (recess.getRight() - 1.0f, recess.getY(), recess.getRight() - 1.0f,
                    recess.getBottom(), 1.4f);

        // Catch-light on the wall the light actually reaches: the left wall's
        // inner face, and the floor immediately below the lip.
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.drawLine (recess.getX() + 1.0f, recess.getY() + 2.0f, recess.getX() + 1.0f,
                    recess.getBottom(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawLine (recess.getX(), recess.getY() + 2.0f, recess.getRight(),
                    recess.getY() + 2.0f, 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (float yPos = face.getY(); yPos < face.getBottom(); yPos += 4.0f)
        g.drawLine (face.getX(), yPos, face.getRight(), yPos, 1.0f);
    // Corner falloff only. A radial gradient large enough to reach the corners
    // leaves a visible seam along each axis, so the darkening is applied as
    // four edge bands instead.
    const float vignetteInset = 26.0f;
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRect (face.getX(), face.getY(), face.getWidth(), vignetteInset);
    g.fillRect (face.getX(), face.getBottom() - vignetteInset, face.getWidth(), vignetteInset);
    g.fillRect (face.getX(), face.getY(), vignetteInset, face.getHeight());
    g.fillRect (face.getRight() - vignetteInset, face.getY(), vignetteInset, face.getHeight());
    g.restoreState();

    // Bevel around the faceplate edge.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (face, 10.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (face.reduced (2.0f), 8.0f, 1.0f);

    // Corner screws on the chassis, as on the original rack panel.
    drawScrew (g, { 12.0f, 11.0f });
    drawScrew (g, { bounds.getWidth() - 12.0f, 11.0f });
    drawScrew (g, { 12.0f, bounds.getHeight() - 11.0f });
    drawScrew (g, { bounds.getWidth() - 12.0f, bounds.getHeight() - 11.0f });

    // Engraved channel captions. The 670 labels each half of the faceplate
    // with the channel it drives, so the dual-mono structure is explicit.
    auto headerFor = [] (const juce::Rectangle<int>& strip)
    {
        return strip.reduced (4, 0).removeFromTop (legendLineHeight + 2);
    };
    drawStripHeader (g, headerFor (layout.upperStrip), "LEFT CHANNEL", "VARI-MU  /  12AX7");
    drawStripHeader (g, headerFor (layout.lowerStrip), "RIGHT CHANNEL", "VARI-MU  /  12AX7");

    // Section seams. The 670 separates the dual channel field from the lower
    // utility section with a machined groove, so the two halves of the panel
    // read as distinct assemblies rather than one continuous field. Drawn as
    // a hairline engraved groove with a light lower lip so it catches the same
    // top-lit shading as the faceplate bevel without becoming a black bar.
    const auto drawSeam = [&g] (const juce::Rectangle<int>& seamBounds)
    {
        auto seam = seamBounds.toFloat().reduced (4.0f, 0.0f);
        const float grooveY = seam.getCentreY() - 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRect (seam.getX(), grooveY, seam.getWidth(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (seam.getX(), grooveY + 1.0f, seam.getWidth(), 1.0f);
    };
    // The gap between the two channel rows.
    drawSeam (layout.upperStrip.withTop (layout.upperStrip.getBottom())
                             .withHeight (layout.lowerStrip.getY() - layout.upperStrip.getBottom()));
    // The gap between the channel field and the utility band.
    drawSeam (layout.lowerStrip.withTop (layout.lowerStrip.getBottom())
                             .withHeight (layout.utilityStrip.getY() - layout.lowerStrip.getBottom()));

    // Cream bottom name plate. The original carries a long engraved warning
    // strip here; this carries the product identity and the live readout
    // instead, which keeps the same visual weight without copying the text.
    auto plate = layout.namePlate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plate.translated (1.5f, 2.5f), 4.0f);
    juce::ColourGradient plateGrad (plateCream, plate.getX(), plate.getY(),
                                    plateCream.darker (0.16f), plate.getX(), plate.getBottom(), false);
    g.setGradientFill (plateGrad);
    g.fillRoundedRectangle (plate, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (plate, 3.0f, 1.0f);

    g.setColour (juce::Colour (0xff2b2320));
    g.setFont (juce::FontOptions (19.0f, juce::Font::italic | juce::Font::bold));
    auto plateText = plate.reduced (14.0f, 0.0f);
    g.drawText ("EON-Vari mu DSP", plateText.removeFromLeft (plateText.getWidth() * 0.46f),
                juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (8.5f, juce::Font::plain));
    g.setColour (juce::Colour (0xff4a3f39));
    g.drawText ("VARIABLE-MU  /  MODEL EON-670", plateText.removeFromLeft (plateText.getWidth() * 0.54f),
                juce::Justification::centred);

    // Hover/turn readout shares the footer line: it replaces the product tag
    // while a control is under the pointer, so the name plate stays readable.
    const auto& footer = layout.footer;
    g.setFont (juce::FontOptions (readoutText.isNotEmpty() ? 10.0f : 9.0f, juce::Font::bold));
    g.setColour (readoutText.isNotEmpty() ? accentGold : dimText.withAlpha (0.72f));
    g.drawFittedText (readoutText.isNotEmpty()
                          ? readoutText
                          : juce::String ("VARIABLE-MU  /  DUAL-MONO  /  CLASS-A TUBE COMPRESSOR"),
                      footer, juce::Justification::centred, 1);
}

void TubeCompAudioProcessorEditor::resized()
{
    const auto layout = makeEditorLayout (getLocalBounds());

    // Toolbar: [presets] < Name > | UNDO REDO | A/B | Scale [combo]
    auto toolbar = layout.toolbar.reduced (4, 0);
    auto leftZone = toolbar.removeFromLeft (300);
    presetPrevButton.setBounds (leftZone.removeFromLeft (26).reduced (0, 3));
    presetNextButton.setBounds (leftZone.removeFromLeft (26).reduced (0, 3));
    presetNameLabel.setBounds (leftZone);

    auto rightZone = toolbar.removeFromRight (150);
    sizeLabel.setBounds (rightZone.removeFromLeft (44));
    sizeBox.setBounds (rightZone.reduced (2, 3));

    toolbar.removeFromLeft (8);
    toolbar.removeFromRight (8);
    undoButton.setBounds (toolbar.removeFromLeft (58).reduced (2, 3));
    toolbar.removeFromLeft (4);
    redoButton.setBounds (toolbar.removeFromLeft (58).reduced (2, 3));
    toolbar.removeFromLeft (4);
    abButton.setBounds (toolbar.removeFromLeft (44).reduced (2, 3));

    // Upper row: variable-mu gain reduction. The threshold dial leads the row
    // as the large engraved ring the 670 is known for, then the remaining
    // compression controls follow at hardware-knob scale.
    placeChannelStrip (layout.upperStrip, leftMeter,
                       { { &ratioSlider, &ratioLabel },
                         { &attackSlider, &attackLabel },
                         { &releaseSlider, &releaseLabel },
                         { &kneeSlider, &kneeLabel } },
                       &thresholdSlider, &thresholdLabel);

    // Lower row: the tube stage that produces the compression curve.
    placeChannelStrip (layout.lowerStrip, rightMeter,
                       { { &inputGainSlider, &inputGainLabel },
                         { &driveSlider, &driveLabel },
                         { &biasDriveSlider, &biasDriveLabel },
                         { &harmonicsSlider, &harmonicsLabel },
                         { &brightnessSlider, &brightnessLabel } });

    // Utility row: gain-reduction meter, the remaining small-signal trims, and
    // the mode switches that sit below the two main control rows. The strip is
    // shared with a fixed budget so every element keeps its minimum width at
    // the smallest supported window instead of overrunning the faceplate.
    auto utility = layout.utilityStrip.reduced (4, 0);
    constexpr int utilityGap = 8;
    const int utilityMeterWidth = 96;
    const int utilityMeterHeight = juce::jmin (utility.getHeight(),
                                              juce::roundToInt (utilityMeterWidth * 1.28f));
    gainReductionMeter.setBounds (utility.removeFromLeft (utilityMeterWidth)
                                      .withSizeKeepingCentre (utilityMeterWidth, utilityMeterHeight));
    utility.removeFromLeft (utilityGap);

    // Remaining trims and mode switches split the leftover width evenly, so
    // the row rescales cleanly between the window size limits.
    struct UtilityCell
    {
        juce::Label* label = nullptr;
        juce::Component* control = nullptr;
    };
    const UtilityCell knobCells[] = {
        { &mixLabel, &mixSlider },
        { &outputGainLabel, &outputGainSlider },
        { &lookAheadLabel, &lookAheadSlider },
        { &sidechainHPFLabel, &sidechainHPFSlider }
    };
    const UtilityCell switchCells[] = {
        { &oversampleLabel, &oversampleBox },
        { &circuitModelLabel, &circuitModelBox },
        { &topologyLabel, &topologyBox },
        { &timeConstantLabel, &timeConstantBox },
        { &linkModeLabel, &linkModeBox },
        { &meterModeLabel, &meterModeBox },
        { &transformerLabel, &transformerBox },
        { nullptr, &bypassButton }
    };

    constexpr int cellGap = 6;
    const int switchGap = 6;
    const int knobCount = static_cast<int> (std::size (knobCells));
    const int switchCount = static_cast<int> (std::size (switchCells));
    // Give the switches exactly the width they need, then hand the remainder
    // to the trims. Computing the switch width from the leftover total is what
    // pushed the last cell off the faceplate.
    constexpr int minSwitchWidth = 96;
    const int totalGaps = cellGap * knobCount + switchGap * (switchCount - 1);
    const int reservedForSwitches = juce::jmin (switchCount * minSwitchWidth,
                                               juce::jmax (0, utility.getWidth() * 7 / 10));
    const int usableKnobWidth = juce::jmax (knobCount * 54,
                                           utility.getWidth() - reservedForSwitches - totalGaps);

    auto knobRow = utility.removeFromLeft (usableKnobWidth);
    placeKnobRow (knobRow, { { &mixSlider, &mixLabel },
                             { &outputGainSlider, &outputGainLabel },
                             { &lookAheadSlider, &lookAheadLabel },
                             { &sidechainHPFSlider, &sidechainHPFLabel } });
    utility.removeFromLeft (cellGap);

    const int switchUsable = juce::jmax (0, utility.getWidth() - switchGap * (switchCount - 1));
    const int switchBase = switchUsable / switchCount;
    const int switchExtra = switchUsable % switchCount;
    int index = 0;
    for (const auto& cell : switchCells)
    {
        const int cellWidth = switchBase + (index < switchExtra ? 1 : 0);
        auto cellArea = utility.removeFromLeft (cellWidth);
        if (cell.label != nullptr)
            placeUtilityControl (cellArea, *cell.label, *cell.control);
        else
            cell.control->setBounds (cellArea.reduced (2, 14));
        if (++index < switchCount)
            utility.removeFromLeft (switchGap);
    }
}
