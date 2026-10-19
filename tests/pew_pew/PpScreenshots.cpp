// pp_screenshots — renders the Pew Pew editor offscreen:
//   every preset at 0 / 50 / 100 % with ROOM and ECHO at 0 and 100 %, at 100 %
//   and 200 % scale, bypassed, plus a hit at three moments for each knob zone.
//   pp_screenshots [out-dir]   (default: screenshots/pew_pew)
// Writes contact_sheet.png and hits_sheet.png for review.

#include "PewPewEditor.h"
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

juce::Image shoot (pp::PewPewProcessor& proc, float scale, float hitSecondsAgo = -1.0f)
{
    proc.editorScale.store (juce::jmin (2.0f, scale));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<pp::PewPewEditor*> (ed.get());
    editor->setHitOverride (hitSecondsAgo);
    editor->snapForSnapshot();
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, juce::jmax (1.0f, scale / 2.0f));
}

juce::Image sheetOf (const std::vector<std::vector<std::pair<juce::String, juce::Image>>>& rows)
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
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots/pew_pew");
    outDir.createDirectory();

    pp::PewPewProcessor proc;
    const auto& presets = proc.getPresets();
    std::vector<std::vector<std::pair<juce::String, juce::Image>>> rows;
    for (int p = 0; p < presets.size(); ++p)
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        const auto slug = presets[p].name.toLowerCase().replaceCharacter (' ', '_');
        setParam (proc.getPresetParameter(), (float) p);
        for (int variant = 0; variant < 7; ++variant)
        {
            const bool bypass = variant == 6;
            const float amount = variant < 3 ? 0.5f * (float) variant : variant < 6 ? 0.5f * (float) (variant - 3) : 0.6f;
            const float fx = variant >= 3 && variant < 6 ? 1.0f : 0.0f;
            setParam (proc.getAmountParameter(), amount);
            setParam (proc.getRoomParameter(), fx);
            setParam (proc.getEchoParameter(), fx);
            setParam (proc.getBypassParameterObject(), bypass ? 1.0f : 0.0f);
            const auto tag = bypass ? juce::String ("bypass")
                                    : juce::String (juce::roundToInt (amount * 100.0f)) + (fx > 0.0f ? "_fx100" : "");
            for (float scale : { 1.0f, 2.0f })
            {
                const auto img = shoot (proc, scale, bypass ? -1.0f : 0.04f);
                writePng (img, outDir.getChildFile (slug + "_" + tag + (scale > 1.5f ? "@2x" : "") + ".png"));
                if (scale < 1.5f)
                    row.push_back ({ presets[p].name + " - " + (bypass ? juce::String ("bypass")
                                                                       : juce::String (juce::roundToInt (amount * 100.0f)) + " %" + (fx > 0.0f ? ", ROOM / ECHO 100 %" : "")),
                                     img });
            }
        }
        rows.push_back (std::move (row));
    }
    writePng (sheetOf (rows), outDir.getChildFile ("contact_sheet.png"));

    // A hit at three moments, for each knob zone (Laser Pew, ROOM / ECHO 60 %)
    std::vector<std::vector<std::pair<juce::String, juce::Image>>> hitRows;
    setParam (proc.getPresetParameter(), 0.0f);
    setParam (proc.getBypassParameterObject(), 0.0f);
    setParam (proc.getRoomParameter(), 0.6f);
    setParam (proc.getEchoParameter(), 0.6f);
    for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        std::vector<std::pair<juce::String, juce::Image>> row;
        setParam (proc.getAmountParameter(), amount);
        for (float ago : { 0.03f, 0.3f, 1.5f })
        {
            const auto img = shoot (proc, 1.0f, ago);
            row.push_back ({ juce::String (juce::roundToInt (amount * 100.0f)) + " %, " + juce::String (juce::roundToInt (ago * 1000.0f)) + " ms after the hit", img });
            writePng (img, outDir.getChildFile ("hit_" + juce::String (juce::roundToInt (amount * 100.0f)) + "_" + juce::String (juce::roundToInt (ago * 1000.0f)) + "ms.png"));
        }
        hitRows.push_back (std::move (row));
    }
    writePng (sheetOf (hitRows), outDir.getChildFile ("hits_sheet.png"));
    std::cout << "screenshots written to " << outDir.getFullPathName() << std::endl;
    return 0;
}
