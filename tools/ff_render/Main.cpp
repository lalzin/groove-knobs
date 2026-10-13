// ff_render — offline renderer / calibration tool for French Filter.
//
//   ff_render render <in.wav> <out.wav> [--preset NAME|INDEX] [--preset-file F]
//                    [--amount X | --automation FILE [--beats] | --sweep]
//                    [--bpm 124] [--bars 16] [--block 256] [--no-makeup] [--stopped]
// --amount is the raw knob position (0.5 = centre); --sweep goes centre → LP → centre → HP → centre.
//   ff_render sweep  [--out-dir renders] [--bpm 124] [--bars 16] [--sr 48000]
//   ff_render gen-loop <out.wav> [--music] [--bpm 124] [--bars 8] [--sr 48000]
//   ff_render gen-pink <out.wav> [--seconds 10] [--sr 48000]
//   ff_render calibrate <preset.json>... [--sr 48000] [--passes 3] [--dry-run]
//   ff_render measure [<preset.json>...] [--sr 48000]
//
// Automation files: one "<time> <amount>" pair per line (seconds, or beats
// with --beats), linearly interpolated; '#' starts a comment.

#include "FrenchFilterProcessor.h"
#include <thread>

namespace
{
using gk::testsignals::grooveLoop;
inline double dspmathFrac (double x) { return gk::dspmath::frac (x); }
using gk::testsignals::pinkNoise;

struct Args
{
    juce::StringArray positional;
    juce::StringPairArray options;
    juce::StringArray flags;

    Args (int argc, char** argv)
    {
        for (int i = 2; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            if (a.startsWith ("--"))
            {
                const auto key = a.substring (2);
                const bool hasValue = i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--");
                if (hasValue && key != "music" && key != "beats" && key != "sweep" && key != "no-makeup" && key != "stopped" && key != "dry-run" && key != "music")
                    options.set (key, argv[++i]);
                else
                    flags.add (key);
            }
            else
                positional.add (a);
        }
    }

    juce::String get (const juce::String& key, const juce::String& fallback = {}) const
    {
        return options.containsKey (key) ? options[key] : fallback;
    }
    bool has (const juce::String& key) const { return flags.contains (key) || options.containsKey (key); }
};

int fail (const juce::String& message)
{
    std::cerr << "ff_render: " << message << std::endl;
    return 1;
}

bool readWav (const juce::File& file, juce::AudioBuffer<float>& buffer, double& sampleRate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return false;
    const int channels = juce::jmin (2, (int) reader->numChannels);
    buffer.setSize (channels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, channels > 1);
    sampleRate = reader->sampleRate;
    return true;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, int bits = 24)
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
                                                   .withBitsPerSample (bits));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

/** Knob journey over `seconds`. Unipolar: 0 → 1 → 0. Bipolar: centre →
    full left (low side) → centre → full right (high side) → centre. */
std::function<float (double)> sweepCurve (double seconds, bool bipolar)
{
    return [seconds, bipolar] (double t)
    {
        const double u = juce::jlimit (0.0, 1.0, t / seconds);
        if (! bipolar)
            return (float) (1.0 - std::abs (1.0 - 2.0 * u));
        const double tri = 1.0 - std::abs (1.0 - 2.0 * dspmathFrac (2.0 * u)); // 0 → 1 → 0, twice
        return (float) (u < 0.5 ? 0.5 - 0.5 * tri : 0.5 + 0.5 * tri);
    };
}

std::function<float (double)> loadAutomation (const juce::File& file, bool beats, double bpm)
{
    std::vector<std::pair<double, float>> points;
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    for (auto line : lines)
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        if (line.isEmpty())
            continue;
        auto tokens = juce::StringArray::fromTokens (line, " \t,;", "");
        tokens.removeEmptyStrings();
        if (tokens.size() < 2)
            continue;
        double t = tokens[0].getDoubleValue();
        if (beats)
            t *= 60.0 / bpm;
        points.emplace_back (t, juce::jlimit (0.0f, 1.0f, tokens[1].getFloatValue()));
    }
    std::sort (points.begin(), points.end());
    if (points.empty())
        return {};
    return [points] (double t)
    {
        if (t <= points.front().first)
            return points.front().second;
        for (size_t i = 1; i < points.size(); ++i)
            if (t <= points[i].first)
            {
                const auto& [t0, v0] = points[i - 1];
                const auto& [t1, v1] = points[i];
                const double f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
                return (float) (v0 + f * (v1 - v0));
            }
        return points.back().second;
    };
}

