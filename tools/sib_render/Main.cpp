// sib_render — listening renders of Sing It Back: a synthetic sung phrase
//   sib_render --input <file.wav> [--key 0-11] [--scale 0-5]: renders a file
//   at CROWD 25 / 50 / 100 % (key / scale guessed from the sung notes if not
//   given) next to it, as <file>_sib_<pct>.wav
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

    // Any file: CROWD 25 / 50 / 100 %
    juce::String input;
    int key = -1, scale = -1;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) == "--input")
            input = argv[i + 1];
        if (juce::String (argv[i]) == "--key")
            key = juce::String (argv[i + 1]).getIntValue();
        if (juce::String (argv[i]) == "--scale")
            scale = juce::String (argv[i + 1]).getIntValue();
    }
    if (input.isNotEmpty())
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (input);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
        {
            std::cerr << "cannot read " << file.getFullPathName() << "\n";
            return 1;
        }
        const double fileSr = reader->sampleRate;
        juce::AudioBuffer<float> voice (2, (int) reader->lengthInSamples);
        reader->read (&voice, 0, voice.getNumSamples(), 0, true, true);
        // key / scale: the major or minor key whose notes cover most of the sung time
        if (key < 0 || scale < 0)
        {
            sib::PitchTracker t;
            t.prepare (fileSr);
            std::array<double, 12> hist {};
            int voicedHops = 0, hops = 0;
            for (int i = 0; i < voice.getNumSamples(); ++i)
                if (t.push (0.5f * (voice.getSample (0, i) + voice.getSample (1, i))))
                {
                    ++hops;
                    if (t.voiced())
                    {
                        ++voicedHops;
                        const int pc = ((int) std::lround (69.0 + 12.0 * std::log2 (t.frequency() / 440.0)) % 12 + 12) % 12;
                        hist[(size_t) pc] += 1.0;
                    }
                }
            double best = -1.0;
            for (int k = 0; k < 12; ++k)
                for (int sc : { 0, 1 })
                {
                    double in = 0.0;
                    for (int d : sib::Harmony::degrees (sc))
                        in += hist[(size_t) ((k + d) % 12)];
                    in += 0.25 * hist[(size_t) k];                                   // tonic weight
                    if (in > best)
                    {
                        best = in;
                        key = k;
                        scale = sc;
                    }
                }
            std::cout << "voiced " << (100 * voicedHops / juce::jmax (1, hops)) << " % of the time; guessed key "
                      << sib::Harmony::noteName (key) << (scale == 1 ? " mineur" : " majeur") << "\n";
            if (voicedHops < hops / 4)
                std::cout << "NOTE: little clear singing found: Sing It Back needs a solo voice (a vocal stem / a cappella), not a full mix\n";
        }
        for (float crowd : { 0.25f, 0.5f, 1.0f })
        {
            sib::SingItBackProcessor proc;
            gk::OfflineRenderer::setParameter (proc.getKeyParameter(), (float) key);
            gk::OfflineRenderer::setParameter (proc.getScaleParameter(), (float) scale);
            gk::OfflineRenderer::Options opt;
            opt.amount = [crowd] (double) { return crowd; };
            const auto out = gk::OfflineRenderer::render (proc, voice, fileSr, opt);
            const auto name = file.getFileNameWithoutExtension().trim() + "_sib_" + juce::String (juce::roundToInt (crowd * 100.0f)) + ".wav";
            writeWav (file.getSiblingFile (name), out, fileSr);
            std::cout << "wrote " << name << "\n";
        }
        return 0;
    }
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
