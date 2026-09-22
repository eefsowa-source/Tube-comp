#include "VUMeterComponent.h"

namespace
{
    constexpr float minDb = -30.0f;
    constexpr float needleMinDeg = -48.0f;
    constexpr float needleMaxDeg = 48.0f;
}

VUMeterComponent::VUMeterComponent (juce::String meterLabel) : label (std::move (meterLabel))
{
    startTimerHz (30);
}

VUMeterComponent::~VUMeterComponent()
{
    stopTimer();
}

void VUMeterComponent::timerCallback()
{
    const float levelDb = levelProvider ? levelProvider() : minDb;
    const float target01 = juce::jlimit (0.0f, 1.0f, juce::jmap (levelDb, minDisplayDb, maxDisplayDb, 0.0f, 1.0f));

    // Slightly faster attack than release, approximating classic VU ballistics.
    const float coeff = (target01 > displayedValue01) ? 0.55f : 0.82f;
    displayedValue01 = target01 + coeff * (displayedValue01 - target01);

    repaint();
}

void VUMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    // Deep shadow and double-machined bezel.
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRoundedRectangle (bounds.translated (2.0f, 3.0f), 7.0f);
    juce::ColourGradient bezelGrad (juce::Colour (0xffc8d0cb), bounds.getX(), bounds.getY(),
                                    juce::Colour (0xff465760), bounds.getRight(), bounds.getBottom(), true);
    g.setGradientFill (bezelGrad);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (juce::Colour (0xff11181c));
    g.fillRoundedRectangle (bounds.reduced (4.0f), 5.0f);

    auto face = bounds.reduced (8.0f);
    face.removeFromBottom (face.getHeight() * 0.16f);

    // Cream dial face with a subtle vignette.
    juce::ColourGradient faceGrad (juce::Colour (0xfff2ecd2), face.getCentreX(), face.getY(),
                                    juce::Colour (0xffd9d0ac), face.getCentreX(), face.getBottom(), false);
    g.setGradientFill (faceGrad);
    g.fillRoundedRectangle (face, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (face.reduced (1.0f), 3.0f, 1.0f);

    const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.28f);
    const float needleLength = face.getHeight() * 1.02f;

    // Scale arc + ticks.
    g.setColour (juce::Colour (0xff4a4436));
    for (int i = 0; i <= 10; ++i)
    {
        const float t = static_cast<float> (i) / 10.0f;
        const float deg = juce::jmap (t, 0.0f, 1.0f, needleMinDeg, needleMaxDeg);
        const float rad = juce::degreesToRadians (deg - 90.0f);
        const bool major = (i % 2 == 0);
        const float outerR = needleLength * 0.92f;
        const float innerR = needleLength * (major ? 0.80f : 0.86f);

        juce::Point<float> p1 (pivot.x + outerR * std::cos (rad), pivot.y + outerR * std::sin (rad));
        juce::Point<float> p2 (pivot.x + innerR * std::cos (rad), pivot.y + innerR * std::sin (rad));

        const bool hotZone = t > 0.82f;
        g.setColour (hotZone ? juce::Colour (0xffb5352c) : juce::Colour (0xff4a4436));
        g.drawLine ({ p1, p2 }, major ? 1.6f : 1.0f);
    }

    auto labelArea = juce::Rectangle<float> (face.getX(), face.getBottom() - face.getHeight() * 0.02f,
                                              face.getWidth(), face.getHeight() * 0.16f);
    g.setFont (juce::FontOptions (10.0f, juce::Font::italic | juce::Font::bold));
    g.setColour (juce::Colour (0xff2a2622));
    g.drawFittedText ("TUBE COMP  " + label, labelArea.toNearestInt(), juce::Justification::centred, 1);

    // Needle.
    const float needleDeg = juce::jmap (displayedValue01, 0.0f, 1.0f, needleMinDeg, needleMaxDeg);
    const float needleRad = juce::degreesToRadians (needleDeg - 90.0f);
    juce::Point<float> tip (pivot.x + needleLength * std::cos (needleRad),
                             pivot.y + needleLength * std::sin (needleRad));

    juce::Path needlePath;
    needlePath.addLineSegment ({ pivot, tip }, 1.6f);
    g.setColour (juce::Colour (0xff1c1a16));
    g.strokePath (needlePath, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (juce::Colour (0xff2a2622));
    g.fillEllipse (pivot.x - 3.5f, pivot.y - 3.5f, 7.0f, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillEllipse (pivot.x - 1.6f, pivot.y - 2.0f, 2.0f, 1.5f);

    // Slight glass reflection across the dial.
    juce::Path reflection;
    reflection.addQuadrilateral (face.getX() + face.getWidth() * 0.08f, face.getY() + 2.0f,
                                 face.getX() + face.getWidth() * 0.38f, face.getY() + 2.0f,
                                 face.getRight() - 4.0f, face.getBottom() * 0.55f,
                                 face.getRight() - 18.0f, face.getBottom() * 0.55f);
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.fillPath (reflection);
}
