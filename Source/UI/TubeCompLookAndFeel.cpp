#include "TubeCompLookAndFeel.h"

#include <algorithm>
#include <array>

namespace
{
    // Fairchild-670-class palette: dark graphite panel, ivory legends and
    // scale printing, plain black turned knobs, and cream switch bodies.
    const juce::Colour panelDark      { 0xff16181a };
    const juce::Colour knobBodyDark   { 0xff08090a };
    const juce::Colour knobBodyLight  { 0xff33383c };
    const juce::Colour indexCream     { 0xffe8e2d2 };
    const juce::Colour scaleCream     { 0xffd8d2c2 };
    const juce::Colour switchCream    { 0xffcfc7b4 };

    // Single light source for every raised element on the panel. The reference
    // photographs are all shot from slightly above and to the left, so the
    // highlight sits on the upper-left of each knob and the cast shadow falls
    // down and to the right. Fixing it as a constant is what makes the panel
    // read as one lit object instead of a set of independently shaded widgets.
    constexpr float lightAngleRadians = 2.35f;   // ~135 degrees, upper-left

    // Controls that render as an engraved ring dial instead of a plain knob.
    std::array<juce::Slider*, 4> ringDials {};
    int ringDialCount = 0;

    // Printed sweep per control. Hardware-scale controls run about 300 degrees
    // with the gap at the bottom; the default JUCE rotary range is a full turn,
    // which puts scale marks all the way round and reads as a generic widget.
    struct Sweep { juce::Slider* slider = nullptr; float start = 0.0f; float end = 0.0f; };
    std::array<Sweep, 32> sweeps {};
    int sweepCount = 0;
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

void TubeCompLookAndFeel::setRingDialStyle (juce::Slider* slider, bool shouldBeRingDial)
{
    if (slider == nullptr)
        return;

    if (! shouldBeRingDial)
    {
        for (auto& slot : ringDials)
            if (slot == slider)
                slot = nullptr;
        return;
    }

    for (const auto* slot : ringDials)
        if (slot == slider)
            return;

    if (ringDialCount < static_cast<int> (ringDials.size()))
        ringDials[static_cast<size_t> (ringDialCount++)] = slider;
}

void TubeCompLookAndFeel::setPrintedSweep (juce::Slider* slider, float startDegrees, float endDegrees)
{
    if (slider == nullptr)
        return;

    for (auto& entry : sweeps)
    {
        if (entry.slider == slider)
        {
            entry.start = juce::degreesToRadians (startDegrees);
            entry.end = juce::degreesToRadians (endDegrees);
            return;
        }
    }

    if (sweepCount < static_cast<int> (sweeps.size()))
    {
        auto& entry = sweeps[static_cast<size_t> (sweepCount++)];
        entry.slider = slider;
        entry.start = juce::degreesToRadians (startDegrees);
        entry.end = juce::degreesToRadians (endDegrees);
    }
}

void TubeCompLookAndFeel::getSweep (const juce::Slider& slider, float rotaryStartAngle,
                                    float rotaryEndAngle, float& startRadians,
                                    float& endRadians) const
{
    for (const auto& entry : sweeps)
    {
        if (entry.slider == &slider)
        {
            startRadians = entry.start;
            endRadians = entry.end;
            return;
        }
    }

    startRadians = rotaryStartAngle;
    endRadians = rotaryEndAngle;
}

void TubeCompLookAndFeel::drawPrintedScale (juce::Graphics& g, juce::Point<float> centre,
                                            float innerRadius, float outerRadius, int numFences,
                                            float startRadians, float endRadians, bool numbered)
{
    g.setColour (scaleCream.withAlpha (0.82f));
    for (int i = 0; i < numFences; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (numFences - 1);
        const float fenceAngle = startRadians + t * (endRadians - startRadians);
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (innerRadius, fenceAngle),
                                       centre.getPointOnCircumference (outerRadius, fenceAngle)),
                    1.1f);
    }

    if (! numbered)
        return;

    // Every other fence carries a printed number. The labels sit outside the
    // tick ring rather than inside it, because the inside is where the knob
    // body sits and they would be hidden behind it.
    g.setFont (juce::FontOptions (7.0f, juce::Font::bold));
    g.setColour (scaleCream);
    for (int i = 0; i < numFences; i += 2)
    {
        const float t = static_cast<float> (i) / static_cast<float> (numFences - 1);
        const float fenceAngle = startRadians + t * (endRadians - startRadians);
        const auto labelCentre = centre.getPointOnCircumference (outerRadius * 1.16f, fenceAngle);
        g.drawText (juce::String (i / 2),
                    juce::roundToInt (labelCentre.x) - 7,
                    juce::roundToInt (labelCentre.y) - 6, 14, 12,
                    juce::Justification::centred);
    }
}

