// Slapper test helpers: a processor on a preset, MIDI riffs, rendering.
#pragma once
#include "SlapperProcessor.h"
#include "GkTestUtils.h"

namespace sltest
{
using gktest::Buffer;

struct Note
{
    double start, length; // seconds
    int note, velocity;
};

inline std::unique_ptr<slapper::SlapperProcessor> makeProc (int preset)
{
    auto p = std::make_unique<slapper::SlapperProcessor>();
    gktest::setParam (p->getPresetParameter(), (float) preset);
    p->applyPreset (preset); // (no message loop here)
    return p;
}

inline juce::MidiMessageSequence sequenceOf (const std::vector<Note>& notes, double sr)
{
    juce::MidiMessageSequence seq;
    for (const auto& n : notes)
    {
        auto on = juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity);
        on.setTimeStamp (std::round (n.start * sr));
        seq.addEvent (on);
        auto off = juce::MidiMessage::noteOff (1, n.note);
        off.setTimeStamp (std::round ((n.start + n.length) * sr));
        seq.addEvent (off);
    }
    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

inline Buffer render (slapper::SlapperProcessor& p, const juce::MidiMessageSequence& seq, double seconds, double sr = 48000.0, int block = 256)
{
    gk::MidiRenderer::Options opt;
    opt.blockSize = block;
    opt.bpm = 110.0;
    return gk::MidiRenderer::render (p, seq, (int) (seconds * sr), sr, opt);
}

/** A funk riff at 100 BPM (sixteenths): roots, octaves, a ghost, a slide (overlapping notes). */
inline std::vector<Note> funkRiff (int bars = 2)
{
    const double s = 60.0 / 100.0 / 4.0;
    std::vector<Note> v;
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * 16 * s;
        v.push_back ({ o + 0 * s, 1.6 * s, 28, 120 });   // E1
        v.push_back ({ o + 2 * s, 0.8 * s, 40, 100 });   // E2
        v.push_back ({ o + 3 * s, 0.5 * s, 28, 12 });    // ghost
        v.push_back ({ o + 4 * s, 0.9 * s, 28, 95 });
        v.push_back ({ o + 6 * s, 0.9 * s, 40, 105 });
        v.push_back ({ o + 8 * s, 1.5 * s, 31, 115 });   // G1
        v.push_back ({ o + 9.2 * s, 1.0 * s, 33, 100 }); // A1, played over G1: a slide in MONO
        v.push_back ({ o + 11 * s, 0.5 * s, 33, 10 });   // ghost
        v.push_back ({ o + 12 * s, 0.9 * s, 45, 110 });  // A2
        v.push_back ({ o + 14 * s, 1.8 * s, 35, 100 });  // B1
    }
    return v;
}
} // namespace sltest
