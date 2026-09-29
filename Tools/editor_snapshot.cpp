/*
    Offline editor snapshot: instantiates the real plugin editor, paints it to
    an offscreen image, and writes a PNG. Gives reviewable visual evidence for
    faceplate work without needing a DAW session, and keeps UI review on the
    same binary that the audio gates exercise.

    Usage: eonchild_EditorSnapshot <output.png> [width height]
*/

#include <juce_audio_utils/juce_audio_utils.h>

#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    const juce::String outputPath = argc > 1 ? juce::String::fromUTF8 (argv[1])
                                             : juce::String ("editor-snapshot.png");
    const int width = argc > 3 ? juce::String::fromUTF8 (argv[2]).getIntValue() : 1180;
    const int height = argc > 3 ? juce::String::fromUTF8 (argv[3]).getIntValue() : 820;

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    TubeCompAudioProcessor processor;
    TubeCompAudioProcessorEditor editor (processor);

    editor.setBounds (0, 0, width, height);

    // The meters run on a 30Hz timer, so give them a few ticks to settle on
    // their resting needle position before the paint pass.
    for (int i = 0; i < 4; ++i)
        juce::Thread::sleep (40);

    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);
    editor.paintEntireComponent (g, true);

    const juce::File file (outputPath);
    file.getParentDirectory().createDirectory();

    juce::PNGImageFormat pngFormat;
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        if (pngFormat.writeImageToStream (image, *stream))
        {
            std::cout << "wrote " << file.getFullPathName() << "  " << width << "x" << height << "\n";
            return 0;
        }
    }

    std::cerr << "failed to write " << file.getFullPathName() << "\n";
    return 1;
}
