#include "Exporter.h"
#include "DaftChunkProcessor.h"

namespace daftchunk
{
std::vector<IdeaNote> ideaNotes (const Pattern& p, float swing)
{
    std::vector<IdeaNote> out;
    const int n = p.numSteps();
    const double swingDelay = juce::jlimit (0.0f, 0.75f, swing) * 0.125;
    for (int k = 0; k < n; ++k)
    {
        const auto& st = p.steps[(size_t) k];
        if (st.slice < 0 || st.cut)
            continue;
        // it holds until the next event (as the engine plays it)
        int run = 1;
        while (run < n)
        {
            const auto& nx = p.steps[(size_t) ((k + run) % n)];
            if (nx.slice >= 0 || nx.cut)
                break;
            ++run;
        }
        const int reps = std::max (1, st.repeat);
        const double start = k * 0.25 + ((k & 1) != 0 ? swingDelay : 0.0);
        for (int j = 0; j < reps; ++j)
        {
            IdeaNote note;
            note.start = start + j * 0.25 / reps;
            note.length = j < reps - 1 ? 0.25 / reps : 0.25 / reps + (run - 1) * 0.25;
            note.slice = st.slice;
            note.velocity = st.velocity;
            note.pitch = st.pitch;
            note.reverse = st.reverse;
            out.push_back (note);
        }
    }
    return out;
}

namespace exporter
{
    namespace
    {
        bool writeWavFile (const juce::File& f, const juce::AudioBuffer<float>& b, double sampleRate)
        {
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
            if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
                return false;
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate).withNumChannels (b.getNumChannels()).withBitsPerSample (24));
            return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        juce::String clean (const juce::String& s)
        {
            auto t = juce::File::createLegalFileName (s).replaceCharacters (" ", "_");
            return t.isEmpty() ? juce::String ("sample") : t.substring (0, 40);
        }

        bool ideaUsable (DaftChunkProcessor& p, int i)
        {
            return juce::isPositiveAndBelow (i, DaftChunkProcessor::numIdeas) && p.getIdea (i).used && p.getKit() != nullptr && p.getSample() != nullptr
                   && ! p.getKit()->slices.empty();
        }

        juce::MidiFile midiFor (const Pattern& pt, float swing, double bpm)
        {
            constexpr int ppq = 960;
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Daft Chunk"), 0.0);
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
            for (const auto& note : ideaNotes (pt, swing))
            {
                const int key = juce::jlimit (0, 127, DaftChunkEngine::baseNote + note.slice);
                const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (note.velocity * 127.0f));
                seq.addEvent (juce::MidiMessage::noteOn (1, key, vel), note.start * ppq);
                seq.addEvent (juce::MidiMessage::noteOff (1, key), (note.start + note.length) * ppq - 1.0);
            }
            seq.addEvent (juce::MidiMessage::endOfTrack(), pt.bars * 4.0 * ppq);
            seq.updateMatchedPairs();
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (ppq);
            mf.addTrack (seq);
            return mf;
        }

