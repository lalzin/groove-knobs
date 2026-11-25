#pragma once

#include "BackwashProcessor.h"

namespace bw
{
/** BOUNCE: the swells of the bar Backwash has heard (its LIVE memory), as a
    WAV loop to drag into the DAW: put it on a track from a bar line, loop it,
    and Backwash can go (no latency at all).

    The memory (one bar, mono, by musical position) is played several times
    in a row through an offline Backwash in the 2 s mode (exact), with the
    plugin's preset, knob, chips and HITS; one bar in the middle, minus the dry
    sound, is the loop: it holds the end of the swells of the bar before and
    the swell into the next bar's first beat, as when it loops. */
namespace bounce
{
    struct Request
    {
        juce::MemoryBlock state; // the plugin's state (getStateInformation)
        double bpm = 120.0, beatsPerBar = 4.0;
    };

    /** Message thread: reads the live plugin's settings. */
    Request requestFrom (BackwashProcessor& live);

    /** Message thread: an offline Backwash with the request's settings, in the
        2 s mode, not yet prepared (create it here: parameter changes on the
        message thread do not leave asynchronous updates behind). */
    std::unique_ptr<BackwashProcessor> makeRenderer (const Request& request);

    /** Renders a buffer that starts on a bar line: [whole bars of swell
        before it][the buffer, with its swells], latency removed. Fills
        `preRollBars` (0 when nothing comes before the first beat). */
    juce::AudioBuffer<float> renderBuffer (BackwashProcessor& renderer, const Request& request, const juce::AudioBuffer<float>& input,
                                           double sampleRate, int& preRollBars, const std::function<bool (float)>& progress = {});

    /** The swell alone (stereo) of one loop of `memory` (mono, one cycle of
        `cycleBeats` quarter notes starting on a bar line, at `sampleRate`). */
    juce::AudioBuffer<float> renderLoop (BackwashProcessor& renderer, const Request& request, const std::vector<float>& memory,
                                         double cycleBeats, double sampleRate);

    /** Message thread: renders the live plugin's memory and writes the WAV in
        `folder` (never over an earlier one). Invalid file if there is no
        complete memory yet or the write fails. */
    juce::File writeLoop (BackwashProcessor& live, const juce::File& folder);

    /** ~/Music/Backwash (created). */
    juce::File defaultFolder();
} // namespace bounce
} // namespace bw
