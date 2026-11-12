#pragma once
// DAFT CHUNK - an idea out of the plug-in (spec 4): the loop rendered as a
// WAV at the host's tempo (24 bits, exactly N bars, looping without a gap:
// rendered twice, the second pass kept, so what rings over the end comes
// back at the start), the MIDI notes that replay it in DAFT CHUNK (slice 1
// on C1), and the KIT (every slice as a numbered WAV, for a Drum Rack, and
// the loop's MIDI, in a folder). The files go to ~/Music/Daft Chunk (the
// host keeps pointing at them: not a temporary folder).

#include "Generator.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace daftchunk
{
class DaftChunkProcessor;

/** A note of an idea, in beats (swing and repeats included). */
struct IdeaNote
{
    double start = 0.0, length = 0.0; // beats
    int slice = -1;
    float velocity = 1.0f;
    int pitch = 0;
    bool reverse = false;
};

/** The notes of one pass of `p` (length: bars x 4 beats); swing 0..0.75. */
std::vector<IdeaNote> ideaNotes (const Pattern& p, float swing);

namespace exporter
{
    /** Where the exports go (created if needed). */
    juce::File folder();
    /** DaftChunk_<sample>_<bpm>bpm_idee<n> (no extension). */
    juce::String baseName (const juce::String& sampleName, double bpm, int ideaIndex);

    /** The tempo exported: the host's, else the sample's. */
    double tempo (const DaftChunkProcessor& p);

    /** Idea `i` rendered: exactly bars x 4 beats at `bpm`, looping (null buffer: nothing to render). */
    juce::AudioBuffer<float> render (DaftChunkProcessor& p, int ideaIndex, double bpm, double sampleRate);

    /** The files (empty: failed). */
    juce::File writeWav (DaftChunkProcessor& p, int ideaIndex);
    juce::File writeMidi (DaftChunkProcessor& p, int ideaIndex);
    /** The kit's slice WAVs (the folder also holds the loop's MIDI). */
    juce::Array<juce::File> writeKit (DaftChunkProcessor& p, int ideaIndex);
} // namespace exporter
} // namespace daftchunk