std::unique_ptr<ff::FrenchFilterProcessor> makeProcessor (const juce::String& presetFile, juce::String& error)
{
    if (presetFile.isEmpty())
        return std::make_unique<ff::FrenchFilterProcessor>();

    gk::PresetManager pm;
    const auto r = pm.add (juce::File (presetFile).loadFileAsString());
    if (r.failed())
    {
        error = presetFile + ": " + r.getErrorMessage();
        return nullptr;
    }
    return std::make_unique<ff::FrenchFilterProcessor> (std::move (pm));
}

int resolvePreset (const gk::PresetManager& presets, const juce::String& spec)
{
    if (spec.isEmpty())
        return 0;
    if (spec.containsOnly ("0123456789"))
        return juce::jlimit (0, presets.size() - 1, spec.getIntValue());
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name.equalsIgnoreCase (spec) || presets[i].name.removeCharacters (" ").equalsIgnoreCase (spec.removeCharacters (" _-")))
            return i;
    return -1;
}

juce::String slug (const juce::String& name)
{
    return name.toLowerCase().replaceCharacter (' ', '_');
}

//==============================================================================
int cmdRender (const Args& args)
{
    if (args.positional.size() < 2)
        return fail ("render needs <in.wav> <out.wav>");

    juce::AudioBuffer<float> input;
    double sr = 0;
    if (! readWav (juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[0]), input, sr))
        return fail ("cannot read " + args.positional[0]);

    juce::String error;
    auto proc = makeProcessor (args.get ("preset-file"), error);
    if (proc == nullptr)
        return fail (error);

    const int preset = resolvePreset (proc->getPresets(), args.get ("preset"));
    if (preset < 0)
        return fail ("unknown preset " + args.get ("preset"));

    gk::OfflineRenderer::Options opt;
    opt.bpm = args.get ("bpm", "124").getDoubleValue();
    opt.blockSize = juce::jlimit (1, 8192, args.get ("block", "256").getIntValue());
    opt.transportPlaying = ! args.has ("stopped");
    opt.preset = preset;

    if (args.has ("automation"))
    {
        opt.amount = loadAutomation (juce::File::getCurrentWorkingDirectory().getChildFile (args.get ("automation")), args.has ("beats"), opt.bpm);
        if (! opt.amount)
            return fail ("empty automation file");
    }
    else if (args.has ("sweep"))
        opt.amount = sweepCurve (args.get ("bars", "16").getDoubleValue() * 4.0 * 60.0 / opt.bpm, proc->getConfig().bipolar);
    else
    {
        // --amount is the raw knob position 0..1 (0.5 = centre when bipolar)
        const float a = juce::jlimit (0.0f, 1.0f, args.get ("amount", "0.5").getFloatValue());
        opt.amount = [a] (double) { return a; };
    }

    proc->setMakeupEnabled (! args.has ("no-makeup"));
    const auto out = gk::OfflineRenderer::render (*proc, input, sr, opt);
    if (! writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[1]), out, sr))
        return fail ("cannot write " + args.positional[1]);

    std::cout << "rendered " << args.positional[1] << " (" << proc->getPresets()[preset].name << ", peak "
              << juce::String (gk::dspmath::gainToDb (out.getMagnitude (0, out.getNumSamples())), 2) << " dBFS)" << std::endl;
    return 0;
}

