#include "TubeCompLookAndFeel.h"

namespace
{
    const juce::Colour panelDark      { 0xff202a2f };
    const juce::Colour knobBodyDark   { 0xff10171b };
    const juce::Colour knobBodyLight  { 0xff40545d };
    const juce::Colour chromeLight    { 0xffe1d7bc };
    const juce::Colour chromeShadow   { 0xff65757c };
    const juce::Colour indexCream     { 0xfff4e9c5 };
    const juce::Colour ledGreen       { 0xff81ff72 };
    const juce::Colour ledRed         { 0xffd85748 };
}

TubeCompLookAndFeel::TubeCompLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, indexCream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, indexCream);
    setColour (juce::ComboBox::textColourId, indexCream);
    setColour (juce::PopupMenu::backgroundColourId, panelDark);
    setColour (juce::PopupMenu::textColourId, indexCream);
}

void TubeCompLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                           static_cast<float> (width), static_cast<float> (height)).reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float outerRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto arc = [&g] (float cx, float cy, float radius, float start, float end, juce::Colour colour, float thickness)
    {
        juce::Path path;
        path.addArc (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, start, end, true);
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    // Layered shadow makes the control sit above the panel like a machined
    // hardware knob rather than a flat vector icon.
    g.setColour (juce::Colours::black.withAlpha (0.58f));
    g.fillEllipse (centre.x - outerRadius + 2.5f, centre.y - outerRadius + 4.0f,
                   outerRadius * 2.0f, outerRadius * 2.0f);

    // Chrome bezel ring.
    {
        juce::ColourGradient ringGrad (chromeLight, centre.x - outerRadius, centre.y - outerRadius,
                                        chromeShadow, centre.x + outerRadius, centre.y + outerRadius, false);
        g.setGradientFill (ringGrad);
        g.fillEllipse (centre.x - outerRadius, centre.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f);
        arc (centre.x, centre.y, outerRadius - 1.0f, juce::degreesToRadians (205.0f),
             juce::degreesToRadians (320.0f), juce::Colours::white.withAlpha (0.42f), 1.2f);
        arc (centre.x, centre.y, outerRadius - 1.0f, juce::degreesToRadians (25.0f),
             juce::degreesToRadians (145.0f), juce::Colours::black.withAlpha (0.55f), 1.4f);
    }

    // Tick marks around the bezel.
    g.setColour (chromeShadow.darker (0.4f));
    constexpr int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (numTicks - 1);
        const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const float inner = outerRadius * 0.92f;
        const float outer = outerRadius * 0.99f;
        juce::Line<float> tick (centre.getPointOnCircumference (inner, tickAngle),
                                 centre.getPointOnCircumference (outer, tickAngle));
        g.drawLine (tick, 1.2f);
    }

    // Knob body.
    const float bodyRadius = outerRadius * 0.78f;
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (centre.x - bodyRadius - 1.0f, centre.y - bodyRadius + 2.0f,
                   bodyRadius * 2.0f + 2.0f, bodyRadius * 2.0f + 2.0f);
    {
        juce::ColourGradient bodyGrad (knobBodyLight, centre.x - bodyRadius * 0.4f, centre.y - bodyRadius * 0.6f,
                                        knobBodyDark, centre.x + bodyRadius * 0.6f, centre.y + bodyRadius * 0.8f, true);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    }
    arc (centre.x, centre.y, bodyRadius - 2.0f, juce::degreesToRadians (210.0f),
         juce::degreesToRadians (315.0f), juce::Colours::white.withAlpha (0.17f), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    // Index dot showing the current position.
    const auto dotPos = centre.getPointOnCircumference (bodyRadius * 0.72f, angle);
    g.setColour (indexCream);
    g.fillEllipse (dotPos.x - 2.6f, dotPos.y - 2.6f, 5.2f, 5.2f);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.fillEllipse (dotPos.x - 1.0f, dotPos.y - 1.6f, 1.8f, 1.8f);

    // Values are intentionally hidden at rest and appear only while the knob
    // is being moved, keeping the faceplate clean like a physical unit.
    if (slider.isMouseButtonDown())
    {
        const auto valueText = slider.getTextFromValue (slider.getValue());
        auto valueBounds = juce::Rectangle<float> (centre.x - 34.0f, bounds.getBottom() - 17.0f, 68.0f, 16.0f);
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRoundedRectangle (valueBounds.translated (1.0f, 1.5f), 3.0f);
        g.setColour (juce::Colour (0xff28231b));
        g.fillRoundedRectangle (valueBounds, 3.0f);
        g.setColour (indexCream);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (valueText, valueBounds.toNearestInt(), juce::Justification::centred, 1);
    }
}

void TubeCompLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool, bool)
{
    auto bounds = button.getLocalBounds().toFloat();

    // LED indicator on the left.
    const float ledDiameter = juce::jmin (bounds.getHeight(), 14.0f);
    juce::Rectangle<float> ledBounds (bounds.getX(), bounds.getCentreY() - ledDiameter * 0.5f,
                                       ledDiameter, ledDiameter);
    const bool engaged = ! button.getToggleState(); // param semantics: state==true means bypassed
    const auto ledColour = engaged ? ledGreen : ledRed;

    g.setColour (ledColour.withAlpha (0.35f));
    g.fillEllipse (ledBounds.expanded (3.0f));
    g.setColour (ledColour);
    g.fillEllipse (ledBounds);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (ledBounds, 1.0f);

    // Recessed switch body + text.
    auto textArea = bounds.withTrimmedLeft (ledDiameter + 8.0f);
    auto switchArea = textArea.removeFromLeft (42.0f).reduced (2.0f, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (switchArea.translated (1.5f, 2.0f), 4.0f);
    g.setColour (juce::Colour (0xff0d0c0a));
    g.fillRoundedRectangle (switchArea, 4.0f);
    g.setColour (juce::Colour (0xff5b5549));
    g.drawRoundedRectangle (switchArea.reduced (0.6f), 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (switchArea.getX() + 3.0f, switchArea.getY() + 2.0f,
                switchArea.getRight() - 3.0f, switchArea.getY() + 2.0f, 1.0f);
    g.setColour (indexCream);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (button.getButtonText(), textArea.toNearestInt(), juce::Justification::centredLeft, 1);
}

void TubeCompLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds.translated (1.5f, 2.0f), 4.0f);
    g.setColour (knobBodyDark);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (chromeShadow);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (bounds.getX() + 4.0f, bounds.getY() + 1.5f, bounds.getRight() - 4.0f,
                bounds.getY() + 1.5f, 1.0f);

    const auto arrowZone = bounds.removeFromRight (bounds.getHeight());
    juce::Path arrow;
    const auto c = arrowZone.getCentre();
    arrow.addTriangle (c.x - 4.0f, c.y - 2.5f, c.x + 4.0f, c.y - 2.5f, c.x, c.y + 3.0f);
    g.setColour (indexCream);
    g.fillPath (arrow);

    juce::ignoreUnused (box);
}

juce::Font TubeCompLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::FontOptions (11.5f, juce::Font::bold);
}

juce::Font TubeCompLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

juce::Font TubeCompLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions (13.0f);
}
