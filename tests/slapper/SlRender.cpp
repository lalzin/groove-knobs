// sl_render — Slapper demos (WAV 48 kHz 24-bit stereo):
//   <out>/slapper_<preset>.wav   the funk riff on every preset
//   <out>/slapper_disco.wav      a disco octave line (Disco Octaves)
//   <out>/slapper_slides.wav     MONO slides: notes played over held ones
//   <out>/slapper_poly.wav       POLY: double stops and a chord (Dusty Wood)
//   sl_render [out-dir]   (default: renders/slapper/test)
#include "SlCommon.h"
#include <iostream>

using namespace sltest;

namespace
{
void write (const Buffer& b, const juce::File& f)
{
    // peak-normalised to -1 dBFS: the presets compared by sound, not level
    Buffer c (b);
    const float peak = c.getMagnitude (0, c.getNumSamples());
    if (peak > 0.0f)
        c.applyGain (0.891f / peak);
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (c.getNumChannels()).withBitsPerSample (24));
    if (w != nullptr)
        w->writeFromAudioSampleBuffer (c, 0, c.getNumSamples());
    std::cout << f.getFileName() << "  (peak before normalising " << juce::Decibels::gainToDecibels (peak) << " dBFS)\n";
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "renders/slapper/test");
    out.createDirectory();
    const double sr = 48000.0;
    auto probe = makeProc (0);
    for (int i = 0; i < probe->getPresets().size(); ++i)
    {
        auto p = makeProc (i);
        const auto name = probe->getPresets()[i].name.toLowerCase().replaceCharacter (' ', '_');
        write (render (*p, sequenceOf (funkRiff (4), sr), 10.5), out.getChildFile ("slapper_" + name + ".wav"));
    }
    {
        // disco: eighths, octave up on the off-beats, 120 BPM, four bars E E A B
        auto p = makeProc (5);
        std::vector<Note> v;
        const int roots[] { 28, 28, 33, 35 };
        const double e = 0.25;
        for (int bar = 0; bar < 8; ++bar)
            for (int k = 0; k < 8; ++k)
                v.push_back ({ (bar * 8 + k) * e, 0.8 * e, roots[bar % 4] + (k % 2 ? 12 : 0), k % 2 ? 90 : 115 });
        write (render (*p, sequenceOf (v, sr), 16.8), out.getChildFile ("slapper_disco.wav"));
    }
    {
        // slides: each target note starts before the last one ends
        auto p = makeProc (7); // Rubber Band
        std::vector<Note> v { { 0.0, 0.62, 28, 110 }, { 0.6, 0.5, 40, 100 }, { 1.2, 0.62, 31, 110 }, { 1.8, 0.62, 33, 100 },
                              { 2.4, 0.3, 35, 100 }, { 2.4 + 0.29, 0.6, 36, 100 }, { 3.6, 1.2, 28, 120 }, { 4.2, 0.9, 40, 105 } };
        write (render (*p, sequenceOf (v, sr), 6.5), out.getChildFile ("slapper_slides.wav"));
    }
    {
        auto p = makeProc (4); // Dusty Wood
        gktest::setParam (p->getModeParameter(), 1.0f);
        std::vector<Note> v { { 0.0, 1.0, 28, 100 }, { 0.0, 1.0, 35, 90 }, { 1.2, 1.0, 33, 100 }, { 1.2, 1.0, 40, 90 },
                              { 2.4, 2.0, 40, 90 }, { 2.45, 2.0, 47, 85 }, { 2.5, 2.0, 52, 80 }, { 2.55, 2.0, 56, 80 } };
        write (render (*p, sequenceOf (v, sr), 5.5), out.getChildFile ("slapper_poly.wav"));
    }
    return 0;
}
