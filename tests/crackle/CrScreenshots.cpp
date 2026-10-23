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
    for (int shot = 0; shot < 4; ++shot)
    {
        const float scale = shot == 1 ? 2.0f : 1.0f;
        crackle::CrackleProcessor proc;
        if (shot == 3)
        {
            // CRUSH in action: Broken Sampler, a second of noise through it first
            const int bs = proc.getPresets().indexOf ("Broken Sampler");
            proc.getPresetParameter().setValueNotifyingHost (proc.getPresetParameter().convertTo0to1 ((float) bs));
            proc.applyPreset (bs);
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            proc.setBusesLayout (layout);
            proc.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            juce::Random r (1);
            for (int n = 0; n < 94; ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        b.setSample (ch, i, 0.3f * (r.nextFloat() * 2.0f - 1.0f));
                proc.processBlock (b, m);
            }
        }
        proc.editorScale.store (scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<crackle::CrackleEditor*> (ed.get());
        editor->setPanelOpen (shot == 2);
        editor->snapForSnapshot();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto f = out.getChildFile ("crackle_" + juce::String ((int) (scale * 100)) + (shot == 2 ? "_panel" : shot == 3 ? "_crush" : "") + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << f.getFullPathName() << "\n";
    }
    return 0;
}
