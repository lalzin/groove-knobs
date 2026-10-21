// sib_screenshots — renders the Sing It Back editor offscreen:
//   CROWD 0 / 25 / 50 / 75 / 100 %, Sugar singing and silent; every style at
//   75 %; 4 and 12 voices; the advanced panel; bypass; 200 % scale.
//   sib_screenshots [out-dir]   (default: screenshots/sing_it_back)
// Writes contact_sheet.png for review.

#include "SingItBackEditor.h"
#include <iostream>
#include "WebAssets.h"

namespace
{
void setParam (juce::RangedAudioParameter& p, float plain) { p.setValueNotifyingHost (p.convertTo0to1 (plain)); }

bool writePng (const juce::Image& img, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
}

juce::Image shoot (sib::SingItBackProcessor& proc, float scale, bool singing, int syllable, bool advanced = false, bool voices = false)
{
    proc.editorScale.store (juce::jmin (2.0f, scale));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<sib::SingItBackEditor*> (ed.get());
    editor->setPreview (singing, syllable, advanced, voices);
    editor->snapForSnapshot();
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
}

using Row = std::vector<std::pair<juce::String, juce::Image>>;

juce::Image sheetOf (const std::vector<Row>& rows)
{
    constexpr int w = 420, h = 520, pad = 12, label = 28;
    size_t cols = 0;
    for (const auto& r : rows)
        cols = std::max (cols, r.size());
    juce::Image sheet (juce::Image::RGB, (int) cols * (w + pad) + pad, (int) rows.size() * (h + pad + label) + pad, true);
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
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

// --web: visuals for lalzin.com (web-assets/sing-it-back) and, last, the
// suite visuals with the five plugins
int runWeb (const juce::File& out)
{
    sib::SingItBackProcessor proc;
    setParam (proc.getKeyParameter(), 9.0f);   // A minor
    setParam (proc.getScaleParameter(), 1.0f);
    auto at = [&] (float crowd, float scale, int syllable, int voices = 1)
    {
        setParam (proc.getCrowdParameter(), crowd);
        setParam (proc.getVoicesParameter(), (float) voices);
        return shoot (proc, scale, true, syllable);
    };
    std::vector<gkweb::Shot> shots { { "hero", at (0.75f, 3.0f, 2) }, { "25", at (0.25f, 2.0f, 1) }, { "100", at (1.0f, 2.0f, 3, 2) } };
    setParam (proc.getStyleParameter(), 1.0f); // Gospel
    shots.push_back ({ "gospel", at (0.75f, 2.0f, 4) });
    gkweb::PluginInfo info;
    info.slug = "sing-it-back";
    info.name = "Sing It Back";
    info.taglineFr = juce::String::fromUTF8 ("Une foule qui chante avec ta voix, en un knob");
    info.taglineEn = "A crowd singing along with your voice, in one knob";
    info.palette = sib::SingItBackEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/sing_it_back/Assets");
    info.framePrefix = "sugar_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "8");
    for (int i = 0; i < 33; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);

    gkweb::writeSuite (out, gkweb::suiteVisuals(), info.palette);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1"); // no network: screenshots and tests never depend on the website
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1); // no network: screenshots and tests never depend on the website
#endif
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/sing_it_back");
    outDir.createDirectory();
    sib::SingItBackProcessor proc;
    std::vector<Row> rows;
    auto add = [&] (Row& row, const juce::String& slug, const juce::String& label, bool singing, int syllable, bool advanced = false)
    {
        const auto img = shoot (proc, 1.0f, singing, syllable, advanced);
        writePng (img, outDir.getChildFile (slug + ".png"));
        row.push_back ({ label, img });
    };

    // CROWD, singing and silent
    for (bool singing : { true, false })
    {
        Row row;
        for (float crowd : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            setParam (proc.getCrowdParameter(), crowd);
            const auto pct = juce::String (juce::roundToInt (crowd * 100.0f));
            add (row, (singing ? "sing_" : "silent_") + pct, pct + " %" + (singing ? ", singing" : ", silent"), singing, 1);
        }
        rows.push_back (std::move (row));
    }
    // Sugar's poses while singing, syllable after syllable (75 %, 100 %)
    for (float crowd : { 0.2f, 0.6f, 1.0f })
    {
        Row row;
        setParam (proc.getCrowdParameter(), crowd);
        for (int syl = 0; syl < 5; ++syl)
            add (row, "pose_" + juce::String (juce::roundToInt (crowd * 100.0f)) + "_" + juce::String (syl),
                 juce::String (juce::roundToInt (crowd * 100.0f)) + " %, syllable " + juce::String (syl + 1), true, syl);
        rows.push_back (std::move (row));
    }
    // Styles at 75 %
    {
        setParam (proc.getCrowdParameter(), 0.75f);
        Row row;
        for (int st = 0; st < proc.getStyleParameter().choices.size(); ++st)
        {
            setParam (proc.getStyleParameter(), (float) st);
            add (row, "style_" + juce::String (st), proc.getStyleParameter().choices[st], true, 2);
        }
        setParam (proc.getStyleParameter(), 0.0f);
        rows.push_back (std::move (row));
    }
    // 4 / 12 voices, RÉPONSE, the advanced panel, bypass
    {
        Row row;
        setParam (proc.getCrowdParameter(), 1.0f);
        setParam (proc.getVoicesParameter(), 0.0f);
        add (row, "voices_4", "4 voices, 100 %", true, 3);
        setParam (proc.getVoicesParameter(), 2.0f);
        add (row, "voices_12", "12 voices, 100 %", true, 3);
        setParam (proc.getResponseParameter(), 2.0f);
        setParam (proc.getKeyParameter(), 9.0f);
        setParam (proc.getScaleParameter(), 1.0f);
        add (row, "response", "1 mesure, A minor: the crowd answers", false, 3);
        setParam (proc.getResponseParameter(), 0.0f);
        setParam (proc.getVoicesParameter(), 1.0f);
        setParam (proc.getCrowdParameter(), 0.5f);
        add (row, "advanced", "advanced panel", false, 0, true);
        // the voice panel, with a few voices set by hand
        setParam (proc.getPartParameter (1), 4.0f);   // 5te +
        setParam (proc.getPartParameter (4), 9.0f);   // muted
        setParam (proc.getVoiceLevelParameter (2), -6.0f);
        setParam (proc.getVoicePanParameter (3), -1.0f);
        setParam (proc.getVoiceSizeParameter (5), 0.2f);
        {
            const auto img = shoot (proc, 1.0f, true, 1, false, true);
            writePng (img, outDir.getChildFile ("voices.png"));
            row.push_back ({ "voice panel", img });
            writePng (shoot (proc, 2.0f, true, 1, false, true), outDir.getChildFile ("voices@2x.png"));
        }
        for (int k = 0; k < 12; ++k)
        {
            setParam (proc.getPartParameter (k), 0.0f);
            setParam (proc.getVoiceLevelParameter (k), 0.0f);
            setParam (proc.getVoicePanParameter (k), sib::SingItBackDsp::defaultPan (k));
            setParam (proc.getVoiceSizeParameter (k), 0.0f);
        }
        setParam (proc.getBypassParameterObject(), 1.0f);
        add (row, "bypass", "bypass", false, 0);
        setParam (proc.getBypassParameterObject(), 0.0f);
        rows.push_back (std::move (row));
    }
    writePng (shoot (proc, 2.0f, true, 2), outDir.getChildFile ("sing_50@2x.png"));
    writePng (sheetOf (rows), outDir.getChildFile ("contact_sheet.png"));
    std::cout << "screenshots written to " << outDir.getFullPathName() << std::endl;
    return 0;
}
