#pragma once

#include "BackwashProcessor.h"

namespace bw
{
/** BOUNCE: a sound file dropped on Backwash comes back as a new WAV with its
    swells, rendered offline in the 2 s mode (exact on any sound, whatever the
    LIVE / LOCK memory) and with no latency left in it.

    The file is assumed to start on a downbeat. The result starts whole bars
    earlier (the room for the first swells, usually one bar) and ends with the
    file: dropped in the DAW that many bars before the original, it lines up
    with it. Same preset, knob, chips and HITS as the plugin, at the host
    tempo and time signature. */
namespace bounce
{
    /** Files Backwash accepts (wav, aif, aiff, flac, mp3, ogg). */
    bool isAcceptedFile (const juce::String& path);

    /** Longest input accepted, seconds. */
    constexpr double maxInputSeconds = 600.0;

    struct Request
    {
        juce::MemoryBlock state; // the plugin's state (getStateInformation)
        double bpm = 120.0, beatsPerBar = 4.0;
    };

    /** Message thread: reads the live plugin's settings. */
    Request requestFrom (BackwashProcessor& live);

    struct Result
    {
        juce::File file;      // the WAV written (invalid on failure)
        juce::String error;   // why it failed, for the UI (French)
        int preRollBars = 0;  // bars of swell before the original's start
    };

    /** Message thread: an offline Backwash with the request's settings, in the
        2 s mode, not yet prepared (create it here: parameter changes on the
        message thread do not leave asynchronous updates behind). */
    std::unique_ptr<BackwashProcessor> makeRenderer (const Request& request);

    /** Any thread: renders `input` through `renderer` and writes the WAV in
        `folder`. `progress` (0..1) may return false to cancel. */
    Result render (BackwashProcessor& renderer, const Request& request, const juce::File& input, const juce::File& folder,
                   const std::function<bool (float)>& progress = {});

    /** Pure: renders a buffer (no files). Fills `preRollBars`. */
    juce::AudioBuffer<float> renderBuffer (BackwashProcessor& renderer, const Request& request, const juce::AudioBuffer<float>& input,
                                           double sampleRate, int& preRollBars, const std::function<bool (float)>& progress = {});

    /** ~/Music/Backwash (created). */
    juce::File defaultFolder();
} // namespace bounce
} // namespace bw
