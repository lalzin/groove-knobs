// gb_render — offline MIDI renderer for the Groove Knobs instruments (Pew Pew).
//
//   gb_render pattern <out.mid> [--bpm 124]
//       writes the test pattern (8 bars: off-beat pews, tom fill, bass over a held note)
//   gb_render render <in.mid> <out.wav> [--preset NAME|INDEX] [--amount 0.6] [--reverb 0]
//                    [--delay 0] [--div 1] [--bpm 124] [--sr 48000] [--tail 3]
//       --div: 0 = 1/16, 1 = 1/8 dotted, 2 = 1/8, 3 = 1/4. Output: stereo 24-bit WAV.
//   gb_render sweep [--out-dir renders/pew_pew] [--bpm 124] [--sr 48000]
//       the pattern for every preset at 0 / 50 / 100 %, ROOM and ECHO at 30 %

#include "PewPewProcessor.h"
#include "PewPatterns.h"
#include <iostream>

namespace
{
struct Args
{
    juce::StringArray positional;
    juce::StringPairArray options;

    Args (int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            if (a.startsWith ("--") && i + 1 < argc)
                options.set (a.substring (2), argv[++i]);
            else
                positional.add (a);
        }
    }
    double num (const char* key, double fallback) const
    {
        return options.containsKey (key) ? options[key].getDoubleValue() : fallback;
    }
};

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (buffer.getNumChannels())
                                                   .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

/** MIDI file → one sequence in beats (all tracks merged). */
juce::MidiMessageSequence readMidi (const juce::File& file)
{
    juce::MidiFile mf;
    juce::FileInputStream in (file);
    if (! in.openedOk() || ! mf.readFrom (in))
        return {};
    const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
    juce::MidiMessageSequence seq;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (const auto* e : *mf.getTrack (t))
            if (e->message.isNoteOnOrOff() || e->message.isController())
            {
                auto m = e->message;
                m.setTimeStamp (m.getTimeStamp() / tpq);
                seq.addEvent (m);
            }
    seq.sort();
    return seq;
}

bool writeMidi (const juce::File& file, const juce::MidiMessageSequence& beats, double bpm)
{
    constexpr int tpq = 960;
    juce::MidiMessageSequence track;
    auto tempo = juce::MidiMessage::tempoMetaEvent ((int) std::round (60.0e6 / bpm));
    tempo.setTimeStamp (0.0);
    track.addEvent (tempo);
    for (const auto* e : beats)
    {
        auto m = e->message;
        m.setTimeStamp (std::round (m.getTimeStamp() * tpq));
        track.addEvent (m);
    }
    track.sort();
    track.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (track);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && mf.writeTo (os);
}

struct Settings
{
    int preset = 0;
    float amount = 0.6f, reverb = 0.0f, delay = 0.0f;
    int division = 1;
    double bpm = 124.0, sr = 48000.0, tail = 3.0;
};

juce::AudioBuffer<float> renderPattern (const juce::MidiMessageSequence& beats, const Settings& s)
{
    pp::PewPewProcessor proc;
    auto set = [] (juce::RangedAudioParameter& p, float v) { p.setValueNotifyingHost (p.convertTo0to1 (v)); };
    set (proc.getPresetParameter(), (float) s.preset);
    set (proc.getAmountParameter(), s.amount);
    set (proc.getRoomParameter(), s.reverb);
    set (proc.getEchoParameter(), s.delay);
    set (proc.getDivisionParameter(), (float) s.division);
    const auto events = pp::beatsToSamples (beats, s.bpm, s.sr);
    const double lastBeat = beats.getNumEvents() > 0 ? beats.getEndTime() : 0.0;
    const int length = (int) std::ceil ((lastBeat * 60.0 / s.bpm + s.tail) * s.sr);
    gk::MidiRenderer::Options opt;
    opt.bpm = s.bpm;
    return gk::MidiRenderer::render (proc, events, length, s.sr, opt);
}

int presetIndex (const juce::String& v)
{
    if (v.containsOnly ("0123456789"))
        return v.getIntValue();
    const auto pm = pp::PewPewProcessor::loadBuiltInPresets();
    for (int i = 0; i < pm.size(); ++i)
        if (pm[i].name.equalsIgnoreCase (v))
            return i;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const Args args (argc, argv);
    const auto cmd = args.positional.isEmpty() ? juce::String() : args.positional[0];
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const double bpm = args.num ("bpm", 124.0);

    if (cmd == "pattern" && args.positional.size() >= 2)
        return writeMidi (cwd.getChildFile (args.positional[1]), pp::testPatternBeats(), bpm) ? 0 : 1;

    if (cmd == "render" && args.positional.size() >= 3)
    {
        Settings s;
        s.preset = presetIndex (args.options.getValue ("preset", "0"));
        s.amount = (float) args.num ("amount", 0.6);
        s.reverb = (float) args.num ("reverb", 0.0);
        s.delay = (float) args.num ("delay", 0.0);
        s.division = (int) args.num ("div", 1.0);
        s.bpm = bpm;
        s.sr = args.num ("sr", 48000.0);
        s.tail = args.num ("tail", 3.0);
        const auto beats = readMidi (cwd.getChildFile (args.positional[1]));
        if (beats.getNumEvents() == 0)
        {
            std::cerr << "no MIDI events in " << args.positional[1] << "\n";
            return 1;
        }
        return writeWav (cwd.getChildFile (args.positional[2]), renderPattern (beats, s), s.sr) ? 0 : 1;
    }

    if (cmd == "sweep")
    {
        const auto dir = cwd.getChildFile (args.options.getValue ("out-dir", "renders/pew_pew"));
        const auto pattern = pp::testPatternBeats();
        writeMidi (dir.getChildFile ("pattern.mid"), pattern, bpm);
        const auto pm = pp::PewPewProcessor::loadBuiltInPresets();
        for (int p = 0; p < pm.size(); ++p)
            for (float amount : { 0.0f, 0.5f, 1.0f })
            {
                Settings s;
                s.preset = p;
                s.amount = amount;
                s.reverb = s.delay = 0.3f;
                s.bpm = bpm;
                s.sr = args.num ("sr", 48000.0);
                const auto out = renderPattern (pattern, s);
                const auto name = pm[p].name.toLowerCase().replaceCharacter (' ', '_') + "_" + juce::String (juce::roundToInt (amount * 100.0f)) + ".wav";
                writeWav (dir.getChildFile (name), out, s.sr);
                std::cout << "wrote " << name << " (peak " << juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())) << " dBFS)\n";
            }
        return 0;
    }

    std::cerr << "usage: gb_render pattern <out.mid> | render <in.mid> <out.wav> [options] | sweep [--out-dir DIR]\n";
    return 2;
}
