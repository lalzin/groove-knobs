// sib_render — listening renders of Sing It Back: a synthetic sung phrase
// (verse + chorus) at 0 / 25 / 50 / 100 % crowd, in C major, A minor and C
// major pentatonic.
//   sib_render [--out-dir renders/sing_it_back] [--sr 48000]

#include "SingItBackProcessor.h"
#include "VocalSynth.h"
#include <iostream>

namespace
{
bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate).withNumChannels (buffer.getNumChannels()).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String outDir = "renders/sing_it_back";
    double sr = 48000.0;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) == "--out-dir")
            outDir = argv[i + 1];
        if (juce::String (argv[i]) == "--sr")
            sr = juce::String (argv[i + 1]).getDoubleValue();
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (outDir);
    struct Case { const char* name; int key, scale; std::vector<int> notes; };
    const std::vector<Case> cases {
        { "c_major", 0, 0, { 60, 62, 64, 65, 67, 69, 71 } },
        { "a_minor", 9, 1, { 57, 59, 60, 62, 64, 65, 67 } },
        { "c_penta", 0, 4, { 60, 62, 64, 67, 69 } },
    };
    for (const auto& c : cases)
    {
        const auto voice = vocalsynth::sing (vocalsynth::phrase (c.notes), sr, 110.0, 3.0);
        writeWav (dir.getChildFile (juce::String (c.name) + "_dry.wav"), voice, sr);
        for (float crowd : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            sib::SingItBackProcessor proc;
            gk::OfflineRenderer::setParameter (proc.getKeyParameter(), (float) c.key);
            gk::OfflineRenderer::setParameter (proc.getScaleParameter(), (float) c.scale);
            gk::OfflineRenderer::Options opt;
            opt.amount = [crowd] (double) { return crowd; };
            opt.bpm = 110.0;
            const auto out = gk::OfflineRenderer::render (proc, voice, sr, opt);
            const auto name = juce::String (c.name) + "_" + juce::String (juce::roundToInt (crowd * 100.0f)) + ".wav";
            writeWav (dir.getChildFile (name), out, sr);
            std::cout << "wrote " << name << " (peak " << juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())) << " dBFS)\n";
        }
    }
    // Styles (C major, 75 %) and RÉPONSE (1/2 bar, 1 bar at 110 BPM, 100 %)
    {
        const auto voice = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 110.0, 3.0);
        sib::SingItBackProcessor names;
        for (int style = 0; style < names.getStyleParameter().choices.size(); ++style)
        {
            sib::SingItBackProcessor proc;
            gk::OfflineRenderer::setParameter (proc.getStyleParameter(), (float) style);
            gk::OfflineRenderer::Options opt;
            opt.amount = [] (double) { return 0.75f; };
            opt.bpm = 110.0;
            const auto out = gk::OfflineRenderer::render (proc, voice, sr, opt);
            const auto name = "style_" + juce::String (style) + "_" + names.getStyleParameter().choices[style].toLowerCase().replace (juce::String::fromUTF8 ("\xc3\xa9"), "e") + ".wav";
            writeWav (dir.getChildFile (name), out, sr);
            std::cout << "wrote " << name << "\n";
        }
        for (int response : { 1, 2 })
        {
            sib::SingItBackProcessor proc;
            gk::OfflineRenderer::setParameter (proc.getResponseParameter(), (float) response);
            gk::OfflineRenderer::Options opt;
            opt.amount = [] (double) { return 1.0f; };
            opt.bpm = 110.0;
            const auto out = gk::OfflineRenderer::render (proc, voice, sr, opt);
            const juce::String name = response == 1 ? "response_half_bar.wav" : "response_one_bar.wav";
            writeWav (dir.getChildFile (name), out, sr);
            std::cout << "wrote " << name << "\n";
        }
    }
    return 0;
}
