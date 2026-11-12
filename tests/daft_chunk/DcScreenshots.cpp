// DAFT CHUNK editor screenshots: empty, a drum loop sliced (TRANSITOIRES,
// GRILLE 1/16, MIXTE), a slice playing; at 100 % and 200 %.
//   dc_screenshots [out-dir] [sample-file]
#include "DaftChunkEditor.h"
#include "DaftChunkProcessor.h"
#include "../daft_chunk/DcSignals.h"
#include <iostream>

namespace
{
void writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

juce::Image shoot (daftchunk::DaftChunkProcessor& proc, float scale, int page = 0)
{
    proc.editorScale.store (scale);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<daftchunk::DaftChunkEditor*> (ed.get());
    editor->showPage (page);
    editor->snapView();
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
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
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
        sp.filter = -2.5f;
        sp.start = 0.15f;
        sp.reverse = true;
        p->selectSlice (6);
        p->setSliceParams (6, sp);
        writePng (shoot (*p, 1.0f, 1), out.getChildFile ("dc_slice_page.png"));
    }
    std::cout << "screenshots in " << out.getFullPathName() << std::endl;
    return 0;
}
