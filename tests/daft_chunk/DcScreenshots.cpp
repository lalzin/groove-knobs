// DAFT CHUNK editor screenshots: empty, a drum loop sliced (TRANSITOIRES,
// GRILLE 1/16, MIXTE), a slice playing; at 100 % and 200 %.
//   dc_screenshots [out-dir] [sample-file]
//   dc_screenshots --web [web-assets]   (the website's visuals, then the suite's: gkweb)
#include "DaftChunkEditor.h"
#include "DaftChunkProcessor.h"
#include "../daft_chunk/DcSignals.h"
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

juce::Image shoot (daftchunk::DaftChunkProcessor& proc, float scale, int page = 0, double zoom = 1.0)
{
    proc.editorScale.store (scale);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<daftchunk::DaftChunkEditor*> (ed.get());
    editor->showPage (page);
    editor->snapView();
    if (zoom > 1.0)
        editor->zoomWaveform (zoom, 0.27f);
    return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
}

std::unique_ptr<daftchunk::DaftChunkProcessor> withSample (const juce::String& file)
{
    auto p = std::make_unique<daftchunk::DaftChunkProcessor>();
    p->prepareToPlay (44100.0, 512);
    if (file.isNotEmpty())
        p->loadFile (juce::File (file));
    else
    {
        // a drum loop with a bass line under it
        auto x = dctest::drumLoop (122.0, 2, 2, 12u);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / dctest::sr;
            const int step = (int) (t / (60.0 / 122.0 / 2.0));
            const double f = (step % 8 < 4 ? 55.0 : 65.4) * (step % 2 == 0 ? 1.0 : 2.0);
            x[i] += 0.18f * (float) std::sin (dctest::twoPi * f * t);
        }
        juce::AudioBuffer<float> b (2, (int) x.size());
        for (int c = 0; c < 2; ++c)
            b.copyFrom (c, 0, x.data(), (int) x.size());
        p->loadAudio (std::move (b), dctest::sr, "Disco Loop 122");
    }
    p->waitForAnalysis();
    return p;
}

void playSlice (daftchunk::DaftChunkProcessor& p, int slice)
{
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, daftchunk::DaftChunkEngine::baseNote + slice, (juce::uint8) 110), 0);
    p.processBlock (b, midi);
    midi.clear();
    for (int k = 0; k < 3; ++k)
        p.processBlock (b, midi);
}
std::unique_ptr<daftchunk::DaftChunkProcessor> withSample (const juce::String& file);

/** The website: the loops made and one playing (SEQUENCE), the slices, the effects. */
int runWeb (const juce::File& out)
{
    auto p = withSample ({});
    p->setMode (daftchunk::SliceMode::grid);
    p->setDivision (16);
    p->getAmountParameter().setValueNotifyingHost (0.5f);
    for (int k = 0; k < 5; ++k)
        p->rollDice();
    p->selectIdea (2);
    p->toggleStar (2);
    p->togglePlay (2);
    playSlice (*p, 4);
    std::vector<gkweb::Shot> shots { { "hero", shoot (*p, 3.0f, 2) } };
    {
        daftchunk::SliceParams sp;
        sp.pitch = 5.0f;
        sp.filter = -0.3f;
        sp.reverse = true;
        p->selectSlice (6);
        p->setSliceParams (6, sp);
        shots.push_back ({ "slice", shoot (*p, 2.0f, 1) });
    }
    using E = daftchunk::DaftChunkEngine;
    auto set = [&] (juce::RangedAudioParameter& prm, float v) { prm.setValueNotifyingHost (prm.convertTo0to1 (v)); };
    set (p->getKnobParameter (E::phaserKnob), 0.45f);
    set (p->getKnobParameter (E::flangerKnob), 0.3f);
    set (p->getKnobParameter (E::pumpKnob), 0.5f);
    playSlice (*p, 2);
    shots.push_back ({ "fx", shoot (*p, 2.0f, 3) });
    shots.push_back ({ "sound", shoot (*p, 2.0f, 0) });

    gkweb::PluginInfo info;
    info.slug = "daft-chunk";
    info.name = "Daft Chunk";
    info.taglineFr = juce::String::fromUTF8 ("Le d\xc3\xa9" "coupeur de samples qui cr\xc3\xa9" "e tes boucles");
    info.taglineEn = "The sample chopper that makes your loops";
    info.palette = daftchunk::DaftChunkEditor::makePalette();
    info.assetsDir = repoRoot().getChildFile ("plugins/daft_chunk/Assets");
    info.framePrefix = "chunky_";
    info.heroFrame = std::atoi (std::getenv ("GK_HERO") != nullptr ? std::getenv ("GK_HERO") : "0");
    for (int i = 0; i < 32; ++i)
        info.poses.push_back (i);
    gkweb::writePlugin (info, shots, out);
    // the suite (every plug-in's visuals already on disk): Daft Chunk is the last one in
    gkweb::writeSuite (out, gkweb::suiteVisuals(), info.palette);
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1");
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--web")
        return runWeb (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "web-assets"));
    const juce::File out (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("dc_screenshots"));
    const juce::String sampleFile = argc > 2 ? juce::String (argv[2]) : juce::String();
    out.createDirectory();
    {
        daftchunk::DaftChunkProcessor empty;
        writePng (shoot (empty, 1.0f), out.getChildFile ("dc_empty.png"));
    }
    auto p = withSample (sampleFile);
    writePng (shoot (*p, 1.0f), out.getChildFile ("dc_transients.png"));
    writePng (shoot (*p, 2.0f), out.getChildFile ("dc_transients@2x.png"));
    p->setMode (daftchunk::SliceMode::grid);
    p->setDivision (16);
    writePng (shoot (*p, 1.0f), out.getChildFile ("dc_grid16.png"));
    p->setMode (daftchunk::SliceMode::mixed);
    p->setDivision (8);
    playSlice (*p, 5);
    writePng (shoot (*p, 1.0f), out.getChildFile ("dc_mixed_playing.png"));
    {
        daftchunk::SliceParams sp;
        sp.pitch = 7.0f;
        sp.filter = -0.4f;
        sp.start = 0.15f;
        sp.reverse = true;
        p->selectSlice (6);
        p->setSliceParams (6, sp);
        writePng (shoot (*p, 1.0f, 1), out.getChildFile ("dc_slice_page.png"));
    }
    {
        // the ideas: three dice, an ENCORE, one playing, the SEQUENCE page
        p->setMode (daftchunk::SliceMode::grid);
        p->setDivision (16);
        p->getAmountParameter().setValueNotifyingHost (0.55f);
        p->rollDice();
        p->rollDice();
        p->rollDice();
        p->selectIdea (1);
        p->encore();
        p->toggleStar (1);
        p->togglePlay (1);
        playSlice (*p, 3);
        writePng (shoot (*p, 1.0f, 2), out.getChildFile ("dc_ideas.png"));
        writePng (shoot (*p, 2.0f, 2), out.getChildFile ("dc_ideas@2x.png"));
        writePng (shoot (*p, 1.0f, 2, 12.0), out.getChildFile ("dc_zoom12.png"));
        writePng (shoot (*p, 1.0f, 3), out.getChildFile ("dc_fx.png"));
        writePng (shoot (*p, 1.0f, 2, 600.0), out.getChildFile ("dc_zoom600.png"));
    }
    std::cout << "screenshots in " << out.getFullPathName() << std::endl;
    return 0;
}
