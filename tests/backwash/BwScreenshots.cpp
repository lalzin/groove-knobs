// bw_screenshots — renders the Backwash editor offscreen:
//   every preset at 0 / 25 / 50 / 75 / 100 % with its default chips, bypassed,
//   at 100 % and 200 % scale, plus the breath at three moments for the four
//   LONG / SHIMMER combinations.
//   bw_screenshots [out-dir]   (default: screenshots/backwash)
// Writes contact_sheet.png and breath_sheet.png for review.

#include "BackwashEditor.h"
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

juce::Image shoot (bw::BackwashProcessor& proc, float scale, float inhale = 0.5f)
{
    proc.editorScale.store (juce::jmin (2.0f, scale)); // the editor caps its zoom at 200 %
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<bw::BackwashEditor*> (ed.get());
    editor->setInhaleOverride (proc.getAmountParameter().getValue() > 0.0f ? inhale : 0.0f);
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
    for (auto d = juce::File::getCurrentWorkingDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("plugins").isDirectory() && d.getChildFile ("modules/groove_core").isDirectory())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

// --web: visuals for lalzin.com (run after ff_ and bt_screenshots --web for
// the suite visuals)
int runWeb (const juce::File& out)
{
    bw::BackwashProcessor proc;
    proc.setCurrentProgram (0);
    // a captured memory: no "empty" warning dots on the marketing shots
    proc.getBackwashDsp().getTelemetry().liveMemoryReady.store (true);
    proc.getBackwashDsp().getTelemetry().liveMemoryComplete.store (true);
    auto at = [&] (float amount, float scale, float inhale)
    {
        setParam (proc.getAmountParameter(), amount);
        return shoot (proc, scale, inhale);
    };
    setParam (proc.getShimmerParameter(), 1.0f);
    auto hero = at (1.0f, 3.0f, 0.95f);
    setParam (proc.getShimmerParameter(), 0.0f);
    std::vector<gkweb::Shot> shots { { "hero", hero }, { "50", at (0.5f, 2.0f, 0.6f) }, { "75", at (0.75f, 2.0f, 0.8f) } };
    gkweb::PluginInfo info;
    info.slug = "backwash";
    info.name = "Backwash";
    info.taglineFr = juce::String::fromUTF8 ("La reverb invers\xc3\xa9""e cal\xc3\xa9""e sur le tempo, en un knob");
    info.taglineEn = "Tempo-synced reverse reverb, in one knob";
    info.palette = bw::BackwashEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/backwash/Assets");
    info.framePrefix = "swell_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "11");
    for (int i = 0; i < 12; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);

    auto suitePalette = info.palette;
    gkweb::writeSuite (out, { { "french-filter", "" }, { "boogie-trio", "" }, { "backwash", "" } }, suitePalette);
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
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/backwash");
    outDir.createDirectory();

    bw::BackwashProcessor proc;
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

    // The breath at three moments (start, middle, just before the hit), for
    // each LONG / SHIMMER combination (Snare Swell at 75 %)
    std::vector<std::vector<std::pair<juce::String, juce::Image>>> chipRows;
    setParam (proc.getBypassParameterObject(), 0.0f);
    proc.setCurrentProgram (0);
    setParam (proc.getAmountParameter(), 0.75f);
    for (int combo = 0; combo < 4; ++combo)
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        setParam (proc.getLongParameter(), (float) (combo & 1));
        setParam (proc.getShimmerParameter(), (float) (combo >> 1));
        const juce::String chips = juce::String ("long ") + ((combo & 1) ? "on" : "off") + ", shimmer " + ((combo >> 1) ? "on" : "off");
        for (float inhale : { 0.1f, 0.5f, 0.95f })
        {
            const auto img = shoot (proc, 1.0f, inhale);
            const auto moment = juce::String (inhale < 0.3f ? "start" : inhale < 0.7f ? "middle" : "before hit");
            writePng (img, outDir.getChildFile ("breath_" + moment.replaceCharacter (' ', '_') + "_l" + juce::String (combo & 1) + "_s"
                                                + juce::String (combo >> 1) + ".png"));
            row.push_back ({ moment + " - " + chips, img });
        }
        chipRows.push_back (std::move (row));
    }
    // The highest wave (100 %, LONG + SHIMMER, deep breath), then the LIVE
    // and LOCK modes
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        setParam (proc.getAmountParameter(), 1.0f);
        setParam (proc.getLongParameter(), 1.0f);
        setParam (proc.getShimmerParameter(), 1.0f);
        for (float inhale : { 0.1f, 0.95f })
        {
            const auto img = shoot (proc, 2.0f, inhale);
            writePng (img, outDir.getChildFile (juce::String ("max_wave_") + (inhale > 0.5f ? "before_hit" : "start") + "@2x.png"));
            row.push_back ({ juce::String ("100 %, long + shimmer, breath ") + juce::String (inhale), shoot (proc, 1.0f, inhale) });
        }
        setParam (proc.getLiveParameter(), 1.0f);
        row.push_back ({ "LIVE on", shoot (proc, 1.0f, 0.5f) });
        writePng (shoot (proc, 2.0f, 0.5f), outDir.getChildFile ("live_on@2x.png"));
        setParam (proc.getLockParameter(), 1.0f);
        row.push_back ({ "LIVE + LOCK", shoot (proc, 1.0f, 0.5f) });
        writePng (shoot (proc, 2.0f, 0.5f), outDir.getChildFile ("live_lock@2x.png"));
        // the longest selector labels
        setParam (proc.getSwellLengthParameter(), 1.0f);
        setParam (proc.getHitsParameter(), 5.0f);
        row.push_back ({ "1/4 BEAT, 1 / 2 BARS", shoot (proc, 1.0f, 0.5f) });
        writePng (shoot (proc, 2.0f, 0.5f), outDir.getChildFile ("selectors_long@2x.png"));
        setParam (proc.getSwellLengthParameter(), 0.0f);
        setParam (proc.getHitsParameter(), 0.0f);
        setParam (proc.getLiveParameter(), 0.0f);
        setParam (proc.getLockParameter(), 0.0f);
        chipRows.push_back (std::move (row));
    }
    writePng (sheetOf (chipRows), outDir.getChildFile ("breath_sheet.png"));
    std::cout << "screenshots written to " << outDir.getFullPathName() << std::endl;
    return 0;
}
