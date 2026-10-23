// cr_screenshots — the Crackle editor offscreen, at 100 % and 200 %:
//   cr_screenshots [out-dir]   (default: screenshots/crackle)
//   cr_screenshots --web [dir] visuals for lalzin.com (web-assets/crackle)
#include "CrackleEditor.h"
#include "WebAssets.h"
#include <iostream>

namespace
{
juce::File repoRoot()
{
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

// --web: visuals for lalzin.com (web-assets/crackle): the hero, the EFFECTS panel, two presets
int runWeb (const juce::File& out)
{
    auto shot = [] (const char* preset, float scale, bool panel)
    {
        crackle::CrackleProcessor proc;
        const int i = proc.getPresets().indexOf (preset);
        proc.getPresetParameter().setValueNotifyingHost (proc.getPresetParameter().convertTo0to1 ((float) i));
        proc.applyPreset (i);
        proc.editorScale.store (juce::jmin (2.0f, scale));
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<crackle::CrackleEditor*> (ed.get());
        editor->setPanelOpen (panel);
        editor->snapForSnapshot();
        return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
    };
    std::vector<gkweb::Shot> shots { { "hero", shot ("Vinyl Sunday", 3.0f, false) },
                                     { "effects", shot ("Cassette Dream", 2.0f, true) },
                                     { "radio", shot ("AM Radio Ghost", 2.0f, false) },
                                     { "crushed", shot ("Broken Sampler", 2.0f, false) } };
    gkweb::PluginInfo info;
    info.slug = "crackle";
    info.name = "Crackle";
    info.taglineFr = "Le grain lo-fi, en un knob";
    info.taglineEn = "Lo-fi grain, in one knob";
    info.palette = crackle::CrackleEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/crackle/Assets");
    info.framePrefix = "hiss_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "6");
    for (int i = 0; i < 16; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1"); // no network: screenshots never depend on the website
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1); // no network: screenshots never depend on the website
#endif
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
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