int cmdSweep (const Args& args)
{
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args.get ("out-dir", "renders"));
    const double bpm = args.get ("bpm", "124").getDoubleValue();
    const int bars = args.get ("bars", "16").getIntValue();
    const double sr = args.get ("sr", "48000").getDoubleValue();

    const auto drums = grooveLoop (sr, bpm, bars, false);
    const auto disco = grooveLoop (sr, bpm, bars, true);
    writeWav (dir.getChildFile ("dry_drums.wav"), drums, sr);
    writeWav (dir.getChildFile ("dry_disco.wav"), disco, sr);

    ff::FrenchFilterProcessor proc;
    const auto& presets = proc.getPresets();
    std::cout << "preset              loop    peak dBFS   K-level vs dry (dB)" << std::endl;
    for (int p = 0; p < presets.size(); ++p)
    {
        for (const auto& [loopName, loop] : { std::pair { "drums", &drums }, std::pair { "disco", &disco } })
        {
            gk::OfflineRenderer::Options opt;
            opt.bpm = bpm;
            opt.preset = p;
            opt.amount = sweepCurve (bars * 4 * 60.0 / bpm, proc.getConfig().bipolar);
            const auto out = gk::OfflineRenderer::render (proc, *loop, sr, opt);
            const auto file = dir.getChildFile (slug (presets[p].name) + "_sweep_" + loopName + ".wav");
            writeWav (file, out, sr);
            const double diff = gk::KWeighting::levelDb (out, sr) - gk::KWeighting::levelDb (*loop, sr);
            std::cout << juce::String (presets[p].name).paddedRight (' ', 20) << juce::String (loopName).paddedRight (' ', 8)
                      << juce::String (gk::dspmath::gainToDb (out.getMagnitude (0, out.getNumSamples())), 2).paddedLeft (' ', 9)
                      << juce::String (diff, 2).paddedLeft (' ', 12) << std::endl;
        }
    }
    std::cout << "renders written to " << dir.getFullPathName() << std::endl;
    return 0;
}

int cmdGenLoop (const Args& args)
{
    if (args.positional.isEmpty())
        return fail ("gen-loop needs <out.wav>");
    const double sr = args.get ("sr", "48000").getDoubleValue();
    const auto loop = grooveLoop (sr, args.get ("bpm", "124").getDoubleValue(), args.get ("bars", "8").getIntValue(), args.has ("music"));
    return writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[0]), loop, sr) ? 0 : fail ("write failed");
}

int cmdGenPink (const Args& args)
{
    if (args.positional.isEmpty())
        return fail ("gen-pink needs <out.wav>");
    const double sr = args.get ("sr", "48000").getDoubleValue();
    const auto pink = pinkNoise (2, (int) (args.get ("seconds", "10").getDoubleValue() * sr), -18.0f);
    return writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[0]), pink, sr) ? 0 : fail ("write failed");
}

//==============================================================================
/** Calibration signals: pink noise and the synthetic drum loop (kick, clap, hats). */
struct CalibrationSignals
{
    double sr;
    juce::AudioBuffer<float> pink, loop, music;
    double pinkDb = 0, loopDb = 0;
    int skip = 0;

    explicit CalibrationSignals (double sampleRate, bool musicLoop = false) : sr (sampleRate)
    {
        pink = pinkNoise (2, (int) (6.0 * sr), -18.0f);
        loop = grooveLoop (sr, 124.0, 4, musicLoop);
        music = grooveLoop (sr, 124.0, 4, true); // peak guard only
        skip = (int) (0.5 * sr);
        pinkDb = gk::KWeighting::levelDb (pink, sr, skip);
        loopDb = gk::KWeighting::levelDb (loop, sr, skip);
    }

    /** Level differences (wet − dry) for pink and loop at a fixed amount. */
    std::pair<double, double> measure (ff::FrenchFilterProcessor& proc, float amount) const
    {
        const auto [a, b, peak] = measureWithPeak (proc, amount);
        juce::ignoreUnused (peak);
        return { a, b };
    }

    /** Same, plus the loop's output peak in dBFS. */
    std::tuple<double, double, double> measureWithPeak (ff::FrenchFilterProcessor& proc, float amount) const
    {
        gk::OfflineRenderer::Options opt;
        opt.blockSize = 512;
        opt.preset = 0;
        opt.amount = [amount] (double) { return amount; };
        const auto a = gk::OfflineRenderer::render (proc, pink, sr, opt);
        const auto b = gk::OfflineRenderer::render (proc, loop, sr, opt);
        const auto c = gk::OfflineRenderer::render (proc, music, sr, opt);
        const float peak = juce::jmax (b.getMagnitude (0, b.getNumSamples()), c.getMagnitude (0, c.getNumSamples()));
        return { gk::KWeighting::levelDb (a, sr, skip) - pinkDb, gk::KWeighting::levelDb (b, sr, skip) - loopDb, gk::dspmath::gainToDb (peak) };
    }
};

