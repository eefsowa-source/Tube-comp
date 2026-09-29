#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Hardware-panel look, styled after Fairchild-670-class hybrid tube gear:
    oxblood-anodised faceplate, black knurled knobs with white index dots,
    large engraved ring dials, mini lever-style rotary switches with etched
    position fences, and toggle switches with LED indicators.

    The surface is an original EON design that borrows the era's visual
    grammar (ring dials, lever switches, cream legends) without reproducing a
    commercial product's faceplate, logo, or artwork.
*/
class TubeCompLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TubeCompLookAndFeel();

    /** Ringed "large dial" control used for THRESHOLD, with an engraved
        0..5 position fence printed around the bezel. */
    void setRingDialStyle (juce::Slider* slider, bool shouldBeRingDial);

    /** Sets the printed sweep for a control. Hardware of this era uses a
        partial arc with the gap at the bottom, not a full revolution, and the
        scale printing has to follow the same arc as the pointer. */
    void setPrintedSweep (juce::Slider* slider, float startDegrees, float endDegrees);

    void drawRingDial (juce::Graphics&, juce::Rectangle<float> bounds,
                       juce::Point<float> centre, float outerRadius, float angle,
                       float rotaryStartAngle, float rotaryEndAngle, juce::Slider&);

    /** Resolves the sweep for a slider, falling back to the host-provided
        rotary angles when no printed sweep was registered. */
    void getSweep (const juce::Slider&, float rotaryStartAngle, float rotaryEndAngle,
                   float& startRadians, float& endRadians) const;

    void drawPrintedScale (juce::Graphics&, juce::Point<float> centre, float innerRadius,
                           float outerRadius, int numFences, float startRadians,
                           float endRadians, bool numbered);

    /** Draws a turned cylindrical knob: cast shadow, rim bevel, concentric body
        with a top-lit dome, knurled grip, and an engraved index groove. The 670's
        controls read as solid hardware because the light comes from one
        direction; every layer here agrees with that single light.
        @param lightAngle  direction the light arrives from, in radians, measured
                          the same way as the slider angles (0 = right, growing
                          anticlockwise because JUCE angles run that way).
    */
    void drawTurnedKnob (juce::Graphics&, juce::Point<float> centre, float radius,
                         float pointerAngle, float lightAngle, float rimRatio,
                         bool tallBody);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH,
                        juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
