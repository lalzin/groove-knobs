// BALLROOM editor screenshots: every preset (the music playing a moment), the
// ROOM panel's three tabs, a contact sheet.
//   bl_screenshots [out-dir]
//   bl_screenshots --web [web-assets]   (the website's visuals, then the suite's: gkweb)
#include "BallroomEditor.h"
#include "BallroomProcessor.h"
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

void writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

std::unique_ptr<bl::BallroomProcessor> playing (int preset)
{
    auto proc = std::make_unique<bl::BallroomProcessor>();
    proc->getPresetParameter().setValueNotifyingHost (proc->getPresetParameter().convertTo0to1 ((float) preset));
    proc->applyPreset (preset);
    proc->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (3);
    for (int k = 0; k < 60; ++k)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                b.setSample (c, i, 0.3f * (rng.nextFloat() - 0.5f));
        proc->processBlock (b, midi);
    }
    return proc;
}

juce::Image shoot (bl::BallroomProcessor& proc, float scale, int roomTab = -1)
{
    proc.editorScale.store (scale);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<bl::BallroomEditor*> (ed.get());
    editor->showRoom (roomTab);
    editor->snapForSnapshot();
    editor->warmUp (0.45); // a few twinkles on the screenshot
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
}
int presetIndex (const char* name)
{
    bl::BallroomProcessor p;
    return juce::jmax (0, p.getPresets().indexOf (name));
}

int runWeb (const juce::File& out)
{
    auto shotOf = [] (const char* preset, float scale, int tab = -1, float space = -1.0f)
    {
        auto proc = playing (presetIndex (preset));
        if (space >= 0.0f)
            proc->getAmountParameter().setValueNotifyingHost (space);
        return shoot (*proc, scale, tab);
    };
    std::vector<gkweb::Shot> shots { { "hero", shotOf ("Disco Ballroom", 3.0f, -1, 0.62f) },
                                     { "room", shotOf ("Disco Ballroom", 2.0f, 0) },
                                     { "mirror", shotOf ("Mirror Ball", 2.0f, -1, 0.8f) },
                                     { "late", shotOf ("Cathedral", 2.0f, 2) } };
    gkweb::PluginInfo info;
    info.slug = "ballroom";
    info.name = "Ballroom";
    info.taglineFr = juce::String::fromUTF8 ("La r\xc3\xa9verbe qui fait de la place \xc3\xa0 ta musique");
    info.taglineEn = "The reverb that makes room for your music";
    info.palette = bl::BallroomEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/ballroom/Assets");
    info.framePrefix = "mira_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "20");
    for (int i = 0; i < 21; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);
    // the suite (every plug-in's visuals already on disk): Ballroom is the last one in
    gkweb::writeSuite (out, gkweb::suiteVisuals(), info.palette);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1");
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/ballroom");
    dir.createDirectory();
    std::vector<juce::Image> shots;
    juce::StringArray names;
    for (int preset = 0; preset < 22; ++preset)
    {
        auto proc = playing (preset);
        const auto name = proc->getPresets()[preset].name.toLowerCase().replaceCharacters (" ", "_");
        const auto img = shoot (*proc, 1.0f);
        writePng (img, dir.getChildFile (name + ".png"));
        writePng (shoot (*proc, 2.0f), dir.getChildFile (name + "@2x.png"));
        shots.push_back (img);
        names.add (proc->getPresets()[preset].name);
    }
    {
        auto proc = playing (0);
        const char* tabs[] { "main", "early", "late" };
        for (int t = 0; t < 3; ++t)
        {
            writePng (shoot (*proc, 2.0f, t), dir.getChildFile (juce::String ("room_") + tabs[t] + "@2x.png"));
            const auto img = shoot (*proc, 1.0f, t);
            writePng (img, dir.getChildFile (juce::String ("room_") + tabs[t] + ".png"));
            shots.push_back (img);
            names.add (juce::String ("ROOM - ") + tabs[t]);
        }
    }
    const int w = shots[0].getWidth(), h = shots[0].getHeight(), pad = 10, label = 22, cols = 5;
    const int rows = ((int) shots.size() + cols - 1) / cols;
    juce::Image sheet (juce::Image::RGB, cols * (w + pad) + pad, rows * (h + pad + label) + pad, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff0a0a0a));
    for (size_t i = 0; i < shots.size(); ++i)
    {
        const int x = pad + (int) (i % cols) * (w + pad), y = pad + (int) (i / cols) * (h + pad + label);
        g.setColour (juce::Colours::white);
        g.drawText (names[(int) i], x, y, w, label, juce::Justification::centredLeft);
        g.drawImageAt (shots[i], x, y + label);
    }
    writePng (sheet, dir.getChildFile ("contact_sheet.png"));
    std::cout << "screenshots written to " << dir.getFullPathName() << std::endl;
    return 0;
}
