#include "VUMeterComponent.h"

#include <array>
#include <cmath>

namespace
{
    constexpr float needleMinDeg = -48.0f;
    constexpr float needleMaxDeg = 48.0f;
    constexpr float minVu = -20.0f;
    constexpr float maxVu = 3.0f;
    constexpr float maxGainReductionDb = 24.0f;

    struct ScaleMark
    {
        float normalised;
        const char* label;
    };

    const std::array<ScaleMark, 5> vuScale {{
        { 0.0f, "-20" },
        { 10.0f / 23.0f, "-10" },
        { 15.0f / 23.0f, "-5" },
        { 20.0f / 23.0f, "0" },
        { 1.0f, "+3" }
    }};

    const std::array<ScaleMark, 6> gainReductionScale {{
        { 0.0f, "0" },
        { std::log1p (1.0f) / std::log1p (maxGainReductionDb), "1" },
        { std::log1p (2.0f) / std::log1p (maxGainReductionDb), "2" },
        { std::log1p (4.0f) / std::log1p (maxGainReductionDb), "4" },
        { std::log1p (8.0f) / std::log1p (maxGainReductionDb), "8" },
        { std::log1p (20.0f) / std::log1p (maxGainReductionDb), "20" }
    }};
}

VUMeterComponent::VUMeterComponent (juce::String meterLabel) : label (std::move (meterLabel))
{
    startTimerHz (30);
}

VUMeterComponent::~VUMeterComponent()
{
    stopTimer();
}

void VUMeterComponent::setDisplayMode (DisplayMode newMode) noexcept
{
    if (displayMode == newMode)
        return;

    displayMode = newMode;
    displayedValue01 = 0.0f;
    repaint();
}

float VUMeterComponent::levelDbFsToVuNormalised (float levelDbFs) noexcept
{
    // A VU scale describes level relative to the chosen alignment, not dBFS.
    // At the studio alignment used here, -18 dBFS lands exactly at 0 VU.
    const float vu = levelDbFs - zeroVuDbFs;
    return juce::jlimit (0.0f, 1.0f, juce::jmap (vu, minVu, maxVu, 0.0f, 1.0f));
}

float VUMeterComponent::gainReductionDbToNormalised (float gainReductionDb) noexcept
{
    const float bounded = juce::jlimit (0.0f, maxGainReductionDb, gainReductionDb);
    // Dense marks at 0-4 dB make normal vari-mu gain riding legible; higher
    // reductions still fit on the same physical arc without a long dead zone.
    return std::log1p (bounded) / std::log1p (maxGainReductionDb);
}

void VUMeterComponent::timerCallback()
{
    const float valueDb = levelProvider ? levelProvider() : -100.0f;
    const float target01 = displayMode == DisplayMode::level
        ? levelDbFsToVuNormalised (valueDb)
        : gainReductionDbToNormalised (valueDb);

    // Slightly faster attack than release, approximating classic VU ballistics.
    const float coeff = (target01 > displayedValue01) ? 0.55f : 0.82f;
    displayedValue01 = target01 + coeff * (displayedValue01 - target01);

    repaint();
}

void VUMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    // Raised chrome bezel. The panel light comes from the upper left, so the
    // bright quadrant is top-left and the shadowed one bottom-right, and the
    // cast shadow falls down and to the right, away from the light. The earlier
    // gradient ran the other way, which made the meter look recessed instead
    // of mounted proud of the panel.
    for (int i = 0; i < 6; ++i)
    {
        const float t = static_cast<float> (i) / 5.0f;
        g.setColour (juce::Colours::black.withAlpha (0.30f * (1.0f - t) * (1.0f - t)));
        // Offset down and to the right only. Expanding equally in both axes
        // centres every layer on the bezel and reads as a symmetric blur, so
        // the meter looked inset instead of standing proud.
        g.fillRoundedRectangle (bounds.translated (1.0f + static_cast<float> (i),
                                                    1.5f + static_cast<float> (i))
                                    .expanded (static_cast<float> (i)), 7.0f);
    }

    juce::ColourGradient bezelGrad (juce::Colour (0xffdfe6e2), bounds.getX(), bounds.getY(),
                                    juce::Colour (0xff2c3439), bounds.getRight(), bounds.getBottom(), true);
    g.setGradientFill (bezelGrad);
    g.fillRoundedRectangle (bounds, 7.0f);

    // Machined step: a bright outer lip and a dark inner edge, so the ring
    // reads as a turned part rather than a painted outline.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 6.0f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (3.5f), 4.5f, 1.2f);

    // Dark cavity the dial sits down inside.
    g.setColour (juce::Colour (0xff0b1114));
    g.fillRoundedRectangle (bounds.reduced (4.5f), 5.0f);

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

    // The level scale is calibrated in VU while the GR scale is intentionally
    // non-linear, so the common 1-4 dB operating range has useful resolution.
    const auto drawScale = [&] (const auto& marks)
    {
        for (const auto& mark : marks)
        {
            const float t = mark.normalised;
            const float deg = juce::jmap (t, 0.0f, 1.0f, needleMinDeg, needleMaxDeg);
            const float rad = juce::degreesToRadians (deg - 90.0f);
            const float outerR = needleLength * 0.92f;
            const float innerR = needleLength * 0.79f;

            juce::Point<float> p1 (pivot.x + outerR * std::cos (rad), pivot.y + outerR * std::sin (rad));
            juce::Point<float> p2 (pivot.x + innerR * std::cos (rad), pivot.y + innerR * std::sin (rad));

            const bool hotZone = displayMode == DisplayMode::level && t >= 20.0f / 23.0f;
            g.setColour (hotZone ? juce::Colour (0xffb5352c) : juce::Colour (0xff4a4436));
            g.drawLine ({ p1, p2 }, 1.35f);

            const float labelRadius = needleLength * 0.62f;
            const auto labelCentre = juce::Point<float> (pivot.x + labelRadius * std::cos (rad),
                                                          pivot.y + labelRadius * std::sin (rad));
            auto labelBounds = juce::Rectangle<float> (labelCentre.x - 11.0f, labelCentre.y - 5.0f, 22.0f, 10.0f);
            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
            g.drawFittedText (mark.label, labelBounds.toNearestInt(), juce::Justification::centred, 1);
        }
    };
    if (displayMode == DisplayMode::level)
        drawScale (vuScale);
    else
        drawScale (gainReductionScale);

    // Keep the legend clear of the needle pivot. The upper face remains empty
    // even at the compact default UI scale, whereas the lower region is shared
    // by the pivot, its cap and the glass reflection.
    auto labelArea = juce::Rectangle<float> (face.getX() + face.getWidth() * 0.08f, face.getY() + 3.0f,
                                              face.getWidth() * 0.84f, 10.0f);
    g.setFont (juce::FontOptions (7.0f, juce::Font::italic | juce::Font::bold));
    g.setColour (juce::Colour (0xff2a2622));
    g.drawFittedText (label + (displayMode == DisplayMode::level ? "  /  0 VU = -18 dBFS"
                                                                  : "  /  GAIN REDUCTION dB"),
                      labelArea.toNearestInt(), juce::Justification::centred, 1);

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

    // Slight glass reflection across the dial. The lower two corners are placed
    // as a fraction of the face height, not of the absolute bottom coordinate,
    // so the highlight stays inside the dial at any meter size.
    const float reflectionFoot = face.getY() + face.getHeight() * 0.55f;
    juce::Path reflection;
    reflection.addQuadrilateral (face.getX() + face.getWidth() * 0.08f, face.getY() + 2.0f,
                                 face.getX() + face.getWidth() * 0.38f, face.getY() + 2.0f,
                                 face.getRight() - 4.0f, reflectionFoot,
                                 face.getRight() - 18.0f, reflectionFoot);
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.fillPath (reflection);
}
