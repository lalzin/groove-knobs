// HEAD SPIN editor screenshots: every preset (the sound somewhere on its
// path), at 100 % and 200 %, and a contact sheet.
//   hs_screenshots [out-dir]
//   hs_screenshots --web [web-assets]   (the website's visuals: gkweb::writePlugin)
#include "HeadSpinEditor.h"
#include "HeadSpinProcessor.h"
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

juce::Image shoot (hs::HeadSpinProcessor& proc, float scale)
{
    proc.editorScale.store (scale);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<hs::HeadSpinEditor*> (ed.get());
    editor->snapView();
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
}
/** A preset playing for 0.7 s (the sound somewhere on its path), its editor. */
juce::Image presetShot (const char* preset, float scale, const std::vector<headspin::Point>* custom = nullptr)
{
    hs::HeadSpinProcessor proc;
    const int i = proc.getPresets().indexOf (preset);
    proc.getPresetParameter().setValueNotifyingHost (proc.getPresetParameter().convertTo0to1 ((float) juce::jmax (0, i)));
    proc.applyPreset (juce::jmax (0, i));
    if (custom != nullptr)
    {
        proc.setCustomShape (*custom);
        proc.getKnob (hs::HeadSpinDsp::shapeKnob).setValueNotifyingHost (proc.getKnob (hs::HeadSpinDsp::shapeKnob).convertTo0to1 (6.0f));
    }
    proc.prepareToPlay (48000.0, 512);
    proc.updateShapeNow();
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (3);
    for (int k = 0; k < 66; ++k)
    {
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 512; ++n)
                b.setSample (c, n, 0.3f * (rng.nextFloat() - 0.5f));
        proc.processBlock (b, midi);
    }
    return shoot (proc, scale);
}

std::vector<headspin::Point> heart()
{
    std::vector<headspin::Point> h;
    for (int k = 0; k < 200; ++k)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * k / 200.0;
        h.push_back ({ 0.075 * 16.0 * std::pow (std::sin (t), 3.0),
                       0.075 * (13.0 * std::cos (t) - 5.0 * std::cos (2.0 * t) - 2.0 * std::cos (3.0 * t) - std::cos (4.0 * t)) + 0.1 });
    }
    return h;
}

int runWeb (const juce::File& out)
{
    const auto h = heart();
    std::vector<gkweb::Shot> shots { { "hero", presetShot ("Merry-Go-Round", 3.0f) },
                                     { "eight", presetShot ("Figure Eight", 2.0f) },
                                     { "custom", presetShot ("Merry-Go-Round", 2.0f, &h) },
                                     { "behind", presetShot ("Behind You", 2.0f) } };
    gkweb::PluginInfo info;
    info.slug = "head-spin";
    info.name = "Head Spin";
    info.taglineFr = juce::String::fromUTF8 ("Le son tourne autour de ta t\xc3\xaate");
    info.taglineEn = "The sound goes round your head";
    info.palette = hs::HeadSpinEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/headspin/Assets");
    info.framePrefix = "kiko_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "5");
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
    setenv ("GK_NO_UPDATE_CHECK", "1", 1);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/headspin");
    dir.createDirectory();
    const double sr = 48000.0;
    std::vector<juce::Image> shots;
    juce::StringArray names;
    for (int preset = 0; preset < 7; ++preset)
    {
        hs::HeadSpinProcessor proc;
        proc.getPresetParameter().setValueNotifyingHost (proc.getPresetParameter().convertTo0to1 ((float) preset));
        proc.applyPreset (preset);
        proc.prepareToPlay (sr, 512);
        proc.updateShapeNow();
        // play 0.7 s of noise: the sound moves along its path
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        for (int k = 0; k < (int) (0.7 * sr / 512); ++k)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    b.setSample (c, i, 0.3f * (rng.nextFloat() - 0.5f));
            proc.processBlock (b, midi);
        }
        const auto name = proc.getPresets()[preset].name.toLowerCase().replaceCharacters (" -", "__");
        for (float scale : { 1.0f, 2.0f })
        {
            const auto img = shoot (proc, scale);
            writePng (img, dir.getChildFile (name + (scale > 1.5f ? "@2x" : "") + ".png"));
            if (scale < 1.5f)
            {
                shots.push_back (img);
                names.add (proc.getPresets()[preset].name);
            }
        }
    }
    // a custom shape: a heart drawn round Kiko
    {
        hs::HeadSpinProcessor proc;
        std::vector<headspin::Point> heart;
        for (int k = 0; k < 200; ++k)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * k / 200.0;
            heart.push_back ({ 0.075 * 16.0 * std::pow (std::sin (t), 3.0),
                               0.075 * (13.0 * std::cos (t) - 5.0 * std::cos (2.0 * t) - 2.0 * std::cos (3.0 * t) - std::cos (4.0 * t)) + 0.1 });
        }
        proc.setCustomShape (heart);
        proc.getKnob (hs::HeadSpinDsp::shapeKnob).setValueNotifyingHost (proc.getKnob (hs::HeadSpinDsp::shapeKnob).convertTo0to1 (6.0f));
        proc.prepareToPlay (sr, 512);
        proc.updateShapeNow();
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        for (int k = 0; k < 40; ++k)
        {
            b.clear();
            b.setSample (0, 0, 0.5f);
            proc.processBlock (b, midi);
        }
        const auto img = shoot (proc, 1.0f);
        writePng (img, dir.getChildFile ("custom_heart.png"));
        writePng (shoot (proc, 2.0f), dir.getChildFile ("custom_heart@2x.png"));
        shots.push_back (img);
        names.add ("Custom (a heart drawn)");
    }
    // contact sheet: 4 x 2
    const int w = shots[0].getWidth(), h = shots[0].getHeight(), pad = 10, label = 22;
    juce::Image sheet (juce::Image::RGB, 4 * (w + pad) + pad, 2 * (h + pad + label) + pad, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff0a0a0a));
    for (size_t i = 0; i < shots.size(); ++i)
    {
        const int x = pad + (int) (i % 4) * (w + pad), y = pad + (int) (i / 4) * (h + pad + label);
        g.setColour (juce::Colours::white);
        g.drawText (names[(int) i], x, y, w, label, juce::Justification::centredLeft);
        g.drawImageAt (shots[i], x, y + label);
    }
    writePng (sheet, dir.getChildFile ("contact_sheet.png"));
    std::cout << "screenshots written to " << dir.getFullPathName() << std::endl;
    return 0;
}