        bool writeMidiFile (const juce::File& f, const juce::MidiFile& mf)
        {
            f.deleteFile();
            juce::FileOutputStream os (f);
            return ! os.failedToOpen() && mf.writeTo (os, 1);
        }
    } // namespace

    juce::File folder()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Daft Chunk");
        d.createDirectory();
        return d;
    }

    juce::String baseName (const juce::String& sampleName, double bpm, int ideaIndex)
    {
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.005;
        const auto tempoText = whole ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 2).replaceCharacter ('.', '_');
        return "DaftChunk_" + clean (sampleName) + "_" + tempoText + "bpm_idee" + juce::String (ideaIndex + 1);
    }

    double tempo (const DaftChunkProcessor& p)
    {
        const double host = p.getHostBpm();
        return host > 0.0 ? host : p.getBpm();
    }

    juce::AudioBuffer<float> render (DaftChunkProcessor& p, int ideaIndex, double bpm, double sampleRate)
    {
        if (! ideaUsable (p, ideaIndex) || bpm <= 0.0 || sampleRate <= 0.0)
            return {};
        const auto kit = p.getKit();
        const auto pattern = p.getIdea (ideaIndex).pattern;
        DaftChunkEngine engine;
        engine.prepare (sampleRate, 512, 2);
        engine.setKit (kit.get());
        std::array<float, DaftChunkEngine::numKnobs> knobs {};
        for (int k = 0; k < DaftChunkEngine::numKnobs; ++k)
            knobs[(size_t) k] = p.getKnobParameter (k).convertFrom0to1 (p.getKnobParameter (k).getValue());
        engine.setKnobs (knobs.data(), DaftChunkEngine::numKnobs);
        std::array<int, DaftChunkEngine::numChoices> choices {};
        for (int c = 0; c < DaftChunkEngine::numChoices; ++c)
            choices[(size_t) c] = p.getChoiceParameter (c).getIndex();
        engine.setChoices (choices.data(), DaftChunkEngine::numChoices);
        engine.setHostTempoKnown (p.getHostBpm() > 0.0);
        engine.setSequence (&pattern);

        // exactly the bars at this tempo, to the sample; twice, the second pass kept (it loops)
        const auto length = (int) std::llround (pattern.bars * 4.0 * 60.0 / bpm * sampleRate);
        juce::AudioBuffer<float> all (2, 2 * length), block (2, 512);
        std::vector<float> amount (512, 0.0f);
        juce::MidiBuffer none;
        gk::BlockTiming t;
        t.bpm = bpm;
        t.beatsPerSample = bpm / (60.0 * sampleRate);
        t.beatsPerBar = 4.0;
        t.isPlaying = true;
        for (int pos = 0; pos < 2 * length; pos += 512)
        {
            const int n = std::min (512, 2 * length - pos);
            t.beatsAtStart = pos * t.beatsPerSample;
            engine.setMidi (none, 0, n);
            engine.processWet (block, n, amount.data(), t);
            for (int c = 0; c < 2; ++c)
                all.copyFrom (c, pos, block, c, 0, n);
        }
        engine.setSequence (nullptr);
        engine.setKit (nullptr);
        juce::AudioBuffer<float> out (2, length);
        const float volume = knobs[DaftChunkEngine::volumeKnob];
        const float gain = volume <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (volume);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                out.setSample (c, i, juce::jlimit (-1.0f, 1.0f, gain * all.getSample (c, length + i)));
        return out;
    }

    juce::File writeWav (DaftChunkProcessor& p, int ideaIndex)
    {
        if (! ideaUsable (p, ideaIndex))
            return {};
        const double bpm = tempo (p), sr = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
        const auto audio = render (p, ideaIndex, bpm, sr);
        if (audio.getNumSamples() == 0)
            return {};
        const auto f = folder().getChildFile (baseName (p.getSample()->name, bpm, ideaIndex) + ".wav");
        return writeWavFile (f, audio, sr) ? f : juce::File();
    }

    juce::File writeMidi (DaftChunkProcessor& p, int ideaIndex)
    {
        if (! ideaUsable (p, ideaIndex))
            return {};
        const double bpm = tempo (p);
        const float swing = p.getKnobParameter (DaftChunkEngine::swingKnob).convertFrom0to1 (p.getKnobParameter (DaftChunkEngine::swingKnob).getValue());
        const auto f = folder().getChildFile (baseName (p.getSample()->name, bpm, ideaIndex) + ".mid");
        return writeMidiFile (f, midiFor (p.getIdea (ideaIndex).pattern, swing, bpm)) ? f : juce::File();
    }

    juce::Array<juce::File> writeKit (DaftChunkProcessor& p, int ideaIndex)
    {
        juce::Array<juce::File> files;
        if (! ideaUsable (p, ideaIndex))
            return files;
        const auto kit = p.getKit();
        const double bpm = tempo (p), sr = kit->sample->sampleRate;
        auto dir = folder().getChildFile (baseName (kit->sample->name, bpm, ideaIndex) + "_KIT");
        dir.deleteRecursively();
        dir.createDirectory();
        // every slice as played (stretched to the tempo in TEMPO), with its own gain, trims and reverse
        const auto& audio = kit->audio();
        for (int i = 0; i < (int) kit->slices.size(); ++i)
        {
            const auto& sl = kit->slices[(size_t) i];
            const auto sp = i < (int) kit->params.size() ? kit->params[(size_t) i] : SliceParams {};
            Slice played = sl;
            const double s0 = (double) sl.start * kit->timeScale, s1 = std::min ((double) audio.getNumSamples(), (double) sl.end * kit->timeScale);
            played.start = (int64_t) (s0 + sp.start * (s1 - s0));
            played.end = (int64_t) (s1 - sp.end * (s1 - s0));
            if (played.end - played.start < 64)
                continue;
            juce::AudioBuffer<float> b (audio.getNumChannels(), (int) (played.end - played.start));
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                auto x = renderSlice (audio.getReadPointer (c), audio.getNumSamples(), played, sr);
                if (sp.reverse)
                    std::reverse (x.begin(), x.end());
                const float g = juce::Decibels::decibelsToGain (sp.gainDb, -60.0f);
                for (size_t k = 0; k < x.size() && (int) k < b.getNumSamples(); ++k)
                    b.setSample (c, (int) k, g * x[k]);
            }
            const auto f = dir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_" + typeName (sl.type) + ".wav");
            if (writeWavFile (f, b, sr))
                files.add (f);
        }
        const float swing = p.getKnobParameter (DaftChunkEngine::swingKnob).convertFrom0to1 (p.getKnobParameter (DaftChunkEngine::swingKnob).getValue());
        writeMidiFile (dir.getChildFile (baseName (kit->sample->name, bpm, ideaIndex) + ".mid"), midiFor (p.getIdea (ideaIndex).pattern, swing, bpm));
        return files;
    }
} // namespace exporter
} // namespace daftchunk