struct CalibrationResult
{
    gk::PresetData preset;
    // index 0 = low (left) side, 1 = high (right) side; unipolar uses [1]
    std::array<std::array<double, gk::MakeupTable::size>, 2> residualPink {}, residualLoop {};
    juce::String error;
};

constexpr double loopTolerance = 0.75; // dB, keeps margin inside the ±1 dB test
constexpr double maxMakeupDb = 9.0;    // never boost more (high-pass riser, see DECISIONS.md)
constexpr double peakLimitDb = -1.5;   // loop peaks after makeup (clipper starts at -1 dBFS)
constexpr double peakLimitHighSideDb = -3.0; // high-pass side: resonant sweeps overshoot static peaks by ~1.5 dB

/** Knob position for step i of a side (sign -1 = low side, +1 = high side). */
float knobFor (const ff::FrenchFilterProcessor& proc, int i, float sign)
{
    const float x = (float) i / (float) (gk::MakeupTable::size - 1);
    return proc.amountFor (sign * x);
}

CalibrationResult calibrateOne (const juce::File& file, double sr, int passes)
{
    CalibrationResult res;
    const auto r = gk::PresetManager::parse (file.loadFileAsString(), res.preset);
    if (r.failed())
    {
        res.error = r.getErrorMessage();
        return res;
    }

    const CalibrationSignals sig (sr);
    res.preset.makeup = {};
    res.preset.makeupLow = {};
    const bool bipolar = res.preset.bipolar;

    for (int pass = 0; pass <= passes; ++pass)
    {
        const bool finalPass = pass == passes; // measure only
        gk::PresetManager pm;
        pm.add (res.preset);
        ff::FrenchFilterProcessor proc (std::move (pm));

        for (int sideIndex = bipolar ? 0 : 1; sideIndex < 2; ++sideIndex)
        {
            const float sign = sideIndex == 0 ? -1.0f : 1.0f;
            auto& table = sideIndex == 0 ? res.preset.makeupLow : res.preset.makeup;

            for (int i = 0; i < gk::MakeupTable::size; ++i)
            {
                const auto [dPink, dLoop, loopPeakDb] = sig.measureWithPeak (proc, knobFor (proc, i, bipolar ? sign : 1.0f));
                res.residualPink[(size_t) sideIndex][(size_t) i] = dPink;
                res.residualLoop[(size_t) sideIndex][(size_t) i] = dLoop;
                if (finalPass)
                    continue;

                auto& m = table.values[(size_t) i];
                // Makeup that would put each signal exactly back at the dry level.
                const double needPink = m - dPink, needLoop = m - dLoop;
                // Aim at the middle of the two (minimax), but never let the drum
                // loop move more than loopTolerance from its dry level.
                const double target = juce::jlimit (needLoop - loopTolerance, needLoop + loopTolerance, 0.5 * (needPink + needLoop));
                // Peak guard: the drum and disco loops (normalised to -3 dBFS) must
                // keep their peaks under -1.5 dBFS, below the safety clipper.
                const double limit = bipolar && sideIndex == 1 ? peakLimitHighSideDb : peakLimitDb;
                const double peakRoom = m + (limit - loopPeakDb);
                m = (float) juce::jlimit (-12.0, juce::jmin (maxMakeupDb, peakRoom), target);
            }
        }
    }

    res.preset.hasMakeup = true;
    return res;
}

