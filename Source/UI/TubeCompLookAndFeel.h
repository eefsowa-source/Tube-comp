#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Hardware-panel look, styled after Fairchild-670-class hybrid tube gear:
    dark brushed-metal chassis, chrome-bezeled black knobs with a single
    white index dot, and small toggle-style switches with LED indicators.
*/
class TubeCompLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TubeCompLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH,
                        juce::ComboBox&) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
