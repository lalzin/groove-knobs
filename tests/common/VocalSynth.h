#pragma once

// A simple synthetic singer for Sing It Back's tests and listening renders:
// a glottal pulse train (band-limited, with a little jitter and vibrato)
// through vowel formants, unvoiced consonants (noise bursts) between notes.

#include <groove_core/groove_core.h>

namespace vocalsynth
{
struct Vowel { double f1, f2, f3; };
inline Vowel vowel (char v)
{
    switch (v)
    {
        case 'a': return { 730.0, 1090.0, 2440.0 };
        case 'e': return { 530.0, 1840.0, 2480.0 };
        case 'i': return { 300.0, 2200.0, 2950.0 };
        case 'o': return { 570.0, 840.0, 2410.0 };
        default:  return { 440.0, 1020.0, 2240.0 }; // 'u'
    }
}

struct Note
{
    double startBeat, beats;
    int midi;
    char vowel = 'a';
    bool consonant = true; // a short "s" / "t" before the vowel
};

/** Renders notes (stereo, same on both channels) at `bpm`. */
inline juce::AudioBuffer<float> sing (const std::vector<Note>& notes, double sr, double bpm, double tailSeconds = 0.5,
                                      double vibratoCents = 18.0, std::uint32_t seed = 3)
{
    double endBeat = 0.0;
    for (const auto& n : notes)
        endBeat = std::max (endBeat, n.startBeat + n.beats);
    const int len = (int) std::ceil ((endBeat * 60.0 / bpm + tailSeconds) * sr);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    gk::testsignals::Rng rng (seed);
    for (const auto& n : notes)
    {
        const int s0 = (int) std::round (n.startBeat * 60.0 / bpm * sr);
        const int s1 = juce::jmin (len, (int) std::round ((n.startBeat + n.beats) * 60.0 / bpm * sr));
        const auto v = vowel (n.vowel);
        gk::TptSvf b1, b2, b3, hs;
        const auto c1 = gk::TptSvf::Coeffs::make (v.f1, sr, 8.0), c2 = gk::TptSvf::Coeffs::make (v.f2, sr, 10.0),
                   c3 = gk::TptSvf::Coeffs::make (v.f3, sr, 12.0), cs = gk::TptSvf::Coeffs::make (5000.0, sr, 0.8);
        const double f0 = 440.0 * std::pow (2.0, (n.midi - 69) / 12.0);
        const int consonant = n.consonant ? (int) (0.06 * sr) : 0;
        double phase = 0.0;
        for (int i = s0; i < s1; ++i)
        {
            const double t = (i - s0) / sr, dur = (s1 - s0) / sr;
            double y = 0.0;
            if (i - s0 < consonant)
            {
                // unvoiced consonant: high-passed noise
                const double e = 0.35 * std::sin (juce::MathConstants<double>::pi * (i - s0) / consonant);
                y = e * hs.processBandpass (rng.bipolar(), cs);
            }
            else
            {
                const double tv = t - consonant / sr;
                const double vib = vibratoCents * std::min (1.0, tv / 0.25) * std::sin (2.0 * juce::MathConstants<double>::pi * 5.3 * tv);
                const double f = f0 * std::pow (2.0, vib / 1200.0) * (1.0 + 0.002 * rng.bipolar());
                phase += f / sr;
                phase -= std::floor (phase);
                // band-limited glottal-like pulse: a few harmonics with a -12 dB / oct tilt
                double g = 0.0;
                for (int h = 1; h <= 40 && h * f < 0.45 * sr; ++h)
                    g += std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / (h * h * 0.5 + 0.5);
                const double att = std::min (1.0, tv / 0.02), rel = std::min (1.0, (dur - t) / 0.05);
                y = 0.5 * att * rel * (b1.processBandpass (g, c1) + 0.7 * b2.processBandpass (g, c2) + 0.35 * b3.processBandpass (g, c3));
            }
            out.addSample (0, i, (float) y);
            out.addSample (1, i, (float) y);
        }
    }
    const float peak = out.getMagnitude (0, len);
    if (peak > 0.0f)
        out.applyGain (0.5f / peak); // -6 dBFS
    return out;
}

/** A sustained vowel at one pitch (tests). */
inline juce::AudioBuffer<float> sustained (int midi, double seconds, double sr, char v = 'a', double vibratoCents = 0.0)
{
    return sing ({ { 0.0, seconds * 2.0, midi, v, false } }, sr, 120.0, 0.1, vibratoCents);
}

/** The listening phrase: a verse and a chorus (C major, 110 BPM), or in A
    minor / C major pentatonic with `scaleShift`. */
inline std::vector<Note> phrase (const std::vector<int>& scaleNotes)
{
    // degrees (index into scaleNotes), beats
    const int verse[][2] = { { 0, 1 }, { 1, 1 }, { 2, 2 }, { 1, 1 }, { 0, 1 }, { 4, 2 }, { 3, 1 }, { 2, 1 }, { 1, 1 }, { 2, 1 }, { 0, 4 } };
    const int chorus[][2] = { { 4, 1 }, { 4, 1 }, { 5, 2 }, { 4, 1 }, { 2, 1 }, { 4, 2 }, { 7, 2 }, { 5, 1 }, { 4, 1 }, { 2, 2 }, { 4, 4 } };
    const char vowels[] = { 'a', 'o', 'e', 'a', 'i', 'o', 'a', 'u', 'e', 'a', 'o' };
    std::vector<Note> notes;
    double beat = 0.0;
    auto add = [&] (const int (&line)[11][2])
    {
        for (int k = 0; k < 11; ++k)
        {
            const int d = line[k][0];
            const int n = (int) scaleNotes.size();
            const int midi = scaleNotes[(size_t) (d % n)] + 12 * (d / n);
            notes.push_back ({ beat, line[k][1] * 0.9, midi, vowels[k], k % 3 == 0 });
            beat += line[k][1];
        }
    };
    add (verse);
    beat += 2.0;
    add (chorus);
    add (chorus);
    return notes;
}
} // namespace vocalsynth
