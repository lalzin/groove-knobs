// sl_screenshots — the Slapper editor offscreen:
//   sl_screenshots [out-dir]       100 % and 200 % (default: screenshots/slapper)
//   sl_screenshots --web [dir]     visuals for lalzin.com (web-assets/slapper)
#include "SlapperEditor.h"
#include "WebAssets.h"
#include <iostream>

namespace
{
void setParam (juce::RangedAudioParameter& p, float plain) { p.setValueNotifyingHost (p.convertTo0to1 (plain)); }

juce::Image shoot (slapper::SlapperProcessor& proc, float scale)
{
    proc.editorScale.store (juce::jmin (2.0f, scale));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<slapper::SlapperEditor*> (ed.get());
    editor->snapForSnapshot();
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
}

juce::File repoRoot()
{
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

// --web: the hero on Thumb Party (the string), then a synth of each kind
int runWeb (const juce::File& out)
{
    slapper::SlapperProcessor proc;
    auto onPreset = [&] (const char* name, float scale)
    {
        setParam (proc.getPresetParameter(), (float) proc.getPresets().indexOf (name));
        proc.applyPreset (proc.getPresets().indexOf (name));
        return shoot (proc, scale);
    };
    std::vector<gkweb::Shot> shots { { "hero", onPreset ("Thumb Party", 3.0f) },
                                     { "analog", onPreset ("Mothership", 2.0f) },
                                     { "fm", onPreset ("Solid FM", 2.0f) },
                                     { "pulse", onPreset ("Roller Disco", 2.0f) } };
    gkweb::PluginInfo info;
    info.slug = "slapper";
    info.name = "Slapper";
    info.taglineFr = "La basse funk, en un knob";
    info.taglineEn = "The funk bass, in one knob";
    info.palette = slapper::SlapperEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/slapper/Assets");
    info.framePrefix = "pluck_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "4");
    for (int i = 0; i < 10; ++i)
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
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/slapper");
    out.createDirectory();
    for (int shot = 0; shot < 3; ++shot)
    {
        const float scale = shot == 1 ? 2.0f : 1.0f;
        slapper::SlapperProcessor proc;
        proc.editorScale.store (scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<slapper::SlapperEditor*> (ed.get());
        editor->setPanelOpen (shot == 2);
        editor->snapForSnapshot();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
        const auto f = out.getChildFile ("slapper_" + juce::String ((int) (scale * 100)) + (shot == 2 ? "_panel" : "") + ".png");
        gkweb::writePng (img, f);
        std::cout << f.getFullPathName() << "\n";
    }
    return 0;
}