void TubeCompLookAndFeel::drawTurnedKnob (juce::Graphics& g, juce::Point<float> centre,
                                          float radius, float pointerAngle,
                                          float lightAngle, float rimRatio,
                                          bool tallBody)
{
    const auto light = centre.getPointOnCircumference (radius, lightAngle);
    const auto shadowDir = centre.getPointOnCircumference (radius, lightAngle + juce::MathConstants<float>::pi);
    const auto shadowOffset = (shadowDir - centre) * 0.16f;

    const float rimRadius = radius * rimRatio;
    const float domeRadius = rimRadius * 0.74f;

    // Contact shadow: tight and dark right under the knob, so the part looks
    // seated on the panel rather than floating over it.
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillEllipse (centre.x - radius * 0.98f + shadowOffset.x,
                   centre.y - radius * 0.98f + shadowOffset.y * 0.7f,
                   radius * 1.96f, radius * 1.96f);
    // Soft penumbra just outside it, which is what a real cast shadow does at
    // the edge. Kept wide and very faint so it reads as light, not as a ring.
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillEllipse (centre.x - radius * 1.14f + shadowOffset.x * 1.3f,
                   centre.y - radius * 1.14f + shadowOffset.y,
                   radius * 2.28f, radius * 2.28f);

    // Tapered skirt. The reference knobs are not cylinders: they flare outward
    // from a smaller top face to a wider base, so the side wall is a visible
    // cone. Drawing it as a real cone (a bright top arc, a dark bottom arc, and
    // gradient side walls) is what separates this from a generic round widget.
    {
        // Body cone: widest at the base, narrowing to the top face.
        juce::ColourGradient rimGrad (juce::Colour (0xff1c1e20), centre.x - radius * 0.55f,
                                      centre.y - radius * 0.72f,
                                      juce::Colour (0xff3f4448), centre.x + radius * 0.62f,
                                      centre.y + radius * 0.55f, true);
        g.setGradientFill (rimGrad);
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

        // Lit arc across the upper-left of the cone wall, and a dark arc under
        // it where the wall turns away from the light.
        auto arc = [&] (float fromDeg, float toDeg, juce::Colour colour, float thickness)
        {
            juce::Path p;
            const int steps = 16;
            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::degreesToRadians (juce::jmap (static_cast<float> (i), 0.0f,
                                                                      static_cast<float> (steps),
                                                                      fromDeg, toDeg));
                const auto pt = centre.getPointOnCircumference (radius * 0.985f, a);
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (thickness));
        };

        arc (150.0f, 300.0f, juce::Colours::white.withAlpha (0.22f), 2.0f);
        arc (-40.0f, 40.0f, juce::Colours::black.withAlpha (0.60f), 2.6f);

        // Inner wall of the cone where it meets the top face: a dark ring plus
        // a thin bright edge on the lit side, so the top reads as raised.
        g.setColour (juce::Colours::black.withAlpha (0.72f));
        g.fillEllipse (centre.x - rimRadius, centre.y - rimRadius,
                       rimRadius * 2.0f, rimRadius * 2.0f);
    }

    // Top face: polished black bakelite, domed very slightly. The reference
    // knobs are glossy enough to carry a hard specular streak, which is the
    // single strongest "this is a real part" cue in the photograph.
    {
        juce::ColourGradient faceGrad (juce::Colour (0xff2b2e31), centre.x - domeRadius * 0.5f,
                                       centre.y - domeRadius * 0.72f,
                                       juce::Colour (0xff08090a), centre.x + domeRadius * 0.55f,
                                       centre.y + domeRadius * 0.62f, true);
        g.setGradientFill (faceGrad);
        g.fillEllipse (centre.x - domeRadius, centre.y - domeRadius,
                       domeRadius * 2.0f, domeRadius * 2.0f);
    }

    // Concentric turning marks on the top face, matching the machined rings the
    // reference knobs show around their centre.
    g.setColour (juce::Colours::white.withAlpha (0.055f));
    for (float ring = 0.34f; ring < 0.92f; ring += 0.15f)
    {
        const float r = domeRadius * ring;
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 0.7f);
    }

    // Hard specular streak across the top face. Drawn as a short arc rather
    // than a full rim highlight, because bakelite reflects a small source as a
    // tight streak, not an even glow.
    {
        juce::Path spec;
        const int steps = 12;
        for (int i = 0; i <= steps; ++i)
        {
            const float a = lightAngle + juce::jmap (static_cast<float> (i), 0.0f,
                                                      static_cast<float> (steps), -0.62f, 0.30f);
            const auto pt = centre.getPointOnCircumference (domeRadius * 0.80f, a);
            if (i == 0)
                spec.startNewSubPath (pt);
            else
                spec.lineTo (pt);
        }
        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.strokePath (spec, juce::PathStrokeType (1.8f));

        // A second, fainter and wider bloom just inside it for the glossy
        // falloff that a hard edge alone does not convey.
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.strokePath (spec, juce::PathStrokeType (5.0f));
    }

    // Silhouette, so the knob separates from the printed scale behind it.
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.1f);

    // Indicator. The reference uses a long tapered pointer that reaches almost
    // to the skirt edge, with a small round hub at the centre.
    const auto tip = centre.getPointOnCircumference (domeRadius * 0.90f, pointerAngle);
    const auto grooveStart = centre.getPointOnCircumference (domeRadius * 0.20f, pointerAngle);
    g.setColour (juce::Colours::black.withAlpha (0.60f));
    g.drawLine (juce::Line<float> (grooveStart, tip), 3.4f);
    g.setColour (indexCream);
    g.drawLine (juce::Line<float> (grooveStart, tip), 2.0f);

    // Hub cap, lit consistently with the top face.
    const float hubRadius = domeRadius * 0.22f;
    const auto hubCentre = centre + (light - centre) * 0.10f;
    juce::ColourGradient hubGrad (juce::Colour (0xff35393d), hubCentre.x, hubCentre.y,
                                  juce::Colour (0xff0a0b0c), centre.x - (hubCentre.x - centre.x),
                                  centre.y - (hubCentre.y - centre.y), true);
    g.setGradientFill (hubGrad);
    g.fillEllipse (centre.x - hubRadius, centre.y - hubRadius, hubRadius * 2.0f, hubRadius * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.70f));
    g.drawEllipse (centre.x - hubRadius, centre.y - hubRadius, hubRadius * 2.0f,
                   hubRadius * 2.0f, 0.9f);

    // Deep skirt on the primary dial only: the reference's largest control has
    // a visibly taller cone than the small trim knobs, which is part of how the
    // hierarchy reads at a glance. The extra wall is added by growing the
    // dark inner ring outward, not by scaling the whole knob.
    if (tallBody)
    {
        const float wallRadius = rimRadius * 0.90f;
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.drawEllipse (centre.x - wallRadius, centre.y - wallRadius,
                       wallRadius * 2.0f, wallRadius * 2.0f, 1.4f);
    }
}

