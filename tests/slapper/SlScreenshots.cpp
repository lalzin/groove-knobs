// sl_screenshots — the Slapper editor offscreen, at 100 % and 200 %:
//   sl_screenshots [out-dir]   (default: screenshots/slapper)
#include "SlapperEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/slapper");
    out.createDirectory();
    for (float scale : { 1.0f, 2.0f })
    {
        slapper::SlapperProcessor proc;
        proc.editorScale.store (scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<slapper::SlapperEditor*> (ed.get());
        editor->snapForSnapshot();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto f = out.getChildFile ("slapper_" + juce::String ((int) (scale * 100)) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << f.getFullPathName() << "\n";
    }
    return 0;
}
