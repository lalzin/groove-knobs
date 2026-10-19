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

/** analyze: onsets, then for each event a short-time spectral track
    (strongest partial with parabolic interpolation, 2nd / 3rd partials,
    spectral centroid, level), to describe a reference "pew". */
int analyze (const juce::File& file, int maxEvents, double frameMs, double fromSeconds = -1.0, double toSeconds = -1.0)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr)
    {
        std::cerr << "cannot read " << file.getFullPathName() << "\n";
        return 1;
    }
    const double sr = r->sampleRate;
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> buf ((int) r->numChannels, n);
    r->read (&buf, 0, n, 0, true, true);
    std::vector<float> x ((size_t) n), side ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float l = buf.getSample (0, i), rr = buf.getNumChannels() > 1 ? buf.getSample (1, i) : l;
        x[(size_t) i] = 0.5f * (l + rr);
        side[(size_t) i] = 0.5f * (l - rr);
    }
    double eM = 0, eS = 0;
    for (int i = 0; i < n; ++i) { eM += x[(size_t) i] * x[(size_t) i]; eS += side[(size_t) i] * side[(size_t) i]; }
    std::printf ("%s: %.2f s, %.0f Hz, %d ch, side/mid energy %.1f dB, peak %.1f dBFS\n", file.getFileName().toRawUTF8(), n / sr, sr,
                 buf.getNumChannels(), 10 * std::log10 (eS / (eM + 1e-20) + 1e-20), juce::Decibels::gainToDecibels (buf.getMagnitude (0, n)));

    // onsets: 2 ms RMS rising 12 dB over the previous 20 ms, 60 ms apart
    const int hop = juce::jmax (1, (int) (0.002 * sr));
    std::vector<double> env;
    for (int i = 0; i + hop <= n; i += hop)
    {
        double e = 0;
        for (int k = 0; k < hop; ++k) e += x[(size_t) (i + k)] * x[(size_t) (i + k)];
        env.push_back (std::sqrt (e / hop));
    }
    std::vector<int> onsets;
    if (fromSeconds >= 0.0)
        onsets = { (int) (fromSeconds * sr), (int) (toSeconds * sr) };
    else
    for (size_t k = 10; k < env.size(); ++k)
    {
        double prev = 1e-9;
        for (size_t j = k - 10; j < k; ++j) prev = std::max (prev, env[j]);
        if (env[k] > 1e-3 && env[k] > 4.0 * prev && (onsets.empty() || (int) k * hop - onsets.back() > (int) (0.06 * sr)))
            onsets.push_back ((int) k * hop);
    }
    std::printf ("%zu onsets:", onsets.size());
    for (int o : onsets) std::printf (" %.3f", o / sr);
    std::printf ("\n");

    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) (2 * size));
    const int step = juce::jmax (1, (int) (frameMs * 0.001 * sr));
    for (int e = 0; e < (int) onsets.size() && e < maxEvents; ++e)
    {
        const int start = fromSeconds >= 0.0 ? onsets[(size_t) e] : juce::jmax (0, onsets[(size_t) e] - (int) (0.005 * sr));
        const int end = e + 1 < (int) onsets.size() ? onsets[(size_t) e + 1] : n;
        double peakLevel = 0;
        std::printf ("\n-- event %d at %.3f s (to %.3f s)\n   t(ms)  level(dB)  f1(Hz)  f2(Hz)  f3(Hz)  centroid(Hz)  hf>4k(%%)\n", e, onsets[(size_t) e] / sr, end / sr);
        for (int pos = start; pos + 512 < end; pos += step)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            const int len = 1024; // ~23 ms analysis window, zero padded to 4096
            double lvl = 0;
            for (int i = 0; i < len && pos + i < n; ++i)
            {
                const float v = x[(size_t) (pos + i)];
                lvl += v * v;
                w[(size_t) i] = v * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / len));
            }
            lvl = std::sqrt (lvl / len);
            peakLevel = std::max (peakLevel, lvl);
            const double db = juce::Decibels::gainToDecibels (lvl, -120.0);
            if (fromSeconds < 0.0 && db < juce::Decibels::gainToDecibels (peakLevel) - 50.0)
                break;
            fft.performFrequencyOnlyForwardTransform (w.data(), true);
            // three strongest local maxima (above 30 Hz)
            std::vector<std::pair<float, double>> peaks;
            double num = 0, den = 0, hf = 0;
            for (int k = 2; k < size / 2 - 1; ++k)
            {
                const double f = k * sr / size, m = w[(size_t) k];
                num += f * m * m; den += m * m;
                if (f > 4000.0) hf += m * m;
                if (f > 30.0 && w[(size_t) k] > w[(size_t) k - 1] && w[(size_t) k] >= w[(size_t) k + 1])
                {
                    const double a = w[(size_t) k - 1], b = w[(size_t) k], c = w[(size_t) k + 1];
                    const double d = 0.5 * (a - c) / (a - 2 * b + c + 1e-20);
                    peaks.push_back ({ w[(size_t) k], (k + d) * sr / size });
                }
            }
            std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            auto fq = [&] (size_t i) { return i < peaks.size() && peaks[i].first > 0.05f * peaks[0].first ? peaks[i].second : 0.0; };
            std::printf ("  %6.1f   %7.1f  %7.1f %7.1f %7.1f   %8.0f      %5.1f\n", (pos - onsets[(size_t) e]) * 1000.0 / sr, db, fq (0), fq (1), fq (2),
                         num / (den + 1e-20), 100.0 * hf / (den + 1e-20));
        }
    }
    return 0;
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

    if (cmd == "analyze" && args.positional.size() >= 2)
        return analyze (juce::File (args.positional[1]), args.options.containsKey ("from") ? 1 : (int) args.num ("events", 3), args.num ("step", 5.0),
                        args.num ("from", -1.0), args.num ("to", -1.0));

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