void TubeCompLookAndFeel::drawRingDial (juce::Graphics& g, juce::Rectangle<float> bounds,
                                         juce::Point<float> centre, float outerRadius, float angle,
                                         float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    // The 670 threshold control is the largest knob on the faceplate, with a
    // printed 0-10 scale around it and no bezel. It gets extra range marks and
    // a wider sweep than the other controls because it is the primary dial.
    const float knobRadius = outerRadius * 0.50f;

    // Printed scale with numbered fence positions.
    drawPrintedScale (g, centre, knobRadius * 1.18f, outerRadius * 0.80f, 11,
                      rotaryStartAngle, rotaryEndAngle, true);

    // The threshold knob is the largest control on the panel, so it gets a
    // deeper skirt and the full turned treatment.
    drawTurnedKnob (g, centre, knobRadius, angle, lightAngleRadians, 0.90f, true);

    juce::ignoreUnused (bounds);

    if (slider.isMouseButtonDown())
    {
        const auto valueText = slider.getTextFromValue (slider.getValue());
        auto valueBounds = juce::Rectangle<float> (centre.x - 34.0f, bounds.getBottom() - 17.0f, 68.0f, 16.0f);
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRoundedRectangle (valueBounds.translated (1.0f, 1.5f), 3.0f);
        g.setColour (juce::Colour (0xff2a231b));
        g.fillRoundedRectangle (valueBounds, 3.0f);
        g.setColour (indexCream);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (valueText, valueBounds.toNearestInt(), juce::Justification::centred, 1);
    }
}

void TubeCompLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                           static_cast<float> (width), static_cast<float> (height)).reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float outerRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    float sweepStart = 0.0f, sweepEnd = 0.0f;
    getSweep (slider, rotaryStartAngle, rotaryEndAngle, sweepStart, sweepEnd);
    const float angle = sweepStart + sliderPosProportional * (sweepEnd - sweepStart);

    const bool isRingDial = std::find (ringDials.begin(), ringDials.end(), &slider) != ringDials.end();
    if (isRingDial)
    {
        drawRingDial (g, bounds, centre, outerRadius, angle, sweepStart, sweepEnd, slider);
        return;
    }
    // The 670 prints its scale on the panel and mounts a plain turned knob with
    // no bezel over it, so the control reads as a scale plus a pointer rather
    // than a self-contained dial. Keeping that split is what makes the panel
    // look like hardware rather than a software widget.
    const float bodyRadius = outerRadius * 0.62f;

    // Printed scale on the panel, outside the knob.
    drawPrintedScale (g, centre, outerRadius * 0.82f, outerRadius * 0.98f, 11,
                      sweepStart, sweepEnd, false);

    drawTurnedKnob (g, centre, bodyRadius, angle, lightAngleRadians, 0.88f, false);

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

    // The 670 uses a cream bat-handle toggle in a chrome collar. Colour, not
    // shape, carries the state: the cap is warm when the circuit is live and
    // desaturated when it is bypassed.
    const bool engaged = ! button.getToggleState(); // param semantics: state==true means bypassed
    const float collar = juce::jmin (bounds.getHeight(), 20.0f);
    const auto collarCentre = juce::Point<float> (bounds.getX() + collar * 0.5f + 3.0f,
                                                  bounds.getCentreY());

    // Chrome collar ring.
    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillEllipse (collarCentre.x - collar * 0.5f + 1.0f, collarCentre.y - collar * 0.5f + 2.0f,
                   collar, collar);
    {
        juce::ColourGradient collarGrad (juce::Colour (0xffd8cfba), collarCentre.x - collar * 0.5f,
                                           collarCentre.y - collar * 0.5f,
                                           juce::Colour (0xff5a5348), collarCentre.x + collar * 0.5f,
                                           collarCentre.y + collar * 0.5f, true);
        g.setGradientFill (collarGrad);
        g.fillEllipse (collarCentre.x - collar * 0.5f, collarCentre.y - collar * 0.5f, collar, collar);
    }

    // Bat-handle cap, tipped to one side so the state is readable at a glance.
    const float tip = collar * 0.22f * (engaged ? -1.0f : 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (juce::Line<float> (juce::Point<float> (collarCentre.x, collarCentre.y + tip * 0.4f),
                                   juce::Point<float> (collarCentre.x, collarCentre.y - collar * 0.42f + tip)),
                3.0f);
    g.setColour (engaged ? switchCream : switchCream.darker (0.42f));
    g.drawLine (juce::Line<float> (juce::Point<float> (collarCentre.x, collarCentre.y + tip * 0.4f),
                                   juce::Point<float> (collarCentre.x, collarCentre.y - collar * 0.42f + tip)),
                2.0f);

    // Legend beside the switch.
    auto textArea = bounds.withTrimmedLeft (collar + 12.0f);
    g.setColour (indexCream);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
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
    g.setColour (juce::Colour (0xff6b6a63));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (bounds.getX() + 4.0f, bounds.getY() + 1.5f, bounds.getRight() - 4.0f,
                bounds.getY() + 1.5f, 1.0f);

    // The arrow shares the box with the text in narrow cells, so reserve its
    // width explicitly and let the box own that space.
    box.setColour (juce::ComboBox::textColourId, indexCream);
    const auto arrowZone = bounds.removeFromRight (juce::jmin (bounds.getHeight(), 20.0f));
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

void TubeCompLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The default layout gives the text the full box width, so on a narrow
    // cell it runs underneath the arrow glyph. Reserve the arrow zone here and
    // allow the legend to compress, so descriptive item names such as
    // "Tube (WDF)" still fit without being truncated into something misleading.
    const int arrowWidth = juce::jmin (box.getHeight(), 20);
    label.setBounds (6, 1, juce::jmax (1, box.getWidth() - arrowWidth - 10), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setMinimumHorizontalScale (0.55f);
}

juce::Font TubeCompLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (10.0f, juce::Font::bold);
}

juce::Font TubeCompLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions (13.0f);
}
