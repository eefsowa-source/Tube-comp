#include "PluginEditor.h"

namespace
{
    // PuigChild/Fairchild-class reference palette: slate-blue faceplate,
    // warm cream legends, black controls, and restrained red/gold accents.
    // This is an original EON surface, not a pixel copy of a commercial UI.
    const juce::Colour panelTop    { 0xff71838a };
    const juce::Colour panelBottom { 0xff2d3a40 };
    const juce::Colour screwColour { 0xffb9b6aa };
    const juce::Colour creamText   { 0xfff4e9c5 };
    const juce::Colour dimText     { 0xffd2c8ac };

    constexpr int cardHeaderHeight = 38;

    struct EditorLayout
    {
        juce::Rectangle<int> title;
        juce::Rectangle<int> meterCard;
        juce::Rectangle<int> compressorCard;
        juce::Rectangle<int> tubeCard;
        juce::Rectangle<int> footer;
    };

    EditorLayout makeEditorLayout (juce::Rectangle<int> bounds)
    {
        auto frame = bounds.reduced (16, 10);
        EditorLayout layout;
        layout.title = frame.removeFromTop (48);
        layout.footer = frame.removeFromBottom (20);

        constexpr int sectionGap = 10;
        const int usableHeight = juce::jmax (0, frame.getHeight() - sectionGap * 2);
        const int meterHeight = juce::jlimit (150, 174, juce::roundToInt (usableHeight * 0.24f));
        const int remainingHeight = usableHeight - meterHeight;
        const int compressorHeight = juce::roundToInt (remainingHeight * 0.44f);

        layout.meterCard = frame.removeFromTop (meterHeight);
        frame.removeFromTop (sectionGap);
        layout.compressorCard = frame.removeFromTop (compressorHeight);
        frame.removeFromTop (sectionGap);
        layout.tubeCard = frame;
        return layout;
    }

    juce::Rectangle<int> getCardBody (juce::Rectangle<int> card)
    {
        auto body = card.reduced (14, 10);
        body.removeFromTop (cardHeaderHeight);
        return body;
    }

    void placeKnob (juce::Rectangle<int> bounds, juce::Slider& slider, juce::Label& label)
    {
        auto labelArea = bounds.removeFromBottom (36).reduced (2, 0);
        bounds.removeFromBottom (8); // guaranteed air gap between knob and typography
        auto knobArea = bounds.reduced (5, 2);
        const int knobSize = juce::jmax (1, juce::jmin (knobArea.getWidth(), knobArea.getHeight()));

        slider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));
        label.setBounds (labelArea);
    }

    void placeKnobRow (juce::Rectangle<int> row,
                       std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls)
    {
        constexpr int gap = 6;
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
        juce::ignoreUnused (big);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::textBoxTextColourId, creamText);
        slider.setTooltip (text);
        slider.setNumDecimalPlacesToDisplay (text == "Ratio" ? 1 : 2);
        parent.addAndMakeVisible (slider);

        label.setText (text.toUpperCase() + "\n" + function, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, creamText);
        label.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        label.setMinimumHorizontalScale (0.72f);
        parent.addAndMakeVisible (label);
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> centre)
    {
        g.setColour (screwColour.darker (0.3f));
        g.fillEllipse (centre.x - 4.5f, centre.y - 4.5f, 9.0f, 9.0f);
        g.setColour (screwColour);
        g.fillEllipse (centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (centre.x - 2.5f, centre.y, centre.x + 2.5f, centre.y, 1.0f);
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
    leftMeter.setLevelProvider ([this] { return processorRef.getChannelLevelDb (0); });
    rightMeter.setLevelProvider ([this] { return processorRef.getChannelLevelDb (1); });
    gainReductionMeter.setRange (0.0f, 24.0f);
    gainReductionMeter.setLevelProvider ([this] { return processorRef.getGainReductionDb(); });

    addAndMakeVisible (bypassButton);
    bypassButton.setColour (juce::ToggleButton::textColourId, creamText);

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
    setupRotary (ratioSlider, ratioLabel, "Ratio", "Compression slope", *this, false);
    setupRotary (attackSlider, attackLabel, "Attack", "Envelope rise time", *this, false);
    setupRotary (releaseSlider, releaseLabel, "Release", "Envelope recovery", *this, false);
    setupRotary (kneeSlider, kneeLabel, "Knee", "Softness around threshold", *this, false);
    setupRotary (lookAheadSlider, lookAheadLabel, "Look-Ahead", "Predictive detector delay", *this, false);
    setupRotary (sidechainHPFSlider, sidechainHPFLabel, "Sidechain HPF", "Detector low-cut", *this, false);

    setupRotary (inputGainSlider, inputGainLabel, "Input", "Drive into tube stage", *this, false);
    setupRotary (driveSlider, driveLabel, "Drive", "12AX7 nonlinear gain", *this, true);
    setupRotary (harmonicsSlider, harmonicsLabel, "Harmonics", "Odd / even balance", *this, false);
    setupRotary (brightnessSlider, brightnessLabel, "Brightness", "High-frequency tilt", *this, false);
    setupRotary (mixSlider, mixLabel, "Mix", "Dry / processed blend", *this, false);
    setupRotary (outputGainSlider, outputGainLabel, "Output", "Final makeup level", *this, false);
    setupRotary (biasDriveSlider, biasDriveLabel, "Bias Drive", "Cathode operating point", *this, false);

    oversampleBox.addItemList ({ "1x", "2x", "4x", "8x" }, 1);
    addAndMakeVisible (oversampleBox);
    oversampleLabel.setText ("Oversampling", juce::dontSendNotification);
    oversampleLabel.setJustificationType (juce::Justification::centred);
    oversampleLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (oversampleLabel);

    circuitModelBox.addItemList ({ "Fast", "Tube (WDF)" }, 1);
    addAndMakeVisible (circuitModelBox);
    circuitModelLabel.setText ("Circuit Model", juce::dontSendNotification);
    circuitModelLabel.setJustificationType (juce::Justification::centred);
    circuitModelLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (circuitModelLabel);

    topologyBox.addItemList ({ "Feedforward", "Feedback" }, 1);
    addAndMakeVisible (topologyBox);
    topologyLabel.setText ("Detector Mode", juce::dontSendNotification);
    topologyLabel.setJustificationType (juce::Justification::centred);
    topologyLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (topologyLabel);

    timeConstantBox.addItemList ({ "Custom", "1", "2", "3", "4", "5", "6" }, 1);
    addAndMakeVisible (timeConstantBox);
    timeConstantLabel.setText ("Time Constant", juce::dontSendNotification);
    timeConstantLabel.setJustificationType (juce::Justification::centred);
    timeConstantLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (timeConstantLabel);

    linkModeBox.addItemList ({ "Left/Right", "Linked", "Lat/Ver" }, 1);
    addAndMakeVisible (linkModeBox);
    linkModeLabel.setText ("Link", juce::dontSendNotification);
    linkModeLabel.setJustificationType (juce::Justification::centred);
    linkModeLabel.setColour (juce::Label::textColourId, creamText);
    addAndMakeVisible (linkModeLabel);

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
    bypassAttachment      = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass, bypassButton);

    setResizable (true, true);
    // The proportional layout needs this minimum canvas to preserve readable
    // two-line labels and hardware-sized rotary controls.
    setResizeLimits (980, 800, 1600, 1120);
    setSize (1180, 820);
}

