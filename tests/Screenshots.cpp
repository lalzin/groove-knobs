// ff_screenshots — renders the editor offscreen for every preset at
// 0 / 50 / 72 / 80 / 100 % and bypassed, at 100 % and 200 % scale.
//   ff_screenshots [out-dir]   (default: screenshots/)
// Also writes contact_sheet.png (all 100 % renders on one image).

#include "FrenchFilterEditor.h"
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
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots");
    outDir.createDirectory();

    ff::FrenchFilterProcessor proc;
    const auto& presets = proc.getPresets();
    // Knob positions: LP 100 %, LP 50 %, centre, HP 50 %, HP 100 %
    const float amounts[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    constexpr int base = 420, baseH = 520;

    struct Shot { juce::String label; juce::Image image; };
    std::vector<std::vector<Shot>> sheet;

    for (int p = 0; p < presets.size(); ++p)
    {
        std::vector<Shot> row;
        for (int variant = 0; variant < 7; ++variant)
        {
            const bool bypass = variant == 5, drop = variant == 6;
            const float amount = bypass ? 0.3f : drop ? 0.5f : amounts[variant];
            setParam (proc.getPresetParameter(), (float) p);
            setParam (proc.getAmountParameter(), amount);
            setParam (proc.getBypassParameterObject(), bypass ? 1.0f : 0.0f);

            for (float scale : { 1.0f, 2.0f })
            {
                proc.editorScale.store (scale);
                std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
                auto* editor = dynamic_cast<gk::OneKnobEditor*> (ed.get());
                if (drop)
                    editor->getMascot().triggerDrop();
                editor->snapForSnapshot();
                const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

                const auto name = presets[p].name.toLowerCase().replaceCharacter (' ', '_') + "_"
                                  + (bypass ? juce::String ("bypass") : drop ? juce::String ("drop")
                                     : amount < 0.5f ? "lp" + juce::String (juce::roundToInt ((0.5f - amount) * 200.0f))
                                     : amount > 0.5f ? "hp" + juce::String (juce::roundToInt ((amount - 0.5f) * 200.0f)) : juce::String ("centre"))
                                  + (scale > 1.5f ? "@2x" : "") + ".png";
                if (! writePng (img, outDir.getChildFile (name)))
                {
                    std::cerr << "cannot write " << name << "\n";
                    return 1;
                }
                std::cout << "wrote " << name << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
                if (scale < 1.5f)
                    row.push_back ({ bypass ? juce::String ("bypass") : drop ? juce::String ("drop!") : proc.getAmountParameter().getText (amount, 32), img });
            }
        }
        sheet.push_back (std::move (row));
    }

    // Contact sheet: one row per preset
    const int cols = (int) sheet.front().size(), rows = (int) sheet.size(), pad = 12, label = 28;
    juce::Image contact (juce::Image::RGB, cols * (base + pad) + pad, rows * (baseH + pad + label) + pad, true);
    {
        juce::Graphics g (contact);
        g.fillAll (juce::Colour (0xff101012));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const int x = pad + c * (base + pad), y = pad + r * (baseH + pad + label);
                g.setColour (juce::Colours::white);
                g.setFont (gk::GrooveFonts::font (18.0f));
                g.drawText (presets[r].name + " - " + sheet[(size_t) r][(size_t) c].label, x, y, base, label, juce::Justification::centredLeft);
                g.drawImageAt (sheet[(size_t) r][(size_t) c].image, x, y + label);
            }
    }
    writePng (contact, outDir.getChildFile ("contact_sheet.png"));
    std::cout << "wrote contact_sheet.png\n";
    return 0;
}