void printResiduals (const CalibrationResult& r)
{
    for (int sideIndex = r.preset.bipolar ? 0 : 1; sideIndex < 2; ++sideIndex)
    {
        double worstPink = 0, worstLoop = 0;
        for (size_t i = 0; i < gk::MakeupTable::size; ++i)
        {
            worstPink = juce::jmax (worstPink, std::abs (r.residualPink[(size_t) sideIndex][i]));
            worstLoop = juce::jmax (worstLoop, std::abs (r.residualLoop[(size_t) sideIndex][i]));
        }
        const auto& t = sideIndex == 0 ? r.preset.makeupLow : r.preset.makeup;
        std::cout << r.preset.name << (r.preset.bipolar ? (sideIndex == 0 ? " [low] " : " [high]") : "") << ": max |deviation| pink "
                  << juce::String (worstPink, 2) << " dB, loop " << juce::String (worstLoop, 2) << " dB; makeup at 25/50/72/100 %: "
                  << juce::String (t.values[25], 2) << " / " << juce::String (t.values[50], 2) << " / " << juce::String (t.values[72], 2) << " / "
                  << juce::String (t.values[100], 2) << " dB" << std::endl;
    }
}

int cmdCalibrate (const Args& args)
{
    if (args.positional.isEmpty())
        return fail ("calibrate needs at least one preset JSON file");
    const double sr = args.get ("sr", "48000").getDoubleValue();
    const int passes = juce::jlimit (1, 6, args.get ("passes", "3").getIntValue());

    std::vector<CalibrationResult> results ((size_t) args.positional.size());
    std::vector<std::thread> threads;
    for (int i = 0; i < args.positional.size(); ++i)
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[i]);
        threads.emplace_back ([&results, i, file, sr, passes] { results[(size_t) i] = calibrateOne (file, sr, passes); });
    }
    for (auto& t : threads)
        t.join();

    int status = 0;
    for (int i = 0; i < (int) results.size(); ++i)
    {
        const auto& r = results[(size_t) i];
        if (r.error.isNotEmpty())
        {
            status = fail (args.positional[i] + ": " + r.error);
            continue;
        }
        printResiduals (r);
        if (! args.has ("dry-run"))
            juce::File::getCurrentWorkingDirectory().getChildFile (args.positional[i]).replaceWithText (gk::PresetManager::toJson (r.preset));
    }
    return status;
}

int cmdMeasure (const Args& args)
{
    const double sr = args.get ("sr", "48000").getDoubleValue();
    const CalibrationSignals sig (sr, args.has ("music"));
    gk::PresetManager all = ff::FrenchFilterProcessor::loadBuiltInPresets();
    if (! args.positional.isEmpty())
    {
        all = {};
        for (const auto& f : args.positional)
            all.add (juce::File::getCurrentWorkingDirectory().getChildFile (f).loadFileAsString());
    }

    std::cout << "amount";
    for (int p = 0; p < all.size(); ++p)
        std::cout << "  " << all[p].name.paddedRight (' ', 16).substring (0, 16) << "pink/loop";
    std::cout << std::endl;

    std::vector<std::unique_ptr<ff::FrenchFilterProcessor>> procs;
    for (int p = 0; p < all.size(); ++p)
    {
        gk::PresetManager pm;
        pm.add (all[p]);
        procs.push_back (std::make_unique<ff::FrenchFilterProcessor> (std::move (pm)));
    }
    for (int i = -100; i <= 100; i += 10)
    {
        std::cout << juce::String (i).paddedLeft (' ', 5) << "%";
        for (auto& proc : procs)
        {
            const auto [a, b] = sig.measure (*proc, proc->amountFor ((float) i / 100.0f));
            std::cout << "  " << juce::String (a, 2).paddedLeft (' ', 12) << " " << juce::String (b, 2).paddedLeft (' ', 12);
        }
        std::cout << std::endl;
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "usage: ff_render render|sweep|gen-loop|gen-pink|calibrate|measure ... (see tools/ff_render/Main.cpp)" << std::endl;
        return 1;
    }

    const juce::String cmd (argv[1]);
    const Args args (argc, argv);
    if (cmd == "render")
        return cmdRender (args);
    if (cmd == "sweep")
        return cmdSweep (args);
    if (cmd == "gen-loop")
        return cmdGenLoop (args);
    if (cmd == "gen-pink")
        return cmdGenPink (args);
    if (cmd == "calibrate")
        return cmdCalibrate (args);
    if (cmd == "measure")
        return cmdMeasure (args);
    return fail ("unknown command " + cmd);
}
