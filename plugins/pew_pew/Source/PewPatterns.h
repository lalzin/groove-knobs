#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pp
{
/** The listening / test pattern (time stamps in beats): 8 bars that cycle
    through disco pews on the off-beats (2 bars), a tom fill (1 bar) and a
    bass line over a held note (1 bar, several voices at once). */
inline juce::MidiMessageSequence testPatternBeats()
{
    juce::MidiMessageSequence seq;
    auto hit = [&seq] (double beat, int note, int velocity, double length = 0.25)
    {
        auto on = juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity);
        on.setTimeStamp (beat);
        auto off = juce::MidiMessage::noteOff (1, note);
        off.setTimeStamp (beat + length);
        seq.addEvent (on);
        seq.addEvent (off);
    };
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const double b0 = cycle * 16.0;
        // Bars 1-2: pews on the off-beats, accents alternating
        for (int k = 0; k < 8; ++k)
            hit (b0 + k + 0.5, k == 7 ? 67 : 60, (k & 1) ? 84 : 112);
        // Bar 3: tom fill, sixteenths falling, getting louder
        const int fill[] { 72, 72, 69, 69, 67, 67, 64, 64, 62, 62, 60, 60, 57, 57, 55, 55 };
        for (int k = 0; k < 16; ++k)
            hit (b0 + 8.0 + 0.25 * k, fill[k] - (cycle == 1 && k >= 12 ? 5 : 0), 70 + 3 * k);
        // Bar 4: disco bass over a held low note (polyphony)
        hit (b0 + 12.0, 48, 120, 4.0);
        const int bass[] { 48, 60, 48, 60, 51, 63, 53, 65 };
        for (int k = 0; k < 8; ++k)
            hit (b0 + 12.0 + 0.5 * k, bass[k], (k & 1) ? 90 : 110, 0.4);
    }
    seq.updateMatchedPairs();
    return seq;
}

/** A sequence in beats → time stamps in samples. */
inline juce::MidiMessageSequence beatsToSamples (const juce::MidiMessageSequence& beats, double bpm, double sampleRate)
{
    juce::MidiMessageSequence out;
    for (const auto* e : beats)
    {
        auto m = e->message;
        m.setTimeStamp (std::round (m.getTimeStamp() * 60.0 / bpm * sampleRate));
        out.addEvent (m);
    }
    out.sort();
    return out;
}
} // namespace pp