TubeCompAudioProcessorEditor::~TubeCompAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void TubeCompAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto layout = makeEditorLayout (getLocalBounds());

    if (bounds.isEmpty())
        return;

    // Brushed blue-grey faceplate.
    juce::ColourGradient panelGrad (panelTop, bounds.getX(), bounds.getY(),
                                     panelBottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (panelGrad);
    g.fillAll();

    // Subtle horizontal brushed-metal streaks.
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (float yPos = 0.0f; yPos < bounds.getHeight(); yPos += 3.0f)
        g.drawLine (0.0f, yPos, bounds.getWidth(), yPos, 1.0f);

    // Recessed rack rails at the sides of the faceplate.
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRect (0.0f, 0.0f, 8.0f, bounds.getHeight());
    g.fillRect (bounds.getRight() - 8.0f, 0.0f, 8.0f, bounds.getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (9.0f, 22.0f, 9.0f, bounds.getBottom() - 22.0f, 1.0f);
    g.drawLine (bounds.getRight() - 9.0f, 22.0f, bounds.getRight() - 9.0f, bounds.getBottom() - 22.0f, 1.0f);

    // Corner screws.
    drawScrew (g, { 14.0f, 14.0f });
    drawScrew (g, { bounds.getWidth() - 14.0f, 14.0f });
    drawScrew (g, { 14.0f, bounds.getHeight() - 14.0f });
    drawScrew (g, { bounds.getWidth() - 14.0f, bounds.getHeight() - 14.0f });

    auto card = [&g] (juce::Rectangle<float> r, juce::Colour accent, const juce::String& title)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r.translated (2.0f, 4.0f), 9.0f);
        g.setColour (juce::Colour (0xff1b252a).withAlpha (0.98f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff9aa6a6).withAlpha (0.32f));
        g.drawRoundedRectangle (r.reduced (4.0f), 5.0f, 1.0f);
        g.setColour (accent.withAlpha (0.85f));
        g.drawRoundedRectangle (r.reduced (0.7f), 8.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawLine (r.getX() + 9.0f, r.getY() + 2.0f, r.getRight() - 9.0f, r.getY() + 2.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.drawLine (r.getX() + 9.0f, r.getBottom() - 2.0f, r.getRight() - 9.0f, r.getBottom() - 2.0f, 1.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle ({ r.getX() + 10.0f, r.getY() + 10.0f, 220.0f, 18.0f }, 3.0f);
        g.setColour (accent.withAlpha (0.45f));
        g.drawLine (r.getX() + 12.0f, r.getY() + 35.0f, r.getRight() - 12.0f, r.getY() + 35.0f, 1.0f);
        g.setColour (accent.brighter (0.35f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (title, static_cast<int> (r.getX() + 16.0f), static_cast<int> (r.getY() + 12.0f), 204, 14,
                    juce::Justification::centredLeft);
    };

    // The painted cards and child controls share the exact same layout model.
    card (layout.meterCard.toFloat(), juce::Colour (0xffb79c68), "METER BRIDGE");
    card (layout.compressorCard.toFloat(), juce::Colour (0xff8a6e45), "VARIABLE-MU COMPRESSOR");
    card (layout.tubeCard.toFloat(), juce::Colour (0xff6e5540), "12AX7 / WDF TUBE CIRCUIT");

    // Original EON badge: vintage compressor hierarchy without copying a
    // commercial product logo or faceplate.
    auto titleArea = layout.title;
    const auto productTitleArea = titleArea.removeFromTop (30);
    const auto signatureArea = titleArea;
    g.setColour (creamText);
    g.setFont (juce::FontOptions (28.0f, juce::Font::italic | juce::Font::bold));
    g.drawText ("EON-Vari mu DSP", productTitleArea, juce::Justification::centred);

    // Handwritten signature beneath the product name.
    g.setColour (creamText.withAlpha (0.86f));
    g.setFont (juce::FontOptions ("Snell Roundhand", 15.0f, juce::Font::plain));
    g.drawText ("DUAL-MONO VARIABLE-MU", signatureArea, juce::Justification::centred);

    g.setColour (dimText.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("EON AUDIO  /  670-STYLE VARIABLE-MU CONTROL SURFACE", layout.footer,
                juce::Justification::centred);

}

void TubeCompAudioProcessorEditor::resized()
{
    const auto layout = makeEditorLayout (getLocalBounds());

    auto meterRow = getCardBody (layout.meterCard);
    auto bypassArea = meterRow.removeFromLeft (124);
    bypassButton.setBounds (bypassArea.withSizeKeepingCentre (112, 38));
    meterRow.removeFromLeft (10);

    constexpr int meterGap = 10;
    const int meterWidth = (meterRow.getWidth() - meterGap * 2) / 3;
    leftMeter.setBounds (meterRow.removeFromLeft (meterWidth));
    meterRow.removeFromLeft (meterGap);
    rightMeter.setBounds (meterRow.removeFromLeft (meterWidth));
    meterRow.removeFromLeft (meterGap);
    gainReductionMeter.setBounds (meterRow);

    placeKnobRow (getCardBody (layout.compressorCard),
                  { { &thresholdSlider, &thresholdLabel },
                    { &ratioSlider, &ratioLabel },
                    { &attackSlider, &attackLabel },
                    { &releaseSlider, &releaseLabel },
                    { &kneeSlider, &kneeLabel },
                    { &lookAheadSlider, &lookAheadLabel },
                    { &sidechainHPFSlider, &sidechainHPFLabel } });

    auto tubeBody = getCardBody (layout.tubeCard);
    auto utilityRow = tubeBody.removeFromBottom (44);
    tubeBody.removeFromBottom (8);

    placeKnobRow (tubeBody,
                  { { &inputGainSlider, &inputGainLabel },
                    { &driveSlider, &driveLabel },
                    { &harmonicsSlider, &harmonicsLabel },
                    { &brightnessSlider, &brightnessLabel },
                    { &mixSlider, &mixLabel },
                    { &outputGainSlider, &outputGainLabel },
                    { &biasDriveSlider, &biasDriveLabel } });

    const int utilityWidth = juce::jmin (180, utilityRow.getWidth() / 5);
    auto oversampleArea = utilityRow.removeFromLeft (utilityWidth);
    oversampleLabel.setBounds (oversampleArea.removeFromTop (16));
    oversampleBox.setBounds (oversampleArea.reduced (8, 0));
    utilityRow.removeFromLeft (12);
    auto circuitArea = utilityRow.removeFromLeft (utilityWidth);
    circuitModelLabel.setBounds (circuitArea.removeFromTop (16));
    circuitModelBox.setBounds (circuitArea.reduced (8, 0));
    utilityRow.removeFromLeft (12);
    auto topologyArea = utilityRow.removeFromLeft (utilityWidth);
    topologyLabel.setBounds (topologyArea.removeFromTop (16));
    topologyBox.setBounds (topologyArea.reduced (8, 0));
    utilityRow.removeFromLeft (12);
    auto timeConstantArea = utilityRow.removeFromLeft (utilityWidth);
    timeConstantLabel.setBounds (timeConstantArea.removeFromTop (16));
    timeConstantBox.setBounds (timeConstantArea.reduced (8, 0));
    utilityRow.removeFromLeft (12);
    auto linkModeArea = utilityRow.removeFromLeft (utilityWidth);
    linkModeLabel.setBounds (linkModeArea.removeFromTop (16));
    linkModeBox.setBounds (linkModeArea.reduced (8, 0));
}
