// cr_screenshots — the Crackle editor offscreen, at 100 % and 200 %:
//   cr_screenshots [out-dir]   (default: screenshots/crackle)
#include "CrackleEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1"); // no network: screenshots never depend on the website
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1); // no network: screenshots never depend on the website
#endif
    juce::ScopedJuceInitialiser_GUI init;
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/crackle");
    out.createDirectory();
    for (int shot = 0; shot < 3; ++shot)
    {
        const float scale = shot == 1 ? 2.0f : 1.0f;
        crackle::CrackleProcessor proc;
        proc.editorScale.store (scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<crackle::CrackleEditor*> (ed.get());
        editor->setPanelOpen (shot == 2);
        editor->snapForSnapshot();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto f = out.getChildFile ("crackle_" + juce::String ((int) (scale * 100)) + (shot == 2 ? "_panel" : "") + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << f.getFullPathName() << "\n";
    }
    return 0;
}
