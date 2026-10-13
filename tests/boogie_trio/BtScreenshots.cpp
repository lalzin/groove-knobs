// bt_screenshots — renders the Boogie Trio editor offscreen:
//   every preset at 0 / 25 / 50 / 75 / 100 % with its default chips, bypassed,
//   at 100 % and 200 % scale, plus the four VINTAGE / WIDE combinations.
//   bt_screenshots [out-dir]   (default: screenshots/boogie_trio)
// Writes contact_sheet.png and chips_sheet.png for review.

#include "BoogieTrioEditor.h"
#include "WebAssets.h"
#include <iostream>

namespace
{
void setParam (juce::RangedAudioParameter& p, float plain) { p.setValueNotifyingHost (p.convertTo0to1 (plain)); }

bool writePng (const juce::Image& img, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
}

juce::Image shoot (bt::BoogieTrioProcessor& proc, float scale)
{
    proc.editorScale.store (juce::jmin (2.0f, scale)); // the editor caps its zoom at 200 %
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<gk::OneKnobEditor*> (ed.get());
    editor->snapForSnapshot();
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
}

juce::Image sheetOf (const std::vector<std::vector<std::pair<juce::String, juce::Image>>>& rows)
{
    constexpr int w = 420, h = 520, pad = 12, label = 28;
    const int cols = (int) rows.front().size();
    juce::Image sheet (juce::Image::RGB, cols * (w + pad) + pad, (int) rows.size() * (h + pad + label) + pad, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff101012));
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t c = 0; c < rows[r].size(); ++c)
        {
            const int x = pad + (int) c * (w + pad), y = pad + (int) r * (h + pad + label);
            g.setColour (juce::Colours::white);
            g.setFont (gk::GrooveFonts::font (18.0f));
            g.drawText (rows[r][c].first, x, y, w, label, juce::Justification::centredLeft);
            g.drawImageAt (rows[r][c].second, x, y + label);
        }
    return sheet;
}
juce::File repoRoot()
{
    // the tools run from the repo root or from build/: walk up to the repo
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

int runWeb (const juce::File& out)
{
    bt::BoogieTrioProcessor proc;
    proc.setCurrentProgram (0);
    auto at = [&] (float amount, float scale)
    {
        setParam (proc.getAmountParameter(), amount);
        return shoot (proc, scale);
    };
    std::vector<gkweb::Shot> shots { { "hero", at (0.75f, 3.0f) }, { "25", at (0.25f, 2.0f) }, { "100", at (1.0f, 2.0f) } };
    gkweb::PluginInfo info;
    info.slug = "boogie-trio";
    info.name = "Boogie Trio";
    info.taglineFr = juce::String::fromUTF8 ("Le chorus triple des string machines 70s, en un knob");
    info.taglineEn = "A 70s string-machine triple chorus, in one knob";
    info.palette = bt::BoogieTrioEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/boogie_trio/Assets");
    info.framePrefix = "velvet_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "0");
    for (int i = 0; i < 11; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--web")
    {
        juce::ScopedJuceInitialiser_GUI webInit;
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/boogie_trio");
    outDir.createDirectory();

    bt::BoogieTrioProcessor proc;
    const auto& presets = proc.getPresets();
    const float amounts[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::vector<std::vector<std::pair<juce::String, juce::Image>>> rows;

    for (int p = 0; p < presets.size(); ++p)
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        const auto slug = presets[p].name.toLowerCase().replaceCharacter (' ', '_');
        for (int variant = 0; variant < 6; ++variant)
        {
            const bool bypass = variant == 5;
            const float amount = bypass ? 0.75f : amounts[variant];
            setParam (proc.getPresetParameter(), (float) p);
            proc.setCurrentProgram (p); // applies the preset's chip defaults
            setParam (proc.getAmountParameter(), amount);
            setParam (proc.getBypassParameterObject(), bypass ? 1.0f : 0.0f);
            const auto tag = bypass ? juce::String ("bypass") : juce::String (juce::roundToInt (amount * 100.0f));
            for (float scale : { 1.0f, 2.0f })
            {
                const auto img = shoot (proc, scale);
                writePng (img, outDir.getChildFile (slug + "_" + tag + (scale > 1.5f ? "@2x" : "") + ".png"));
                if (scale < 1.5f)
                    row.push_back ({ presets[p].name + " - " + (bypass ? juce::String ("bypass") : tag + " %"), img });
            }
        }
        rows.push_back (std::move (row));
    }
    writePng (sheetOf (rows), outDir.getChildFile ("contact_sheet.png"));

    // Chip combinations (Disco Strings at 75 % and 100 %)
    std::vector<std::vector<std::pair<juce::String, juce::Image>>> chipRows;
    setParam (proc.getBypassParameterObject(), 0.0f);
    proc.setCurrentProgram (0);
    for (float amount : { 0.75f, 1.0f })
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        for (int combo = 0; combo < 4; ++combo)
        {
            setParam (proc.getAmountParameter(), amount);
            setParam (proc.getVintageParameter(), (float) (combo & 1));
            setParam (proc.getWideParameter(), (float) (combo >> 1));
            const auto img = shoot (proc, 1.0f);
            const juce::String name = juce::String ("vintage ") + ((combo & 1) ? "on" : "off") + ", wide " + ((combo >> 1) ? "on" : "off");
            writePng (img, outDir.getChildFile ("chips_" + juce::String (juce::roundToInt (amount * 100.0f)) + "_v" + juce::String (combo & 1)
                                                + "_w" + juce::String (combo >> 1) + ".png"));
            row.push_back ({ juce::String (juce::roundToInt (amount * 100.0f)) + " % - " + name, img });
        }
        chipRows.push_back (std::move (row));
    }
    writePng (sheetOf (chipRows), outDir.getChildFile ("chips_sheet.png"));
    std::cout << "screenshots written to " << outDir.getFullPathName() << std::endl;
    return 0;
}
